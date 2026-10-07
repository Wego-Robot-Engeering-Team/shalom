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

    def pause_navigation(self, timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/nav_pause", timeout_s=timeout_s)

    def resume_navigation(self, timeout_s: float = 5.0) -> Response:
        return self._client.request("cmd/nav_resume", timeout_s=timeout_s)

    def set_initial_pose(self, x: float, y: float, theta: float, timeout_s: float = 3.0) -> Response:
        self._finite(x, y, theta)
        return self._client.request("cmd/localization/initial_pose", {"x": x, "y": y, "theta": theta}, timeout_s)

    def set_speed_limits(self, linear_mps: float, angular_rps: float, timeout_s: float = 3.0) -> Response:
        self._finite(linear_mps, angular_rps)
        if not 0.10 <= linear_mps <= 0.60 or not 0.05 <= angular_rps <= 0.80:
            raise ValueError("linear speed must be 0.10-0.60 m/s; angular speed 0.05-0.80 rad/s")
        return self._client.request("cmd/navigation/speed_limit", {"speed_limit_mps": linear_mps,
                                    "angular_speed_limit_rps": angular_rps}, timeout_s)

    def set_speed_ranges(self, min_linear_mps: float, max_linear_mps: float,
                         min_angular_rps: float, max_angular_rps: float,
                         timeout_s: float = 3.0) -> Response:
        self._finite(min_linear_mps, max_linear_mps, min_angular_rps, max_angular_rps)
        if not 0.10 <= min_linear_mps <= max_linear_mps <= 0.60 or not 0.05 <= min_angular_rps <= max_angular_rps <= 0.80:
            raise ValueError("invalid speed ranges")
        return self._client.request("cmd/navigation/speed_settings", {"min_speed_mps": min_linear_mps,
            "max_speed_mps": max_linear_mps, "min_angular_speed_rps": min_angular_rps,
            "max_angular_speed_rps": max_angular_rps}, timeout_s)

    def trail_snapshot(self, timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/trail/snapshot", timeout_s=timeout_s)
