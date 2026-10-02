# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""One-shot database reload. This command never submits a motion goal."""

import argparse
from dataclasses import dataclass
import math
import shutil
import sys
import tempfile
import time

import rclpy
from nav2_msgs.srv import ReloadDockDatabase
from rclpy.executors import SingleThreadedExecutor

from .config import (
    DockConfigError,
    dock_type_from_config,
    generate_dock_database,
    read_nav2_config,
)


@dataclass(frozen=True)
class ReloadResult:
    success: bool
    detail: str
    # A timeout cannot cancel a remote ROS service. Keep its file available
    # for a late server callback, rather than claiming the reload failed.
    outcome_unknown: bool = False


def reload_database(node, database, service="/docking_server/reload_database", timeout_sec=5.0):
    if not math.isfinite(timeout_sec) or timeout_sec <= 0:
        raise ValueError("timeout은 양의 유한한 값이어야 합니다")
    deadline = time.monotonic() + timeout_sec
    client = node.create_client(ReloadDockDatabase, service)
    executor = SingleThreadedExecutor(context=node.context)
    executor.add_node(node)
    sent = False
    try:
        if not client.wait_for_service(timeout_sec=max(0.0, deadline - time.monotonic())):
            return ReloadResult(False, f"도킹 DB 서비스를 찾지 못했습니다: {service}")
        request = ReloadDockDatabase.Request(filepath=str(database))
        future = client.call_async(request)
        sent = True
        executor.spin_until_future_complete(
            future, timeout_sec=max(0.0, deadline - time.monotonic()),
        )
        if not future.done():
            return ReloadResult(False, "도킹 DB 응답 시간 초과: 적용 여부 미확인", True)
        response = future.result()
        if response is None:
            return ReloadResult(False, "도킹 DB 응답이 없습니다: 적용 여부 미확인", True)
        if not response.success:
            return ReloadResult(False, "Nav2가 도킹 DB를 거절했습니다 (도킹 중이거나 DB 오류)")
        return ReloadResult(True, "도킹 DB 갱신 완료")
    except (Exception, KeyboardInterrupt) as exc:
        return ReloadResult(False, f"도킹 DB 통신 오류: {exc}", sent)
    finally:
        executor.remove_node(node)
        executor.shutdown()
        node.destroy_client(client)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map", required=True, help="선택된 map.yaml 절대 경로; none은 DB 비우기")
    parser.add_argument("--nav2-config", required=True, help="현재 서버의 도킹 플러그인 설정 파일")
    parser.add_argument("--service", default="/docking_server/reload_database")
    parser.add_argument("--timeout", type=float, default=5.0)
    args, ros_args = parser.parse_known_args(argv)
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("--timeout은 양의 유한한 값이어야 합니다")

    directory = tempfile.mkdtemp(prefix="shalom_docking_reload_")
    keep_file = False
    node = None
    context = rclpy.context.Context()
    try:
        policy = read_nav2_config(args.nav2_config)
        database = generate_dock_database(
            None if args.map == "none" else args.map, directory, dock_type_from_config(policy),
        )
        rclpy.init(args=ros_args, context=context)
        node = rclpy.create_node("dock_database_reload", context=context)
        result = reload_database(node, database, args.service, args.timeout)
        keep_file = result.outcome_unknown
        if result.success:
            node.get_logger().info(f"{result.detail}: map={args.map}")
            return 0
        detail = result.detail
        if keep_file:
            detail += f"; 지연 요청 처리를 위해 생성 파일 보존: {database}"
        node.get_logger().error(detail)
        return 1
    except (DockConfigError, OSError, ValueError, RuntimeError) as exc:
        print(f"도킹 DB 갱신 실패: {exc}", file=sys.stderr)
        return 1
    finally:
        if node is not None:
            node.destroy_node()
        if context.ok():
            context.shutdown()
        if not keep_file:
            shutil.rmtree(directory)


if __name__ == "__main__":
    sys.exit(main())
