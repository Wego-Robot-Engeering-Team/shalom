# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Public values returned by the low-level protocol client."""

from __future__ import annotations

from dataclasses import dataclass, field
import time
from typing import Any, Mapping, Optional


@dataclass(frozen=True)
class Message:
    """One decoded protocol envelope and its optional binary payload."""

    envelope: Mapping[str, Any]
    payload: bytes
    received_monotonic_s: float = field(default_factory=time.monotonic)

    @property
    def type(self) -> str:
        return str(self.envelope["t"])

    @property
    def channel(self) -> str:
        return str(self.envelope.get("ch", ""))

    @property
    def age_s(self) -> float:
        return max(0.0, time.monotonic() - self.received_monotonic_s)


@dataclass(frozen=True)
class Response:
    """The response correlated to one request id."""

    request_id: str
    ok: bool
    error_code: Optional[str]
    error_message: Optional[str]
    message: Message

    @property
    def data(self) -> Mapping[str, Any]:
        return self.message.envelope["p"]
