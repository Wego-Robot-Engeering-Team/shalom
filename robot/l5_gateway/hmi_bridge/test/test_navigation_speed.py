# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Exercise HMI commands, robot storage and gated motion requests in isolation."""

import json
import os
from pathlib import Path
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest

os.environ["ROS_DOMAIN_ID"] = str(170 + os.getpid() % 50)
os.environ["ROS_AUTOMATIC_DISCOVERY_RANGE"] = "LOCALHOST"
os.environ.pop("ROS_LOCALHOST_ONLY", None)

import rclpy
from nav2_msgs.msg import SpeedLimit
from sensor_msgs.msg import JointState
from interfaces.msg import MotionAuthority, SafetyState, MissionState
from interfaces.srv import AuthorityRequest, SafetyCommand, MissionControl
from rclpy.qos import QoSProfile, DurabilityPolicy
from rcl_interfaces.msg import ParameterType, ParameterValue
from rcl_interfaces.srv import GetParameters, SetParametersAtomically
from lifecycle_msgs.msg import State, Transition
from lifecycle_msgs.srv import GetState, ChangeState

BRIDGE = sys.argv.pop(1)


class NavigationSpeedTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="shalom-speed-test-")
        self.root = Path(self.directory.name)
        self.process = None
        self.peer = None
        self.buffer = b""
        self.sequence = 0
        self.map_directory = self.root / "maps" / "test-map"
        self.map_directory.mkdir(parents=True)
        (self.map_directory / "map.yaml").write_text("image: map.pgm\n")
        self.point = {"id": "wp", "name": "Waypoint", "x": 1.0, "y": 2.0,
                      "theta": 0.5, "description": "Keep this description"}
        (self.map_directory / "waypoints.json").write_text(json.dumps({"points": [self.point]}))
        self.mission = {"id": "mission", "name": "Mission", "revision": 1, "archived": False,
                        "steps": [{"id": "step", "type": "navigate", "location_id": "wp"}]}
        (self.map_directory / "missions.json").write_text(json.dumps({"missions": [self.mission]}))
        self.dock = {"kind": "dock", "x": 0.0, "y": 0.0, "theta": 0.0}
        (self.map_directory / "locations.json").write_text(json.dumps({"locations": [self.dock]}))
        rclpy.init()
        self.node = rclpy.create_node("navigation_speed_test")
        self.start_bridge()

    def tearDown(self):
        self.stop_bridge()
        self.node.destroy_node()
        rclpy.shutdown()
        self.directory.cleanup()

    def start_bridge(self):
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            port = reservation.getsockname()[1]
        self.buffer = b""
        self.process = subprocess.Popen(
            [BRIDGE, "--ros-args", "-p", "robot_id:=speed-test", "-p", f"port:={port}",
             "-p", f"robot_data_dir:={self.root}", "-p", f"maps_dir:={self.root / 'maps'}",
             "-p", f"initial_map:={self.map_directory / 'map.yaml'}",
             "-p", "arm.execution_enabled:=true"] + getattr(self, "extra_bridge_args", []),
            stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            self.assertIsNone(self.process.poll(), "bridge exited before opening its TCP port")
            try:
                self.peer = socket.create_connection(("127.0.0.1", port), timeout=0.2)
                self.peer.settimeout(0.05)
                return
            except OSError:
                time.sleep(0.05)
        self.fail("bridge did not open its TCP port")

    def stop_bridge(self):
        if self.peer:
            self.peer.close()
            self.peer = None
        if self.process:
            self.process.send_signal(signal.SIGINT)
            try:
                self.process.wait(timeout=4)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=2)
            self.process = None

    def request(self, payload, channel="cmd/navigation/speed_limit"):
        self.sequence += 1
        request_id = f"speed-{self.sequence}"
        header = json.dumps({"v": 1, "t": "req", "robot": "speed-test", "id": request_id,
                             "ch": channel, "p": payload}).encode()
        self.peer.sendall(struct.pack("<III", 0x4D4C4853, len(header) + 4, len(header)) + header)
        return self.receive(lambda e: e.get("t") == "res" and e.get("id") == request_id)["p"]

    def receive(self, predicate):
        deadline = time.monotonic() + getattr(self, "receive_timeout_s", 4)
        while time.monotonic() < deadline:
            rclpy.spin_once(self.node, timeout_sec=0.01)
            while len(self.buffer) >= 8:
                magic, size = struct.unpack_from("<II", self.buffer)
                self.assertEqual(magic, 0x4D4C4853)
                if len(self.buffer) < size + 8:
                    break
                header_size = struct.unpack_from("<I", self.buffer, 8)[0]
                envelope = json.loads(self.buffer[12:12 + header_size])
                self.buffer = self.buffer[8 + size:]
                if predicate(envelope):
                    return envelope
            try:
                chunk = self.peer.recv(65536)
                self.assertTrue(chunk, "bridge closed its TCP connection")
                self.buffer += chunk
            except socket.timeout:
                pass
        self.fail("expected bridge message was not received")

    def report(self):
        return self.receive(lambda e: e.get("ch") == "state/navigation_speed")["p"]

    def spin_for(self, duration):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            rclpy.spin_once(self.node, timeout_sec=0.02)

    def fake_controller(self):
        self.controller_values = {}
        self.controller_sets = []
        self.controller_accepts = True
        self.controller_active = True

        def state(_, response):
            response.current_state.id = State.PRIMARY_STATE_ACTIVE if self.controller_active else State.PRIMARY_STATE_INACTIVE
            return response

        def get(request, response):
            response.values = [ParameterValue(type=ParameterType.PARAMETER_DOUBLE,
                double_value=float(self.controller_values[name])) if name in self.controller_values
                else ParameterValue() for name in request.names]
            return response

        def set_values(request, response):
            self.controller_sets.append({p.name: p.value.double_value for p in request.parameters})
            response.result.successful = self.controller_accepts
            response.result.reason = "" if self.controller_accepts else "Controller rejected change"
            if self.controller_accepts:
                self.controller_values.update(self.controller_sets[-1])
            return response

        self.node.create_service(GetParameters, "/controller_server/get_parameters", get)
        self.node.create_service(GetState, "/controller_server/get_state", state)
        self.node.create_service(SetParametersAtomically, "/controller_server/set_parameters_atomically", set_values)

    def expect_applied(self, linear, angular, applied=True):
        state = self.receive(lambda e: e.get("ch") == "state/navigation_speed" and
            e["p"].get("autonomous_applied") == applied and
            e["p"].get("speed_limit_mps") == linear and
            e["p"].get("angular_speed_limit_rps") == angular)["p"]
        if applied:
            self.assertAlmostEqual(self.controller_values["FollowPath.vx_max"], linear)
            self.assertAlmostEqual(self.controller_values["FollowPath.vx_min"], -linear * 2 / 3)
            self.assertAlmostEqual(self.controller_values["FollowPath.vy_max"], linear * 2 / 3)
            self.assertAlmostEqual(self.controller_values["FollowPath.wz_max"], angular)
        return state

    def test_waypoint_conflict_reference_check_and_atomic_storage(self):
        channel = "cmd/waypoints/set"
        changed = dict(self.point, name="Changed")
        payload = {"map_id": "test-map", "expected_points": [self.point], "points": [changed]}
        self.assertTrue(self.request(payload, channel)["ok"])
        stored = self.map_directory / "waypoints.json"
        self.assertEqual(json.loads(stored.read_text()), {"points": [changed]})
        self.assertFalse(self.request(payload, channel)["ok"], "stale snapshot was accepted")
        self.assertFalse(self.request(dict(payload, map_id="another-map"), channel)["ok"])
        self.assertFalse(self.request({"map_id": "test-map", "expected_points": [changed],
                                       "points": []}, channel)["ok"], "mission reference was deleted")
        self.assertEqual(json.loads(stored.read_text()), {"points": [changed]})
        self.assertTrue(self.request({"id": "mission", "expected_revision": 1},
                                     "cmd/missions/archive")["ok"])
        self.assertTrue(self.request({"map_id": "test-map", "expected_points": [changed],
                                      "points": []}, channel)["ok"])
        self.assertEqual(json.loads(stored.read_text()), {"points": []})

    def test_location_write_failure_preserves_confirmed_data(self):
        stored = self.map_directory / "locations.json"
        backup = self.map_directory / "locations.backup"
        stored.rename(backup)
        stored.mkdir()
        changed = dict(self.dock, x=7.0)
        self.assertFalse(self.request({"locations": [changed]}, "cmd/locations/set")["ok"])
        stored.rmdir()
        backup.rename(stored)
        self.assertEqual(json.loads(stored.read_text()), {"locations": [self.dock]})
        self.assertFalse(self.request({"locations": [changed, changed]}, "cmd/locations/set")["ok"])
        self.assertTrue(self.request({"locations": [changed]}, "cmd/locations/set")["ok"])
        self.assertEqual(json.loads(stored.read_text()), {"locations": [changed]})
        self.stop_bridge()
        self.start_bridge()
        catalog = self.receive(lambda e: e.get("ch") == "state/locations")["p"]
        self.assertEqual(catalog["locations"], [changed])

    def test_arm_pose_crud_revisions_and_mission_references(self):
        pose = {"id": "inspect", "name": "Inspection", "description": "Arm pose",
                "positions": [0.0, -0.4, 0.5, -1.2, 0.0, 0.4]}
        self.assertTrue(self.request({"preset": pose}, "cmd/arm/pose_presets/save")["ok"])
        stored = self.root / "arm_pose_presets.json"
        original = json.loads(stored.read_text())["presets"][0]
        self.assertEqual(original["revision"], 1)
        changed = dict(pose, name="Changed")
        update = {"preset": changed, "expected_revision": 1}
        self.assertTrue(self.request(update, "cmd/arm/pose_presets/update")["ok"])
        self.assertFalse(self.request(update, "cmd/arm/pose_presets/update")["ok"])
        current = json.loads(stored.read_text())["presets"][0]
        self.assertEqual(current["revision"], 2)
        self.assertEqual(current["name"], "Changed")
        mission = dict(self.mission, steps=[{"id": "arm", "type": "arm_move", "pose": "inspect"}])
        self.assertTrue(self.request({"mission": mission, "expected_revision": 1}, "cmd/missions/save")["ok"])
        archive = {"id": "inspect", "expected_revision": 2}
        self.assertFalse(self.request(archive, "cmd/arm/pose_presets/archive")["ok"])
        self.assertTrue(self.request({"id": "mission", "expected_revision": 2}, "cmd/missions/archive")["ok"])
        self.assertTrue(self.request(archive, "cmd/arm/pose_presets/archive")["ok"])
        current = json.loads(stored.read_text())["presets"][0]
        self.assertEqual(current["revision"], 3)
        self.assertTrue(current["archived"])

    def test_arm_waits_for_authority_and_rejects_stale_feedback(self):
        transient = QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL)
        authority_pub = self.node.create_publisher(MotionAuthority, "/motion/authority", transient)
        safety_pub = self.node.create_publisher(SafetyState, "/safety/state", transient)
        joint_pub = self.node.create_publisher(JointState, "/fr3/joint_states", 10)
        accepted_at = []
        commands = []
        mode = [MotionAuthority.BASE_ACTIVE]
        joints = JointState(name=[f"j{i}" for i in range(1, 7)],
                            position=[0.0, -0.4, 0.5, -1.2, 0.0, 0.4])

        def publish():
            if accepted_at and time.monotonic() - accepted_at[0] >= 0.25:
                mode[0] = MotionAuthority.ARM_ACTIVE
            authority_pub.publish(MotionAuthority(state=mode[0], owner="hmi_bridge"))
            safety_pub.publish(SafetyState(state=SafetyState.NORMAL, motion_permitted=True))
            joint_pub.publish(joints)

        def authority(request, response):
            self.assertEqual(request.operation, AuthorityRequest.Request.REQUEST_ARM)
            accepted_at.append(time.monotonic())
            response.accepted = True
            mode[0] = MotionAuthority.BASE_STOPPING
            return response

        def resume(request, response):
            self.assertEqual(request.operation, SafetyCommand.Request.RESUME)
            response.accepted = True
            return response

        self.node.create_service(AuthorityRequest, "/motion/authority/request", authority)
        self.node.create_service(SafetyCommand, "/safety/command", resume)
        self.node.create_subscription(JointState, "/motion/arm/joint_command/manual_hold",
            lambda message: commands.append((time.monotonic(), message)), 10)
        self.node.create_subscription(JointState, "/motion/safe/arm/joint_command", lambda message: None, 10)
        timer = self.node.create_timer(0.03, publish)
        self.spin_for(0.8)
        goal = [0.2, -0.4, 0.5, -1.2, 0.0, 0.4]
        self.assertTrue(self.request({"positions": goal}, "cmd/arm/joint_goal")["ok"])
        self.spin_for(0.1)
        self.assertEqual(len(commands), 1)
        self.assertGreaterEqual(commands[0][0] - accepted_at[0], 0.25)
        self.assertEqual(list(commands[0][1].position), goal)
        timer.cancel()
        self.spin_for(1.2)
        self.assertFalse(self.request({"positions": goal}, "cmd/arm/joint_goal")["ok"])
        self.assertFalse(self.request({"positions": [0.0] * 5}, "cmd/arm/joint_goal")["ok"])
        self.spin_for(0.05)
        self.assertEqual(len(commands), 1)

    def test_return_to_dock_uses_mission_control_when_paused(self):
        transient = QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL)
        publisher = self.node.create_publisher(MissionState, "/mission/state", transient)
        publisher.publish(MissionState(state=MissionState.PAUSED, mission_id="mission"))
        operations = []

        def control(request, response):
            operations.append(request.operation)
            response.accepted = True
            return response

        self.node.create_service(MissionControl, "/mission/control", control)
        self.spin_for(0.6)
        result = self.request({}, "cmd/mission/return_dock")
        self.assertTrue(result["ok"])
        self.assertEqual(operations, [MissionControl.Request.RETURN_DOCK])

    def expect_ros_limit(self, expected):
        # Nav2 uses a volatile subscription and may connect after the bridge.
        received = []
        subscription = self.node.create_subscription(SpeedLimit, "/speed_limit", received.append, 10)
        try:
            deadline = time.monotonic() + 4
            while time.monotonic() < deadline:
                rclpy.spin_once(self.node, timeout_sec=0.1)
                if any(abs(message.speed_limit - expected) < 1e-8 and not message.percentage
                       for message in received):
                    return
            self.fail("late ROS subscriber did not receive the configured absolute speed limit")
        finally:
            self.node.destroy_subscription(subscription)

    def test_real_nav2_controller_accepts_independent_limits(self):
        from ament_index_python.packages import get_package_prefix, PackageNotFoundError
        try:
            executable = Path(get_package_prefix("nav2_controller")) / "lib/nav2_controller/controller_server"
            get_package_prefix("nav2_mppi_controller")
        except PackageNotFoundError:
            self.skipTest("Nav2 controller and MPPI are not installed")
        from geometry_msgs.msg import TransformStamped
        from tf2_ros.static_transform_broadcaster import StaticTransformBroadcaster

        # Isolated ROS domain, empty costmap and no action goal or hardware.
        parameters = {
            "controller_server": {"ros__parameters": {
                "controller_frequency": 20.0, "bond_heartbeat_period": 0.0,
                "controller_plugins": ["FollowPath"],
                "FollowPath.plugin": "nav2_mppi_controller::MPPIController",
                "FollowPath.motion_model": "Omni", "FollowPath.batch_size": 100,
                "FollowPath.time_steps": 15, "FollowPath.model_dt": 0.05,
                "FollowPath.critics": ["ConstraintCritic"],
                "FollowPath.vx_max": 0.6, "FollowPath.vx_min": -0.4,
                "FollowPath.vy_max": 0.4, "FollowPath.wz_max": 0.8}},
            "local_costmap": {"local_costmap": {"ros__parameters": {
                "global_frame": "odom", "robot_base_frame": "base_link",
                "rolling_window": True, "width": 5, "height": 5, "resolution": 0.1,
                "plugins": ["inflation_layer"],
                "inflation_layer.plugin": "nav2_costmap_2d::InflationLayer"}}}}
        configuration = self.root / "controller_test.yaml"
        configuration.write_text(json.dumps(parameters))
        broadcaster = StaticTransformBroadcaster(self.node)
        transform = TransformStamped()
        transform.header.stamp = self.node.get_clock().now().to_msg()
        transform.header.frame_id, transform.child_frame_id = "odom", "base_link"
        transform.transform.rotation.w = 1.0
        broadcaster.sendTransform(transform)
        controller = subprocess.Popen([str(executable), "--ros-args", "--params-file", str(configuration)],
                                      stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        try:
            lifecycle = self.node.create_client(ChangeState, "/controller_server/change_state")
            self.assertTrue(lifecycle.wait_for_service(timeout_sec=5))
            for transition in (Transition.TRANSITION_CONFIGURE, Transition.TRANSITION_ACTIVATE):
                request = ChangeState.Request()
                request.transition.id = transition
                future = lifecycle.call_async(request)
                rclpy.spin_until_future_complete(self.node, future, timeout_sec=5)
                self.assertTrue(future.done(), "controller lifecycle transition timed out")
                self.assertTrue(future.result().success, "controller lifecycle transition failed")

            reader = self.node.create_client(GetParameters, "/controller_server/get_parameters")
            for linear, angular in ((0.30, 0.50), (0.30, 0.35), (0.15, 0.35)):
                self.assertTrue(self.request({"speed_limit_mps": linear, "angular_speed_limit_rps": angular})["ok"])
                self.receive(lambda e: e.get("ch") == "state/navigation_speed" and
                    e["p"].get("autonomous_applied") and
                    e["p"].get("speed_limit_mps") == linear and
                    e["p"].get("angular_speed_limit_rps") == angular)
                future = reader.call_async(GetParameters.Request(names=["FollowPath.vx_max", "FollowPath.wz_max"]))
                rclpy.spin_until_future_complete(self.node, future, timeout_sec=2)
                self.assertTrue(future.done())
                values = future.result().values
                self.assertAlmostEqual(values[0].double_value, linear)
                self.assertAlmostEqual(values[1].double_value, angular)
        finally:
            controller.send_signal(signal.SIGINT)
            try:
                controller.wait(timeout=3)
            except subprocess.TimeoutExpired:
                controller.kill()
                controller.wait(timeout=2)

    def test_storage_validation_restarts_and_late_controller(self):
        initial = self.report()
        self.assertEqual(initial["speed_limit_mps"], 0.30)
        self.assertEqual(initial["angular_speed_limit_rps"], 0.50)
        self.assertFalse(initial["autonomous_applied"])
        self.assertTrue(self.request({"speed_limit_mps": 0.40, "angular_speed_limit_rps": 0.65})["ok"])
        self.expect_applied(0.40, 0.65, False)
        # Nav2 can start after the bridge. Both limits are then applied atomically.
        self.fake_controller()
        self.expect_applied(0.40, 0.65)
        setting = self.root / "navigation_settings.json"
        self.assertEqual(json.loads(setting.read_text())["speed_limit_mps"], 0.40)
        self.assertEqual(json.loads(setting.read_text())["angular_speed_limit_rps"], 0.65)
        self.expect_ros_limit(0.40)
        for invalid in ({}, {"speed_limit_mps": 0}, {"speed_limit_mps": 0.61},
                        {"speed_limit_mps": "0.2"}, {"speed_limit_mps": True},
                        {"speed_limit_mps": None}, {"speed_limit_mps": [0.2]}):
            result = self.request(invalid)
            self.assertFalse(result["ok"])
            self.assertEqual(result["err"]["code"], "E_BAD_PAYLOAD")
        for angular in (0, 0.81, "0.5", True, None, [0.5]):
            result = self.request({"speed_limit_mps": 0.20, "angular_speed_limit_rps": angular})
            self.assertFalse(result["ok"])
            self.assertEqual(result["err"]["code"], "E_BAD_PAYLOAD")
        self.assertEqual(json.loads(setting.read_text())["speed_limit_mps"], 0.40)
        self.stop_bridge()
        self.start_bridge()
        self.expect_applied(0.40, 0.65)
        self.expect_ros_limit(0.40)
        # A failed atomic rename must leave both memory and saved settings intact.
        backup = self.root / "navigation_settings.backup"
        setting.rename(backup)
        setting.mkdir()
        self.assertFalse(self.request({"speed_limit_mps": 0.20})["ok"])
        self.assertEqual(self.report()["speed_limit_mps"], 0.40)
        self.expect_ros_limit(0.40)
        setting.rmdir()
        backup.rename(setting)
        self.assertEqual(json.loads(setting.read_text())["speed_limit_mps"], 0.40)
        for boundary in (0.10, 0.60):
            self.assertTrue(self.request({"speed_limit_mps": boundary})["ok"])
            self.expect_applied(boundary, 0.65)
            self.expect_ros_limit(boundary)
        # Only rotation changes; the linear setting remains intact.
        for angular in (0.05, 0.80):
            self.assertTrue(self.request({"speed_limit_mps": 0.60, "angular_speed_limit_rps": angular})["ok"])
            self.expect_applied(0.60, angular)
        # Failed application is reported separately from successful persistence.
        self.controller_accepts = False
        self.assertTrue(self.request({"speed_limit_mps": 0.20, "angular_speed_limit_rps": 0.35})["ok"])
        self.expect_applied(0.20, 0.35, False)
        self.assertEqual(json.loads(setting.read_text()),
                         {"speed_limit_mps": 0.20, "min_speed_mps": 0.10, "max_speed_mps": 0.60,
                          "angular_speed_limit_rps": 0.35, "min_angular_speed_rps": 0.05,
                          "max_angular_speed_rps": 0.80})
        self.controller_accepts = True
        self.expect_applied(0.20, 0.35)
        # A lifecycle restart restores MPPI's base parameters; reapply both values.
        self.controller_values.clear()
        previous_sets = len(self.controller_sets)
        self.spin_for(1.2)
        self.expect_applied(0.20, 0.35)
        self.assertGreater(len(self.controller_sets), previous_sets)
        previous_sets = len(self.controller_sets)
        self.spin_for(1.2)
        self.assertEqual(len(self.controller_sets), previous_sets, "unchanged limits reset MPPI every tick")
        # Parameter storage alone is insufficient while the plugin is inactive.
        self.controller_active = False
        self.expect_applied(0.20, 0.35, False)
        previous_sets = len(self.controller_sets)
        self.spin_for(1.2)
        self.assertEqual(len(self.controller_sets), previous_sets)
        self.controller_active = True
        self.expect_applied(0.20, 0.35)
        self.assertGreater(len(self.controller_sets), previous_sets, "activation did not update the MPPI plugin")
        # Legacy linear-only files acquire the configured angular default.
        self.stop_bridge()
        setting.write_text(json.dumps({"speed_limit_mps": 0.40}))
        self.start_bridge()
        self.expect_applied(0.40, 0.50)

    def test_ranges_only_preserve_or_clamp_robot_speed_and_persist(self):
        self.fake_controller()
        self.expect_applied(0.30, 0.50)
        channel = "cmd/navigation/speed_settings"
        ranges = {"min_speed_mps": 0.20, "max_speed_mps": 0.45,
                  "min_angular_speed_rps": 0.10, "max_angular_speed_rps": 0.65}
        self.assertTrue(self.request(ranges, channel)["ok"])
        self.expect_applied(0.30, 0.50)
        # The range editor sends no stale speed snapshot to overwrite this change.
        self.assertTrue(self.request({"speed_limit_mps": 0.40, "angular_speed_limit_rps": 0.60})["ok"])
        self.expect_applied(0.40, 0.60)
        self.assertTrue(self.request(ranges, channel)["ok"])
        self.expect_applied(0.40, 0.60)
        ranges.update(max_speed_mps=0.35, max_angular_speed_rps=0.40)
        self.assertTrue(self.request(ranges, channel)["ok"])
        self.expect_applied(0.35, 0.40)
        ranges.update(min_speed_mps=0.36, max_speed_mps=0.45,
                      min_angular_speed_rps=0.45, max_angular_speed_rps=0.65)
        self.assertTrue(self.request(ranges, channel)["ok"])
        self.expect_applied(0.36, 0.45)
        stored = self.root / "navigation_settings.json"
        expected = dict(ranges, speed_limit_mps=0.36, angular_speed_limit_rps=0.45)
        self.assertEqual(json.loads(stored.read_text()), expected)
        for update in ({"min_speed_mps": 0.50}, {"min_angular_speed_rps": 0.70},
                       {"max_speed_mps": 0.61}, {"max_angular_speed_rps": 0.81},
                       {"max_speed_mps": "0.45"}, {"min_speed_mps": True}):
            self.assertFalse(self.request(dict(ranges, **update), channel)["ok"])
            self.assertEqual(json.loads(stored.read_text()), expected)
        for missing in ranges:
            invalid = {key: value for key, value in ranges.items() if key != missing}
            self.assertFalse(self.request(invalid, channel)["ok"])
        self.stop_bridge()
        self.start_bridge()
        self.expect_applied(0.36, 0.45)
        self.assertEqual(json.loads(stored.read_text()), expected)

    def test_speed_ranges_persist_validate_and_control_future_settings(self):
        self.fake_controller()
        self.expect_applied(0.30, 0.50)
        channel = "cmd/navigation/speed_settings"
        settings = {"speed_limit_mps": 0.35, "min_speed_mps": 0.20, "max_speed_mps": 0.45,
                    "angular_speed_limit_rps": 0.55, "min_angular_speed_rps": 0.15,
                    "max_angular_speed_rps": 0.65}
        self.assertTrue(self.request(settings, channel)["ok"])
        report = self.expect_applied(0.35, 0.55)
        for key, value in settings.items():
            self.assertEqual(report[key], value)
        stored = self.root / "navigation_settings.json"
        self.assertEqual(json.loads(stored.read_text()), settings)
        for update in ({"min_speed_mps": 0.40}, {"max_speed_mps": 0.30},
                       {"min_speed_mps": 0.09}, {"max_speed_mps": 0.61},
                       {"min_angular_speed_rps": 0.60}, {"max_angular_speed_rps": 0.50},
                       {"min_angular_speed_rps": 0.04}, {"max_angular_speed_rps": 0.81},
                       {"min_speed_mps": True}, {"max_speed_mps": "0.45"},
                       {"min_angular_speed_rps": None}, {"max_angular_speed_rps": []}):
            invalid = dict(settings, **update)
            self.assertFalse(self.request(invalid, channel)["ok"], update)
            self.assertEqual(json.loads(stored.read_text()), settings)
        missing = dict(settings)
        del missing["max_speed_mps"]
        self.assertFalse(self.request(missing, channel)["ok"])
        # The driving card uses the saved range, not its previous local range.
        self.assertFalse(self.request({"speed_limit_mps": 0.10})["ok"])
        self.assertFalse(self.request({"speed_limit_mps": 0.46})["ok"])
        self.assertFalse(self.request({"speed_limit_mps": 0.35, "angular_speed_limit_rps": 0.70})["ok"])
        # Range-only changes do not reset an active MPPI controller.
        previous_sets = len(self.controller_sets)
        settings["max_speed_mps"] = 0.50
        self.assertTrue(self.request(settings, channel)["ok"])
        self.expect_applied(0.35, 0.55)
        self.spin_for(1.1)
        self.assertEqual(len(self.controller_sets), previous_sets)
        self.stop_bridge()
        self.start_bridge()
        report = self.expect_applied(0.35, 0.55)
        for key, value in settings.items():
            self.assertEqual(report[key], value)
        self.assertTrue(self.request({"speed_limit_mps": 0.50})["ok"])
        settings["speed_limit_mps"] = 0.50
        self.expect_applied(0.50, 0.55)
        self.assertEqual(json.loads(stored.read_text()), settings)
        # Failed range persistence must change neither confirmed state nor disk.
        backup = self.root / "navigation_settings.backup"
        stored.rename(backup)
        stored.mkdir()
        changed = dict(settings, min_speed_mps=0.25)
        self.assertFalse(self.request(changed, channel)["ok"])
        report = self.report()
        self.assertEqual(report["min_speed_mps"], 0.20)
        stored.rmdir()
        backup.rename(stored)
        self.assertEqual(json.loads(stored.read_text()), settings)

    def test_invalid_saved_ranges_fall_back_atomically(self):
        self.fake_controller()
        self.stop_bridge()
        invalid = {"speed_limit_mps": 0.40, "min_speed_mps": 0.50, "max_speed_mps": 0.60,
                   "angular_speed_limit_rps": 0.65, "min_angular_speed_rps": 0.05,
                   "max_angular_speed_rps": 0.80}
        (self.root / "navigation_settings.json").write_text(json.dumps(invalid))
        self.start_bridge()
        report = self.expect_applied(0.30, 0.50)
        self.assertEqual(report["min_speed_mps"], 0.10)
        self.assertEqual(report["max_speed_mps"], 0.60)


if __name__ == "__main__":
    unittest.main()
