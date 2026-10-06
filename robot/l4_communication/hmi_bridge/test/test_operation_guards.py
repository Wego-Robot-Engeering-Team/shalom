"""Malformed data, asynchronous map/mission exclusion and camera source freshness."""

import threading
import time
import unittest

import test_operation_storage as storage
import test_navigation_speed as fixture
import rclpy
from rclpy.callback_groups import ReentrantCallbackGroup
from rclpy.executors import MultiThreadedExecutor
from rclpy.qos import qos_profile_sensor_data
from nav2_msgs.srv import LoadMap
from sensor_msgs.msg import Image
from geometry_msgs.msg import TransformStamped
from geometry_msgs.msg import Twist
from tf2_ros import TransformBroadcaster
from shalom_interfaces.srv import ConfigureMission, MissionControl


class OperationGuardsTest(storage.OperationStorageTest):
    def setUp(self):
        super().setUp()
        self.fake_controller()
        self.expect_applied(0.3, 0.5)
        self.server_node = None
        self.server_executor = None
        self.server_worker = None
        self.release_service = threading.Event()
        self.release_service.set()

    def tearDown(self):
        self.release_service.set()
        if self.server_executor:
            self.server_executor.shutdown(timeout_sec=3)
            self.server_worker.join(timeout=3)
            self.server_node.destroy_node()
        super().tearDown()

    def create_servers(self, configure=None, control=None, load=None):
        self.server_node = rclpy.create_node("operation_guards_fake_server")
        group = ReentrantCallbackGroup()
        for kind, name, callback in [(ConfigureMission, "/mission/configure", configure),
                (MissionControl, "/mission/control", control), (LoadMap, "/map_server/load_map", load)]:
            if callback:
                self.server_node.create_service(kind, name, callback, callback_group=group)
        self.server_executor = MultiThreadedExecutor(num_threads=3)
        self.server_executor.add_node(self.server_node)
        self.server_worker = threading.Thread(target=self.server_executor.spin, daemon=True)
        self.server_worker.start()
        self.spin_for(0.4)

    def send(self, channel, payload):
        self.sequence += 1
        request_id = f"guard-{self.sequence}"
        header = fixture.json.dumps({"v": 1, "t": "req", "robot": "speed-test",
            "id": request_id, "ch": channel, "p": payload}).encode()
        self.peer.sendall(fixture.struct.pack("<III", 0x4D4C4853, len(header) + 4, len(header)) + header)
        return request_id

    def test_bad_payload_does_not_terminate_bridge(self):
        for channel, payload in [("cmd/arm/pose_presets/save", {"preset": {"id": 9}}),
                ("cmd/arm/pose_presets/update", {"preset": {"id": "p"}, "expected_revision": []}),
                ("cmd/missions/save", {"mission": {"id": []}}),
                ("cmd/maps/select", {"id": {"wrong": "type"}})]:
            with self.subTest(channel=channel):
                result = self.request(payload, channel)
                self.assertFalse(result["ok"])
                self.assertEqual(result["err"]["code"], "E_BAD_PAYLOAD")
                self.assertIsNone(self.process.poll())
        self.assertTrue(self.request({}, "cmd/arm/pose_presets/list")["ok"])

    def test_bad_arm_file_keeps_valid_items_and_preserves_original(self):
        valid = {"id": "inspect", "name": "Inspection", "positions": [0.0] * 6,
                 "revision": 1, "archived": False}
        malformed = {"id": 9, "name": "Broken", "positions": [0.0] * 6}
        self.stop_bridge()
        path = self.root / "arm_pose_presets.json"
        original = fixture.json.dumps({"presets": [valid, malformed]})
        path.write_text(original)
        self.start_bridge()
        catalog = self.receive(lambda e: e.get("ch") == "state/arm_pose_presets")["p"]
        self.assertEqual(catalog["presets"], [valid])
        self.assertFalse(self.request({"preset": dict(valid, id="new", name="New")},
                                      "cmd/arm/pose_presets/save")["ok"])
        self.assertEqual(path.read_text(), original)
        self.assertIsNone(self.process.poll())

    def test_bad_arm_root_or_missing_key_blocks_overwrite(self):
        path = self.root / "arm_pose_presets.json"
        valid = {"id": "inspect", "name": "Inspection", "positions": [0.0] * 6}
        for document in ([], {}, {"presets": "wrong"}):
            with self.subTest(document=document):
                self.stop_bridge()
                original = fixture.json.dumps(document)
                path.write_text(original)
                self.start_bridge()
                self.assertFalse(self.request({"preset": valid}, "cmd/arm/pose_presets/save")["ok"])
                self.assertEqual(path.read_text(), original)
                self.assertIsNone(self.process.poll())

    def test_missing_and_archived_arm_pose_cannot_be_saved_in_mission(self):
        mission = dict(self.mission, id="arm-mission", steps=[
            {"id": "arm", "type": "arm_move", "pose": "missing"}])
        self.assertFalse(self.request({"mission": mission}, "cmd/missions/save")["ok"])
        preset = {"id": "inspect", "name": "Inspection", "positions": [0.0] * 6}
        self.assertTrue(self.request({"preset": preset}, "cmd/arm/pose_presets/save")["ok"])
        self.assertTrue(self.request({"id": "inspect", "expected_revision": 1},
                                    "cmd/arm/pose_presets/archive")["ok"])
        mission["steps"][0]["pose"] = "inspect"
        self.assertFalse(self.request({"mission": mission}, "cmd/missions/save")["ok"])

    def test_mission_start_blocks_map_change_until_services_complete(self):
        entered = threading.Event()
        self.release_service.clear()
        started = []
        def configure(request, response):
            entered.set()
            self.release_service.wait(timeout=5)
            response.accepted = True
            response.state.state = fixture.MissionState.READY
            response.state.mission_id = request.plan.mission_id
            return response
        def control(request, response):
            started.append(request)
            response.accepted = True
            response.state.state = fixture.MissionState.READY
            response.state.mission_id = request.mission_id
            return response
        self.create_servers(configure=configure, control=control)
        target = self.create_map("new-map")
        self.report_mission_state(fixture.MissionState.COMPLETED)
        request_id = self.send("cmd/mission/start", {"mission_id": "mission"})
        self.assertTrue(entered.wait(timeout=2))
        result = self.request({"id": target.name}, "cmd/maps/select")
        self.assertFalse(result["ok"])
        self.assertEqual(result["err"]["code"], "E_BUSY")
        self.release_service.set()
        response = self.receive(lambda e: e.get("t") == "res" and e.get("id") == request_id)["p"]
        self.assertTrue(response["ok"])
        self.assertEqual(len(started), 1)

    def test_map_change_blocks_mission_start_while_load_is_pending(self):
        entered = threading.Event()
        self.release_service.clear()
        def load(_, response):
            entered.set()
            self.release_service.wait(timeout=5)
            response.result = LoadMap.Response.RESULT_SUCCESS
            return response
        self.create_servers(load=load)
        target = self.create_map("new-map")
        self.report_mission_state(fixture.MissionState.COMPLETED)
        request_id = self.send("cmd/maps/select", {"id": target.name})
        self.assertTrue(entered.wait(timeout=2))
        result = self.request({"mission_id": "mission"}, "cmd/mission/start")
        self.assertFalse(result["ok"])
        self.assertEqual(result["err"]["code"], "E_BUSY")
        self.release_service.set()
        self.assertTrue(self.receive(lambda e: e.get("t") == "res" and e.get("id") == request_id)["p"]["ok"])

    def test_late_mission_configure_response_cannot_start_after_timeout(self):
        started = []
        def configure(request, response):
            time.sleep(3.5)
            response.accepted = True
            response.state.state = fixture.MissionState.READY
            response.state.mission_id = request.plan.mission_id
            return response
        def control(request, response):
            started.append(request)
            response.accepted = True
            return response
        self.create_servers(configure=configure, control=control)
        result = self.request({"mission_id": "mission"}, "cmd/mission/start")
        self.assertFalse(result["ok"])
        self.assertIn("시간", result["err"]["msg"])
        self.spin_for(0.8)
        self.assertEqual(started, [])

    def test_map_timeout_blocks_motion_and_ignores_late_load_reply(self):
        def load(_, response):
            time.sleep(10.5)
            response.result = LoadMap.Response.RESULT_SUCCESS
            return response
        self.create_servers(load=load)
        target = self.create_map("new-map")
        self.report_mission_state(fixture.MissionState.COMPLETED)
        self.receive_timeout_s = 12
        result = self.request({"id": target.name}, "cmd/maps/select")
        self.assertFalse(result["ok"])
        self.assertIn("시간", result["err"]["msg"])
        self.spin_for(0.8)
        for channel, payload in [("cmd/goto", {"x": 1.0, "y": 2.0}),
                ("cmd/mission/start", {"mission_id": "mission"}),
                ("cmd/maps/select", {"id": target.name})]:
            self.assertFalse(self.request(payload, channel)["ok"])
        self.assertTrue(self.request({}, "cmd/maps/list")["ok"])
        active = self.receive(lambda e: e.get("ch") == "state/active_map")["p"]
        self.assertEqual(active["id"], "test-map")

    def test_old_resume_reply_cannot_release_disconnect_hold_on_new_connection(self):
        entered = threading.Event()
        self.release_service.clear()
        running = [False]
        def control(request, response):
            entered.set()
            self.release_service.wait(timeout=5)
            running[0] = True
            response.accepted = True
            response.state.state = fixture.MissionState.RUNNING
            response.state.mission_id = "mission"
            return response
        self.create_servers(control=control)
        transient = fixture.QoSProfile(depth=1, durability=fixture.DurabilityPolicy.TRANSIENT_LOCAL)
        safety = self.node.create_publisher(fixture.SafetyState, "/safety/state", transient)
        authority = self.node.create_publisher(fixture.MotionAuthority, "/motion/authority", transient)
        mission = self.node.create_publisher(fixture.MissionState, "/mission/state", transient)
        holds = []
        self.node.create_subscription(Twist, "/motion/manual_hold/cmd_vel", holds.append, 10)
        def report():
            safety.publish(fixture.SafetyState(state=fixture.SafetyState.NORMAL, motion_permitted=True))
            authority.publish(fixture.MotionAuthority(state=fixture.MotionAuthority.BASE_ACTIVE))
            mission.publish(fixture.MissionState(state=fixture.MissionState.RUNNING if running[0]
                else fixture.MissionState.PAUSED, mission_id="mission"))
        self.node.create_timer(0.05, report)
        self.spin_for(0.3)
        endpoint = self.peer.getpeername()
        self.send("cmd/mission/resume", {})
        self.assertTrue(entered.wait(timeout=2))
        self.peer.close()
        self.peer = None
        self.spin_for(0.15)
        self.peer = fixture.socket.create_connection(endpoint, timeout=0.2)
        self.peer.settimeout(0.05)
        self.buffer = b""
        self.release_service.set()
        self.spin_for(0.3)
        holds.clear()
        self.spin_for(0.25)
        self.assertGreaterEqual(len(holds), 2, "an old connection's response released the new connection hold")


