"""브릿지 프로토콜 v1 봉투 인코딩/디코딩.

명세: docs/bridge_protocol.md
이 모듈은 GCS 와 브릿지 노드가 **동일하게** 사용한다.
ROS2/Qt 어느 쪽에도 의존하지 않는 순수 파이썬으로 유지할 것 —
브릿지(rclpy)와 관제(PySide6) 양쪽에서 import 되기 때문이다.
"""

from __future__ import annotations

import json
import struct
import time
import uuid
from dataclasses import dataclass, field
from typing import Any

PROTOCOL_VERSION = 1

# 메시지 종류
T_HB = "hb"
T_SUB = "sub"
T_UNSUB = "unsub"
T_PUB = "pub"
T_REQ = "req"
T_RES = "res"
T_EVT = "evt"

_HEADER_LEN = struct.Struct("<I")   # uint32 little-endian


class ProtocolError(Exception):
    """봉투를 해석할 수 없을 때. 연결을 닫아야 하는 수준의 오류."""


@dataclass
class Envelope:
    t: str
    ch: str = ""
    p: dict[str, Any] = field(default_factory=dict)
    id: str | None = None
    ts: float = field(default_factory=time.time)
    seq: int | None = None
    v: int = PROTOCOL_VERSION
    payload: bytes | None = None    # 바이너리 프레임의 본문

    # ---- 인코딩 --------------------------------------------------------
    def to_dict(self) -> dict[str, Any]:
        d: dict[str, Any] = {"v": self.v, "t": self.t, "ts": round(self.ts, 4)}
        if self.ch:
            d["ch"] = self.ch
        if self.id is not None:
            d["id"] = self.id
        if self.seq is not None:
            d["seq"] = self.seq
        if self.p:
            d["p"] = self.p
        return d

    def encode(self) -> str | bytes:
        """payload 유무에 따라 텍스트/바이너리 프레임을 만든다."""
        header = json.dumps(self.to_dict(), separators=(",", ":"), ensure_ascii=False)
        if self.payload is None:
            return header
        hb = header.encode("utf-8")
        return _HEADER_LEN.pack(len(hb)) + hb + self.payload

    # ---- 디코딩 --------------------------------------------------------
    @classmethod
    def decode(cls, frame: str | bytes) -> "Envelope":
        if isinstance(frame, (bytes, bytearray, memoryview)):
            return cls._decode_binary(bytes(frame))
        return cls._from_dict(cls._loads(frame))

    @staticmethod
    def _loads(s: str) -> dict[str, Any]:
        try:
            d = json.loads(s)
        except json.JSONDecodeError as e:
            raise ProtocolError(f"JSON 파싱 실패: {e}") from e
        if not isinstance(d, dict):
            raise ProtocolError("봉투는 객체여야 한다")
        return d

    @classmethod
    def _decode_binary(cls, raw: bytes) -> "Envelope":
        if len(raw) < _HEADER_LEN.size:
            raise ProtocolError("바이너리 프레임이 헤더 길이보다 짧다")
        (hlen,) = _HEADER_LEN.unpack_from(raw, 0)
        start = _HEADER_LEN.size
        end = start + hlen
        if end > len(raw):
            raise ProtocolError(f"헤더 길이 {hlen} 가 프레임 크기 {len(raw)} 를 넘는다")
        env = cls._from_dict(cls._loads(raw[start:end].decode("utf-8")))
        env.payload = raw[end:]
        return env

    @classmethod
    def _from_dict(cls, d: dict[str, Any]) -> "Envelope":
        v = d.get("v")
        if v != PROTOCOL_VERSION:
            raise ProtocolError(f"프로토콜 버전 불일치: 수신 {v}, 기대 {PROTOCOL_VERSION}")
        t = d.get("t")
        if not isinstance(t, str):
            raise ProtocolError("필드 't' 누락")
        p = d.get("p") or {}
        if not isinstance(p, dict):
            raise ProtocolError("필드 'p' 는 객체여야 한다")
        return cls(
            t=t,
            ch=d.get("ch", ""),
            p=p,
            id=d.get("id"),
            ts=float(d.get("ts", 0.0)),
            seq=d.get("seq"),
            v=v,
        )


# ---- 생성 헬퍼 ---------------------------------------------------------

def request(channel: str, payload: dict[str, Any] | None = None) -> Envelope:
    return Envelope(t=T_REQ, ch=channel, p=payload or {}, id=uuid.uuid4().hex[:12])


def response(req: Envelope, ok: bool = True, err_code: str = "", err_msg: str = "",
             **extra: Any) -> Envelope:
    p: dict[str, Any] = {"ok": ok, **extra}
    if not ok:
        p["err"] = {"code": err_code, "msg": err_msg}
    return Envelope(t=T_RES, ch=req.ch, p=p, id=req.id)


def publish(channel: str, payload: dict[str, Any], seq: int | None = None,
            binary: bytes | None = None) -> Envelope:
    return Envelope(t=T_PUB, ch=channel, p=payload, seq=seq, payload=binary)


def event(channel: str, payload: dict[str, Any]) -> Envelope:
    return Envelope(t=T_EVT, ch=channel, p=payload)


def heartbeat(seq: int) -> Envelope:
    return Envelope(t=T_HB, p={"seq": seq})


def subscribe(channels: list[str]) -> Envelope:
    return Envelope(t=T_SUB, ch="", p={"channels": channels}, id=uuid.uuid4().hex[:12])
