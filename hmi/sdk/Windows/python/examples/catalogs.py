#!/usr/bin/env python3
"""Read-only map, mission and arm-pose catalog queries."""
import argparse
import json
import sys
import time
from robot_sdk import Client, ClientError, RobotApi


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("host")
    parser.add_argument("--port", type=int, default=9090)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("port must be 1-65535")
    try:
        with Client() as client:
            client.connect(args.host, args.port)
            robot = RobotApi(client)
            for query in (robot.list_maps, robot.list_missions, robot.list_arm_poses):
                reply = query()
                if not reply.ok:
                    print(reply.error_code, reply.error_message, file=sys.stderr)
                    return 1
            channels = ("state/maps", "state/missions", "state/arm_pose_presets")
            deadline = time.monotonic() + 2
            while any(client.latest(channel) is None for channel in channels) and time.monotonic() < deadline:
                client.poll()
            for channel in channels:
                message = client.latest(channel)
                print(json.dumps({"channel": channel, "data": message.envelope["p"] if message else None},
                                 ensure_ascii=False))
    except (ClientError, ValueError) as exc:
        print(str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
