"""Persistent map, power-policy and site-configuration commands."""

from __future__ import annotations

from typing import Any, Mapping, Sequence

from ..types import Response
from ._base import _ApiBase


class ConfigurationApi(_ApiBase):
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

    def set_waypoints(self, points: Sequence[Mapping[str, Any]],
                      timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/waypoints/set", {"points": list(points)}, timeout_s)

    def set_locations(self, locations: Sequence[Mapping[str, Any]],
                      timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/locations/set", {"locations": list(locations)}, timeout_s)

    def set_markers(self, markers: Sequence[Mapping[str, Any]],
                    timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/markers/set", {"markers": list(markers)}, timeout_s)
