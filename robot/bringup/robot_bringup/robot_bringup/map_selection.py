# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Default map selection shared by physical and simulated bringup."""

import json
from pathlib import Path

from launch.actions import SetLaunchConfiguration
from launch.substitutions import LaunchConfiguration


def resolve_default_map(context):
    requested = LaunchConfiguration("map").perform(context).strip()
    if requested == "none":
        return [SetLaunchConfiguration("map", "")]
    if requested != "auto":
        resolved = str(Path(requested).expanduser()) if requested else ""
        return [SetLaunchConfiguration("map", resolved)]
    maps_dir = Path(LaunchConfiguration("maps_dir").perform(context))
    setting = maps_dir / "default_map.json"
    if not setting.is_file():
        return [SetLaunchConfiguration("map", "")]
    try:
        map_id = json.loads(setting.read_text(encoding="utf-8"))["map_id"]
    except (OSError, KeyError, ValueError, TypeError) as exc:
        raise RuntimeError(f"invalid default map setting: {setting}") from exc
    if map_id == "":
        return [SetLaunchConfiguration("map", "")]
    if (not isinstance(map_id, str) or Path(map_id).name != map_id or
            map_id in (".", "..") or ".." in map_id):
        raise RuntimeError(f"invalid default map id in {setting}")
    map_yaml = maps_dir / map_id / "map.yaml"
    if not map_yaml.is_file():
        raise RuntimeError(f"default map is missing: {map_yaml}")
    return [SetLaunchConfiguration("map", str(map_yaml))]
