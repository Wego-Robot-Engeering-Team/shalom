# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Robot-owned map catalogs; replacements require the original edit snapshot."""

from __future__ import annotations
import copy
import math
from typing import Any, Mapping, Sequence
from ..types import Response
from ._base import _ApiBase


class ConfigurationApi(_ApiBase):
    def list_maps(self, timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/maps/list", timeout_s=timeout_s)

    def select_map(self, map_id: str, timeout_s: float = 10.0) -> Response:
        self._text(map_id, "map_id")
        return self._client.request("cmd/maps/select", {"id": map_id}, timeout_s)

    def rename_map(self, map_id: str, name: str, timeout_s: float = 5.0) -> Response:
        self._text(map_id, "map_id")
        self._text(name, "name")
        return self._client.request("cmd/maps/rename", {"id": map_id, "name": name}, timeout_s)

    def delete_map(self, map_id: str, timeout_s: float = 5.0) -> Response:
        self._text(map_id, "map_id")
        return self._client.request("cmd/maps/delete", {"id": map_id}, timeout_s)

    def set_default_map(self, map_id: str, timeout_s: float = 3.0) -> Response:
        if map_id != "":
            self._text(map_id, "map_id")
        return self._client.request("cmd/maps/set_default", {"id": map_id}, timeout_s)

    def set_power_policy(self, return_at: int, depart_at: int, timeout_s: float = 3.0) -> Response:
        if not all(type(v) is int and 0 <= v <= 100 for v in (return_at, depart_at)) or return_at >= depart_at:
            raise ValueError("power policy requires 0 <= return_at < depart_at <= 100")
        return self._client.request("cmd/power/policy", {"return_at": return_at, "depart_at": depart_at}, timeout_s)

    @staticmethod
    def _entries(entries: Sequence[Mapping[str, Any]]) -> list[dict[str, Any]]:
        if isinstance(entries, (str, bytes, Mapping)) or not isinstance(entries, Sequence):
            raise ValueError("catalog must be an array of objects")
        if not all(isinstance(entry, Mapping) for entry in entries):
            raise ValueError("catalog entries must be objects")
        return copy.deepcopy([dict(entry) for entry in entries])

    def set_waypoints(self, points: Sequence[Mapping[str, Any]], *, map_id: str,
                      expected_points: Sequence[Mapping[str, Any]], timeout_s: float = 3.0) -> Response:
        self._text(map_id, "map_id")
        values = self._entries(points)
        expected = self._entries(expected_points)
        seen = set()
        for point in values:
            self._text(point.get("id"), "waypoint id")
            if point["id"] in seen:
                raise ValueError("waypoint ids must be unique")
            seen.add(point["id"])
            if "name" in point:
                self._text(point["name"], "waypoint name")
            self._finite(point.get("x"), point.get("y"), point.get("theta", 0.0))
        # state/waypoints adds mission progress; it is not part of the stored snapshot.
        for point in values + expected:
            for key in ("status", "captured_from", "localization_ok", "tag_id", "kind"):
                point.pop(key, None)
        return self._client.request("cmd/waypoints/set", {"map_id": map_id, "points": values,
                                    "expected_points": expected}, timeout_s)

    def set_locations(self, locations: Sequence[Mapping[str, Any]], *, map_id: str,
                      expected_locations: Sequence[Mapping[str, Any]], timeout_s: float = 3.0) -> Response:
        self._text(map_id, "map_id")
        values = self._entries(locations)
        seen = set()
        for location in values:
            kind = location.get("kind")
            if kind not in ("home", "dock") or kind in seen:
                raise ValueError("location kinds must be unique home/dock")
            seen.add(kind)
            self._finite(location.get("x"), location.get("y"), location.get("theta"))
        return self._client.request("cmd/locations/set", {"map_id": map_id, "locations": values,
                                    "expected_locations": self._entries(expected_locations)}, timeout_s)

    def set_markers(self, markers: Sequence[Mapping[str, Any]], *, map_id: str,
                    expected_markers: Sequence[Mapping[str, Any]], timeout_s: float = 3.0) -> Response:
        self._text(map_id, "map_id")
        values = self._entries(markers)
        seen = set()
        for marker in values:
            tag_id = marker.get("id")
            if type(tag_id) is not int or not 0 <= tag_id <= 100000 or tag_id in seen:
                raise ValueError("marker ids must be unique integers from 0 to 100000")
            seen.add(tag_id)
            self._finite(marker.get("x"), marker.get("y"))
            if ("z" in marker) != ("yaw" in marker):
                raise ValueError("marker z and yaw must be provided together")
            if "z" in marker:
                self._finite(marker["z"], marker["yaw"])
                if abs(marker["yaw"]) > math.pi + 1e-6:
                    raise ValueError("marker yaw must be from -pi to pi")
        return self._client.request("cmd/markers/set", {"map_id": map_id, "markers": values,
                                    "expected_markers": self._entries(expected_markers)}, timeout_s)
