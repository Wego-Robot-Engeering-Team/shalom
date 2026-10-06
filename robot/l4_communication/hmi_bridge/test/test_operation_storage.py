# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Robot-side operation storage, using temporary files and an isolated ROS domain."""

import json
import unittest

import test_navigation_speed as fixture
from nav2_msgs.srv import LoadMap


class OperationStorageTest(fixture.NavigationSpeedTest):
    def report_mission_state(self, state):
        if not hasattr(self, "mission_publisher"):
            qos = fixture.QoSProfile(depth=1, durability=fixture.DurabilityPolicy.TRANSIENT_LOCAL)
            self.mission_publisher = self.node.create_publisher(
                fixture.MissionState, "/mission/state", qos)
            self.spin_for(0.3)
        self.mission_publisher.publish(fixture.MissionState(state=state, mission_id="mission"))
        wire = {fixture.MissionState.COMPLETED: "completed",
                fixture.MissionState.FAILED: "failed",
                fixture.MissionState.RUNNING: "running"}[state]
        self.receive(lambda e: e.get("ch") == "state/mission" and e["p"].get("state") == wire)

    def create_map(self, name):
        directory = self.root / "maps" / name
        directory.mkdir()
        (directory / "map.yaml").write_text("image: map.pgm\n")
        return directory

    def test_missing_waypoint_mission_is_rejected_without_changing_storage(self):
        before = (self.map_directory / "missions.json").read_text()
        invalid = dict(self.mission, id="broken", steps=[
            {"id": "move-1", "type": "navigate", "location_id": "missing"}])
        result = self.request({"mission": invalid, "expected_revision": 0}, "cmd/missions/save")
        self.assertFalse(result["ok"])
        self.assertEqual(result["err"]["code"], "E_BAD_PAYLOAD")
        self.assertIn("missing", result["err"]["msg"])
        self.assertEqual((self.map_directory / "missions.json").read_text(), before)
        changed = dict(self.point, name="Renamed waypoint")
        self.assertTrue(self.request({"map_id": "test-map", "expected_points": [self.point],
                                      "points": [changed]}, "cmd/waypoints/set")["ok"])

    def test_legacy_missing_reference_does_not_block_unrelated_waypoint_changes(self):
        self.stop_bridge()
        invalid = dict(self.mission, id="broken", steps=[
            {"id": "move-1", "type": "navigate", "location_id": "missing"}])
        (self.map_directory / "missions.json").write_text(
            json.dumps({"missions": [self.mission, invalid]}))
        self.start_bridge()
        changed = dict(self.point, name="Renamed waypoint")
        added = dict(self.point, id="new-point", name="New waypoint", x=3.0)
        self.assertTrue(self.request({"map_id": "test-map", "expected_points": [self.point],
                                      "points": [changed, added]}, "cmd/waypoints/set")["ok"])
        self.assertFalse(self.request({"map_id": "test-map", "expected_points": [changed, added],
                                       "points": [added]}, "cmd/waypoints/set")["ok"])
        self.assertEqual(json.loads((self.map_directory / "waypoints.json").read_text()),
                         {"points": [changed, added]})

    def test_terminal_mission_allows_map_select_rename_and_delete(self):
        def load_map(_, response):
            response.result = LoadMap.Response.RESULT_SUCCESS
            return response

        self.node.create_service(LoadMap, "/map_server/load_map", load_map)
        for state, label in [(fixture.MissionState.COMPLETED, "completed"),
                             (fixture.MissionState.FAILED, "failed")]:
            with self.subTest(state=label):
                directory = self.create_map("map-" + label)
                unused = self.create_map("unused-" + label)
                self.report_mission_state(state)
                self.assertTrue(self.request({"id": directory.name}, "cmd/maps/select")["ok"])
                renamed = directory.with_name(directory.name + "-renamed")
                self.assertTrue(self.request({"id": directory.name, "name": renamed.name},
                                             "cmd/maps/rename")["ok"])
                self.assertTrue((renamed / "map.yaml").is_file())
                self.assertFalse(directory.exists())
                self.assertTrue(self.request({"id": unused.name}, "cmd/maps/delete")["ok"])
                self.assertFalse(unused.exists())
                self.assertTrue(any((self.root / "maps" / ".trash").glob(unused.name + "-*")))

    def test_running_mission_still_blocks_map_changes(self):
        directory = self.create_map("unused")
        self.report_mission_state(fixture.MissionState.RUNNING)
        for channel, payload in [("cmd/maps/select", {"id": directory.name}),
                                 ("cmd/maps/rename", {"id": directory.name, "name": "renamed"}),
                                 ("cmd/maps/delete", {"id": directory.name})]:
            with self.subTest(channel=channel):
                result = self.request(payload, channel)
                self.assertFalse(result["ok"])
                self.assertEqual(result["err"]["code"], "E_BUSY")
        self.assertTrue(directory.is_dir())


if __name__ == "__main__":
    suite = unittest.TestSuite(OperationStorageTest(name) for name in
        OperationStorageTest.__dict__ if name.startswith("test_"))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    raise SystemExit(not result.wasSuccessful())
