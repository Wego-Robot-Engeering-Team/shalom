# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Capture and commissioning-only arm commands."""

from __future__ import annotations

from typing import Any, Optional, Sequence

from ..types import Response
from ._base import _ApiBase


class InspectionApi(_ApiBase):
    def trigger_capture(self, vehicle_number: str, train_number: str, car_number: str,
                        point_id: str, tag_id: Optional[int] = None,
                        timeout_s: float = 3.0) -> Response:
        for name, value in (("vehicle_number", vehicle_number), ("train_number", train_number),
                            ("car_number", car_number), ("point_id", point_id)):
            self._text(value, name)
        if tag_id is not None and (type(tag_id) is not int or not 0 <= tag_id <= 100000):
            raise ValueError("tag_id must be an integer from 0 to 100000")
        payload: dict[str, Any] = {"vehicle_number": vehicle_number, "car_number": car_number,
                                   "point_id": point_id, "train_number": train_number}
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
        if len(values) != 6:
            raise ValueError("arm joint goal requires six joint angles in radians")
        self._finite(*values)
        return self._client.request("cmd/arm/joint_goal", {"positions": values}, timeout_s)

    def arm_stop(self, timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/arm/stop", timeout_s=timeout_s)

    def list_arm_poses(self, timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/arm/pose_presets/list", timeout_s=timeout_s)

    def save_arm_pose(self, preset: dict[str, Any], timeout_s: float = 3.0) -> Response:
        self._validate_preset(preset)
        return self._client.request("cmd/arm/pose_presets/save", {"preset": preset}, timeout_s)

    def update_arm_pose(self, preset: dict[str, Any], expected_revision: int,
                        timeout_s: float = 3.0) -> Response:
        self._validate_preset(preset)
        self._revision(expected_revision)
        return self._client.request("cmd/arm/pose_presets/update", {"preset": preset,
                                    "expected_revision": expected_revision}, timeout_s)

    def archive_arm_pose(self, pose_id: str, expected_revision: int,
                         timeout_s: float = 3.0) -> Response:
        self._id(pose_id)
        self._revision(expected_revision)
        return self._client.request("cmd/arm/pose_presets/archive", {"id": pose_id,
                                    "expected_revision": expected_revision}, timeout_s)

    def _validate_preset(self, preset: dict[str, Any]) -> None:
        if not isinstance(preset, dict):
            raise ValueError("preset must be an object")
        self._id(preset.get("id"))
        self._text(preset.get("name"), "pose name", 80)
        description = preset.get("description", "")
        if not isinstance(description, str) or len(description.encode("utf-8")) > 400:
            raise ValueError("description must be a string of at most 400 UTF-8 bytes")
        positions = preset.get("positions")
        if not isinstance(positions, (list, tuple)) or len(positions) != 6:
            raise ValueError("positions must contain six joint angles")
        self._finite(*positions)
