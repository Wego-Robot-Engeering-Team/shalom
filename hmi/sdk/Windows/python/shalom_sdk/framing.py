"""Wire framing for the Shalom bridge protocol v1."""

from __future__ import annotations

import json
import struct
from dataclasses import dataclass
from typing import Any, Mapping, Union

MAGIC = b"SHLM"
MAX_BODY_LENGTH = 32 * 1024 * 1024
_PREFIX_LENGTH = 8
_MIN_BODY_LENGTH = 4


class FramingError(ValueError):
    """The peer sent bytes that cannot be a Shalom protocol frame."""


@dataclass(frozen=True)
class Frame:
    header: bytes
    payload: bytes


def encode_frame(header: Union[str, Mapping[str, Any]], payload: bytes = b"") -> bytes:
    """Encode one complete frame; all integer fields are little-endian."""
    if isinstance(header, str):
        encoded_header = header.encode("utf-8")
    else:
        encoded_header = json.dumps(header, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    body_length = _MIN_BODY_LENGTH + len(encoded_header) + len(payload)
    if body_length > MAX_BODY_LENGTH:
        raise FramingError("frame body exceeds 32 MiB limit")
    return MAGIC + struct.pack("<II", body_length, len(encoded_header)) + encoded_header + payload


class FrameDecoder:
    """Incremental decoder for a TCP byte stream.

    ``feed`` accepts arbitrary fragments and returns every complete frame. A
    malformed frame is terminal: callers must close the connection rather than
    search the stream for the next magic value.
    """

    def __init__(self) -> None:
        self._buffer = bytearray()

    def reset(self) -> None:
        self._buffer.clear()

    def feed(self, data: bytes) -> list[Frame]:
        self._buffer.extend(data)
        frames: list[Frame] = []
        while True:
            if len(self._buffer) < _PREFIX_LENGTH:
                return frames
            if bytes(self._buffer[:4]) != MAGIC:
                raise FramingError("invalid frame magic")
            body_length = struct.unpack_from("<I", self._buffer, 4)[0]
            if body_length < _MIN_BODY_LENGTH or body_length > MAX_BODY_LENGTH:
                raise FramingError("invalid frame body length")
            total = _PREFIX_LENGTH + body_length
            if len(self._buffer) < total:
                return frames
            header_length = struct.unpack_from("<I", self._buffer, 8)[0]
            if _MIN_BODY_LENGTH + header_length > body_length:
                raise FramingError("invalid frame header length")
            header_begin = 12
            header_end = header_begin + header_length
            frames.append(Frame(bytes(self._buffer[header_begin:header_end]),
                                bytes(self._buffer[header_end:total])))
            del self._buffer[:total]
