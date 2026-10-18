#!/usr/bin/env python3
"""Translate line-oriented text with a resumable JSON checkpoint."""

from __future__ import annotations

import argparse
import json
import os
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Any


GOOGLE_ENDPOINT = "https://translation.googleapis.com/language/translate/v2"
RETRYABLE_STATUS_CODES = frozenset({429, 500, 502, 503, 504})


@dataclass(frozen=True)
class BackoffPolicy:
    initial_delay: float = 1.0
    max_delay: float = 30.0
    max_attempts: int = 5


class Checkpoint:
    """JSON-backed line translations with atomic save and reload."""

    def __init__(self, path: Path) -> None:
        self.path = path
        self.data: dict[str, Any] = {"version": 1, "translations": {}}

    def load(self) -> None:
        if not self.path.exists():
            return
        try:
            payload = json.loads(self.path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise ValueError(f"invalid checkpoint: {self.path}") from error
        if (
            not isinstance(payload, dict)
            or payload.get("version") != 1
            or not isinstance(payload.get("translations"), dict)
            or not all(
                isinstance(key, str) and isinstance(value, str)
                for key, value in payload["translations"].items()
            )
        ):
            raise ValueError(f"unsupported checkpoint schema: {self.path}")
        self.data = payload

    def save(self) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.path.with_name(self.path.name + ".tmp")
        temporary.write_text(
            json.dumps(self.data, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        os.replace(temporary, self.path)


def write_atomic(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(content, encoding="utf-8")
    os.replace(temporary, path)


def render_lines(source_lines: list[str], translations: dict[str, str]) -> str:
    translated = [translations.get(line, line) for line in source_lines]
    return "\n".join(translated) + ("\n" if source_lines else "")


def translate_text(
    text: str,
    source: str,
    target: str,
    api_key: str,
    timeout: float,
    backoff: BackoffPolicy,
    sleep: Any = time.sleep,
    opener: Any = urllib.request.build_opener(),
) -> str:
    body = urllib.parse.urlencode(
        {
            "q": text,
            "source": source,
            "target": target,
            "format": "text",
            "key": api_key,
        }
    ).encode("utf-8")
    request = urllib.request.Request(
        GOOGLE_ENDPOINT,
        data=body,
        headers={"Content-Type": "application/x-www-form-urlencoded"},
        method="POST",
    )

    delay = backoff.initial_delay
    for attempt in range(1, backoff.max_attempts + 1):
        try:
            with opener.open(request, timeout=timeout) as response:
                payload = json.loads(response.read().decode("utf-8"))
            translations = payload.get("data", {}).get("translations", [])
            if not translations or not isinstance(translations[0].get("translatedText"), str):
                raise ValueError("unexpected Google Translate response schema")
            return translations[0]["translatedText"]
        except urllib.error.HTTPError as error:
            if attempt == backoff.max_attempts or error.code not in RETRYABLE_STATUS_CODES:
                raise
            sleep(min(delay, backoff.max_delay))
            delay *= 2
        except urllib.error.URLError as error:
            if attempt == backoff.max_attempts:
                raise
            sleep(min(delay, backoff.max_delay))
            delay *= 2


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("checkpoint", type=Path)
    parser.add_argument("--source", required=True)
    parser.add_argument("--target", required=True)
    parser.add_argument("--api-key", default=os.environ.get("GOOGLE_TRANSLATE_API_KEY", ""))
    parser.add_argument("--request-interval", type=float, default=0.2)
    parser.add_argument(
        "--initial-backoff", type=float, default=BackoffPolicy.initial_delay
    )
    parser.add_argument("--max-backoff", type=float, default=BackoffPolicy.max_delay)
    parser.add_argument("--max-attempts", type=int, default=BackoffPolicy.max_attempts)
    parser.add_argument("--timeout", type=float, default=10.0)
    return parser


def main(argv: list[str] | None = None) -> int:
    return build_parser().parse_args(argv) and 0


if __name__ == "__main__":
    raise SystemExit(main())
