"""Service acknowledgement, rejection and timeout without motion actions."""

import math
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest
import uuid

import rclpy
from nav2_msgs.srv import ReloadDockDatabase
from rclpy.context import Context
from rclpy.executors import SingleThreadedExecutor
import yaml

from docking.reload import reload_database


class ReloadTests(unittest.TestCase):
    def setUp(self):
        self.context = Context()
        rclpy.init(context=self.context, domain_id=187)
        self.node = rclpy.create_node("dock_reload_test", context=self.context)
        self.server = rclpy.create_node("dock_reload_server_test", context=self.context)
        self.executor = SingleThreadedExecutor(context=self.context)
        self.executor.add_node(self.server)
        self.thread = threading.Thread(target=self.executor.spin)
        self.thread.start()
        self.service_name = "/dock_database_test_" + uuid.uuid4().hex

    def tearDown(self):
        self.executor.shutdown(timeout_sec=2)
        self.thread.join(timeout=2)
        self.server.destroy_node()
        self.node.destroy_node()
        self.context.shutdown()

    def test_success_passes_the_generated_path(self):
        received = []

        def callback(request, response):
            received.append(request.filepath)
            response.success = True
            return response

        self.server.create_service(ReloadDockDatabase, self.service_name, callback)
        result = reload_database(self.node, "/tmp/example/dock_database.yaml", self.service_name, 3)
        self.assertTrue(result.success, result.detail)
        self.assertFalse(result.outcome_unknown)
        self.assertEqual(received, ["/tmp/example/dock_database.yaml"])

    def test_server_rejection_is_not_reported_as_success(self):
        self.server.create_service(ReloadDockDatabase, self.service_name, lambda _, response: response)
        result = reload_database(self.node, "/tmp/example.yaml", self.service_name, 3)
        self.assertFalse(result.success)
        self.assertFalse(result.outcome_unknown)
        self.assertIn("거절", result.detail)

    def test_missing_service_has_a_bounded_known_failure(self):
        result = reload_database(self.node, "/tmp/example.yaml", self.service_name, 0.1)
        self.assertFalse(result.success)
        self.assertFalse(result.outcome_unknown)

    def test_response_timeout_is_an_unknown_outcome(self):
        release = threading.Event()
        entered = threading.Event()

        def callback(_, response):
            entered.set()
            release.wait(timeout=2)
            response.success = True
            return response

        self.server.create_service(ReloadDockDatabase, self.service_name, callback)
        client = self.node.create_client(ReloadDockDatabase, self.service_name)
        try:
            self.assertTrue(client.wait_for_service(timeout_sec=3))
            result = reload_database(self.node, "/tmp/example.yaml", self.service_name, 0.2)
            self.assertTrue(entered.is_set())
            self.assertFalse(result.success)
            self.assertTrue(result.outcome_unknown)
        finally:
            release.set()
            self.node.destroy_client(client)

    def test_invalid_timeout_is_rejected(self):
        for timeout in (0, -1, math.inf, math.nan):
            with self.subTest(timeout=timeout), self.assertRaises(ValueError):
                reload_database(self.node, "/tmp/example.yaml", self.service_name, timeout)

    def test_cli_success_generates_database_and_removes_temporary_files(self):
        self.run_cli_check(True)

    def test_cli_rejection_exits_nonzero_and_removes_temporary_files(self):
        self.run_cli_check(False)

    def run_cli_check(self, accepted):
        received = []

        def callback(request, response):
            database = Path(request.filepath)
            received.append((database, yaml.safe_load(database.read_text())))
            response.success = accepted
            return response

        self.server.create_service(ReloadDockDatabase, self.service_name, callback)
        with tempfile.TemporaryDirectory(prefix="dock_cli_test_") as directory:
            root = Path(directory)
            map_yaml = root / "map.yaml"
            map_yaml.write_text("image: map.pgm\n", encoding="utf-8")
            (root / "locations.json").write_text(json.dumps({"locations": [
                {"kind": "dock", "x": 3.5, "y": -1.0, "theta": 0.2},
            ]}), encoding="utf-8")
            config = root / "nav2.yaml"
            config.write_text(yaml.safe_dump({"docking_server": {"ros__parameters": {
                "dock_plugins": ["simple_charging_dock"],
                "simple_charging_dock": {"plugin": "opennav_docking::SimpleChargingDock"},
            }}}), encoding="utf-8")
            env = os.environ.copy()
            env["ROS_DOMAIN_ID"] = "187"
            env["PYTHONPATH"] = str(Path(__file__).resolve().parents[1]) + os.pathsep + env.get("PYTHONPATH", "")
            command = subprocess.run([
                sys.executable, "-m", "docking.reload",
                "--map", str(map_yaml), "--nav2-config", str(config),
                "--service", self.service_name,
            ], env=env, capture_output=True, text=True, timeout=8)
        self.assertEqual(command.returncode, 0 if accepted else 1, command.stdout + command.stderr)
        self.assertEqual(len(received), 1)
        database, value = received[0]
        self.assertEqual(value["docks"]["dock"]["pose"], [3.5, -1.0, 0.2])
        self.assertFalse(database.parent.exists())


if __name__ == "__main__":
    unittest.main()
