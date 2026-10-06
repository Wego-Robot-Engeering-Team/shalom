"""Check AprilTag configuration and launch wiring without starting ROS nodes."""

from __future__ import annotations

import importlib.util
from pathlib import Path

import pytest
import yaml

from launch import LaunchContext, LaunchDescription
from launch.actions import DeclareLaunchArgument, GroupAction, IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.utilities import normalize_to_list_of_substitutions, perform_substitutions
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare
from launch_ros.utilities import evaluate_parameters


PACKAGE_DIR = Path(__file__).resolve().parents[1]
CONFIG_FILE = PACKAGE_DIR / "config" / "apriltag_sim.yaml"


def _load_launch(filename):
    path = PACKAGE_DIR / "launch" / filename
    spec = importlib.util.spec_from_file_location(filename.replace(".", "_"), path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _configure_context(description, **overrides):
    context = LaunchContext()
    context.launch_configurations.update(overrides)
    for entity in description.entities:
        if isinstance(entity, DeclareLaunchArgument):
            entity.execute(context)
    return context


def _resolve(context, value):
    return perform_substitutions(context, normalize_to_list_of_substitutions(value))


def _bringup_description(monkeypatch):
    module = _load_launch("bringup.launch.py")
    monkeypatch.setattr(module, "get_package_share_directory", lambda name: str(PACKAGE_DIR))

    class SourceWithLocation(PythonLaunchDescriptionSource):
        def __init__(self, location):
            self.declared_location = location
            super().__init__(location)

    monkeypatch.setattr(module, "PythonLaunchDescriptionSource", SourceWithLocation)
    return module.generate_launch_description()


def _includes(entities, context):
    includes = {}
    for entity in entities:
        if isinstance(entity, IncludeLaunchDescription):
            path = _resolve(context, entity.launch_description_source.declared_location)
            includes[Path(path).name] = entity
        elif isinstance(entity, GroupAction):
            includes.update(_includes(entity.get_sub_entities(), context))
    return includes


@pytest.fixture
def source_package_lookup(monkeypatch):
    # Resolve this package to its source tree, even before a colcon installation.
    def package_share(self, context):
        name = perform_substitutions(context, self.package)
        if name == "simulation_bringup":
            return str(PACKAGE_DIR)
        return str(PACKAGE_DIR.parent / name)

    monkeypatch.setattr(FindPackageShare, "perform", package_share)


def test_detector_configuration_matches_simulated_camera_and_tag():
    document = yaml.safe_load(CONFIG_FILE.read_text(encoding="utf-8"))
    assert set(document) == {"/**"}
    parameters = document["/**"]["ros__parameters"]
    assert parameters["image_transport"] == "raw"
    assert parameters["qos_profile"] == "sensor_data"
    assert parameters["family"] == "36h11"
    assert parameters["pose_estimation_method"] == "pnp"
    assert parameters["max_hamming"] == 0
    # The current texture has cropped white margins; this is its black/white
    # detection boundary, not the full 0.32 m panel or a generic 8/10 ratio.
    assert parameters["size"] == pytest.approx(0.3125)
    assert parameters["tag"]["ids"] == [0]
    assert parameters["tag"]["frames"] == ["b2/dock_tag_0"]
    assert parameters["tag"]["sizes"] == pytest.approx([parameters["size"]])
    assert parameters["detector"]["decimate"] == 1.0
    assert parameters["detector"]["debug"] is False


@pytest.mark.parametrize("custom_inputs", [False, True])
@pytest.mark.parametrize("use_sim_time", ["true", "false"])
def test_standalone_launch_uses_camera_topics_and_typed_sim_time(
    source_package_lookup, monkeypatch, custom_inputs, use_sim_time,
):
    module = _load_launch("apriltag.launch.py")
    description = module.generate_launch_description()
    overrides = {"use_sim_time": use_sim_time}
    if custom_inputs:
        overrides.update(
            image_topic="/test/ideal_image",
            camera_info_topic="/test/camera_info",
            params_file=str(CONFIG_FILE),
        )
    context = _configure_context(description, **overrides)
    nodes = [entity for entity in description.entities if isinstance(entity, Node)]
    assert len(nodes) == 1
    detector = nodes[0]
    assert detector.node_package == "apriltag_ros"
    assert _resolve(context, detector.node_executable) == "apriltag_node"

    # launch_ros exposes expanded namespace/remappings publicly, but its input
    # parameter list is private. Evaluate that list without executing the node.
    parameters = evaluate_parameters(context, detector._Node__parameters)
    assert parameters[0] == CONFIG_FILE
    expected_sim_time = use_sim_time == "true"
    assert parameters[1] == {"use_sim_time": expected_sim_time}
    assert type(parameters[1]["use_sim_time"]) is bool

    # Substitution expansion normally writes an override YAML to a temporary
    # file. Preserve the same validation while avoiding that unnecessary write.
    evaluated_overrides = []

    def existing_parameter_file(values):
        evaluated_overrides.append(values)
        return str(CONFIG_FILE)

    monkeypatch.setattr(detector, "_create_params_file_from_dict", existing_parameter_file)
    detector._perform_substitutions(context)
    assert evaluated_overrides == [{"use_sim_time": expected_sim_time}]
    assert detector.expanded_node_namespace == "/b2/front_camera"
    assert dict(detector.expanded_remapping_rules) == {
        "image_rect": overrides.get("image_topic", "/b2/front_camera/image_raw"),
        "camera_info": overrides.get("camera_info_topic", "/b2/front_camera/camera_info"),
        "detections": "tag_detections",
    }
    assert detector.expanded_node_namespace + "/tag_detections" == (
        "/b2/front_camera/tag_detections"
    )


@pytest.mark.parametrize("front_camera, apriltag", [
    ("false", "false"),
    ("true", "false"),
    ("false", "true"),
    ("true", "true"),
])
def test_full_bringup_enables_camera_when_detector_is_requested(
    source_package_lookup, monkeypatch, front_camera, apriltag,
):
    description = _bringup_description(monkeypatch)
    declarations = {
        entity.name: entity for entity in description.entities
        if isinstance(entity, DeclareLaunchArgument)
    }
    default_context = LaunchContext()
    assert _resolve(default_context, declarations["front_camera"].default_value) == "false"
    assert _resolve(default_context, declarations["apriltag"].default_value) == "false"
    context = _configure_context(description, front_camera=front_camera, apriltag=apriltag)
    includes = _includes(description.entities, context)
    platform = includes["b2_sim.launch.py"]
    camera_enabled = _resolve(context, dict(platform.launch_arguments)["front_camera"]).lower()
    assert camera_enabled == str(front_camera == "true" or apriltag == "true").lower()

    detector = includes["apriltag.launch.py"]
    groups = [entity for entity in description.entities if isinstance(entity, GroupAction)]
    assert len(groups) == 1
    assert groups[0].condition.evaluate(context) is (apriltag == "true")
    assert detector.condition is None
    assert _resolve(context, dict(detector.launch_arguments)["use_sim_time"]) == "true"


def test_detector_scope_isolates_parameters_but_preserves_dds_environment(
    source_package_lookup, monkeypatch,
):
    expected_environment = {
        "ROS_DOMAIN_ID": "199",
        "RMW_IMPLEMENTATION": "rmw_cyclonedds_cpp",
        "CYCLONEDDS_URI": "file:///test/custom_cyclonedds.xml",
    }
    # Check both a parent with no generic params_file and a parent whose
    # Nav2/segmentation parameters must not be inherited by the detector.
    for parent_params_file in (None, "/test/nav2_params.yaml"):
        description = _bringup_description(monkeypatch)
        context = _configure_context(description, apriltag="true")
        context.launch_configurations.update(
            image_topic="/parent/image",
            camera_info_topic="/parent/camera_info",
            use_sim_time="false",
        )
        if parent_params_file is not None:
            context.launch_configurations["params_file"] = parent_params_file
        parent_configurations = dict(context.launch_configurations)
        context.environment.update(expected_environment)
        group = next(entity for entity in description.entities if isinstance(entity, GroupAction))
        assert group.condition.evaluate(context)
        detector_checked = False

        for action in group.execute(context):
            if not isinstance(action, IncludeLaunchDescription):
                action.execute(context)
                continue
            assert "params_file" not in context.launch_configurations
            assert "image_topic" not in context.launch_configurations
            assert {key: context.environment[key] for key in expected_environment} == expected_environment
            # Include execution resolves the child description and arguments;
            # only execute declarations, never the detector Node itself.
            for child_action in action.execute(context):
                if not isinstance(child_action, LaunchDescription):
                    child_action.execute(context)
                    continue
                for declaration in child_action.entities:
                    if isinstance(declaration, DeclareLaunchArgument):
                        declaration.execute(context)
                detector = next(entity for entity in child_action.entities if isinstance(entity, Node))
                parameters = evaluate_parameters(context, detector._Node__parameters)
                assert tuple(parameters) == (CONFIG_FILE, {"use_sim_time": True})
                assert context.launch_configurations["image_topic"] == "/b2/front_camera/image_raw"
                assert context.launch_configurations["camera_info_topic"] == "/b2/front_camera/camera_info"
                detector_checked = True

        assert detector_checked
        assert context.launch_configurations == parent_configurations
        assert {key: context.environment[key] for key in expected_environment} == expected_environment
