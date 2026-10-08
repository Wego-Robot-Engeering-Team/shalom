"""Map data stays authoritative and robot control policy stays unchanged."""

import json
from pathlib import Path
import tempfile
import unittest

import yaml

from docking.config import (
    DockConfigError,
    dock_type_from_config,
    generate_dock_database,
    generate_nav2_config,
    read_map_docks,
)


class DockConfigTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="dock_config_test_")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.output = self.root / "runtime"
        self.policy = {
            "controller_server": {"ros__parameters": {"controller_frequency": 20.0}},
            "docking_server": {"ros__parameters": {
                "dock_plugins": ["example_model"],
                "example_model": {
                    "plugin": "opennav_docking::SimpleChargingDock",
                    "staging_x_offset": -0.7,
                    "use_external_detection_pose": True,
                },
                "controller": {"simulation_step": 0.1},
            }},
        }
        self.config = self.root / "nav2.yaml"
        self.config.write_text(yaml.safe_dump(self.policy), encoding="utf-8")

    def make_map(self, document, name="inspection_a"):
        directory = self.root / "maps" / name
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "map.yaml").write_text("image: map.pgm\n", encoding="utf-8")
        (directory / "locations.json").write_text(json.dumps(document), encoding="utf-8")
        return directory / "map.yaml"

    def station(self, **changes):
        return {"kind": "dock", "x": 4.18, "y": 0.45, "theta": 0.426, **changes}

    def test_existing_locations_are_the_only_coordinate_source(self):
        map_yaml = self.make_map({
            "locations": [self.station(), {"kind": "home", "x": 0, "y": 0, "theta": 0}],
            "nav2_docks": [{"id": "obsolete", "x": 100, "y": 100, "theta": 100}],
        })
        before = {p.name: p.read_bytes() for p in map_yaml.parent.iterdir()}
        generated = generate_nav2_config(self.config, map_yaml, self.output)
        params = yaml.safe_load(generated.read_text())
        db = Path(params["docking_server"]["ros__parameters"].pop("dock_database"))
        self.assertEqual(params, self.policy)
        self.assertEqual(yaml.safe_load(db.read_text()), {"docks": {"dock": {
            "type": "example_model", "frame": "map", "pose": [4.18, 0.45, 0.426],
        }}})
        self.assertEqual({p.name: p.read_bytes() for p in map_yaml.parent.iterdir()}, before)
        self.assertEqual(yaml.safe_load(self.config.read_text()), self.policy)

    def test_runtime_regeneration_updates_pose_and_replaces_previous_map(self):
        first = self.make_map({"locations": [self.station()]})
        second = self.make_map({"locations": [self.station(x=-5, theta=-0.3)]}, "inspection_b")
        db = generate_dock_database(first, self.output, "example_model")
        generate_dock_database(second, self.output, "example_model")
        self.assertEqual(yaml.safe_load(db.read_text())["docks"]["dock"]["pose"], [-5.0, 0.45, -0.3])
        (second.parent / "locations.json").write_text(
            json.dumps({"locations": [self.station(x=7)]}), encoding="utf-8",
        )
        generate_dock_database(second, self.output, "example_model")
        self.assertEqual(yaml.safe_load(db.read_text())["docks"]["dock"]["pose"][0], 7.0)

    def test_no_map_missing_file_and_no_station_generate_empty_database(self):
        map_yaml = self.make_map({"locations": [self.station()]})
        db = generate_dock_database(map_yaml, self.output, "example_model")
        generate_dock_database(None, self.output, "example_model")
        self.assertEqual(yaml.safe_load(db.read_text()), {"docks": {}})
        (map_yaml.parent / "locations.json").unlink()
        generate_dock_database(map_yaml, self.output, "example_model")
        self.assertEqual(yaml.safe_load(db.read_text()), {"docks": {}})
        empty = self.make_map({"locations": [{"kind": "home"}]})
        generate_dock_database(empty, self.output, "example_model")
        self.assertEqual(yaml.safe_load(db.read_text()), {"docks": {}})

    def test_invalid_coordinates_do_not_overwrite_existing_database(self):
        map_yaml = self.make_map({"locations": [self.station()]})
        db = generate_dock_database(map_yaml, self.output, "example_model")
        before = db.read_bytes()
        cases = [
            self.station(x=True), self.station(y="0.4"), self.station(x=float("nan")),
            self.station(theta=float("inf")), self.station(y=None),
            self.station(x=10 ** 1000),
            {"kind": "dock", "x": 0, "y": 0},
        ]
        for location in cases:
            with self.subTest(location=location):
                self.make_map({"locations": [location]})
                with self.assertRaises(DockConfigError):
                    generate_dock_database(map_yaml, self.output, "example_model")
                self.assertEqual(db.read_bytes(), before)

    def test_malformed_or_ambiguous_locations_are_rejected(self):
        cases = [
            [], {}, {"locations": {}}, {"locations": [None]},
            {"locations": [{"kind": "unknown"}]},
            {"locations": [self.station(), self.station()]},
            {"locations": [{"kind": "home"}, {"kind": "home"}]},
        ]
        for document in cases:
            with self.subTest(document=document):
                map_yaml = self.make_map(document)
                with self.assertRaises(DockConfigError):
                    read_map_docks(map_yaml, "example_model")
        (map_yaml.parent / "locations.json").write_text("{broken", encoding="utf-8")
        with self.assertRaises(DockConfigError):
            read_map_docks(map_yaml, "example_model")

    def test_invalid_map_path_and_map_output_directory_are_rejected(self):
        map_yaml = self.make_map({"locations": [self.station()]})
        for invalid in ("relative/map.yaml", self.root / "missing/map.yaml", self.config):
            with self.subTest(path=invalid), self.assertRaises(DockConfigError):
                read_map_docks(invalid, "example_model")
        with self.assertRaisesRegex(DockConfigError, "지도 폴더 밖"):
            generate_dock_database(map_yaml, map_yaml.parent / "generated", "example_model")
        self.assertFalse((map_yaml.parent / "generated").exists())

    def test_dock_type_must_be_unambiguous_and_have_an_implementation(self):
        self.assertEqual(dock_type_from_config(self.policy), "example_model")
        for plugins in ([], ["a", "b"], "example_model", [True], ["missing"], [""]):
            with self.subTest(plugins=plugins), self.assertRaises(DockConfigError):
                dock_type_from_config({"docking_server": {"ros__parameters": {"dock_plugins": plugins}}})

    def test_actual_robot_policy_is_preserved_except_database_path(self):
        source = Path(__file__).resolve().parents[3] / "l3_control/navigation/config/nav2.yaml"
        original = yaml.safe_load(source.read_text())
        generated = generate_nav2_config(source, None, self.output)
        params = yaml.safe_load(generated.read_text())
        params["docking_server"]["ros__parameters"].pop("dock_database")
        self.assertEqual(params, original)


if __name__ == "__main__":
    unittest.main()
