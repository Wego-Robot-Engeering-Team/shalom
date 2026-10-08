# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Keep a navigation mission running with the actual authority publisher."""

import os
import signal
import subprocess
import sys
import time
import unittest

os.environ["ROS_DOMAIN_ID"] = str(120 + os.getpid() % 40)
os.environ["ROS_AUTOMATIC_DISCOVERY_RANGE"] = "LOCALHOST"
os.environ.pop("ROS_LOCALHOST_ONLY", None)

import rclpy
from ament_index_python.packages import get_package_prefix
from nav2_msgs.action import NavigateToPose
from nav_msgs.msg import Odometry
from rclpy.action import ActionServer, CancelResponse
from rclpy.qos import QoSProfile, DurabilityPolicy
from rclpy.task import Future
from interfaces.msg import MissionState, MissionWaypoint, MotionAuthority, MotionStopped, SafetyState
from interfaces.srv import ConfigureMission, MissionControl, SafetyCommand

MISSION_BINARY = sys.argv.pop(1)


class AuthorityHeartbeatTest(unittest.TestCase):
    def test_mission_runs_and_only_pauses_when_authority_publisher_stops(self):
        rclpy.init()
        node = rclpy.create_node("mission_authority_heartbeat_test")
        processes = []
        states = []
        authorities = []
        transient = QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL)
        node.create_subscription(MissionState, "/mission/state", states.append, transient)
        node.create_subscription(MotionAuthority, "/motion/authority", authorities.append, transient)
        safety_pub = node.create_publisher(SafetyState, "/safety/state", transient)
        odom_pub = node.create_publisher(Odometry, "kiss/odometry", 10)
        stopped_pub = node.create_publisher(MotionStopped, "/motion/stopped", 10)
        stopped_sequence = 0

        def publish():
            nonlocal stopped_sequence
            stamp = node.get_clock().now().to_msg()
            safety_pub.publish(SafetyState(stamp=stamp, sequence=1,
                state=SafetyState.NORMAL, motion_permitted=True))
            odom = Odometry()
            odom.header.stamp = stamp
            odom.pose.pose.orientation.w = 1.0
            odom_pub.publish(odom)
            stopped_sequence += 1
            stopped_pub.publish(MotionStopped(stamp=stamp, sequence=stopped_sequence,
                resource=MotionStopped.BASE, stopped=True, source="test"))

        def safety_command(_, response):
            response.accepted = True
            response.state.state = SafetyState.NORMAL
            response.state.motion_permitted = True
            return response

        async def execute(goal):
            done = Future()

            def tick():
                if goal.is_cancel_requested:
                    goal.canceled()
                    timer.cancel()
                    done.set_result(NavigateToPose.Result())

            timer = node.create_timer(0.02, tick)
            try:
                return await done
            finally:
                node.destroy_timer(timer)

        def spin_until(predicate, timeout=4):
            deadline = time.monotonic() + timeout
            while not predicate() and time.monotonic() < deadline:
                rclpy.spin_once(node, timeout_sec=0.02)
            self.assertTrue(predicate(), "condition not reached before timeout")

        def call(client, request):
            self.assertTrue(client.wait_for_service(timeout_sec=4))
            future = client.call_async(request)
            spin_until(future.done)
            response = future.result()
            self.assertTrue(response.accepted, response.detail)
            return response

        try:
            node.create_timer(0.05, publish)
            node.create_service(SafetyCommand, "/safety/command", safety_command)
            action = ActionServer(node, NavigateToPose, "navigate_to_pose",
                execute_callback=execute, cancel_callback=lambda _: CancelResponse.ACCEPT)
            interlock = subprocess.Popen([
                get_package_prefix("motion_interlock_manager") +
                "/lib/motion_interlock_manager/motion_interlock_manager_node"],
                stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
            processes.append(interlock)
            processes.append(subprocess.Popen([MISSION_BINARY, "--ros-args", "-p",
                "available_capabilities:=[navigation]"], stdout=subprocess.DEVNULL,
                stderr=subprocess.STDOUT))
            spin_until(lambda: authorities and states)
            configure = node.create_client(ConfigureMission, "/mission/configure")
            control = node.create_client(MissionControl, "/mission/control")
            request = ConfigureMission.Request(request_id="configure", operator_id="test")
            request.plan.mission_id = "test-mission"
            request.plan.map_id = "test-map"
            request.plan.revision = 1
            waypoint = MissionWaypoint(waypoint_id="wp", operation=MissionWaypoint.NAVIGATE_ONLY)
            waypoint.target_pose.header.frame_id = "map"
            waypoint.target_pose.pose.position.x = 2.0
            waypoint.target_pose.pose.orientation.w = 1.0
            request.plan.waypoints = [waypoint]
            call(configure, request)
            call(control, MissionControl.Request(request_id="start", operator_id="test",
                mission_id="test-mission", operation=MissionControl.Request.START))
            spin_until(lambda: states[-1].state == MissionState.RUNNING)
            count = len(authorities)
            deadline = time.monotonic() + 2.0
            while time.monotonic() < deadline:
                rclpy.spin_once(node, timeout_sec=0.02)
                self.assertEqual(states[-1].state, MissionState.RUNNING, states[-1].reason_code)
            self.assertGreaterEqual(len(authorities) - count, 6)
            interlock.send_signal(signal.SIGINT)
            interlock.wait(timeout=3)
            spin_until(lambda: states[-1].state == MissionState.PAUSED)
            self.assertTrue(any(s.reason_code == "MISSION_AUTHORITY_LOST" for s in states))
            action.destroy()
        finally:
            for process in processes:
                if process.poll() is None:
                    process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=2)
            node.destroy_node()
            rclpy.shutdown()


if __name__ == "__main__":
    unittest.main()
