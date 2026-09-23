# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Goal navigation commands."""

from __future__ import annotations

from ..types import Response
from ._base import _ApiBase


class NavigationApi(_ApiBase):
    def navigate_to(self, x: float, y: float, theta: float = 0.0,
                    timeout_s: float = 3.0) -> Response:
        self._finite(x, y, theta)
        return self._client.request("cmd/goto", {"x": x, "y": y, "theta": theta}, timeout_s)

    def cancel_navigation(self, timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/nav_cancel", timeout_s=timeout_s)

    def publish_velocity(self, vx: float, vy: float, wz: float) -> None:
        """Rejected: manual velocity belongs to the commissioned HMI UDP path."""
        self._finite(vx, vy, wz)
        raise RuntimeError("manual velocity is not available through the customer TCP SDK")
