# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Adapt locations.json to Nav2's YAML format; never modify map data."""

import json
import math
import os
from pathlib import Path
import tempfile

import yaml


class DockConfigError(ValueError):
    """Map coordinates or docking policy cannot be used safely."""


def read_nav2_config(path):
    try:
        value = yaml.safe_load(Path(path).read_text(encoding="utf-8"))
    except (OSError, UnicodeError, yaml.YAMLError) as exc:
        raise DockConfigError(f"Nav2 설정을 읽을 수 없습니다: {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise DockConfigError("Nav2 설정의 최상위 값은 객체여야 합니다")
    return value


def dock_type_from_config(params):
    docking = params.get("docking_server", {})
    if not isinstance(docking, dict):
        raise DockConfigError("docking_server 설정은 객체여야 합니다")
    policy = docking.get("ros__parameters", {})
    if not isinstance(policy, dict):
        raise DockConfigError("docking_server.ros__parameters는 객체여야 합니다")
    plugins = policy.get("dock_plugins", [])
    # The current HMI supports one dock per map and has no dock-type selector.
    # Do not guess a type if the robot policy later defines several models.
    if not isinstance(plugins, list) or len(plugins) != 1:
        raise DockConfigError("현재 충전 위치에는 dock_plugins 하나가 필요합니다")
    dock_type = plugins[0]
    if not isinstance(dock_type, str) or not dock_type or dock_type.strip() != dock_type:
        raise DockConfigError("dock_plugins 이름이 올바르지 않습니다")
    plugin = policy.get(dock_type, {})
    if not isinstance(plugin, dict) or not isinstance(plugin.get("plugin"), str) or not plugin["plugin"]:
        raise DockConfigError(f"도킹 플러그인 구현이 없습니다: {dock_type}")
    return dock_type


def _map_path(map_yaml):
    if map_yaml is None or str(map_yaml) == "":
        return None
    path = Path(map_yaml).expanduser()
    if not path.is_absolute() or path.name != "map.yaml" or not path.is_file():
        raise DockConfigError("존재하는 map.yaml의 절대 경로가 필요합니다")
    return path.resolve()


def _coordinate(location, key):
    value = location.get(key)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise DockConfigError(f"충전 위치 {key}는 숫자여야 합니다")
    try:
        number = float(value)
    except OverflowError as exc:
        raise DockConfigError(f"충전 위치 {key}가 너무 큽니다") from exc
    if not math.isfinite(number):
        raise DockConfigError(f"충전 위치 {key}는 유한해야 합니다")
    return number


def read_map_docks(map_yaml, dock_type, frame="map"):
    """Read the single final docking pose; home locations are not Nav2 docks."""
    for name, value in (("dock_type", dock_type), ("frame", frame)):
        if not isinstance(value, str) or not value or value.strip() != value:
            raise DockConfigError(f"{name}이 올바르지 않습니다")
    path = _map_path(map_yaml)
    if path is None:
        return {}
    source = path.parent / "locations.json"
    if not source.exists():
        return {}
    try:
        document = json.loads(source.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, ValueError) as exc:
        raise DockConfigError(f"locations.json을 읽을 수 없습니다: {source}: {exc}") from exc
    if not isinstance(document, dict) or not isinstance(document.get("locations"), list):
        raise DockConfigError("locations.json에는 locations 배열이 필요합니다")
    docks = {}
    kinds = set()
    for location in document["locations"]:
        if not isinstance(location, dict) or location.get("kind") not in ("dock", "home"):
            raise DockConfigError("위치 종류는 dock 또는 home이어야 합니다")
        kind = location["kind"]
        if kind in kinds:
            raise DockConfigError(f"중복 위치 종류: {kind}")
        kinds.add(kind)
        if kind == "dock":
            docks["dock"] = {
                "type": dock_type,
                "frame": frame,
                "pose": [_coordinate(location, key) for key in ("x", "y", "theta")],
            }
    return docks


def _output_directory(output_dir, map_yaml):
    output = Path(output_dir).expanduser().resolve()
    path = _map_path(map_yaml)
    if path is not None and output.is_relative_to(path.parent):
        raise DockConfigError("생성 파일은 지도 폴더 밖의 실행 디렉터리에 저장해야 합니다")
    output.mkdir(parents=True, exist_ok=True)
    return output


def _write_yaml_atomic(path, value):
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", dir=path.parent,
            prefix=f".{path.name}.", suffix=".tmp", delete=False,
        ) as stream:
            temporary = Path(stream.name)
            yaml.safe_dump(value, stream, allow_unicode=True, sort_keys=False)
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def generate_dock_database(map_yaml, output_dir, dock_type, frame="map"):
    """Validate first, then atomically replace the derived database."""
    docks = read_map_docks(map_yaml, dock_type, frame)
    path = _output_directory(output_dir, map_yaml) / "dock_database.yaml"
    _write_yaml_atomic(path, {"docks": docks})
    return path


def generate_nav2_config(nav2_config, map_yaml, output_dir):
    """Only override dock_database, preserving all robot control parameters."""
    params = read_nav2_config(nav2_config)
    dock_type = dock_type_from_config(params)
    database = generate_dock_database(map_yaml, output_dir, dock_type)
    policy = params["docking_server"]["ros__parameters"]
    policy["dock_database"] = str(database)
    policy.pop("docks", None)
    result = database.parent / "nav2_params.yaml"
    _write_yaml_atomic(result, params)
    return result
