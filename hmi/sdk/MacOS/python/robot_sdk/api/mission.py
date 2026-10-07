# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Saved mission definitions and execution lifecycle."""
from __future__ import annotations
import copy
from typing import Any, Mapping
from ..types import Response
from ._base import _ApiBase


class MissionApi(_ApiBase):
    def list_missions(self, timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/missions/list", timeout_s=timeout_s)

    def save_mission(self, mission: Mapping[str, Any], *, map_id: str,
                     expected_revision: int = 0, timeout_s: float = 3.0) -> Response:
        self._text(map_id, "map_id")
        self._revision(expected_revision, allow_zero=True)
        if not isinstance(mission, Mapping):
            raise ValueError("mission must be an object")
        value = copy.deepcopy(dict(mission))
        self._id(value.get("id"))
        self._text(value.get("name"), "mission name")
        if "description" in value and not isinstance(value["description"], str):
            raise ValueError("description must be a string")
        steps = value.get("steps")
        if not isinstance(steps, list):
            raise ValueError("mission steps must be an array")
        ids = set()
        for step in steps:
            if not isinstance(step, Mapping):
                raise ValueError("step must be an object")
            self._id(step.get("id"))
            if step["id"] in ids:
                raise ValueError("step ids must be unique")
            ids.add(step["id"])
            kind = step.get("type")
            if kind not in ("navigate", "capture", "arm_move", "dock"):
                raise ValueError("unsupported step type")
            reference = {"navigate": "location_id", "capture": "preset", "arm_move": "pose"}.get(kind)
            if reference:
                self._id(step.get(reference))
        if "map_id" in value and value["map_id"] != map_id:
            raise ValueError("mission map_id differs from edit map")
        value["map_id"] = map_id
        return self._client.request("cmd/missions/save", {"mission": value,
                                    "expected_revision": expected_revision}, timeout_s)

    def archive_mission(self, mission_id: str, *, map_id: str, expected_revision: int,
                        timeout_s: float = 3.0) -> Response:
        self._id(mission_id)
        self._text(map_id, "map_id")
        self._revision(expected_revision)
        return self._client.request("cmd/missions/archive", {"id": mission_id, "map_id": map_id,
                                    "expected_revision": expected_revision}, timeout_s)

    def mission_start(self, mission_id: str, timeout_s: float = 5.0) -> Response:
        self._id(mission_id)
        return self._client.request("cmd/mission/start", {"mission_id": mission_id}, timeout_s)

    def mission_pause(self, timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/mission/pause", timeout_s=timeout_s)

    def mission_resume(self, timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/mission/resume", timeout_s=timeout_s)

    def mission_stop(self, timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/mission/stop", timeout_s=timeout_s)

    def return_to_dock(self, timeout_s: float = 5.0) -> Response:
        return self._client.request("cmd/mission/return_dock", timeout_s=timeout_s)
