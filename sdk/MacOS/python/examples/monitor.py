#!/usr/bin/env python3
"""Read-only protocol monitor. It intentionally never sends cmd/* messages."""

from __future__ import annotations

import argparse
import json

from shalom_sdk import Client, ClientError


def main() -> int:
    parser = argparse.ArgumentParser(description="Shalom robot read-only monitor")
    parser.add_argument("host")
    parser.add_argument("--port", type=int, default=9090)
    args = parser.parse_args()

    client = Client()
    try:
        client.connect(args.host, args.port)
        print(f"connected to {args.host}:{args.port}; Ctrl-C to stop")

        def show(message: object) -> None:
            # Message is deliberately printed as a protocol envelope so this
            # sample remains useful without prescribing a customer UI model.
            print(json.dumps(message.envelope, ensure_ascii=False, separators=(",", ":")))

        client.run(show)
    except KeyboardInterrupt:
        return 0
    except ClientError as exc:
        print(f"error: {exc}")
        return 1
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
