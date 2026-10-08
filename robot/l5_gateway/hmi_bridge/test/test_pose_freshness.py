# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Source TF freshness and stationary-state checks in an isolated ROS domain."""

import time
import unittest

import test_navigation_speed as fixture

import rclpy
from geometry_msgs.msg import TransformStamped
from rcl_interfaces.srv import SetParameters
from rclpy.parameter import Parameter
from rosgraph_msgs.msg import Clock
from tf2_ros import TransformBroadcaster


class PoseFreshnessTest(fixture.NavigationSpeedTest):
    def setUp(self):
        super().setUp()
        self.broadcaster = TransformBroadcaster(self.node)
        self.spin_for(0.5)

    def collect(self, duration):
        reports = []
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            rclpy.spin_once(self.node, timeout_sec=0.01)
            while len(self.buffer) >= 8:
                magic, size = fixture.struct.unpack_from("<II", self.buffer)
                self.assertEqual(magic, 0x4D4C4853)
                if len(self.buffer) < size + 8:
                    break
                header_size = fixture.struct.unpack_from("<I", self.buffer, 8)[0]
                reports.append(fixture.json.loads(self.buffer[12:12 + header_size]))
                self.buffer = self.buffer[8 + size:]
            try:
                chunk = self.peer.recv(65536)
                self.assertTrue(chunk, "bridge closed its TCP connection")
                self.buffer += chunk
            except fixture.socket.timeout:
                pass
        return reports

    def send_tf(self, x=4.2, stamp=None):
        transform = TransformStamped()
        transform.header.frame_id = "map"
        transform.child_frame_id = "base_link"
        transform.header.stamp = stamp or self.node.get_clock().now().to_msg()
        transform.transform.translation.x = x
        transform.transform.rotation.w = 1.0
        self.broadcaster.sendTransform(transform)
        return transform.header.stamp

    def pose(self):
        return self.receive(lambda e: e.get("ch") == "state/pose")["p"]

    def assert_stationary_confirmation_missing(self):
        result = self.request({"posture": "stand_down"}, "cmd/base/posture")
        self.assertFalse(result["ok"])
        self.assertIn("정지 상태를 확인", result["err"]["msg"])

    def test_cached_transform_does_not_keep_pose_or_motion_fresh(self):
        self.send_tf()
        first = self.pose()
        self.assertIsNone(first["speed"], "one sample cannot measure velocity")
        self.assertTrue(first["moving"])
        self.spin_for(0.15)
        self.send_tf()
        confirmed = self.pose()
        self.assertEqual(confirmed["speed"], 0.0)
        self.assertFalse(confirmed["moving"])
        self.collect(1.3)
        self.assertFalse(any(e.get("ch") == "state/pose" for e in self.collect(0.35)),
                         "cached TF was repeatedly published as a new position")
        self.assert_stationary_confirmation_missing()

        # Reconnect a source at a different position. Do not differentiate across
        # the gap, or confuse the first recovered pose with a stopped robot.
        self.send_tf(x=8.4)
        recovered = self.pose()
        self.assertIsNone(recovered["speed"])
        self.assertTrue(recovered["moving"])
        self.spin_for(0.15)
        self.send_tf(x=8.4)
        self.assertFalse(self.pose()["moving"])

    def test_old_source_timestamp_is_not_a_fresh_position(self):
        stamp = self.node.get_clock().now().to_msg()
        stamp.sec -= 5
        self.send_tf(stamp=stamp)
        self.assertFalse(any(e.get("ch") == "state/pose" for e in self.collect(0.5)))
        self.assert_stationary_confirmation_missing()

    def test_stopped_sim_clock_does_not_preserve_stationary_confirmation(self):
        client = self.node.create_client(SetParameters, "/hmi_bridge/set_parameters")
        self.assertTrue(client.wait_for_service(timeout_sec=3))
        request = SetParameters.Request(parameters=[Parameter("use_sim_time", value=True).to_parameter_msg()])
        future = client.call_async(request)
        rclpy.spin_until_future_complete(self.node, future, timeout_sec=3)
        self.assertTrue(future.done())
        self.assertTrue(future.result().results[0].successful)
        clock_pub = self.node.create_publisher(Clock, "/clock", 10)
        self.spin_for(0.3)
        clock = Clock()
        clock.clock.sec = 100
        for _ in range(3):
            clock_pub.publish(clock)
            self.spin_for(0.05)
        self.send_tf(stamp=clock.clock)
        self.assertIsNone(self.pose()["speed"])
        clock.clock.nanosec = 200_000_000
        clock_pub.publish(clock)
        self.spin_for(0.15)
        self.send_tf(stamp=clock.clock)
        self.assertFalse(self.pose()["moving"])
        self.collect(1.3)
        # Repeating the same source stamp while ROS time is frozen must not
        # renew either the HMI pose stream or the stationary-state check.
        self.send_tf(stamp=clock.clock)
        self.assertFalse(any(e.get("ch") == "state/pose" for e in self.collect(0.35)))
        self.assert_stationary_confirmation_missing()


if __name__ == "__main__":
    suite = unittest.TestSuite(PoseFreshnessTest(name) for name in PoseFreshnessTest.__dict__
                               if name.startswith("test_"))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    raise SystemExit(not result.wasSuccessful())
