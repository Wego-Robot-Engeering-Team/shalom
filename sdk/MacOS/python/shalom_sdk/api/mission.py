"""Mission lifecycle commands."""

from __future__ import annotations

from ..types import Response
from ._base import _ApiBase


class MissionApi(_ApiBase):
    def mission_start(self, timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/mission/start", timeout_s=timeout_s)

    def mission_pause(self, timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/mission/pause", timeout_s=timeout_s)

    def mission_resume(self, timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/mission/resume", timeout_s=timeout_s)

    def mission_stop(self, timeout_s: float = 3.0) -> Response:
        return self._client.request("cmd/mission/stop", timeout_s=timeout_s)
