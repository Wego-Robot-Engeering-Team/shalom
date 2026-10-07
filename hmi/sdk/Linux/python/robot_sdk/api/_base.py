# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Shared implementation base for the public API domains."""

from __future__ import annotations

import math
import re

from ..client import Client


class _ApiBase:
    def __init__(self, client: Client) -> None:
        self._client = client

    @staticmethod
    def _finite(*values: float) -> None:
        if not all(not isinstance(value, bool) and isinstance(value, (int, float)) and math.isfinite(value) for value in values):
            raise ValueError("numeric arguments must be finite")

    @staticmethod
    def _text(value: str, name: str, maximum: int = 120) -> None:
        if not isinstance(value, str) or not value or len(value.encode("utf-8")) > maximum or any(c in value for c in "\n\r\x00"):
            raise ValueError(f"{name} must be a non-empty UTF-8 string (at most {maximum} bytes)")

    @staticmethod
    def _revision(value: int, allow_zero: bool = False) -> None:
        if type(value) is not int or not (0 if allow_zero else 1) <= value < 2**64:
            raise ValueError("expected_revision must be uint64")

    @staticmethod
    def _id(value: str) -> None:
        if not isinstance(value, str) or re.fullmatch(r"[A-Za-z0-9_-]{1,96}", value) is None:
            raise ValueError("id must contain 1-96 ASCII letters, digits, '-' or '_'")
