# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Public Python API for the robot bridge protocol v1."""

from .api import RobotApi
from .client import Client
from .errors import (ClientError, ConnectionClosed, ProtocolError, RequestTimeout,
                     RobotMismatchError)
from .types import Message, Response

__version__ = "0.4.0"

__all__ = ["Client", "RobotApi", "ClientError", "ConnectionClosed", "Message",
           "ProtocolError", "RequestTimeout", "Response", "RobotMismatchError"]
