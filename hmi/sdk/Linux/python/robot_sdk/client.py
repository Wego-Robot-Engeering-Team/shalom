# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Single-threaded TCP client with bounded requests and explicit polling."""

from __future__ import annotations

import copy
import json
import math
import select
import socket
import time
from typing import Any, Callable, Mapping, Optional

from .errors import ClientError, ConnectionClosed, ProtocolError, RequestTimeout, RobotMismatchError
from .framing import FrameDecoder, FramingError, encode_frame
from .types import Message, Response

PROTOCOL_VERSION = 1
HEARTBEAT_PERIOD_S = 0.2
MAX_PENDING_REQUESTS = 128
MessageHandler = Callable[[Message], None]


def _duration(value: float, name: str, allow_zero: bool = False) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"{name} must be finite")
    if value < 0 or (not allow_zero and value == 0):
        raise ValueError(f"{name} must be {'non-negative' if allow_zero else 'positive'}")
    return float(value)


def _channel(value: str) -> None:
    if not isinstance(value, str) or not value or len(value) > 256 or any(ord(c) < 32 for c in value):
        raise ValueError("channel must be a non-empty string without control characters")


class Client:
    """Call poll/run every 200 ms. Use this client from one thread.

    request() maintains heartbeats while waiting. Timed-out commands are never
    retried: they may already have been executed by the robot.
    """

    def __init__(self, on_message: Optional[MessageHandler] = None) -> None:
        self._socket: Optional[socket.socket] = None
        self._decoder = FrameDecoder()
        self._next_heartbeat = 0.0
        self._heartbeat_sequence = 0
        self._request_sequence = 0
        self._robot_id: Optional[str] = None
        self._responses: dict[str, Message] = {}
        self._pending: dict[str, tuple[str, float]] = {}
        self._latest: dict[str, Message] = {}
        self._on_message = on_message
        self._request_active = False

    def __enter__(self) -> Client:
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    @property
    def connected(self) -> bool:
        return self._socket is not None

    @property
    def robot_id(self) -> Optional[str]:
        return self._robot_id

    def latest(self, channel: str) -> Optional[Message]:
        """Copy of the last received state publication for this connection."""
        return copy.deepcopy(self._latest.get(channel))

    def connect(self, host: str, port: int = 9090, timeout_s: float = 5.0) -> None:
        timeout_s = _duration(timeout_s, "timeout_s")
        if not isinstance(host, str) or not host.strip():
            raise ValueError("host must not be empty")
        if type(port) is not int or not 1 <= port <= 65535:
            raise ValueError("port must be an integer from 1 to 65535")
        self.close()
        sock = None
        try:
            sock = socket.create_connection((host, port), timeout=timeout_s)
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            sock.settimeout(min(0.5, timeout_s))
        except OSError as exc:
            if sock is not None:
                sock.close()
            raise ClientError(f"cannot connect to {host}:{port}: {exc}") from exc
        self._socket = sock
        self._next_heartbeat = time.monotonic()
        self._send_heartbeat_if_due()

    def close(self) -> None:
        sock, self._socket = self._socket, None
        if sock is not None:
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            sock.close()
        self._decoder.reset()
        self._responses.clear()
        self._pending.clear()
        self._latest.clear()
        self._robot_id = None
        self._heartbeat_sequence = 0
        # Request IDs are not reused across reconnects.

    def poll(self, timeout_s: float = HEARTBEAT_PERIOD_S) -> list[Message]:
        """Read a batch; poll(0) performs a non-blocking receive attempt."""
        timeout_s = _duration(timeout_s, "timeout_s", allow_zero=True)
        sock = self._require_socket()
        self._expire_requests()
        self._send_heartbeat_if_due()
        wait_s = min(timeout_s, max(0.0, self._next_heartbeat - time.monotonic()))
        try:
            readable, _, _ = select.select([sock], [], [], wait_s)
            if not readable:
                self._send_heartbeat_if_due()
                return []
            chunk = sock.recv(16 * 1024)
        except (OSError, ValueError) as exc:
            self.close()
            raise ClientError(f"socket receive failed: {exc}") from exc
        if not chunk:
            self.close()
            raise ConnectionClosed("robot closed the TCP connection")
        return self._decode(chunk)

    def run(self, on_message: Optional[MessageHandler] = None) -> None:
        handler = on_message or self._on_message
        if handler is None:
            raise ValueError("run requires a message handler")
        while self.connected:
            for message in self.poll():
                handler(message)
                if not self.connected:
                    return

    def send_request(self, channel: str, payload: Optional[Mapping[str, Any]] = None,
                     timeout_s: float = 3.0) -> str:
        """Return a send ID. Consume the reply with take_response() after poll()."""
        _channel(channel)
        timeout_s = _duration(timeout_s, "timeout_s")
        self._require_socket()
        self._expire_requests()
        if len(self._pending) >= MAX_PENDING_REQUESTS:
            raise ClientError("too many pending requests; consume or forget pending replies")
        if payload is not None and not isinstance(payload, Mapping):
            raise ValueError("request payload must be an object")
        self._request_sequence += 1
        request_id = f"c{self._request_sequence}"
        self._send_heartbeat_if_due()
        self._send_envelope({"v": PROTOCOL_VERSION, "t": "req", "ch": channel,
                             "id": request_id, "ts": time.time(), "p": dict(payload or {})})
        self._pending[request_id] = (channel, time.monotonic() + timeout_s)
        return request_id

    def take_response(self, request_id: str) -> Optional[Response]:
        message = self._responses.pop(request_id, None)
        if message is None:
            return None
        self._pending.pop(request_id, None)
        return self._as_response(request_id, message)

    def forget_request(self, request_id: str) -> None:
        """Drop local bookkeeping; this does not cancel robot execution."""
        self._pending.pop(request_id, None)
        self._responses.pop(request_id, None)

    def request(self, channel: str, payload: Optional[Mapping[str, Any]] = None,
                timeout_s: float = 3.0, on_message: Optional[MessageHandler] = None) -> Response:
        if self._request_active:
            raise ClientError("nested request() is unsupported; use send_request() in callbacks")
        timeout_s = _duration(timeout_s, "timeout_s")
        self._request_active = True
        request_id = None
        try:
            request_id = self.send_request(channel, payload, timeout_s)
            deadline = time.monotonic() + timeout_s
            handler = on_message or self._on_message
            while True:
                reply = self.take_response(request_id)
                if reply is not None:
                    return reply
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise RequestTimeout(f"no response for {channel} ({request_id}); execution is unknown")
                for message in self.poll(min(HEARTBEAT_PERIOD_S, remaining)):
                    if not (message.type == "res" and message.envelope.get("id") == request_id):
                        if handler is not None:
                            handler(message)
        finally:
            if request_id is not None:
                self.forget_request(request_id)
            self._request_active = False

    def publish(self, channel: str, payload: Mapping[str, Any]) -> None:
        _channel(channel)
        if channel == "cmd/cmd_vel":
            raise ValueError("TCP cmd/cmd_vel is disabled; manual control uses UDP teleop")
        if not isinstance(payload, Mapping):
            raise ValueError("publish payload must be an object")
        self._send_heartbeat_if_due()
        self._send_envelope({"v": PROTOCOL_VERSION, "t": "pub", "ch": channel,
                             "ts": time.time(), "p": dict(payload)})

    def _expire_requests(self) -> None:
        now = time.monotonic()
        for request_id, (_, deadline) in list(self._pending.items()):
            if deadline < now:
                self.forget_request(request_id)

    def _send_heartbeat_if_due(self) -> None:
        if time.monotonic() < self._next_heartbeat:
            return
        self._heartbeat_sequence += 1
        self._send_envelope({"v": PROTOCOL_VERSION, "t": "hb", "ts": time.time(),
                             "p": {"seq": self._heartbeat_sequence}})
        self._next_heartbeat = time.monotonic() + HEARTBEAT_PERIOD_S

    def _send_envelope(self, envelope: Mapping[str, Any]) -> None:
        outgoing = dict(envelope)
        if self._robot_id:
            outgoing["robot"] = self._robot_id
        frame = encode_frame(outgoing)
        try:
            self._require_socket().sendall(frame)
        except OSError as exc:
            self.close()
            raise ClientError(f"socket send failed: {exc}") from exc

    def _decode(self, chunk: bytes) -> list[Message]:
        try:
            frames = self._decoder.feed(chunk)
            messages = []
            for frame in frames:
                def invalid_constant(value: str) -> None:
                    raise ValueError(f"non-finite JSON constant: {value}")
                envelope = json.loads(frame.header.decode("utf-8"), parse_constant=invalid_constant)
                if not isinstance(envelope, dict) or type(envelope.get("v")) is not int or envelope["v"] != 1:
                    raise ProtocolError("unsupported or malformed protocol version")
                kind = envelope.get("t")
                if kind not in ("hb", "pub", "evt", "res"):
                    raise ProtocolError("unsupported server message type")
                if not isinstance(envelope.get("p"), dict):
                    raise ProtocolError("payload must be an object")
                timestamp = envelope.get("ts")
                if isinstance(timestamp, bool) or not isinstance(timestamp, (int, float)) or not math.isfinite(timestamp):
                    raise ProtocolError("timestamp must be finite")
                if kind != "hb":
                    _channel(envelope.get("ch"))
                robot = envelope.get("robot")
                if robot is not None and (not isinstance(robot, str) or not robot):
                    raise ProtocolError("robot identifier must be a non-empty string")
                if robot:
                    if self._robot_id is not None and self._robot_id != robot:
                        raise RobotMismatchError(f"robot changed from {self._robot_id} to {robot}")
                    self._robot_id = robot
                message = Message(envelope, frame.payload)
                if kind == "res":
                    request_id = envelope.get("id")
                    if not isinstance(request_id, str) or not request_id:
                        raise ProtocolError("response requires a request id")
                    self._as_response(request_id, message)
                    pending = self._pending.get(request_id)
                    if pending is not None:
                        if pending[0] != message.channel:
                            raise ProtocolError("response channel does not match request")
                        if pending[1] >= time.monotonic() and request_id not in self._responses:
                            self._responses[request_id] = copy.deepcopy(message)
                elif kind == "pub":
                    if message.channel.startswith("state/") and (message.channel in self._latest or len(self._latest) < 128):
                        self._latest[message.channel] = copy.deepcopy(message)
                messages.append(message)
            return messages
        except (FramingError, ValueError, UnicodeDecodeError, ProtocolError, RecursionError) as exc:
            self.close()
            if isinstance(exc, ProtocolError):
                raise
            raise ProtocolError(str(exc)) from exc

    @staticmethod
    def _as_response(request_id: str, message: Message) -> Response:
        payload = message.envelope["p"]
        if type(payload.get("ok")) is not bool:
            raise ProtocolError("response ok must be bool")
        error = payload.get("err")
        if error is not None and (not isinstance(error, dict) or not isinstance(error.get("code"), str)
                                  or not isinstance(error.get("msg"), str)):
            raise ProtocolError("response error requires string code and msg")
        return Response(request_id, payload["ok"], error.get("code") if error else None,
                        error.get("msg") if error else None, message)

    def _require_socket(self) -> socket.socket:
        if self._socket is None:
            raise ClientError("client is not connected")
        return self._socket
