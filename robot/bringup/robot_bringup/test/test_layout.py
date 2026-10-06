# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Check source layout; optionally check a rebuilt isolated or merged install.

python3 test_layout.py --install-root /path/to/install
The checks never launch nodes or modify robot operation data.
"""

import argparse
import ast
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

import yaml

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[4]
INSTALL_ROOT = None
PACKAGES = {
    "interfaces": "robot/common/interfaces",
    "pandar_xt32": "robot/l1_drivers/sensors/pandar_xt32",
    "slamtec_aurora": "robot/l1_drivers/sensors/slamtec_aurora",
    "vectornav_vn100": "robot/l1_drivers/sensors/vectornav_vn100",
    "velodyne_vlp16": "robot/l1_drivers/sensors/velodyne_vlp16",
    "joint_mux": "robot/l2_control/joint_mux",
    "docking": "robot/l2_control/docking",
    "safety_gate": "robot/l2_control/safety_gate",
    "lidar_slam": "robot/l2_control/lidar_slam",
    "navigation": "robot/l2_control/navigation",
    "mission_manager": "robot/l3_system/mission_manager",
    "motion_interlock_manager": "robot/l3_system/motion_interlock_manager",
    "safety_manager": "robot/l3_system/safety_manager",
    "estop_bridge": "robot/l4_communication/estop_bridge",
    "gateway_transport": "robot/l4_communication/gateway_transport",
    "hmi_bridge": "robot/l4_communication/hmi_bridge",
    "teleop_bridge": "robot/l4_communication/teleop_bridge",
    "robot_bringup": "robot/bringup/robot_bringup",
}
LAYERS = ("common", "l1_drivers", "l2_control", "l3_system", "l4_communication", "bringup")


def helper(path, name, package_shares):
    """Evaluate a path-only launch helper without constructing ROS nodes."""
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    function = next(node for node in tree.body
                    if isinstance(node, ast.FunctionDef) and node.name == name)
    namespace = {
        "Path": Path, "yaml": yaml, "__file__": str(path),
        "get_package_share_directory": lambda package: str(package_shares[package]),
    }
    # The robot-data helper calls the maps helper; retain that definition too.
    functions = [node for node in tree.body if isinstance(node, ast.FunctionDef)
                 and node.name in (name, "_writable_maps_dir")]
    exec(compile(ast.Module(body=functions, type_ignores=[]), str(path), "exec"), namespace)
    return namespace[function.name]()


class LayoutTest(unittest.TestCase):
    def source_share(self, package):
        if package == "simulation_bringup":
            return ROOT / "simulation/simulation_bringup"
        return ROOT / PACKAGES[package]

    def installed_share(self, package):
        for prefix in (INSTALL_ROOT / package, INSTALL_ROOT):
            share = prefix / "share" / package
            if share.is_dir():
                return share
        self.fail(f"rebuilt package share is missing: {package} under {INSTALL_ROOT}")

    def test_owned_package_names_and_source_paths(self):
        discovered = {}
        for layer in LAYERS:
            for manifest in (ROOT / "robot" / layer).rglob("package.xml"):
                name = ET.parse(manifest).findtext("name")
                self.assertNotIn(name, discovered, f"duplicate package: {manifest}")
                discovered[name] = manifest.parent.relative_to(ROOT).as_posix()
        self.assertEqual(discovered, PACKAGES)
        for legacy in ("interfaces", "gateway", "control", "sensors", "robot_bringup", "navigation"):
            self.assertEqual(list((ROOT / "robot" / legacy).rglob("package.xml")), [])
        self.assertFalse((ROOT / "robot/config/robot_metadata.yaml").exists())

    def test_colcon_discovers_all_owned_packages(self):
        colcon = shutil.which("colcon")
        if not colcon:
            self.skipTest("colcon is not installed")
        with tempfile.TemporaryDirectory(prefix="shalom-layout-discovery-") as directory:
            command = [colcon, "--log-base", str(Path(directory) / "log"), "list", "--base-paths"]
            command += [str(ROOT / "robot" / layer) for layer in LAYERS]
            result = subprocess.run(command, cwd=directory, check=True, text=True,
                                    capture_output=True, timeout=30)
        discovered = {}
        for line in result.stdout.splitlines():
            fields = line.split()
            if fields:
                discovered[fields[0]] = Path(fields[1]).resolve().relative_to(ROOT).as_posix()
        self.assertEqual(discovered, PACKAGES)

    def test_cmake_cross_directory_references_exist(self):
        pattern = re.compile(r"\$\{CMAKE_CURRENT_SOURCE_DIR\}/([A-Za-z0-9_./-]+)")
        for relative in PACKAGES.values():
            directory = ROOT / relative
            if ET.parse(directory / "package.xml").findtext("export/build_type") == "ament_python":
                self.assertTrue((directory / "setup.py").is_file(), str(directory))
                continue
            cmake = directory / "CMakeLists.txt"
            self.assertTrue(cmake.is_file(), str(cmake))
            for match in pattern.finditer(cmake.read_text(encoding="utf-8")):
                target = (directory / match.group(1)).resolve()
                self.assertTrue(target.exists(), f"{cmake}: missing {target}")

    def test_metadata_helpers_use_package_config(self):
        share = self.source_share("robot_bringup")
        metadata = share / "config/robot_metadata.yaml"
        expected = str(yaml.safe_load(metadata.read_text(encoding="utf-8"))["robot"]["id"])
        self.assertTrue(expected)
        for name in ("bringup.launch.py",):
            self.assertEqual(helper(share / "launch" / name, "_robot_id_from_metadata",
                                    {"robot_bringup": share}), expected)

    def test_sdk_and_common_header_paths_stay_independent(self):
        self.assertTrue((ROOT / "common/protocol/include/inspection/framing.hpp").is_file())
        self.assertTrue((ROOT / "robot/third_party/aurora_ros/src/aurora_remote_public/include").is_dir())
        for platform in ("Linux", "MacOS", "Windows"):
            sdk = ROOT / "hmi/sdk" / platform
            self.assertTrue((sdk / "python/pyproject.toml").is_file())
            self.assertTrue((sdk / "python/robot_sdk/client.py").is_file())
            self.assertTrue((sdk / "python/examples/monitor.py").is_file())
            self.assertTrue((sdk / "cpp/include/robot_sdk/api.hpp").is_file())
            self.assertTrue((sdk / "cpp/cmake/RobotSdkConfig.cmake.in").is_file())
            cmake = (sdk / "cpp/CMakeLists.txt").read_text(encoding="utf-8")
            self.assertIn("add_library(robot_sdk::sdk ALIAS robot_sdk)", cmake)
            self.assertIn("RobotSdkConfig.cmake.in", cmake)

    def test_owned_source_names_use_roles(self):
        roots = [ROOT / "robot" / layer for layer in LAYERS]
        roots += [ROOT / "hmi/sdk", ROOT / "deploy/packaging"]
        for root in roots:
            for path in root.rglob("*"):
                relative = path.relative_to(ROOT)
                if any(part in ("build", "__pycache__") or part.endswith(".egg-info")
                       for part in relative.parts):
                    continue
                self.assertNotIn("shalom", relative.as_posix().lower(), str(relative))

    def test_package_dependencies_have_no_duplicates(self):
        for relative in PACKAGES.values():
            package = ET.parse(ROOT / relative / "package.xml")
            dependencies = [(element.tag, element.text) for element in package.getroot()
                            if element.tag.endswith("depend")]
            self.assertEqual(len(dependencies), len(set(dependencies)), relative)

    def test_simulation_writable_paths_remain_simulation_owned(self):
        share = self.source_share("simulation_bringup")
        launch = share / "launch/bringup.launch.py"
        shares = {"simulation_bringup": share}
        maps = Path(helper(launch, "_writable_maps_dir", shares))
        data = Path(helper(launch, "_writable_robot_data_dir", shares))
        self.assertEqual(maps, share / "maps")
        self.assertEqual(data, share / "robot_data")
        self.assertTrue(maps.is_dir())
        self.assertTrue(data.is_dir())

    def test_installed_launch_and_configuration_resources(self):
        if INSTALL_ROOT is None:
            self.skipTest("pass --install-root to inspect rebuilt installed resources")
        for package in (*PACKAGES, "simulation_bringup"):
            source = self.source_share(package)
            share = self.installed_share(package)
            self.assertTrue((share / "package.xml").is_file(), str(share))
            for directory in ("launch", "config", "rviz", "behavior_trees", "licenses"):
                if not (source / directory).is_dir():
                    continue
                for original in (source / directory).rglob("*"):
                    if not original.is_file() or "__pycache__" in original.parts:
                        continue
                    installed = share / original.relative_to(source)
                    self.assertTrue(installed.is_file(), f"missing resource: {installed}")
                    self.assertEqual(installed.read_bytes(), original.read_bytes(), str(installed))
        metadata = self.source_share("robot_bringup") / "config/robot_metadata.yaml"
        self.assertEqual((self.installed_share("robot_bringup") / "config/robot_metadata.yaml").read_bytes(),
                         metadata.read_bytes())
        gateway_prefix = self.installed_share("gateway_transport").parent.parent
        self.assertEqual((gateway_prefix / "include/inspection/framing.hpp").read_bytes(),
                         (ROOT / "common/protocol/include/inspection/framing.hpp").read_bytes())

    def test_installed_metadata_and_simulation_helpers(self):
        if INSTALL_ROOT is None:
            self.skipTest("pass --install-root to inspect rebuilt installed resources")
        shares = {name: self.installed_share(name)
                  for name in ("robot_bringup", "hmi_bridge", "simulation_bringup")}
        expected = str(yaml.safe_load((shares["robot_bringup"] / "config/robot_metadata.yaml")
                                    .read_text(encoding="utf-8"))["robot"]["id"])
        for package, names in (("robot_bringup", ("bringup.launch.py",)),):
            for name in names:
                self.assertEqual(helper(shares[package] / "launch" / name,
                                        "_robot_id_from_metadata", shares), expected)
        simulation = shares["simulation_bringup"]
        launch = simulation / "launch/bringup.launch.py"
        maps = Path(helper(launch, "_writable_maps_dir", shares))
        data = Path(helper(launch, "_writable_robot_data_dir", shares))
        self.assertTrue((maps / "README.md").is_file())
        self.assertEqual(data, maps.parent / "robot_data")

    def test_launch_resources_are_owned_by_their_package(self):
        bringup = self.source_share("robot_bringup")
        navigation = self.source_share("navigation")
        self.assertFalse((bringup / "robot_bringup/map_selection.py").exists())
        self.assertTrue((navigation / "launch/navigation.launch.py").is_file())
        self.assertTrue((navigation / "behavior_trees/navigate_to_pose_w_replanning_and_recovery.xml").is_file())
        for obsolete in ("platform.launch.py", "slam.launch.py", "navigation.launch.py", "runtime.launch.py"):
            self.assertFalse((bringup / "launch" / obsolete).exists())
            if INSTALL_ROOT is not None:
                installed = self.installed_share("robot_bringup") / "launch" / obsolete
                self.assertFalse(installed.exists() or installed.is_symlink(), str(installed))
        for path in (bringup / "launch").glob("*.launch.py"):
            functions = [node.name for node in ast.parse(path.read_text()).body
                         if isinstance(node, ast.FunctionDef)]
            self.assertIn("generate_launch_description", functions, str(path))
        for package in ("robot_bringup", "hmi_bridge"):
            cmake = (self.source_share(package) / "CMakeLists.txt").read_text()
            self.assertNotIn("/../../l2_control/navigation", cmake)
            self.assertNotIn("/../../bringup/robot_bringup", cmake)

    def test_launch_node_dependencies_are_declared(self):
        for package in ("robot_bringup", "hmi_bridge", "navigation", "lidar_slam"):
            share = self.source_share(package)
            dependencies = {element.text for element in ET.parse(share / "package.xml").getroot()
                            if element.tag in ("depend", "exec_depend")}
            for path in (share / "launch").glob("*.launch.py"):
                for call in ast.walk(ast.parse(path.read_text())):
                    if not isinstance(call, ast.Call) or not isinstance(call.func, ast.Name):
                        continue
                    if call.func.id != "Node":
                        continue
                    for keyword in call.keywords:
                        if keyword.arg == "package" and isinstance(keyword.value, ast.Constant):
                            target = keyword.value.value
                            if target != package:
                                self.assertIn(target, dependencies, f"{path}: missing {target}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--install-root", type=Path)
    options, remaining = parser.parse_known_args()
    INSTALL_ROOT = options.install_root.resolve() if options.install_root else None
    unittest.main(argv=[sys.argv[0], *remaining], verbosity=2)
