#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import json
import sys
from pathlib import Path


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
