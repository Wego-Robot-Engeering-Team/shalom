"""Bringup wiring and runtime-file cleanup without launching the robot stack."""

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from launch import LaunchContext
from launch.actions import OpaqueFunction, RegisterEventHandler
from launch.events import Shutdown
import yaml

from docking.config import DockConfigError


ROBOT_ROOT = Path(__file__).resolve().parents[3]
LAUNCH_PATH = ROBOT_ROOT / "bringup/robot_bringup/launch/navigation.launch.py"


class NavigationLaunchTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="dock_launch_test_")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        spec = importlib.util.spec_from_file_location("navigation_launch_test", LAUNCH_PATH)
        self.launch = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.launch)
        self.share = self.root / "robot_bringup"
        config_dir = self.share / "navigation/config"
        config_dir.mkdir(parents=True)
        source = ROBOT_ROOT / "l2_control/navigation/config/nav2.yaml"
        (config_dir / "nav2.yaml").write_bytes(source.read_bytes())
        self.context = LaunchContext()
        self.context.launch_configurations["map"] = ""

    def prepare(self):
        with patch("ament_index_python.packages.get_package_share_directory", return_value=str(self.share)):
            actions = self.launch._prepare_docking(self.context)
        actions[0].execute(self.context)
        handler = next(action for action in actions if isinstance(action, RegisterEventHandler))
        self.addCleanup(handler.event_handler.handle, Shutdown(reason="test cleanup"), self.context)
        generated = Path(self.context.launch_configurations["nav2_params"])
        return generated, handler

    def test_selected_map_is_injected_and_generated_files_are_cleaned_on_shutdown(self):
        directory = self.root / "map_a"
        directory.mkdir()
        (directory / "map.yaml").write_text("image: map.pgm\n", encoding="utf-8")
        (directory / "locations.json").write_text(json.dumps({"locations": [
            {"kind": "dock", "x": 1, "y": 2, "theta": 0.3},
        ]}), encoding="utf-8")
        self.context.launch_configurations["map"] = str(directory / "map.yaml")
        generated, handler = self.prepare()
        params = yaml.safe_load(generated.read_text())
        database = Path(params["docking_server"]["ros__parameters"]["dock_database"])
        self.assertEqual(yaml.safe_load(database.read_text())["docks"]["dock"]["pose"], [1.0, 2.0, 0.3])
        self.assertNotEqual(database.parent, directory)
        handler.event_handler.handle(Shutdown(reason="test"), self.context)
        self.assertFalse(generated.parent.exists())

    def test_mapless_start_produces_empty_database(self):
        generated, _ = self.prepare()
        params = yaml.safe_load(generated.read_text())
        database = Path(params["docking_server"]["ros__parameters"]["dock_database"])
        self.assertEqual(yaml.safe_load(database.read_text()), {"docks": {}})

    def test_generation_is_disabled_when_nav2_is_disabled(self):
        description = self.launch.generate_launch_description()
        preparation = next(action for action in description.entities
                           if isinstance(action, OpaqueFunction) and
                           action.condition is not None)
        self.context.launch_configurations["nav2"] = "false"
        self.assertFalse(preparation.condition.evaluate(self.context))

    def test_malformed_selected_coordinates_fail_before_nav2_launch(self):
        directory = self.root / "broken"
        directory.mkdir()
        (directory / "map.yaml").write_text("image: map.pgm\n", encoding="utf-8")
        (directory / "locations.json").write_text('{"locations": [{"kind": "dock"}]}', encoding="utf-8")
        self.context.launch_configurations["map"] = str(directory / "map.yaml")
        with patch("ament_index_python.packages.get_package_share_directory", return_value=str(self.share)):
            with self.assertRaises(DockConfigError):
                self.launch._prepare_docking(self.context)
        self.assertNotIn("nav2_params", self.context.launch_configurations)


if __name__ == "__main__":
    unittest.main()
