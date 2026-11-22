"""Def-name to language-name conversions shared by the emitters."""

from __future__ import annotations


def words(name: str):
    """Split a lowerCamel / snake name into lowercase words."""
    parts = []
    current = ""
    for ch in name.lstrip("_"):
        if ch == "_":
            if current:
                parts.append(current)
                current = ""
        elif ch.isupper():
            if current:
                parts.append(current)
            current = ch.lower()
        else:
            current += ch
    if current:
        parts.append(current)
    return parts


def to_pascal(name: str) -> str:
    """playerName -> PlayerName, chat_message -> ChatMessage."""
    return "".join(word.capitalize() for word in words(name)) or name


def to_camel(name: str) -> str:
    """PlayerName -> playerName (leading underscore is dropped)."""
    pascal = to_pascal(name)
    return pascal[0].lower() + pascal[1:] if pascal else name
