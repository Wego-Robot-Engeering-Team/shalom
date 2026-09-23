# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Emergency-stop and operating-mode commands."""

from __future__ import annotations

from ..types import Response
from ._base import _ApiBase


class SafetyApi(_ApiBase):
    def emergency_stop(self, timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/estop", timeout_s=timeout_s)

    def release_emergency_stop(self, timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/estop_release", timeout_s=timeout_s)

    def set_mode(self, mode: str, timeout_s: float = 3.0) -> Response:
        if mode not in {"auto", "manual"}:
            raise ValueError("mode must be 'auto' or 'manual'")
        return self._client.request("cmd/mode", {"mode": mode}, timeout_s)
