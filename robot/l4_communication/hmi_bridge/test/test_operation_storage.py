# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Robot-side operation storage, using temporary files and an isolated ROS domain."""

import json
import os
import struct
import unittest

import test_navigation_speed as fixture
from nav2_msgs.srv import LoadMap


class OperationStorageTest(fixture.NavigationSpeedTest):
    def test_malformed_heartbeat_does_not_close_bridge_connection(self):
        for payload in [{"seq": "bad"}, {"seq": []}, {"seq": None},
                        {"seq": 1.5}, {"seq": 2 ** 64 - 1}, [], {"seq": 42}]:
            header = json.dumps({"v": 1, "t": "hb", "robot": "speed-test", "p": payload}).encode()
            self.peer.sendall(struct.pack("<III", 0x4D4C4853, len(header) + 4, len(header)) + header)
        reply = self.receive(lambda e: e.get("t") == "hb" and e["p"].get("seq") == 42)
        self.assertEqual(reply["robot"], "speed-test")
        self.assertTrue(self.request({}, "cmd/maps/list")["ok"])
        self.assertIsNone(self.process.poll())

    def test_marker_snapshot_rejects_stale_catalog_and_wrong_map(self):
        marker = {"id": 1, "x": 1.0, "y": 2.0, "z": 1.2, "yaw": 0.0}
        channel = "cmd/markers/set"
        payload = {"map_id": "test-map", "expected_markers": [], "markers": [marker]}
        self.assertTrue(self.request(payload, channel)["ok"])
        stored = self.map_directory / "markers.json"
        before = stored.read_text()
        changed = dict(marker, x=8.0)
        self.assertFalse(self.request(dict(payload, markers=[changed]), channel)["ok"])
        self.assertFalse(self.request({"map_id": "other-map", "expected_markers": [marker],
                                      "markers": [changed]}, channel)["ok"])
        self.assertEqual(stored.read_text(), before)
        self.assertTrue(self.request({"map_id": "test-map", "expected_markers": [marker],
                                     "markers": [changed]}, channel)["ok"])
        self.assertEqual(json.loads(stored.read_text())["markers"], [changed])

    def test_fixed_location_snapshot_compares_kinds_and_preserves_latest(self):
        channel = "cmd/locations/set"
        dock = dict(self.dock, x=3.0)
        home = {"kind": "home", "x": 4.0, "y": 2.0, "theta": 0.1}
        self.assertTrue(self.request({"map_id": "test-map", "expected_locations": [self.dock],
                                     "locations": [home, dock]}, channel)["ok"])
        stored = self.map_directory / "locations.json"
        before = stored.read_text()
        self.assertFalse(self.request({"map_id": "test-map", "expected_locations": [self.dock],
                                      "locations": [self.dock]}, channel)["ok"])
        self.assertFalse(self.request({"map_id": "other-map", "expected_locations": [dock, home],
                                      "locations": [dock]}, channel)["ok"])
        self.assertEqual(stored.read_text(), before)
        self.assertTrue(self.request({"map_id": "test-map", "expected_locations": [dock, home],
                                     "locations": [dock, home]}, channel)["ok"])

    def test_mission_archive_rejects_other_map(self):
        channel = "cmd/missions/archive"
        stored = self.map_directory / "missions.json"
        before = stored.read_text()
        self.assertFalse(self.request({"map_id": "other-map", "id": "mission",
                                      "expected_revision": 1}, channel)["ok"])
        self.assertEqual(stored.read_text(), before)
        self.assertTrue(self.request({"map_id": "test-map", "id": "mission",
                                     "expected_revision": 1}, channel)["ok"])

    def test_system_reports_configured_capture_motion_thresholds(self):
        self.stop_bridge()
        self.extra_bridge_args = ["-p", "capture.enabled:=true",
                                  "-p", "capture.max_linear_speed:=0.012",
                                  "-p", "capture.max_angular_speed:=0.02"]
        self.start_bridge()
        state = self.receive(lambda e: e.get("ch") == "state/system")["p"]
        self.assertAlmostEqual(state["capture_max_linear_speed"], 0.012)
        self.assertAlmostEqual(state["capture_max_angular_speed"], 0.02)

    def test_mission_close_failure_preserves_file_and_confirmed_catalog(self):
        stored = self.map_directory / "missions.json"
        before = stored.read_text()
        temporary = self.map_directory / ".missions.json.tmp"
        temporary.symlink_to("/dev/full")
        changed = dict(self.mission, name="Must not replace confirmed mission")
        result = self.request({"mission": changed, "expected_revision": 1}, "cmd/missions/save")
        self.assertFalse(result["ok"])
        self.assertEqual(result["err"]["code"], "E_HARDWARE")
        self.assertEqual(stored.read_text(), before)
        self.assertFalse(temporary.is_symlink())
        self.assertTrue(self.request({}, "cmd/maps/list")["ok"])
        missions = self.receive(lambda e: e.get("ch") == "state/missions")["p"]["missions"]
        self.assertEqual(missions, [self.mission])

    def test_waypoint_replace_requires_explicit_points_to_clear_catalog(self):
        self.assertTrue(self.request({"id": "mission", "expected_revision": 1},
                                     "cmd/missions/archive")["ok"])
        stored = self.map_directory / "waypoints.json"
        before = stored.read_text()
        payload = {"map_id": "test-map", "expected_points": [self.point]}
        result = self.request(payload, "cmd/waypoints/set")
        self.assertFalse(result["ok"])
        self.assertEqual(result["err"]["code"], "E_BAD_PAYLOAD")
        self.assertEqual(stored.read_text(), before)
        self.assertTrue(self.request(dict(payload, points=[]), "cmd/waypoints/set")["ok"])
        self.assertEqual(json.loads(stored.read_text()), {"points": []})

    def test_arm_pose_archive_rejects_unreadable_other_map_missions(self):
        pose = {"id": "inspect", "name": "Inspection", "positions": [0.0] * 6}
        self.assertTrue(self.request({"preset": pose}, "cmd/arm/pose_presets/save")["ok"])
        stored = self.root / "arm_pose_presets.json"
        before = stored.read_text()
        directory = self.create_map("unreadable-missions")
        missions = directory / "missions.json"
        for kind in ["fifo", "broken-link", "directory", "missing-array"]:
            with self.subTest(kind=kind):
                if kind == "fifo":
                    os.mkfifo(missions)
                elif kind == "broken-link":
                    missions.symlink_to("missing.json")
                elif kind == "directory":
                    missions.mkdir()
                else:
                    missions.write_text("{}")
                result = self.request({"id": "inspect", "expected_revision": 1},
                                      "cmd/arm/pose_presets/archive")
                self.assertFalse(result["ok"])
                self.assertEqual(result["err"]["code"], "E_HARDWARE")
                self.assertEqual(stored.read_text(), before)
                self.assertTrue(self.request({}, "cmd/maps/list")["ok"])
                if kind == "directory":
                    missions.rmdir()
                else:
                    missions.unlink()
        self.assertTrue(self.request({"id": "inspect", "expected_revision": 1},
                                     "cmd/arm/pose_presets/archive")["ok"])

    def test_arm_pose_archive_checks_missions_in_dot_prefixed_maps(self):
        pose = {"id": "inspect", "name": "Inspection", "positions": [0.0] * 6}
        self.assertTrue(self.request({"preset": pose}, "cmd/arm/pose_presets/save")["ok"])
        stored = self.root / "arm_pose_presets.json"
        before = stored.read_text()
        directory = self.create_map(".survey")
        mission = dict(self.mission, steps=[{"id": "arm", "type": "arm_move", "pose": "inspect"}])
        (directory / "missions.json").write_text(json.dumps({"missions": [mission]}))
        result = self.request({"id": "inspect", "expected_revision": 1},
                              "cmd/arm/pose_presets/archive")
        self.assertFalse(result["ok"])
        self.assertEqual(result["err"]["code"], "E_BUSY")
        self.assertIn(directory.name, result["err"]["msg"])
        self.assertEqual(stored.read_text(), before)

    def report_mission_state(self, state, current_step=0):
        if not hasattr(self, "mission_publisher"):
            qos = fixture.QoSProfile(depth=1, durability=fixture.DurabilityPolicy.TRANSIENT_LOCAL)
            self.mission_publisher = self.node.create_publisher(
                fixture.MissionState, "/mission/state", qos)
            self.spin_for(0.3)
        self.mission_publisher.publish(fixture.MissionState(
            state=state, mission_id="mission", current_step=current_step))
        wire = {fixture.MissionState.COMPLETED: "completed",
                fixture.MissionState.FAILED: "failed",
                fixture.MissionState.IDLE: "idle",
                fixture.MissionState.PAUSED: "paused",
                fixture.MissionState.RUNNING: "running"}[state]
        self.receive(lambda e: e.get("ch") == "state/mission" and
            e["p"].get("state") == wire and e["p"].get("index") == current_step)

    def create_map(self, name):
        directory = self.root / "maps" / name
        directory.mkdir()
        (directory / "map.yaml").write_text("image: map.pgm\n")
        return directory

    def test_map_rename_rejects_special_catalog_files_without_moving_folder(self):
        self.report_mission_state(fixture.MissionState.IDLE)
        for index, (filename, kind) in enumerate([
                ("metadata.json", "fifo"), ("waypoints.json", "fifo"),
                ("locations.json", "fifo"), ("markers.json", "fifo"),
                ("missions.json", "fifo"), ("markers.json", "broken-link")]):
            with self.subTest(filename=filename, kind=kind):
                directory = self.create_map(f"special-rename-{index}")
                source = directory / filename
                if kind == "fifo":
                    os.mkfifo(source)
                else:
                    source.symlink_to("missing.json")
                target = directory.with_name(directory.name + "-renamed")
                result = self.request({"id": directory.name, "name": target.name},
                                      "cmd/maps/rename")
                self.assertFalse(result["ok"])
                self.assertEqual(result["err"]["code"], "E_HARDWARE")
                self.assertTrue(directory.is_dir())
                self.assertFalse(target.exists())
                self.assertEqual(list(directory.glob(".*.rename.tmp")), [])
        self.assertTrue(self.request({}, "cmd/maps/list")["ok"])

    def test_special_navigation_settings_do_not_block_startup(self):
        self.stop_bridge()
        stored = self.root / "navigation_settings.json"
        os.mkfifo(stored)
        self.start_bridge()
        self.assertTrue(self.request({}, "cmd/maps/list")["ok"])
        self.assertTrue(stored.is_fifo())

    def test_invalid_current_arm_catalog_is_preserved_without_legacy_migration(self):
        self.stop_bridge()
        pose = {"id": "inspect", "name": "Inspection", "positions": [0.0] * 6}
        legacy = self.root / "maps" / "arm_pose_presets.json"
        legacy.write_text(json.dumps({"presets": [pose]}))
        stored = self.root / "arm_pose_presets.json"
        for kind in ["fifo", "broken-link"]:
            with self.subTest(kind=kind):
                if kind == "fifo":
                    os.mkfifo(stored)
                else:
                    stored.symlink_to("missing.json")
                self.start_bridge()
                result = self.request({"preset": pose}, "cmd/arm/pose_presets/save")
                self.assertFalse(result["ok"])
                self.assertEqual(result["err"]["code"], "E_HARDWARE")
                self.assertTrue(stored.is_fifo() if kind == "fifo" else stored.is_symlink())
                self.assertEqual(json.loads(legacy.read_text()), {"presets": [pose]})
                self.assertTrue(self.request({}, "cmd/maps/list")["ok"])
                self.stop_bridge()
                stored.unlink()
        self.start_bridge()
        self.assertEqual(json.loads(stored.read_text()),
                         {"presets": [dict(pose, revision=1, archived=False)]})
        self.assertEqual(json.loads(legacy.read_text()), {"presets": [pose]})

    def test_invalid_startup_waypoint_does_not_crash_or_replace_files(self):
        self.stop_bridge()
        stored = self.map_directory / "waypoints.json"
        broken = json.dumps({"points": [dict(self.point, id=7)]})
        stored.write_text(broken)
        self.start_bridge()
        active = self.receive(lambda e: e.get("ch") == "state/active_map")["p"]
        self.assertEqual(active["id"], "live")
        self.report_mission_state(fixture.MissionState.RUNNING)
        self.assertTrue(self.request({}, "cmd/maps/list")["ok"])
        self.assertEqual(self.receive(lambda e: e.get("ch") == "state/waypoints")["p"]["points"], [])
        self.assertEqual(stored.read_text(), broken)
        self.assertIsNone(self.process.poll())

    def test_map_catalog_skips_unreadable_yaml_symlink_without_crashing(self):
        self.stop_bridge()
        directory = self.create_map("unreadable")
        yaml = directory / "map.yaml"
        yaml.unlink()
        yaml.symlink_to("map.yaml")
        self.start_bridge()
        maps = self.receive(lambda e: e.get("ch") == "state/maps")["p"]["maps"]
        self.assertEqual([item["id"] for item in maps], ["test-map"])
        self.assertTrue(self.request({}, "cmd/maps/list")["ok"])
        self.receive(lambda e: e.get("ch") == "state/active_map" and e["p"]["id"] == "test-map")
        self.assertIsNone(self.process.poll())

    def test_map_bundle_rejects_special_files_and_broken_links(self):
        self.report_mission_state(fixture.MissionState.IDLE)
        for kind, filename in [("directory", "locations.json"),
                               ("broken-link", "markers.json"),
                               ("fifo-missions", "missions.json"),
                               ("fifo-metadata", "metadata.json"),
                               ("fifo-waypoints", "waypoints.json")]:
            with self.subTest(kind=kind):
                directory = self.create_map("special-" + kind)
                stored = directory / filename
                if kind == "directory":
                    stored.mkdir()
                elif kind == "broken-link":
                    stored.symlink_to("missing.json")
                else:
                    os.mkfifo(stored)
                result = self.request({"id": directory.name}, "cmd/maps/select")
                self.assertFalse(result["ok"])
                self.assertEqual(result["err"]["code"], "E_BAD_PAYLOAD")
                self.assertIn(filename, result["err"]["msg"])
        self.assertTrue(self.request({}, "cmd/maps/list")["ok"])
        self.assertEqual(self.receive(lambda e: e.get("ch") == "state/active_map")["p"]["id"], "test-map")

    def test_map_bundle_rejects_invalid_items_without_changing_active_catalog(self):
        loads = []

        def load_map(request, response):
            loads.append(request.map_url)
            response.result = LoadMap.Response.RESULT_SUCCESS
            return response

        self.node.create_service(LoadMap, "/map_server/load_map", load_map)
        self.report_mission_state(fixture.MissionState.IDLE)
        marker = {"id": 1, "x": 1.0, "y": 2.0, "z": 1.2, "yaw": 0.0}
        step = self.mission["steps"][0]
        cases = [
            ("waypoints.json", "points", [dict(self.point, id=7)]),
            ("waypoints.json", "points", [dict(self.point, x="1")]),
            ("waypoints.json", "points", [dict(self.point, theta=None)]),
            ("waypoints.json", "points", [dict(self.point, name=1)]),
            ("waypoints.json", "points", [self.point, self.point]),
            ("locations.json", "locations", [dict(self.dock, kind=1)]),
            ("locations.json", "locations", [dict(self.dock, x="0")]),
            ("locations.json", "locations", [dict(self.dock, id=3)]),
            ("locations.json", "locations", [self.dock, self.dock]),
            ("markers.json", "markers", [dict(marker, id="1")]),
            ("markers.json", "markers", [dict(marker, id=2 ** 64 - 1)]),
            ("markers.json", "markers", [dict(marker, yaw=4.0)]),
            ("markers.json", "markers", [{"id": 1, "x": 0, "y": 0, "z": 1}]),
            ("markers.json", "markers", [marker, marker]),
            ("missions.json", "missions", [dict(self.mission, id=1)]),
            ("missions.json", "missions", [dict(self.mission, name=1)]),
            ("missions.json", "missions", [dict(self.mission, archived="false")]),
            ("missions.json", "missions", [dict(self.mission, revision="1")]),
            ("missions.json", "missions", [dict(self.mission, revision=-1)]),
            ("missions.json", "missions", [dict(self.mission, steps={})]),
            ("missions.json", "missions", [dict(self.mission, steps=[dict(step, type=1)])]),
            ("missions.json", "missions", [dict(self.mission, steps=[dict(step, location_id=1)])]),
            ("missions.json", "missions", [dict(self.mission, steps=[step, step])]),
            ("missions.json", "missions", [self.mission, self.mission]),
        ]
        for index, (filename, key, entries) in enumerate(cases):
            with self.subTest(filename=filename, case=index):
                directory = self.create_map(f"broken-{index}")
                stored = directory / filename
                broken = json.dumps({key: entries})
                stored.write_text(broken)
                result = self.request({"id": directory.name}, "cmd/maps/select")
                self.assertFalse(result["ok"])
                self.assertEqual(result["err"]["code"], "E_BAD_PAYLOAD")
                self.assertIn(filename, result["err"]["msg"])
                self.assertEqual(stored.read_text(), broken)
        self.assertEqual(loads, [], "invalid data reached the map_server")
        self.assertTrue(self.request({}, "cmd/maps/list")["ok"])
        self.assertEqual(self.receive(lambda e: e.get("ch") == "state/active_map")["p"]["id"], "test-map")
        points = self.receive(lambda e: e.get("ch") == "state/waypoints")["p"]["points"]
        self.assertEqual(points, [dict(self.point, status="todo")])
        self.assertEqual(json.loads((self.map_directory / "waypoints.json").read_text()),
                         {"points": [self.point]})

    def test_legacy_optional_fields_and_missing_references_still_load(self):
        def load_map(_, response):
            response.result = LoadMap.Response.RESULT_SUCCESS
            return response

        self.node.create_service(LoadMap, "/map_server/load_map", load_map)
        directory = self.create_map("legacy")
        point = {"id": "old", "x": 1.0, "y": 2.0}
        marker = {"id": 2, "x": 2.0, "y": 1.0}
        location = {"kind": "dock", "x": 0.0, "y": 0.0}
        mission = {"id": "legacy", "name": "Legacy", "steps": [
            {"id": "move", "type": "navigate", "location_id": "missing"}]}
        for filename, key, entries in [("waypoints.json", "points", [point]),
                                       ("markers.json", "markers", [marker]),
                                       ("locations.json", "locations", [location]),
                                       ("missions.json", "missions", [mission])]:
            (directory / filename).write_text(json.dumps({key: entries}))
        self.report_mission_state(fixture.MissionState.IDLE)
        self.assertTrue(self.request({"id": directory.name}, "cmd/maps/select")["ok"])
        self.assertTrue(self.request({}, "cmd/maps/list")["ok"])
        self.assertEqual(self.receive(lambda e: e.get("ch") == "state/waypoints")["p"]["points"],
                         [dict(point, status="todo")])
        self.assertEqual(self.receive(lambda e: e.get("ch") == "state/missions")["p"]["missions"], [mission])
        self.assertEqual(self.receive(lambda e: e.get("ch") == "state/locations")["p"]["locations"], [location])
        self.assertEqual(self.receive(lambda e: e.get("ch") == "state/markers")["p"]["markers"], [marker])

    def test_repeated_waypoint_visit_keeps_current_and_error_status(self):
        self.stop_bridge()
        mission = dict(self.mission, steps=[
            {"id": "first", "type": "navigate", "location_id": "wp"},
            {"id": "photo", "type": "capture", "preset": "test"},
            {"id": "again", "type": "navigate", "location_id": "wp"}])
        stored = self.map_directory / "missions.json"
        stored.write_text(json.dumps({"missions": [mission]}))
        self.start_bridge()
        for state, step, expected in [(fixture.MissionState.RUNNING, 0, "current"),
                                      (fixture.MissionState.PAUSED, 0, "current"),
                                      (fixture.MissionState.RUNNING, 1, "todo"),
                                      (fixture.MissionState.RUNNING, 2, "current"),
                                      (fixture.MissionState.FAILED, 0, "error"),
                                      (fixture.MissionState.COMPLETED, 2, "done")]:
            with self.subTest(state=state, step=step):
                self.report_mission_state(state, step)
                self.assertTrue(self.request({}, "cmd/maps/list")["ok"])
                point = self.receive(lambda e: e.get("ch") == "state/waypoints")["p"]["points"][0]
                self.assertEqual(point["status"], expected)
        self.assertEqual(json.loads(stored.read_text()), {"missions": [mission]})

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
