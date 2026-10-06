# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""SDK exceptions. Transport and protocol failures are never hidden."""


class ClientError(RuntimeError):
    """Base class for connection and protocol-client failures."""


class ProtocolError(ClientError):
    """The peer sent a malformed or incompatible protocol message."""


class RobotMismatchError(ProtocolError):
    """A connection changed to a different robot identifier."""


class ConnectionClosed(ClientError):
    """The robot closed the TCP connection."""


class RequestTimeout(ClientError):
    """A request did not receive its matching response before the deadline."""
