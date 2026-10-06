"""Check SDK/deployment names without installing or starting services."""

from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[2]
SDK = ROOT / "hmi/sdk"
PACKAGING = ROOT / "deploy/packaging"


class NamingTests(unittest.TestCase):
    def test_sdk_versions_and_exported_names_match(self):
        version = (SDK / "VERSION").read_text().strip()
        for platform in ("Linux", "MacOS", "Windows"):
            with self.subTest(platform=platform):
                directory = SDK / platform
                project = tomllib.loads((directory / "python/pyproject.toml").read_text())
                self.assertEqual(project["project"]["name"], "robot-sdk")
                self.assertEqual(project["project"]["version"], version)
                self.assertEqual(project["tool"]["setuptools"]["packages"]["find"]["include"],
                                 ["robot_sdk*"])
                cmake = (directory / "cpp/CMakeLists.txt").read_text()
                self.assertIn(f"VERSION {version}", cmake)
                self.assertIn("add_library(robot_sdk::sdk ALIAS robot_sdk)", cmake)
                self.assertIn("RobotSdkConfig.cmake.in", cmake)
                for path in (directory / "cpp").rglob("*"):
                    if path.suffix not in (".cpp", ".hpp") or "build" in path.parts:
                        continue
                    for old in ("namespace shalom", "shalom::", "shalom/", "SHALOM_SDK"):
                        self.assertNotIn(old, path.read_text(), str(path))

    def test_packaging_references_match_sources(self):
        runtime = PACKAGING / "robot-runtime"
        self.assertTrue((runtime / "bin/robot-runtime.in").is_file())
        self.assertTrue((runtime / "systemd/robot-runtime.service").is_file())
        self.assertTrue((PACKAGING / "site-config/etc/robot-runtime/robot.env").is_file())
        script = (ROOT / "deploy/scripts/build_robot_runtime.sh").read_text()
        self.assertIn("$template_root/bin/robot-runtime.in", script)
        self.assertIn("$template_root/systemd/robot-runtime.service", script)
        unit = (runtime / "systemd/robot-runtime.service").read_text()
        self.assertIn("ExecStart=/opt/shalom/releases/current/bin/robot-runtime", unit)
        self.assertIn("StateDirectory=shalom", unit)
        self.assertIn("EnvironmentFile=-/etc/shalom/robot.env", unit)
        for package, legacy in (("robot-runtime", "shalom-runtime"),
                                ("site-config", "shalom-site-config")):
            control = (PACKAGING / package / "debian/control.in").read_text()
            self.assertIn(f"Conflicts: {legacy}", control)
            self.assertIn(f"Replaces: {legacy}", control)

    def run_postinst(self, active=False, stop_fails=False):
        with tempfile.TemporaryDirectory(prefix="runtime-migration-test-") as directory:
            temporary = Path(directory)
            commands = temporary / "commands"
            for name in ("systemctl", "getent", "adduser", "ln"):
                mock = temporary / name
                mock.write_text(
                    '#!/bin/sh\n'
                    'printf "%s %s\\n" "${0##*/}" "$*" >> "$COMMAND_LOG"\n'
                    'if [ "${0##*/}" = systemctl ]; then\n'
                    '  if [ "$1" = is-active ]; then\n'
                    '    [ "$LEGACY_ACTIVE" = 1 ]; exit $?\n'
                    '  fi\n'
                    '  if [ "$1" = stop ] && [ "$STOP_FAILS" = 1 ]; then exit 1; fi\n'
                    'fi\nexit 0\n'
                )
                mock.chmod(0o755)
            env = dict(os.environ, PATH=f"{temporary}:/usr/bin:/bin",
                       COMMAND_LOG=str(commands), LEGACY_ACTIVE=str(int(active)),
                       STOP_FAILS=str(int(stop_fails)))
            result = subprocess.run(
                ["/bin/sh", str(PACKAGING / "robot-runtime/debian/postinst.in")],
                env=env, capture_output=True, text=True, timeout=10,
            )
            return result, commands.read_text().splitlines()

    @unittest.skipUnless(shutil.which("systemd-analyze"), "systemd-analyze is required")
    def test_unit_syntax_without_a_production_install(self):
        with tempfile.TemporaryDirectory(prefix="runtime-unit-test-") as directory:
            unit = Path(directory) / "robot-runtime.service"
            original = (PACKAGING / "robot-runtime/systemd/robot-runtime.service").read_text()
            unit.write_text(original.replace(
                "ExecStart=/opt/shalom/releases/current/bin/robot-runtime",
                "ExecStart=/usr/bin/true",
            ))
            result = subprocess.run(["systemd-analyze", "verify", str(unit)],
                                    capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_upgrade_stops_old_service_before_starting_new_service(self):
        result, commands = self.run_postinst(active=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertLess(commands.index("systemctl stop shalom-robot.service"),
                        commands.index("systemctl start robot-runtime.service"))
        self.assertNotIn("systemctl try-restart robot-runtime.service", commands)

    def test_fresh_install_does_not_start_a_service(self):
        result, commands = self.run_postinst()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("systemctl stop shalom-robot.service", commands)
        self.assertNotIn("systemctl start robot-runtime.service", commands)
        self.assertIn("systemctl try-restart robot-runtime.service", commands)

    def test_failed_legacy_stop_blocks_new_service_start(self):
        result, commands = self.run_postinst(active=True, stop_fails=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("systemctl start robot-runtime.service", commands)
        self.assertNotIn("systemctl daemon-reload", commands)


if __name__ == "__main__":
    unittest.main(verbosity=2)
