# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

import importlib.util
from pathlib import Path
import xml.etree.ElementTree as ET

import yaml
from launch import LaunchContext
from launch.actions import DeclareLaunchArgument, GroupAction, IncludeLaunchDescription
from launch.utilities import normalize_to_list_of_substitutions, perform_substitutions
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare
from launch_ros.utilities import evaluate_parameters


PACKAGE = Path(__file__).resolve().parents[1]
DRIVER = PACKAGE.parents[1] / "l1_drivers/slamtec_aurora"


def resolve(context, value):
    return perform_substitutions(context, normalize_to_list_of_substitutions(value))


def description(path, monkeypatch, **overrides):
    shares = {"person_perception": PACKAGE, "slamtec_aurora": DRIVER}
    monkeypatch.setattr(FindPackageShare, "find", lambda self, name: str(shares[name]))
    spec = importlib.util.spec_from_file_location("aurora_test_launch", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    result = module.generate_launch_description()
    context = LaunchContext()
    context.launch_configurations.update(overrides)
    for entity in result.entities:
        if isinstance(entity, DeclareLaunchArgument):
            entity.execute(context)
    return result, context


def enabled(description, context, cls):
    result = []
    def visit(entities):
        for entity in entities:
            if entity.condition is not None and not entity.condition.evaluate(context):
                continue
            if isinstance(entity, cls):
                result.append(entity)
            elif isinstance(entity, GroupAction):
                visit(entity.get_sub_entities())
    visit(description.entities)
    return result


def test_driver_only_starts_sensor_acquisition(monkeypatch):
    launch, context = description(DRIVER / "launch/aurora_s.launch.py", monkeypatch,
                                  odom="false", imaging="true",
                                  ip_address="192.0.2.50", frame_topic="/test/frames")
    nodes = enabled(launch, context, Node)
    assert len(nodes) == 1
    assert nodes[0].node_package == "slamtec_aurora"
    assert resolve(context, nodes[0].node_executable) == "aurora_imaging"
    parameters = evaluate_parameters(context, nodes[0]._Node__parameters)
    assert parameters[0] == DRIVER / "config/aurora_s.yaml"
    assert parameters[1] == {"ip_address": "192.0.2.50", "frame_topic": "/test/frames"}


def test_combined_launch_forwards_settings(monkeypatch):
    launch, context = description(PACKAGE / "launch/person_cloud_test.launch.py", monkeypatch,
                                  ip_address="192.0.2.50", frame_topic="/test/frames",
                                  driver_config_file="/tmp/driver.yaml", config_file="/tmp/person.yaml")
    includes = enabled(launch, context, IncludeLaunchDescription)
    assert len(includes) == 1
    assert {name: resolve(context, value) for name, value in includes[0].launch_arguments} == {
        "ip_address": "192.0.2.50", "frame_topic": "/test/frames", "config_file": "/tmp/driver.yaml",
        "odom": "false", "imaging": "true"}
    nodes = enabled(launch, context, Node)
    assert [(node.node_package, resolve(context, node.node_executable)) for node in nodes] == [
        ("person_perception", "aurora_person_cloud"), ("rviz2", "rviz2")]
    parameters = evaluate_parameters(context, nodes[0]._Node__parameters)
    assert list(parameters) == [Path("/tmp/person.yaml"), {"input_topic": "/test/frames"}]


def test_processing_only_for_bag_or_existing_driver(monkeypatch):
    launch, context = description(PACKAGE / "launch/person_cloud_test.launch.py", monkeypatch,
                                  driver="false", rviz="false")
    assert enabled(launch, context, IncludeLaunchDescription) == []
    nodes = enabled(launch, context, Node)
    assert len(nodes) == 1 and nodes[0].node_package == "person_perception"
    parameters = evaluate_parameters(context, nodes[0]._Node__parameters)
    assert parameters[0] == PACKAGE / "config/person_cloud.yaml"


def test_sensor_and_perception_dependency_boundaries():
    driver_dependencies = {element.text for element in ET.parse(DRIVER / "package.xml").getroot()
                           if element.tag.endswith("depend")}
    assert "person_perception" not in driver_dependencies
    assert "interfaces" in driver_dependencies
    perception_source = (PACKAGE / "src/person_cloud_node.cpp").read_text()
    assert "RemoteSDK" not in perception_source
    assert "aurora_pubsdk" not in perception_source
    assert "third_party" not in (PACKAGE / "CMakeLists.txt").read_text()
    driver_source = (DRIVER / "src/enhanced_imaging_node.cpp").read_text()
    for algorithm in ("person_label_id", "mask_erosion_pixels", "extractPersonPoints"):
        assert algorithm not in driver_source


def test_configuration_split_preserves_tuning_and_output_topics():
    original = yaml.safe_load((DRIVER / "config/aurora_s.yaml").read_text())
    assert set(original) == {"/aurora_driver", "/aurora_imaging"}
    imaging = original["/aurora_imaging"]["ros__parameters"]
    person = yaml.safe_load((PACKAGE / "config/person_cloud.yaml").read_text())["/aurora_person_cloud"]["ros__parameters"]
    assert person["input_topic"] == imaging["frame_topic"]
    assert person["max_depth_m"] == 10.0 and person["max_planar_range_m"] == 3.0
    assert person["mask_erosion_pixels"] == 2
    assert person["max_local_depth_jump_m"] == 0.25
    assert "ip_address" not in person and "max_publish_rate_hz" not in person
    assert person["points3d_topic"] == "/aurora/person/points3d"
    assert person["points2d_topic"] == "/aurora/person/points2d"
    assert not (DRIVER / "launch/person_cloud_test.launch.py").exists()


def test_driver_config_cannot_replace_person_config(monkeypatch):
    launch, context = description(PACKAGE / "launch/person_cloud_test.launch.py", monkeypatch,
                                  config_file="/tmp/person.yaml", driver_config_file="/tmp/driver.yaml")
    baseline = dict(context.launch_configurations)
    for group in launch.entities:
        if not isinstance(group, GroupAction):
            continue
        for action in group.execute(context):
            if isinstance(action, IncludeLaunchDescription):
                arguments = {name: resolve(context, value) for name, value in action.launch_arguments}
                context.launch_configurations.update(arguments)
                assert context.launch_configurations["config_file"] == "/tmp/driver.yaml"
            else:
                action.execute(context)
    assert context.launch_configurations == baseline
    node = enabled(launch, context, Node)[0]
    assert evaluate_parameters(context, node._Node__parameters)[0] == Path("/tmp/person.yaml")
