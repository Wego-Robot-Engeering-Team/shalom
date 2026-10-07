"""SDK against the real hmi_bridge binary, temporary storage and isolated ROS.

Run with a built hmi_bridge_node path. No actual hardware is used.
"""
import json
from pathlib import Path
import sys
import time
import unittest

PLATFORM_ROOT = Path(__file__).resolve().parents[2]
SDK_ROOT = PLATFORM_ROOT.parent
PROJECT = SDK_ROOT.parents[1]
sys.path.insert(0, str(PLATFORM_ROOT / "python"))
sys.path.insert(0, str(PROJECT / "robot/l4_communication/hmi_bridge/test"))
# The existing fixture consumes argv[1] (bridge executable) and selects an isolated domain.
import test_operation_storage as fixture
from robot_sdk import Client, ClientError, RobotApi


class SdkBridgeTests(fixture.OperationStorageTest):
    def setUp(self):
        super().setUp()
        address = self.peer.getpeername()
        self.peer.close()
        self.peer = None
        self.sdk = Client(on_message=lambda _: fixture.fixture.rclpy.spin_once(self.node, timeout_sec=0))
        self.addCleanup(self.sdk.close)
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            try:
                self.sdk.connect(*address)
                RobotApi(self.sdk).list_maps()
                break
            except ClientError:
                self.sdk.close()
                time.sleep(0.05)
        else:
            self.fail("SDK could not connect to isolated bridge")
        self.api = RobotApi(self.sdk)

    def tearDown(self):
        self.sdk.close()
        super().tearDown()

    def test_sdk_catalog_queries(self):
        self.assertTrue(self.api.list_maps().ok)
        self.assertTrue(self.api.list_missions().ok)
        self.assertTrue(self.api.list_arm_poses().ok)
        self.assertEqual(self.sdk.robot_id, "speed-test")
        self.assertEqual(self.sdk.latest("state/missions").envelope["p"]["map_id"], "test-map")
        self.assertEqual(self.sdk.latest("state/missions").envelope["p"]["missions"][0]["id"], "mission")

    def test_sdk_waypoint_save_and_conflict(self):
        before = [self.point]
        changed = [dict(self.point, name="SDK waypoint", description="SDK description")]
        self.assertTrue(self.api.set_waypoints(changed, map_id="test-map", expected_points=before).ok)
        self.assertFalse(self.api.set_waypoints(changed, map_id="test-map", expected_points=before).ok)
        result = json.loads((self.map_directory / "waypoints.json").read_text())
        self.assertEqual(result["points"], changed)
        self.assertTrue(self.api.archive_mission("mission", map_id="test-map", expected_revision=1).ok)
        reported = self.sdk.latest("state/waypoints").envelope["p"]["points"]
        self.assertTrue(self.api.set_waypoints([], map_id="test-map", expected_points=reported).ok)
        self.assertEqual(json.loads((self.map_directory / "waypoints.json").read_text()), {"points": []})

    def test_sdk_arm_and_mission_revisions(self):
        pose = {"id": "sdk_arm", "name": "SDK arm", "positions": [0.0] * 6, "description": "saved pose"}
        self.assertTrue(self.api.save_arm_pose(pose).ok)
        updated = dict(pose, name="Updated arm")
        self.assertTrue(self.api.update_arm_pose(updated, 1).ok)
        self.assertFalse(self.api.update_arm_pose(pose, 1).ok)
        mission = {"id": "sdk_mission", "name": "SDK mission", "description": "retained", "steps": [
            {"id": "nav", "type": "navigate", "location_id": "wp"},
            {"id": "arm", "type": "arm_move", "pose": "sdk_arm"}]}
        self.assertTrue(self.api.save_mission(mission, map_id="test-map").ok)
        self.assertFalse(self.api.archive_arm_pose("sdk_arm", 2).ok)
        self.assertTrue(self.api.archive_mission("sdk_mission", map_id="test-map", expected_revision=1).ok)
        self.assertTrue(self.api.archive_arm_pose("sdk_arm", 2).ok)

    def test_sdk_marker_and_location_snapshots(self):
        marker = {"id": 3, "x": 1.0, "y": 2.0, "z": 1.2, "yaw": 0.5, "description": "wall"}
        self.assertTrue(self.api.set_markers([marker], map_id="test-map", expected_markers=[]).ok)
        self.assertFalse(self.api.set_markers([], map_id="test-map", expected_markers=[]).ok)
        self.assertEqual(json.loads((self.map_directory / "markers.json").read_text())["markers"], [marker])
        location = dict(self.dock, x=3.0)
        self.assertTrue(self.api.set_locations([location], map_id="test-map", expected_locations=[self.dock]).ok)
        self.assertFalse(self.api.set_locations([self.dock], map_id="test-map", expected_locations=[self.dock]).ok)

    def test_sdk_default_and_speed_storage(self):
        self.assertTrue(self.api.set_default_map("test-map").ok)
        self.assertEqual(json.loads((self.root / "maps/default_map.json").read_text()), {"map_id": "test-map"})
        self.assertTrue(self.api.set_default_map("").ok)
        self.assertTrue(self.api.set_speed_limits(0.3, 0.5).ok)
        self.assertTrue(self.api.set_speed_ranges(0.2, 0.4, 0.1, 0.6).ok)
        settings = json.loads((self.root / "navigation_settings.json").read_text())
        self.assertEqual(settings["min_speed_mps"], 0.2)
        self.assertEqual(settings["angular_speed_limit_rps"], 0.5)


if __name__ == "__main__":
    # Exclude inherited fixture tests: this suite specifically tests SDK integration.
    suite = unittest.TestSuite(SdkBridgeTests(name) for name in (
        "test_sdk_catalog_queries", "test_sdk_waypoint_save_and_conflict", "test_sdk_arm_and_mission_revisions",
        "test_sdk_marker_and_location_snapshots", "test_sdk_default_and_speed_storage"))
    raise SystemExit(0 if unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful() else 1)
