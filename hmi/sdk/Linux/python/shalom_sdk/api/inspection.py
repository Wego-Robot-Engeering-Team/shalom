"""Capture and commissioning-only arm commands."""

from __future__ import annotations

from typing import Any, Optional, Sequence

from ..types import Response
from ._base import _ApiBase


class InspectionApi(_ApiBase):
    def trigger_capture(self, vehicle_number: str = "UNKNOWN", car_number: str = "00",
                        point_id: str = "MANUAL", tag_id: Optional[int] = None,
                        timeout_s: float = 3.0) -> Response:
        payload: dict[str, Any] = {"vehicle_number": vehicle_number, "car_number": car_number,
                                   "point_id": point_id}
        if tag_id is not None:
            payload["tag_id"] = tag_id
        return self._client.request("cmd/capture/trigger", payload, timeout_s)

    # Do not expose arm methods in an operator UI until the delivered FR3
    # authority path has been approved.
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
