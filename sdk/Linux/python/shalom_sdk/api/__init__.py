"""Stable public command facade assembled from domain API modules."""

from __future__ import annotations

from ..client import Client
from ._base import _ApiBase
from .configuration import ConfigurationApi
from .inspection import InspectionApi
from .mission import MissionApi
from .navigation import NavigationApi
from .safety import SafetyApi


class RobotApi(SafetyApi, NavigationApi, MissionApi, ConfigurationApi, InspectionApi):
    """Typed public facade with the same protocol coverage as the C++ API."""

    def __init__(self, client: Client) -> None:
        _ApiBase.__init__(self, client)


__all__ = ["ConfigurationApi", "InspectionApi", "MissionApi", "NavigationApi", "RobotApi",
           "SafetyApi"]
