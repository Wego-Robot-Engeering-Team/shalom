# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Shared implementation base for the public API domains."""

from __future__ import annotations

import math

from ..client import Client


class _ApiBase:
    def __init__(self, client: Client) -> None:
        self._client = client

    @staticmethod
    def _finite(*values: float) -> None:
        if not all(isinstance(value, (int, float)) and math.isfinite(value) for value in values):
            raise ValueError("numeric arguments must be finite")
