"""Public values returned by the low-level protocol client."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Mapping, Optional


@dataclass(frozen=True)
class Message:
    """One decoded protocol envelope and its optional binary payload."""

    envelope: Mapping[str, Any]
    payload: bytes

    @property
    def type(self) -> str:
        return str(self.envelope["t"])

    @property
    def channel(self) -> str:
        return str(self.envelope.get("ch", ""))


@dataclass(frozen=True)
class Response:
    """The response correlated to one request id."""

    request_id: str
    ok: bool
    error_code: Optional[str]
    error_message: Optional[str]
    message: Message
