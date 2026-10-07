#!/usr/bin/env python3
# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary
"""Read-only monitor; no cmd/* messages are sent."""
from __future__ import annotations
import argparse
import json
import math
import sys
import time
from robot_sdk import Client, ClientError


def main() -> int:
    parser = argparse.ArgumentParser(description="robot state monitor")
    parser.add_argument("host")
    parser.add_argument("--port", type=int, default=9090)
    parser.add_argument("--channel", action="append", help="only show this channel; repeatable")
    parser.add_argument("--duration", type=float, help="stop after this many seconds")
    parser.add_argument("--heartbeat", action="store_true", help="also show heartbeat replies")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("port must be 1-65535")
    if args.duration is not None and (not math.isfinite(args.duration) or args.duration <= 0):
        parser.error("duration must be positive and finite")
    try:
        with Client() as client:
            client.connect(args.host, args.port)
            print(f"connected to {args.host}:{args.port}", file=sys.stderr)
            deadline = time.monotonic() + args.duration if args.duration is not None else math.inf
            while time.monotonic() < deadline:
                for message in client.poll(min(0.2, max(0.0, deadline - time.monotonic()))):
                    if message.type == "hb" and not args.heartbeat:
                        continue
                    if args.channel and message.channel not in args.channel:
                        continue
                    print(json.dumps(message.envelope, ensure_ascii=False, separators=(",", ":")))
    except KeyboardInterrupt:
        return 0
    except ClientError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
