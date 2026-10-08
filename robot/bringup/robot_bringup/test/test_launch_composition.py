# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Check launch roles, argument forwarding and scoping without executing nodes."""

from collections import Counter
import importlib.util
import json
from pathlib import Path
import xml.etree.ElementTree as ET

import pytest
import yaml
from launch import LaunchContext, LaunchDescription, LaunchService
from launch.actions import (DeclareLaunchArgument, GroupAction, IncludeLaunchDescription,
                            OpaqueFunction, SetLaunchConfiguration)
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.utilities import normalize_to_list_of_substitutions, perform_substitutions
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare
from launch_ros.utilities import evaluate_parameters


ROOT = Path(__file__).resolve().parents[4]
SHARES = {
    "robot_bringup": ROOT / "robot/bringup/robot_bringup",
    "navigation": ROOT / "robot/l3_control/navigation",
    "hmi_bridge": ROOT / "robot/l5_gateway/hmi_bridge",
    "simulation_bringup": ROOT / "simulation/simulation_bringup",
}


def resolve(context, value):
    return perform_substitutions(context, normalize_to_list_of_substitutions(value))


class SourceWithLocation(PythonLaunchDescriptionSource):
    def __init__(self, location):
        self.declared_location = location
        super().__init__(location)


def load(package, filename, monkeypatch):
    path = SHARES[package] / "launch" / filename
    spec = importlib.util.spec_from_file_location(f"{package}_{filename.replace('.', '_')}", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    monkeypatch.setattr(module, "PythonLaunchDescriptionSource", SourceWithLocation, raising=False)
    monkeypatch.setattr(module, "get_package_share_directory",
                        lambda name: str(SHARES.get(name, Path("/test/share") / name)), raising=False)
    return module


def configure(description, **overrides):
    context = LaunchContext()
    context.launch_configurations.update(overrides)
    for entity in description.entities:
        if isinstance(entity, DeclareLaunchArgument):
            entity.execute(context)
    return context


def includes(entities, context):
    result = {}
    for entity in entities:
        if entity.condition is not None and not entity.condition.evaluate(context):
            continue
        if isinstance(entity, IncludeLaunchDescription):
            path = resolve(context, entity.launch_description_source.declared_location)
            result[Path(path).name] = entity
        elif isinstance(entity, GroupAction):
            result.update(includes(entity.get_sub_entities(), context))
    return result


def forwarded(include, context):
    return {name: resolve(context, value) for name, value in include.launch_arguments}


def node_id(node, context):
    return node.node_package, resolve(context, node.node_executable)


def nodes(entities, context):
    for entity in entities:
        if entity.condition is not None and not entity.condition.evaluate(context):
            continue
        if isinstance(entity, Node):
            yield entity
        elif isinstance(entity, GroupAction):
            yield from nodes(entity.get_sub_entities(), context)


def resolve_navigation(context, monkeypatch):
    module = load("navigation", "navigation.launch.py", monkeypatch)
    for action in module._resolve_map(context):
        action.execute(context)


def walk_launch(entities, context, monkeypatch, started, on_node=None):
    """Execute launch wiring in order, recording nodes without starting them."""
    for entity in entities:
        if entity.condition is not None and not entity.condition.evaluate(context):
            continue
        if isinstance(entity, Node):
            started.append({"node": node_id(entity, context),
                            "parameters": evaluate_parameters(context, entity._Node__parameters)})
            if on_node:
                on_node(entity, context)
        elif isinstance(entity, IncludeLaunchDescription):
            path = Path(resolve(context, entity.launch_description_source.declared_location))
            package = next((name for name, share in SHARES.items() if path.parent == share / "launch"), None)
            if package is None:
                started.append({"external_launch": path.name})
                continue
            context.launch_configurations.update(forwarded(entity, context))
            module = load(package, path.name, monkeypatch)
            if package == "navigation":
                # Dock DB generation has separate package tests; use a marker to
                # check that its launch configuration stays navigation-local.
                monkeypatch.setattr(module, "_prepare_docking", lambda _context: [
                    SetLaunchConfiguration("nav2_params", "/navigation/generated.yaml"),
                ])
            walk_launch(module.generate_launch_description().entities, context, monkeypatch, started, on_node)
        else:
            children = entity.execute(context)
            if children:
                walk_launch(children, context, monkeypatch, started, on_node)


@pytest.fixture(autouse=True)
def source_lookup(monkeypatch):
    def share(self, context):
        package = perform_substitutions(context, self.package)
        return str(SHARES.get(package, Path("/test/share") / package))
    monkeypatch.setattr(FindPackageShare, "perform", share)


@pytest.mark.parametrize("filename,expected", [
    ("control.launch.py", {"twist_mux", "joint_mux", "safety_gate"}),
    ("system.launch.py", {"mission_manager", "safety_manager", "motion_interlock_manager"}),
    ("gateway.launch.py", {"teleop_bridge"}),
])
def test_launch_roles_are_separate(monkeypatch, filename, expected):
    description = load("robot_bringup", filename, monkeypatch).generate_launch_description()
    assert {entity.node_package for entity in description.entities if isinstance(entity, Node)} == expected


@pytest.mark.parametrize("package,time,odometry,peer,rviz", [
    ("robot_bringup", "false", "/b2/odom", "", "false"),
    ("simulation_bringup", "true", "/b2/odom_gt", "127.0.0.1", "true"),
])
def test_entrypoints_include_subsystems_directly_and_preserve_defaults(
    monkeypatch, package, time, odometry, peer, rviz,
):
    description = load(package, "bringup.launch.py", monkeypatch).generate_launch_description()
    context = configure(description)
    children = includes(description.entities, context)
    assert "runtime.launch.py" not in children
    assert {"navigation.launch.py", "system.launch.py", "control.launch.py",
            "gateway.launch.py"} <= children.keys()
    assert context.launch_configurations["rviz"] == rviz
    navigation = forwarded(children["navigation.launch.py"], context)
    system = forwarded(children["system.launch.py"], context)
    control = forwarded(children["control.launch.py"], context)
    gateway = forwarded(children["gateway.launch.py"], context)
    for arguments in (navigation, system, control, gateway):
        assert arguments["use_sim_time"] == time
    assert system["base_odometry_topic"] == odometry
    assert system["base_output_topic"] == control["base_output_topic"] == "/cmd_vel"
    assert gateway["teleop_allowed_peer"] == peer
    assert navigation["map"] == gateway["map"] == "auto"
    assert navigation["maps_dir"] == gateway["maps_dir"]
    if package == "robot_bringup":
        assert "drivers.launch.py" in children
        metadata = yaml.safe_load((SHARES[package] / "config/robot_metadata.yaml").read_text())
        assert gateway["robot_id"] == metadata["robot"]["id"]
    else:
        assert "b2_sim.launch.py" in children
        assert gateway["robot_id"] == "SIM-B2-1"
        assert gateway["bridge_config"].endswith("config/bridge_sim.yaml")


@pytest.mark.parametrize("package", ["robot_bringup", "simulation_bringup"])
def test_map_preflight_runs_before_drivers(monkeypatch, tmp_path, package):
    description = load(package, "bringup.launch.py", monkeypatch).generate_launch_description()
    context = configure(description, maps_dir=str(tmp_path))
    (tmp_path / "default_map.json").write_text('{"map_id": "missing"}')
    assert not any(isinstance(action, OpaqueFunction) for action in description.entities)
    started = []
    with pytest.raises(RuntimeError):
        walk_launch(description.entities, context, monkeypatch, started)
    assert started == []


@pytest.mark.parametrize("package,time,odometry,peer", [
    ("robot_bringup", "false", "/b2/odom", "192.0.2.10"),
    ("simulation_bringup", "true", "/b2/odom_gt", "127.0.0.1"),
])
def test_bringup_has_each_endpoint_once_and_preserves_parameters(monkeypatch, package, time, odometry, peer):
    module = load(package, "bringup.launch.py", monkeypatch)
    description = module.generate_launch_description()
    context = configure(description, robot_id="TEST-42", teleop_allowed_peer=peer,
                        map="none", rviz="true")
    resolve_navigation(context, monkeypatch)
    children = includes(description.entities, context)
    assert {"navigation.launch.py", "system.launch.py", "control.launch.py",
            "gateway.launch.py", "rviz.launch.py"} <= children.keys()
    collected = []
    for filename, child in children.items():
        if filename not in {"navigation.launch.py", "system.launch.py", "control.launch.py",
                            "gateway.launch.py", "rviz.launch.py"}:
            continue
        package = "navigation" if filename == "navigation.launch.py" else "robot_bringup"
        leaf = load(package, filename, monkeypatch).generate_launch_description()
        leaf_context = configure(leaf, **forwarded(child, context))
        for node in nodes(leaf.entities, leaf_context):
            collected.append(node_id(node, leaf_context))
            params = evaluate_parameters(leaf_context, node._Node__parameters)
            assert params[-1]["use_sim_time"] is (time == "true")
            if node.node_package == "safety_gate":
                assert params[-1]["output_base_topic"] == "/cmd_vel"
                assert params[-1]["authority_timeout_ms"] == 500
            if node.node_package == "safety_manager":
                assert params[-1]["require_external_heartbeat"] is True
            if resolve(leaf_context, node.node_executable) == "base_motion_monitor_node":
                assert params[-1]["odometry_topic"] == odometry
            if node.node_package == "teleop_bridge":
                assert params[-1]["robot_id"] == "TEST-42"
                assert params[-1]["allowed_peer"] == peer
                assert params[-1]["output_topic"] == "/motion/teleop/cmd_vel"
        if filename == "gateway.launch.py":
            tcp = includes(leaf.entities, leaf_context)["bridge.launch.py"]
            bridge_description = load("hmi_bridge", "bridge.launch.py", monkeypatch).generate_launch_description()
            tcp_context = configure(bridge_description, **forwarded(tcp, leaf_context))
            for node in bridge_description.entities:
                if isinstance(node, Node):
                    collected.append(node_id(node, tcp_context))
                    params = evaluate_parameters(tcp_context, node._Node__parameters)[-1]
                    assert params["robot_id"] == "TEST-42"
                    assert params["use_sim_time"] is (time == "true")
    assert Counter(collected) == Counter({
        ("mission_manager", "mission_manager_node"): 1,
        ("safety_manager", "safety_manager_node"): 1,
        ("motion_interlock_manager", "motion_interlock_manager_node"): 1,
        ("motion_interlock_manager", "base_motion_monitor_node"): 1,
        ("twist_mux", "twist_mux"): 1, ("joint_mux", "joint_mux_node"): 1,
        ("safety_gate", "safety_gate_node"): 1, ("teleop_bridge", "teleop_bridge_node"): 1,
        ("hmi_bridge", "hmi_bridge_node"): 1, ("estop_bridge", "estop_bridge_node"): 1,
        ("nav2_map_server", "map_server"): 1, ("nav2_amcl", "amcl"): 1,
        ("nav2_lifecycle_manager", "lifecycle_manager"): 1, ("rviz2", "rviz2"): 1,
    })


def test_bridge_disabled_preserves_udp_and_system_safety(monkeypatch):
    description = load("robot_bringup", "gateway.launch.py", monkeypatch).generate_launch_description()
    context = configure(description, robot_id="TEST", bridge="false")
    assert includes(description.entities, context) == {}
    assert [entity.node_package for entity in description.entities if isinstance(entity, Node)] == ["teleop_bridge"]


@pytest.mark.parametrize("package", ["robot_bringup", "simulation_bringup"])
def test_bringup_scopes_do_not_leak_child_configuration(monkeypatch, package):
    description = load(package, "bringup.launch.py", monkeypatch).generate_launch_description()
    context = configure(description, robot_id="TEST", map="none", rviz="true")
    for group in description.entities:
        if not isinstance(group, GroupAction):
            continue
        if group.condition is not None and not group.condition.evaluate(context):
            continue
        baseline = dict(context.launch_configurations)
        for action in group.execute(context):
            if isinstance(action, IncludeLaunchDescription):
                context.launch_configurations["params_file"] = "/child/generated.yaml"
                context.launch_configurations["use_sim_time"] = "opposite"
            else:
                action.execute(context)
        assert context.launch_configurations == baseline


@pytest.mark.parametrize("mode", ["auto", "none", "", "explicit"])
def test_shared_map_resolution(monkeypatch, tmp_path, mode):
    module = load("navigation", "navigation.launch.py", monkeypatch)
    directory = tmp_path / "maps" / "inspection"
    directory.mkdir(parents=True)
    map_file = directory / "map.yaml"
    map_file.write_text("image: map.pgm\n")
    (directory.parent / "default_map.json").write_text(json.dumps({"map_id": "inspection"}))
    requested = str(map_file) if mode == "explicit" else mode
    context = LaunchContext()
    context.launch_configurations.update(map=requested, maps_dir=str(directory.parent))
    for action in module._resolve_map(context):
        action.execute(context)
    assert context.launch_configurations["map"] == (str(map_file) if mode in ("auto", "explicit") else "")


@pytest.mark.parametrize("map_id", ["../outside", "missing", 1, None, "nested/map", ".", "..", True])
def test_invalid_default_map_fails_before_nodes(monkeypatch, tmp_path, map_id):
    module = load("navigation", "navigation.launch.py", monkeypatch)
    (tmp_path / "default_map.json").write_text(json.dumps({"map_id": map_id}))
    context = LaunchContext()
    context.launch_configurations.update(map="auto", maps_dir=str(tmp_path))
    with pytest.raises(RuntimeError):
        module._resolve_map(context)


def test_missing_default_map_starts_mapless(monkeypatch, tmp_path):
    module = load("navigation", "navigation.launch.py", monkeypatch)
    context = LaunchContext()
    context.launch_configurations.update(map="auto", maps_dir=str(tmp_path))
    for action in module._resolve_map(context):
        action.execute(context)
    assert context.launch_configurations["map"] == ""


@pytest.mark.parametrize("document", ["{}", "[]", "null", "{bad json"])
def test_malformed_default_setting_is_rejected(monkeypatch, tmp_path, document):
    module = load("navigation", "navigation.launch.py", monkeypatch)
    (tmp_path / "default_map.json").write_text(document)
    context = LaunchContext()
    context.launch_configurations.update(map="auto", maps_dir=str(tmp_path))
    with pytest.raises(RuntimeError, match="invalid default map setting"):
        module._resolve_map(context)


@pytest.mark.parametrize("mode", ["none", "", "explicit"])
def test_explicit_selection_does_not_read_default(monkeypatch, tmp_path, mode):
    module = load("navigation", "navigation.launch.py", monkeypatch)
    (tmp_path / "default_map.json").write_text("{bad json")
    selected = tmp_path / "map.yaml"
    selected.write_text("image: map.pgm\n")
    context = LaunchContext()
    context.launch_configurations.update(map=str(selected) if mode == "explicit" else mode,
                                         maps_dir=str(tmp_path))
    for action in module._resolve_map(context):
        action.execute(context)
    assert context.launch_configurations["map"] == (str(selected) if mode == "explicit" else "")


@pytest.mark.parametrize("requested", ["relative/map.yaml", "/missing/map.yaml", "wrong_filename"])
def test_invalid_explicit_map_is_rejected(monkeypatch, tmp_path, requested):
    module = load("navigation", "navigation.launch.py", monkeypatch)
    if requested == "wrong_filename":
        selected = tmp_path / "other.yaml"
        selected.write_text("image: map.pgm\n")
        requested = str(selected)
    context = LaunchContext()
    context.launch_configurations.update(map=requested, maps_dir=str(tmp_path))
    with pytest.raises(RuntimeError):
        module._resolve_map(context)


@pytest.mark.parametrize("package", ["robot_bringup", "simulation_bringup"])
@pytest.mark.parametrize("mode", ["auto", "explicit", "none", "empty-default", "missing-default"])
def test_startup_map_matches_navigation_and_bridge(monkeypatch, tmp_path, package, mode):
    directory = tmp_path / "inspection"
    directory.mkdir()
    selected = directory / "map.yaml"
    selected.write_text("image: map.pgm\n")
    setting = tmp_path / "default_map.json"
    if mode != "missing-default":
        setting.write_text(json.dumps({"map_id": "" if mode == "empty-default" else "inspection"}))
    requested = str(selected) if mode == "explicit" else "none" if mode == "none" else "auto"
    description = load(package, "bringup.launch.py", monkeypatch).generate_launch_description()
    context = configure(description, map=requested, maps_dir=str(tmp_path))
    expected = str(selected) if mode in ("auto", "explicit") else ""
    started = []
    walk_launch(description.entities, context, monkeypatch, started)
    server = next(item for item in started if item.get("node") == ("nav2_map_server", "map_server"))
    bridge = next(item for item in started if item.get("node") == ("hmi_bridge", "hmi_bridge_node"))
    manager = next(item for item in started if item.get("node") == ("nav2_lifecycle_manager", "lifecycle_manager"))
    assert server["parameters"][-1]["yaml_filename"] == expected
    assert bridge["parameters"][-1]["initial_map"] == expected
    assert bridge["parameters"][-1]["maps_dir"] == str(tmp_path)
    assert manager["parameters"][-1]["autostart"] is bool(expected)
    assert "nav2_params" not in context.launch_configurations


@pytest.mark.parametrize("package", ["robot_bringup", "simulation_bringup"])
def test_default_is_read_once_even_if_setting_changes_during_startup(monkeypatch, tmp_path, package):
    for name in ("first", "second"):
        directory = tmp_path / name
        directory.mkdir()
        (directory / "map.yaml").write_text("image: map.pgm\n")
    setting = tmp_path / "default_map.json"
    setting.write_text('{"map_id": "first"}')
    reads = []
    original = Path.read_text

    def read(path, *args, **kwargs):
        if path == setting:
            reads.append(path)
        return original(path, *args, **kwargs)

    monkeypatch.setattr(Path, "read_text", read)
    description = load(package, "bringup.launch.py", monkeypatch).generate_launch_description()
    context = configure(description, robot_id="TEST", maps_dir=str(tmp_path))

    def change_setting(node, _context):
        if node.node_package == "nav2_map_server":
            setting.write_text('{"map_id": "second"}')

    started = []
    walk_launch(description.entities, context, monkeypatch, started, change_setting)
    bridge = next(item for item in started if item.get("node") == ("hmi_bridge", "hmi_bridge_node"))
    assert bridge["parameters"][-1]["initial_map"] == str(tmp_path / "first/map.yaml")
    assert reads == [setting]


@pytest.mark.parametrize("package", ["robot_bringup", "simulation_bringup"])
def test_launch_service_forwards_resolved_map_and_dds_before_nodes(monkeypatch, tmp_path, package):
    directory = tmp_path / "inspection"
    directory.mkdir()
    selected = directory / "map.yaml"
    selected.write_text("image: map.pgm\n")
    (tmp_path / "default_map.json").write_text('{"map_id": "inspection"}')

    def description(source, context):
        path = Path(resolve(context, source.declared_location))
        package = next((name for name, share in SHARES.items() if path.parent == share / "launch"), None)
        if package is None:
            return LaunchDescription()
        module = load(package, path.name, monkeypatch)
        if package == "navigation":
            monkeypatch.setattr(module, "_prepare_docking", lambda _context: [
                SetLaunchConfiguration("nav2_params", "/navigation/generated.yaml"),
            ])
        return module.generate_launch_description()

    monkeypatch.setattr(SourceWithLocation, "get_launch_description", description)
    monkeypatch.setattr(SourceWithLocation, "try_get_launch_description_without_context", lambda self: None)
    reported = {}
    domains = []

    def record(node, context):
        domains.append(context.environment.get("ROS_DOMAIN_ID"))
        assert context.environment["RMW_IMPLEMENTATION"] == "rmw_cyclonedds_cpp"
        assert context.environment["CYCLONEDDS_URI"].endswith("/config/cyclonedds.xml")
        if node.node_package in ("nav2_map_server", "hmi_bridge"):
            reported[node.node_package] = evaluate_parameters(context, node._Node__parameters)[-1]
        if node.node_package == "hmi_bridge":
            assert "nav2_params" not in context.launch_configurations
        return None

    # Exercise ROS launch's real depth-first action traversal and scope stack,
    # while replacing all processes with parameter capture.
    monkeypatch.setattr(Node, "execute", record)
    bringup = load(package, "bringup.launch.py", monkeypatch).generate_launch_description()
    service = LaunchService()
    service.include_launch_description(LaunchDescription([
        SetLaunchConfiguration("robot_id", "TEST"),
        SetLaunchConfiguration("domain_id", "37"),
        SetLaunchConfiguration("maps_dir", str(tmp_path)),
        bringup,
    ]))
    assert service.run() == 0
    assert reported["nav2_map_server"]["yaml_filename"] == str(selected)
    assert reported["hmi_bridge"]["initial_map"] == str(selected)
    assert domains and set(domains) == {"37"}


def test_navigation_exports_only_resolved_map(monkeypatch, tmp_path):
    module = load("navigation", "navigation.launch.py", monkeypatch)
    description = module.generate_launch_description()
    context = configure(description, map="none", maps_dir=str(tmp_path), params_file="/parent/params.yaml")
    for action in module._resolve_map(context):
        action.execute(context)
    baseline = dict(context.launch_configurations)
    group = next(entity for entity in description.entities if isinstance(entity, GroupAction))
    for action in group.execute(context):
        if isinstance(action, IncludeLaunchDescription):
            context.launch_configurations["nav2_params"] = "/child/generated.yaml"
            context.launch_configurations["params_file"] = "/child/params.yaml"
            context.launch_configurations["use_sim_time"] = "opposite"
        elif not isinstance(action, (Node, OpaqueFunction, GroupAction)):
            action.execute(context)
    assert context.launch_configurations == baseline
    assert context.launch_configurations["map"] == ""


def test_navigation_owns_bt_and_visualization():
    config = yaml.safe_load((SHARES["navigation"] / "config/nav2.yaml").read_text())
    tree = config["bt_navigator"]["ros__parameters"]["default_nav_to_pose_bt_xml"]
    assert tree.startswith("$(find-pkg-share navigation)/behavior_trees/")
    filename = tree.split("/behavior_trees/", 1)[1]
    ET.parse(SHARES["navigation"] / "behavior_trees" / filename)
    assert (SHARES["navigation"] / "licenses/LICENSE-APACHE-2.0.txt").is_file()
    for profile in ("slam", "nav2", "slam_nav2"):
        assert (SHARES["navigation"] / "rviz" / f"{profile}.rviz").is_file()