class CaptureFreshnessTest(fixture.NavigationSpeedTest):
    def setUp(self):
        super().setUp()
        self.stop_bridge()
        self.extra_bridge_args = ["-p", "capture.enabled:=true", "-p", "capture.require_mount:=false",
            "-p", f"capture.spool_dir:={self.root / 'captures'}"]
        self.start_bridge()
        self.broadcaster = TransformBroadcaster(self.node)
        self.color_pub = self.node.create_publisher(Image, "/fr3/camera_2d/image_raw", qos_profile_sensor_data)
        self.depth_pub = self.node.create_publisher(Image, "/fr3/camera_3d/image_raw", qos_profile_sensor_data)
        self.spin_for(0.4)

    def stopped(self):
        for _ in range(3):
            transform = TransformStamped()
            transform.header.frame_id = "map"
            transform.child_frame_id = "base_link"
            transform.header.stamp = self.node.get_clock().now().to_msg()
            transform.transform.rotation.w = 1.0
            self.broadcaster.sendTransform(transform)
            self.spin_for(0.08)

    def images(self, age=0.0, difference=0.0):
        now_ns = self.node.get_clock().now().nanoseconds
        rgb_ns = now_ns - int(age * 1e9)
        depth_ns = rgb_ns - int(difference * 1e9)
        rgb = Image(height=1, width=1, encoding="rgb8", step=3, data=[255, 0, 0])
        depth = Image(height=1, width=1, encoding="16UC1", step=2, data=[232, 3])
        rgb.header.stamp.sec, rgb.header.stamp.nanosec = divmod(rgb_ns, 1_000_000_000)
        depth.header.stamp.sec, depth.header.stamp.nanosec = divmod(depth_ns, 1_000_000_000)
        self.color_pub.publish(rgb)
        self.depth_pub.publish(depth)
        self.spin_for(0.08)

    def capture(self):
        return self.request({"vehicle_number": "v", "train_number": "t", "car_number": "c",
            "point_id": "p"}, "cmd/capture/trigger")

    def test_stale_rgb_depth_and_unsynchronized_images_are_rejected(self):
        self.stopped()
        self.images(age=3.0)
        self.assertFalse(self.capture()["ok"])
        self.images(difference=0.5)
        self.assertFalse(self.capture()["ok"])
        self.images()
        self.assertTrue(self.capture()["ok"])
        self.spin_for(1.2)
        self.stopped()
        self.assertFalse(self.capture()["ok"], "stale receipt time was refreshed by the capture command")
        self.assertEqual(len(list((self.root / "captures").glob("*.png"))), 1)


if __name__ == "__main__":
    suite = unittest.TestSuite(cls(name) for cls in (OperationGuardsTest, CaptureFreshnessTest)
        for name in cls.__dict__ if name.startswith("test_"))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    raise SystemExit(not result.wasSuccessful())
