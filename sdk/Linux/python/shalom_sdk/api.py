"""High-level Shalom command facade.

``Client`` owns TCP, framing, heartbeat and raw request/response handling.
``RobotApi`` maps the public command contract to those generic primitives.
"""

from __future__ import annotations

import math
from typing import Any, Mapping, Optional, Sequence

from .client import Client
from .types import Response


class RobotApi:
    """Typed command facade with the same protocol coverage as C++ RobotApi."""

    def __init__(self, client: Client) -> None:
        self._client = client

    def emergency_stop(self, timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/estop", timeout_s=timeout_s)

    def release_emergency_stop(self, timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/estop_release", timeout_s=timeout_s)

    def set_mode(self, mode: str, timeout_s: float = 3.0) -> Response:
        if mode not in {"auto", "manual"}:
            raise ValueError("mode must be 'auto' or 'manual'")
        return self._client.request("cmd/mode", {"mode": mode}, timeout_s)

    def navigate_to(self, x: float, y: float, theta: float = 0.0,
                    timeout_s: float = 3.0) -> Response:
        self._finite(x, y, theta)
        return self._client.request("cmd/goto", {"x": x, "y": y, "theta": theta}, timeout_s)

    def cancel_navigation(self, timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/nav_cancel", timeout_s=timeout_s)

    def mission_start(self, timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/mission/start", timeout_s=timeout_s)

    def mission_pause(self, timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/mission/pause", timeout_s=timeout_s)

    def mission_resume(self, timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/mission/resume", timeout_s=timeout_s)

    def mission_stop(self, timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/mission/stop", timeout_s=timeout_s)

    def list_maps(self, timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/maps/list", timeout_s=timeout_s)

    def select_map(self, map_id: str, timeout_s: float = 3.0) -> Response:
        if not map_id:
            raise ValueError("map_id must not be empty")
        return self._client.request("cmd/maps/select", {"id": map_id}, timeout_s)

    def set_power_policy(self, return_at: int, depart_at: int,
                         timeout_s: float = 3.0) -> Response:
        if not all(isinstance(value, int) and 0 <= value <= 100
                   for value in (return_at, depart_at)):
            raise ValueError("power policy values must be integers from 0 to 100")
        return self._client.request("cmd/power/policy",
                                    {"return_at": return_at, "depart_at": depart_at}, timeout_s)

    def set_waypoints(self, points: Sequence[Mapping[str, Any]], timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/waypoints/set", {"points": list(points)}, timeout_s)

    def set_locations(self, locations: Sequence[Mapping[str, Any]],
                      timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/locations/set", {"locations": list(locations)}, timeout_s)

    def set_markers(self, markers: Sequence[Mapping[str, Any]], timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/markers/set", {"markers": list(markers)}, timeout_s)

    def trigger_capture(self, vehicle_number: str = "UNKNOWN", car_number: str = "00",
                        point_id: str = "MANUAL", tag_id: Optional[int] = None,
                        timeout_s: float = 3.0) -> Response:
        payload: dict[str, Any] = {"vehicle_number": vehicle_number, "car_number": car_number,
                                   "point_id": point_id}
        if tag_id is not None:
            payload["tag_id"] = tag_id
        return self._client.request("cmd/capture/trigger", payload, timeout_s)

    # Commissioning-only: do not expose these methods in an operator UI until
    # the delivered FR3 authority path has been approved.
    def arm_preset(self, name: str, timeout_s: float = 3.0) -> Response:
        if not name:
            raise ValueError("arm preset must not be empty")
        return self._client.request("cmd/arm/preset", {"name": name}, timeout_s)

    def arm_joint_goal(self, positions: Sequence[float], timeout_s: float = 3.0) -> Response:
        values = list(positions)
        if not values:
            raise ValueError("arm joint goal must not be empty")
        self._finite(*values)
        return self._client.request("cmd/arm/joint_goal", {"positions": values}, timeout_s)

    def arm_stop(self, timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/arm/stop", timeout_s=timeout_s)

    def publish_velocity(self, vx: float, vy: float, wz: float) -> None:
        """Publish one manual velocity sample; callers must send it at 20 Hz."""
        self._finite(vx, vy, wz)
        self._client.publish("cmd/cmd_vel", {"vx": vx, "vy": vy, "wz": wz})

    @staticmethod
    def _finite(*values: float) -> None:
        if not all(isinstance(value, (int, float)) and math.isfinite(value) for value in values):
            raise ValueError("numeric arguments must be finite")
