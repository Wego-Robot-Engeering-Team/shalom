"""Synchronous customer API for the Shalom robot TCP bridge."""

from __future__ import annotations

import json
import select
import socket
import time
from typing import Any, Callable, Mapping, Optional

from .errors import (ClientError, ConnectionClosed, ProtocolError, RequestTimeout,
                     RobotMismatchError)
from .framing import FrameDecoder, FramingError, encode_frame
from .types import Message, Response

PROTOCOL_VERSION = 1
HEARTBEAT_PERIOD_S = 0.2


MessageHandler = Callable[[Message], None]


class Client:
    """Single-threaded Shalom protocol client.

    Call :meth:`poll` regularly or use :meth:`run`; either keeps the mandatory
    5 Hz heartbeat alive. The class does not start a hidden thread, so socket
    lifetime and callbacks stay in the application's own event thread.
    """

    def __init__(self) -> None:
        self._socket: Optional[socket.socket] = None
        self._decoder = FrameDecoder()
        self._next_heartbeat = 0.0
        self._heartbeat_sequence = 0
        self._request_sequence = 0
        self._robot_id: Optional[str] = None
        self._responses: dict[str, Message] = {}

    @property
    def connected(self) -> bool:
        return self._socket is not None

    @property
    def robot_id(self) -> Optional[str]:
        """Robot identifier pinned from the first received frame."""
        return self._robot_id

    def connect(self, host: str, port: int = 9090, timeout_s: float = 5.0) -> None:
        self.close()
        try:
            sock = socket.create_connection((host, port), timeout=timeout_s)
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        except OSError as exc:
            raise ClientError(f"cannot connect to {host}:{port}: {exc}") from exc
        self._socket = sock
        self._decoder.reset()
        self._responses.clear()
        self._robot_id = None
        self._heartbeat_sequence = 0
        self._request_sequence = 0
        self._next_heartbeat = time.monotonic()

    def close(self) -> None:
        if self._socket is not None:
            try:
                self._socket.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            self._socket.close()
        self._socket = None
        self._decoder.reset()

    def poll(self, timeout_s: float = HEARTBEAT_PERIOD_S) -> list[Message]:
        """Receive available messages while maintaining heartbeats.

        ``timeout_s`` is a maximum wait. It may return earlier after a heartbeat
        deadline so callers can safely use a much larger UI/event-loop timeout.
        """
        sock = self._require_socket()
        deadline = time.monotonic() + max(0.0, timeout_s)
        while True:
            self._send_heartbeat_if_due()
            now = time.monotonic()
            if now >= deadline:
                return []
            wait_s = min(deadline - now, max(0.0, self._next_heartbeat - now))
            try:
                readable, _, _ = select.select([sock], [], [], wait_s)
            except OSError as exc:
                raise ClientError(f"socket wait failed: {exc}") from exc
            if not readable:
                continue
            try:
                chunk = sock.recv(16 * 1024)
            except OSError as exc:
                raise ClientError(f"socket receive failed: {exc}") from exc
            if not chunk:
                self.close()
                raise ConnectionClosed("robot closed the TCP connection")
            return self._decode(chunk)

    def run(self, on_message: MessageHandler) -> None:
        """Run until the peer closes, forwarding every decoded message."""
        while True:
            for message in self.poll():
                on_message(message)

    def send_request(self, channel: str, payload: Optional[Mapping[str, Any]] = None) -> str:
        """Send a ``req`` and return its correlation id without waiting."""
        if not channel:
            raise ValueError("channel must not be empty")
        self._request_sequence += 1
        request_id = f"c{self._request_sequence}"
        self._send_envelope({"v": PROTOCOL_VERSION, "t": "req", "ch": channel,
                             "id": request_id, "ts": time.time(), "p": payload or {}})
        return request_id

    def request(self, channel: str, payload: Optional[Mapping[str, Any]] = None,
                timeout_s: float = 3.0, on_message: Optional[MessageHandler] = None) -> Response:
        """Send a request and wait for its matching ``res`` frame."""
        request_id = self.send_request(channel, payload)
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            response = self._responses.pop(request_id, None)
            if response is not None:
                return self._as_response(request_id, response)
            for message in self.poll(min(HEARTBEAT_PERIOD_S, deadline - time.monotonic())):
                if message.envelope.get("id") != request_id and on_message is not None:
                    on_message(message)
        raise RequestTimeout(f"no response for {channel} ({request_id})")

    def publish(self, channel: str, payload: Mapping[str, Any]) -> None:
        """Publish one protocol sample; `cmd/cmd_vel` is the only public command use."""
        if not channel:
            raise ValueError("channel must not be empty")
        self._send_envelope({"v": PROTOCOL_VERSION, "t": "pub", "ch": channel,
                             "ts": time.time(), "p": dict(payload)})

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
        try:
            self._require_socket().sendall(encode_frame(outgoing))
        except OSError as exc:
            self.close()
            raise ClientError(f"socket send failed: {exc}") from exc

    def _decode(self, chunk: bytes) -> list[Message]:
        try:
            frames = self._decoder.feed(chunk)
        except FramingError as exc:
            self.close()
            raise ProtocolError(str(exc)) from exc
        messages: list[Message] = []
        for frame in frames:
            try:
                envelope = json.loads(frame.header.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                self.close()
                raise ProtocolError("invalid JSON envelope") from exc
            if not isinstance(envelope, dict) or envelope.get("v") != PROTOCOL_VERSION:
                self.close()
                raise ProtocolError("unsupported or malformed protocol envelope")
            if not isinstance(envelope.get("t"), str):
                self.close()
                raise ProtocolError("envelope is missing message type")
            robot = envelope.get("robot")
            if robot is not None and not isinstance(robot, str):
                self.close()
                raise ProtocolError("robot identifier must be a string")
            if robot:
                if self._robot_id is None:
                    self._robot_id = robot
                elif self._robot_id != robot:
                    self.close()
                    raise RobotMismatchError(f"robot changed from {self._robot_id} to {robot}")
            message = Message(envelope, frame.payload)
            if message.type == "res" and isinstance(envelope.get("id"), str):
                self._responses[envelope["id"]] = message
            messages.append(message)
        return messages

    @staticmethod
    def _as_response(request_id: str, message: Message) -> Response:
        payload = message.envelope.get("p", {})
        if not isinstance(payload, Mapping):
            raise ProtocolError("response payload must be an object")
        error = payload.get("err")
        if error is not None and not isinstance(error, Mapping):
            raise ProtocolError("response error must be an object")
        return Response(request_id=request_id, ok=payload.get("ok") is True,
                        error_code=str(error.get("code")) if error and error.get("code") else None,
                        error_message=str(error.get("msg")) if error and error.get("msg") else None,
                        message=message)

    def _require_socket(self) -> socket.socket:
        if self._socket is None:
            raise ClientError("client is not connected")
        return self._socket
