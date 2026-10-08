"""Real Jazzy Docking Server DB checks on an isolated domain, with no goals."""

import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import unittest

import rclpy
from lifecycle_msgs.msg import Transition
from lifecycle_msgs.srv import ChangeState
from rcl_interfaces.srv import GetParameters
from rclpy.context import Context
from rclpy.executors import SingleThreadedExecutor
import yaml

from docking.config import generate_dock_database, generate_nav2_config
from docking.reload import reload_database


DOCKING_BINARY = Path("/opt/ros/jazzy/lib/opennav_docking/opennav_docking")


@unittest.skipUnless(DOCKING_BINARY.is_file(), "installed Jazzy Docking Server is required")
class Nav2DatabaseTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="dock_nav2_test_")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.context = Context()
        rclpy.init(context=self.context, domain_id=188)
        self.addCleanup(self.context.shutdown)
        self.node = rclpy.create_node("nav2_dock_database_test", context=self.context)
        self.addCleanup(self.node.destroy_node)
        self.executor = SingleThreadedExecutor(context=self.context)
        self.executor.add_node(self.node)
        self.addCleanup(self.executor.shutdown)
        config = self.root / "nav2.yaml"
        config.write_text(yaml.safe_dump({"docking_server": {"ros__parameters": {
            "dock_plugins": ["simple_charging_dock"],
            "simple_charging_dock": {
                "plugin": "opennav_docking::SimpleChargingDock",
                "use_external_detection_pose": False,
                "use_battery_status": False,
                "use_stall_detection": False,
            },
            "controller": {"use_collision_detection": False},
        }}}), encoding="utf-8")
        map_yaml = self.make_map("first", 1.25)
        self.params = generate_nav2_config(config, map_yaml, self.root / "startup")
        env = os.environ.copy()
        env["ROS_DOMAIN_ID"] = "188"
        env["ROS_LOCALHOST_ONLY"] = "1"
        self.output = self.root / "nav2.log"
        stream = self.output.open("w", encoding="utf-8")
        self.process = subprocess.Popen([
            str(DOCKING_BINARY), "--ros-args", "--params-file", str(self.params),
            "-r", "cmd_vel:=/dock_database_test/cmd_vel",
        ], stdout=stream, stderr=subprocess.STDOUT, env=env)
        stream.close()
        self.addCleanup(self.stop_server)

    def make_map(self, name, x):
        directory = self.root / name
        directory.mkdir()
        (directory / "map.yaml").write_text("image: map.pgm\n", encoding="utf-8")
        (directory / "locations.json").write_text(json.dumps({"locations": [
            {"kind": "dock", "x": x, "y": 2.5, "theta": -0.3},
        ]}), encoding="utf-8")
        return directory / "map.yaml"

    def stop_server(self):
        if self.process.poll() is None:
            self.process.send_signal(signal.SIGINT)
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=2)

    def call(self, service_type, service, request):
        client = self.node.create_client(service_type, service)
        try:
            self.assertTrue(client.wait_for_service(timeout_sec=8), self.output.read_text())
            future = client.call_async(request)
            self.executor.spin_until_future_complete(future, timeout_sec=8)
            self.assertTrue(future.done(), self.output.read_text())
            return future.result()
        finally:
            self.node.destroy_client(client)

    def test_real_server_initial_load_map_reload_empty_and_rejection(self):
        # Configure creates the database but never activates or runs a motion action.
        response = self.call(
            ChangeState, "/docking_server/change_state",
            ChangeState.Request(transition=Transition(id=Transition.TRANSITION_CONFIGURE)),
        )
        self.assertTrue(response.success, self.output.read_text())
        self.assertIn("1 dock instances", self.output.read_text())
        response = self.call(
            GetParameters, "/docking_server/get_parameters",
            GetParameters.Request(names=["dock_database"]),
        )
        self.assertEqual(response.values[0].string_value, str(self.root / "startup/dock_database.yaml"))

        second = self.make_map("second", -4.75)
        database = generate_dock_database(second, self.root / "reload", "simple_charging_dock")
        self.assertEqual(yaml.safe_load(database.read_text())["docks"]["dock"]["pose"], [-4.75, 2.5, -0.3])
        result = reload_database(self.node, database)
        self.assertTrue(result.success, result.detail + "\n" + self.output.read_text())

        database = generate_dock_database(None, self.root / "empty", "simple_charging_dock")
        result = reload_database(self.node, database)
        self.assertTrue(result.success, result.detail + "\n" + self.output.read_text())

        invalid = self.root / "invalid.yaml"
        invalid.write_text("docks:\n  bad:\n    type: simple_charging_dock\n", encoding="utf-8")
        result = reload_database(self.node, invalid)
        self.assertFalse(result.success)
        self.assertFalse(result.outcome_unknown)
        self.assertIsNone(self.process.poll(), self.output.read_text())


if __name__ == "__main__":
    unittest.main()
