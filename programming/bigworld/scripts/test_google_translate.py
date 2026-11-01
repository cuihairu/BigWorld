#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import json
import sys
import urllib.error
import urllib.parse
from pathlib import Path
from urllib.request import Request


SCRIPT = Path(__file__).with_name("google_translate.py")
spec = importlib.util.spec_from_file_location("google_translate", SCRIPT)
google_translate = importlib.util.module_from_spec(spec)
assert spec.loader is not None
sys.modules[spec.name] = google_translate
spec.loader.exec_module(google_translate)


def test_checkpoint_save_is_atomic(tmp_path: Path) -> None:
    checkpoint_path = tmp_path / "state" / "checkpoint.json"
    checkpoint = google_translate.Checkpoint(checkpoint_path)
    checkpoint.data["translations"]["hello"] = "bonjour"
    checkpoint.save()

    payload = json.loads(checkpoint_path.read_text(encoding="utf-8"))
    assert payload == {"version": 1, "translations": {"hello": "bonjour"}}
    assert list(checkpoint_path.parent.iterdir()) == [checkpoint_path]


def test_checkpoint_loads_valid_json(tmp_path: Path) -> None:
    checkpoint_path = tmp_path / "checkpoint.json"
    checkpoint_path.write_text(
        json.dumps({"version": 1, "translations": {"hello": "bonjour"}}),
        encoding="utf-8",
    )
    checkpoint = google_translate.Checkpoint(checkpoint_path)
    checkpoint.load()

    assert checkpoint.data["translations"] == {"hello": "bonjour"}


def test_checkpoint_rejects_invalid_schema(tmp_path: Path) -> None:
    checkpoint_path = tmp_path / "checkpoint.json"
    checkpoint_path.write_text("{}", encoding="utf-8")

    try:
        google_translate.Checkpoint(checkpoint_path).load()
    except ValueError as error:
        assert "unsupported checkpoint schema" in str(error)
    else:
        raise AssertionError("invalid checkpoint was accepted")


def test_render_lines_preserves_final_newline() -> None:
    rendered = google_translate.render_lines(
        ["hello", "world"], {"hello": "bonjour"}
    )

    assert rendered == "bonjour\nworld\n"


def test_write_atomic_replaces_output(tmp_path: Path) -> None:
    output = tmp_path / "output.txt"
    output.write_text("old", encoding="utf-8")

    google_translate.write_atomic(output, "new\n")

    assert output.read_text(encoding="utf-8") == "new\n"
    assert list(output.parent.iterdir()) == [output]


class FakeResponse:
    def __init__(self, payload: dict[str, object], status: int = 200) -> None:
        self.payload = payload
        self.status = status

    def read(self) -> bytes:
        return json.dumps(self.payload).encode("utf-8")

    def __enter__(self) -> "FakeResponse":
        return self

    def __exit__(self, *args: object) -> None:
        pass


class RetryOpener:
    def __init__(self, responses: list[object]) -> None:
        self.responses = iter(responses)
        self.requests: list[Request] = []
        self.timeouts: list[float] = []

    def open(self, request: Request, timeout: float) -> object:
        self.requests.append(request)
        self.timeouts.append(timeout)
        response = next(self.responses)
        if isinstance(response, (urllib.error.HTTPError, urllib.error.URLError)):
            raise response
        return response


def test_translate_text_sends_standard_request() -> None:
    opener = RetryOpener(
        [FakeResponse({"data": {"translations": [{"translatedText": "bonjour"}]}})]
    )

    translated = google_translate.translate_text(
        "hello",
        "en",
        "fr",
        "test-key",
        3.5,
        google_translate.BackoffPolicy(),
        opener=opener,
    )

    assert translated == "bonjour"
    assert len(opener.requests) == 1
    assert opener.timeouts == [3.5]
    assert opener.requests[0].full_url == google_translate.GOOGLE_ENDPOINT
    assert opener.requests[0].get_header("Content-type") == "application/x-www-form-urlencoded"
    assert opener.requests[0].data == urllib.parse.urlencode(
        {
            "q": "hello",
            "source": "en",
            "target": "fr",
            "format": "text",
            "key": "test-key",
        }
    ).encode("utf-8")


def test_translate_text_retries_http_429_with_exponential_backoff() -> None:
    opener = RetryOpener(
        [
            urllib.error.HTTPError(
                google_translate.GOOGLE_ENDPOINT, 429, "rate limited", {}, None
            ),
            urllib.error.HTTPError(
                google_translate.GOOGLE_ENDPOINT, 429, "rate limited", {}, None
            ),
            FakeResponse({"data": {"translations": [{"translatedText": "bonjour"}]}}),
        ]
    )
    delays: list[float] = []

    translated = google_translate.translate_text(
        "hello",
        "en",
        "fr",
        "test-key",
        3.5,
        google_translate.BackoffPolicy(),
        sleep=delays.append,
        opener=opener,
    )

    assert translated == "bonjour"
    assert delays == [1.0, 2.0]
    assert opener.timeouts == [3.5, 3.5, 3.5]


def test_translate_text_caps_backoff_at_maximum() -> None:
    opener = RetryOpener(
        [
            urllib.error.HTTPError(
                google_translate.GOOGLE_ENDPOINT, 503, "unavailable", {}, None
            ),
            FakeResponse({"data": {"translations": [{"translatedText": "salve"}]}}),
        ]
    )
    delays: list[float] = []

    translated = google_translate.translate_text(
        "hello",
        "en",
        "la",
        "test-key",
        3.5,
        google_translate.BackoffPolicy(initial_delay=16.0, max_delay=10.0),
        sleep=delays.append,
        opener=opener,
    )

    assert translated == "salve"
    assert delays == [10.0]


def test_translate_text_retries_url_errors() -> None:
    opener = RetryOpener(
        [
            urllib.error.URLError("connection reset"),
            FakeResponse({"data": {"translations": [{"translatedText": "hola"}]}}),
        ]
    )
    delays: list[float] = []

    translated = google_translate.translate_text(
        "hello",
        "en",
        "es",
        "test-key",
        3.5,
        google_translate.BackoffPolicy(),
        sleep=delays.append,
        opener=opener,
    )

    assert translated == "hola"
    assert delays == [1.0]


def test_translate_text_raises_after_last_retry() -> None:
    opener = RetryOpener(
        [
            urllib.error.HTTPError(
                google_translate.GOOGLE_ENDPOINT, 429, "rate limited", {}, None
            )
            for _ in range(3)
        ]
    )
    delays: list[float] = []

    try:
        google_translate.translate_text(
            "hello",
            "en",
            "fr",
            "test-key",
            3.5,
            google_translate.BackoffPolicy(max_attempts=3),
            sleep=delays.append,
            opener=opener,
        )
    except urllib.error.HTTPError as error:
        assert error.code == 429
    else:
        raise AssertionError("translate_text did not raise after the final retry")

    assert len(opener.requests) == 3
    assert delays == [1.0, 2.0]


def test_translate_text_does_not_retry_response_schema_errors() -> None:
    opener = RetryOpener([FakeResponse({"unexpected": True})])

    try:
        google_translate.translate_text(
            "hello",
            "en",
            "fr",
            "test-key",
            3.5,
            google_translate.BackoffPolicy(),
            opener=opener,
        )
    except ValueError as error:
        assert "unexpected Google Translate response schema" in str(error)
    else:
        raise AssertionError("invalid response schema was accepted")

    assert len(opener.requests) == 1
