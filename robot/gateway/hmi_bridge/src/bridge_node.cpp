// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "hmi_bridge/bridge_node.hpp"

#include <tf2/LinearMath/Quaternion.hpp>
#include <lifecycle_msgs/msg/transition.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <fstream>
#include <filesystem>
#include <limits>
#include <optional>
#include <iterator>
#include <sstream>
#include <string_view>
#include <string>
#include <stdexcept>
#include <unordered_set>

// 점유격자를 PNG 로 눌러 관제에 보낸다 (encodeGridPng).
#include <zlib.h>

#include <tf2/LinearMath/Matrix3x3.h>
#include <tf2/LinearMath/Quaternion.h>

#include <cmath>

namespace hmi_bridge {
namespace {

// 채널 이름. 관제측 gcs/src/net/Channels.h 와 반드시 같아야 하며,
// 양쪽 모두 docs/bridge_protocol.md 를 따른다.
constexpr auto kChPose = "state/pose";
constexpr auto kChBattery = "state/battery";
constexpr auto kChSafety = "state/safety";
constexpr auto kChArm = "state/arm";
constexpr auto kChArmPosePresets = "state/arm_pose_presets";
constexpr auto kChPlan = "state/plan";
constexpr auto kChMap = "map/occupancy";
constexpr auto kChHealth = "state/health";
constexpr auto kChNav = "state/nav";
constexpr auto kChNavigationSpeed = "state/navigation_speed";
constexpr auto kChSystem = "state/system";
constexpr auto kChTrail = "state/trail";
constexpr auto kChWaypoints = "state/waypoints";
constexpr auto kChMissions = "state/missions";
constexpr auto kChLocations = "state/locations";
constexpr auto kChMarkers = "state/markers";
constexpr auto kChMission = "state/mission";
constexpr auto kChMaps = "state/maps";
constexpr auto kChActiveMap = "state/active_map";

/// Nav2 has this long to say whether it accepts a goal before the station's
/// request is answered with a failure. Goal acceptance is a planner-side
/// decision that normally lands in milliseconds; two seconds means it is wedged.
constexpr std::chrono::seconds kGoalAcceptTimeout{2};
constexpr auto kChLog = "evt/log";

constexpr auto kCmdEstop = "cmd/estop";
constexpr auto kCmdEstopRelease = "cmd/estop_release";
constexpr auto kCmdMode = "cmd/mode";
constexpr auto kCmdGoto = "cmd/goto";
constexpr auto kCmdInitialPose = "cmd/localization/initial_pose";
constexpr auto kCmdNavCancel = "cmd/nav_cancel";
constexpr auto kCmdNavPause = "cmd/nav_pause";
constexpr auto kCmdNavResume = "cmd/nav_resume";
constexpr auto kCmdNavigationSpeedLimit = "cmd/navigation/speed_limit";
constexpr auto kCmdNavigationSpeedSettings = "cmd/navigation/speed_settings";
constexpr auto kCmdWaypointsSet = "cmd/waypoints/set";
constexpr auto kCmdMissionsList = "cmd/missions/list";
constexpr auto kCmdMissionsSave = "cmd/missions/save";
constexpr auto kCmdMissionsArchive = "cmd/missions/archive";
constexpr auto kCmdArmPosePresetsList = "cmd/arm/pose_presets/list";
constexpr auto kCmdArmPosePresetsSave = "cmd/arm/pose_presets/save";
constexpr auto kCmdArmPosePresetsUpdate = "cmd/arm/pose_presets/update";
constexpr auto kCmdArmPosePresetsArchive = "cmd/arm/pose_presets/archive";
constexpr auto kCmdLocationsSet = "cmd/locations/set";
constexpr auto kCmdMarkersSet = "cmd/markers/set";
constexpr auto kCmdMapsList = "cmd/maps/list";
constexpr auto kCmdMapsSelect = "cmd/maps/select";
constexpr auto kCmdMapsRename = "cmd/maps/rename";
constexpr auto kCmdMapsSetDefault = "cmd/maps/set_default";
constexpr auto kCmdMapsDelete = "cmd/maps/delete";
constexpr auto kCmdTrailSnapshot = "cmd/trail/snapshot";
constexpr auto kChPreview = "capture/preview";
constexpr auto kChCaptureSpool = "state/capture_spool";
constexpr auto kCmdCapture = "cmd/capture/trigger";
constexpr auto kCmdMissionStart = "cmd/mission/start";
constexpr auto kCmdMissionPause = "cmd/mission/pause";
constexpr auto kCmdMissionResume = "cmd/mission/resume";
constexpr auto kCmdMissionStop = "cmd/mission/stop";
constexpr auto kCmdMissionReturnDock = "cmd/mission/return_dock";
constexpr auto kCmdPowerPolicy = "cmd/power/policy";
constexpr auto kCmdArmPreset = "cmd/arm/preset";
constexpr auto kCmdArmJointGoal = "cmd/arm/joint_goal";
constexpr auto kCmdArmEeGoal = "cmd/arm/ee_goal";
constexpr auto kCmdArmStop = "cmd/arm/stop";
constexpr auto kCmdBasePosture = "cmd/base/posture";
constexpr auto kChBase = "state/base";

std::string readDefaultMapId(const std::filesystem::path &root)
{
    std::ifstream in(root / "default_map.json");
    if (!in)
        return {};
    try {
        json document;
        if (in >> document && document.is_object() &&
            document.contains("map_id") && document["map_id"].is_string())
            return document["map_id"].get<std::string>();
    } catch (const json::exception &) {
    }
    return {};
}

/// 주행 결과를 카탈로그에 등록된 코드로 옮긴다.
///
/// 예전에는 "NAV_" + navStatus_ 로 문자열을 조립했다. 그러면 카탈로그에
/// 없는 코드가 런타임에 생기고, 관제는 그 코드를 설명하지 못한 채 원문만
/// 띄운다. 조작자에게는 원인도 조치도 없는 줄 하나가 남을 뿐이다.
const char *navResultCode(const std::string &status)
{
    if (status == "succeeded")
        return "MISSION_WAYPOINT_REACHED";
    if (status == "canceled")
        return "NAV_GOAL_CANCELED";
    if (status == "rejected")
        return "NAV_GOAL_REJECTED";
    return "NAV_GOAL_FAILED";
}

const char *safetyStateName(uint8_t state)
{
    using State = shalom_interfaces::msg::SafetyState;
    switch (state) {
    case State::INITIALIZING: return "initializing";
    case State::CONTROLLED_STOP: return "controlled_stop";
    case State::NORMAL: return "normal";
    case State::E_STOP_LATCHED: return "e_stop_latched";
    case State::FAULT: return "fault";
    default: return "unknown";
    }
}

const char *authorityName(uint8_t state)
{
    using Authority = shalom_interfaces::msg::MotionAuthority;
    switch (state) {
    case Authority::NONE: return "none";
    case Authority::BASE_ACTIVE: return "base_active";
    case Authority::BASE_STOPPING: return "base_stopping";
    case Authority::ARM_ACTIVE: return "arm_active";
    case Authority::ARM_STOPPING: return "arm_stopping";
    default: return "unknown";
    }
}

const char *missionStateName(uint8_t state)
{
    using State = shalom_interfaces::msg::MissionState;
    switch (state) {
    case State::IDLE: return "idle";
    case State::READY: return "ready";
    case State::RUNNING: return "running";
    case State::PAUSING: return "pausing";
    case State::PAUSED: return "paused";
    case State::RECOVERING: return "recovering";
    case State::RETURNING: return "returning";
    case State::COMPLETED: return "completed";
    case State::FAILED: return "failed";
    default: return "failed";
    }
}

/// FR3 ros2_control / URDF joint names, in kinematic-chain order.
const std::vector<std::string> kArmJointNames{
    "j1", "j2", "j3", "j4", "j5", "j6"};

bool validArmPresetId(const std::string &value)
{
    return !value.empty() && value.size() <= 96 &&
        std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '-' || c == '_';
        });
}

bool validArmPresetPositions(const json &positions)
{
    if (!positions.is_array() || positions.size() != kArmJointNames.size())
        return false;
    static constexpr double kJointMin[] = {-3.0543, -4.6251, -2.8274, -4.6251, -3.0543, -3.0543};
    static constexpr double kJointMax[] = { 3.0543,  1.4835,  2.8274,  1.4835,  3.0543,  3.0543};
    for (size_t i = 0; i < positions.size(); ++i) {
        if (!positions[i].is_number() || !std::isfinite(positions[i].get<double>()) ||
            positions[i].get<double>() < kJointMin[i] || positions[i].get<double>() > kJointMax[i])
            return false;
    }
    return true;
}

bool validArmPresetText(const std::string &name, const std::string &description)
{
    return !name.empty() && name.size() <= 80 && description.size() <= 400 &&
        name.find_first_of("\r\n") == std::string::npos &&
        description.find_first_of("\r\n") == std::string::npos;
}

// 자세는 지도와 독립적이지만 미션은 지도별로 저장된다. 삭제 전에 모든 지도에서
// 살아 있는 미션의 참조를 확인해야 다른 지도에 끊어진 단계가 남지 않는다.
std::optional<std::string> mapUsingArmPose(const std::filesystem::path &root,
                                           const std::string &poseId, std::string *error)
{
    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) {
        if (ec && error) *error = "지도 목록을 확인할 수 없습니다: " + ec.message();
        return std::nullopt;
    }
    for (const auto &entry : std::filesystem::directory_iterator(root, ec)) {
        if (ec) break;
        if (!entry.is_directory())
            continue;
        const std::string mapId = entry.path().filename().string();
        if (mapId.empty() || mapId.front() == '.')
            continue;
        const auto missionsPath = entry.path() / "missions.json";
        if (!std::filesystem::exists(missionsPath, ec)) {
            if (ec) break;
            continue;
        }
        std::ifstream in(missionsPath);
        if (!in) {
            if (error) *error = "미션 파일을 읽지 못했습니다: " + mapId;
            return std::nullopt;
        }
        try {
            json document;
            in >> document;
            if (!document.is_object() || !document.value("missions", json::array()).is_array())
                throw std::runtime_error("invalid missions");
            for (const auto &mission : document["missions"]) {
                if (!mission.is_object())
                    throw std::runtime_error("invalid mission");
                if (mission.value("archived", false))
                    continue;
                const json steps = mission.value("steps", json::array());
                if (!steps.is_array())
                    throw std::runtime_error("invalid mission steps");
                for (const auto &step : steps) {
                    if (step.is_object() && step.value("type", std::string{}) == "arm_move" &&
                        step.value("pose", std::string{}) == poseId)
                        return mapId;
                }
            }
        } catch (const std::exception &) {
            if (error) *error = "미션 파일을 검사하지 못했습니다: " + mapId;
            return std::nullopt;
        }
    }
    if (ec && error) *error = "지도 목록을 확인할 수 없습니다: " + ec.message();
    return std::nullopt;
}

/// 트레일을 이만큼 움직였을 때만 한 점을 남긴다. 서 있는 로봇이 초당 두 점씩
/// 같은 자리를 쌓으면 화면의 선이 뭉치고 대역폭만 먹는다.
constexpr double kTrailMinStepM = 0.05;
// 새 AMCL pose가 반영되기 전의 map->base는 옛 위치일 수 있다.
constexpr double kTrailLocalizationMatchM = 0.30;

bool mountPointActive(const std::string &mountPoint)
{
    if (mountPoint.empty())
        return false;
    std::ifstream mounts("/proc/self/mountinfo");
    std::string line;
    while (std::getline(mounts, line)) {
        std::istringstream fields(line);
        std::string id, parent, device, root, mountedAt;
        if (fields >> id >> parent >> device >> root >> mountedAt &&
            mountedAt == mountPoint)
            return true;
    }
    return false;
}

std::string captureFilePart(std::string value)
{
    for (char &ch : value) {
        const auto byte = static_cast<unsigned char>(ch);
        if (byte < 32 || std::string_view("<>:\"/\\|?*,_").find(ch) != std::string_view::npos)
            ch = '-';
    }
    const auto begin = value.find_first_not_of(" \t");
    if (begin == std::string::npos)
        return {};
    value = value.substr(begin, value.find_last_not_of(" \t") - begin + 1);
    if (value.size() > 80)
        value.resize(80);
    return value;
}

}  // namespace

BridgeNode::BridgeNode() : rclcpp::Node("hmi_bridge")
{
    port_ = int(declare_parameter("port", port_));
    robotId_ = declare_parameter("robot_id", robotId_);
    robotName_ = declare_parameter("robot_name", robotName_);
    // 식별자는 이제 안전 장치다. 나가는 모든 프레임에 찍히고, 이것으로
    // 들어오는 명령의 수신자를 확인한다. 비워 두면 그 확인이 통째로 꺼지므로
    // 조용히 빈 값을 받아들이지 않는다.
    if (robotId_.empty()) {
        throw std::invalid_argument("robot_id must be configured from robot metadata");
    }
    RCLCPP_INFO(get_logger(), "로봇 식별자 %s (%s)", robotId_.c_str(),
                robotName_.c_str());
    mapFrame_ = declare_parameter("map_frame", mapFrame_);
    baseFrame_ = declare_parameter("base_frame", baseFrame_);
    mapsDir_ = declare_parameter("maps_dir", mapsDir_);
    robotDataDir_ = declare_parameter("robot_data_dir", robotDataDir_);
    if (robotDataDir_.empty())
        throw std::invalid_argument("robot_data_dir must not be empty");
    navigationMinSpeed_ = declare_parameter("navigation.min_speed_mps", navigationMinSpeed_);
    navigationMaxSpeed_ = declare_parameter("navigation.max_speed_mps", navigationMaxSpeed_);
    navigationSpeed_ = declare_parameter("navigation.default_speed_mps", navigationSpeed_);
    if (!std::isfinite(navigationMinSpeed_) || !std::isfinite(navigationMaxSpeed_) ||
        !std::isfinite(navigationSpeed_) || navigationMinSpeed_ < 0.10 ||
        navigationMaxSpeed_ > 0.60 || navigationMinSpeed_ > navigationMaxSpeed_ ||
        navigationSpeed_ < navigationMinSpeed_ || navigationSpeed_ > navigationMaxSpeed_)
        throw std::invalid_argument("navigation speeds must satisfy 0.10 <= min <= default <= max <= 0.60");
    navigationMinAngularSpeed_ = declare_parameter("navigation.min_angular_speed_rps", navigationMinAngularSpeed_);
    navigationMaxAngularSpeed_ = declare_parameter("navigation.max_angular_speed_rps", navigationMaxAngularSpeed_);
    navigationAngularSpeed_ = declare_parameter("navigation.default_angular_speed_rps", navigationAngularSpeed_);
    if (!std::isfinite(navigationMinAngularSpeed_) || !std::isfinite(navigationMaxAngularSpeed_) ||
        !std::isfinite(navigationAngularSpeed_) || navigationMinAngularSpeed_ < 0.05 ||
        navigationMaxAngularSpeed_ > 0.80 || navigationMinAngularSpeed_ > navigationMaxAngularSpeed_ ||
        navigationAngularSpeed_ < navigationMinAngularSpeed_ ||
        navigationAngularSpeed_ > navigationMaxAngularSpeed_)
        throw std::invalid_argument("angular speeds must satisfy 0.05 <= min <= default <= max <= 0.80");
    {
        std::ifstream in(std::filesystem::path(robotDataDir_) / "navigation_settings.json");
        if (in) {
            try {
                json document;
                in >> document;
                json settings;
                std::string error;
                if (!readNavigationSpeed(document, false, &settings, &error))
                    throw std::invalid_argument(error);
                navigationSpeed_ = settings.at("speed_limit_mps").get<double>();
                navigationAngularSpeed_ = settings.at("angular_speed_limit_rps").get<double>();
                navigationMinSpeed_ = settings.at("min_speed_mps").get<double>();
                navigationMaxSpeed_ = settings.at("max_speed_mps").get<double>();
                navigationMinAngularSpeed_ = settings.at("min_angular_speed_rps").get<double>();
                navigationMaxAngularSpeed_ = settings.at("max_angular_speed_rps").get<double>();
            } catch (const std::exception &e) {
                RCLCPP_WARN(get_logger(), "주행 속도 설정을 읽지 못했습니다: %s", e.what());
            }
        }
    }
    speedLimitPub_ = create_publisher<nav2_msgs::msg::SpeedLimit>(
        declare_parameter("navigation.speed_limit_topic", std::string("/speed_limit")),
        rclcpp::QoS(1).reliable().transient_local());
    publishSpeedLimit();
    auto controllerNode = declare_parameter("navigation.controller_node", std::string("/controller_server"));
    navigationControllerId_ = declare_parameter("navigation.controller_id", navigationControllerId_);
    if (controllerNode.empty() || navigationControllerId_.empty())
        throw std::invalid_argument("navigation controller node and ID must not be empty");
    if (controllerNode.back() != '/') controllerNode += '/';
    navigationSpeedGetClient_ = create_client<rcl_interfaces::srv::GetParameters>(controllerNode + "get_parameters");
    navigationSpeedStateClient_ = create_client<lifecycle_msgs::srv::GetState>(controllerNode + "get_state");
    for (auto node : {controllerNode,
            declare_parameter("navigation.planner_node", std::string("/planner_server")),
            declare_parameter("navigation.navigator_node", std::string("/bt_navigator")),
            declare_parameter("localization.amcl_node", std::string("/amcl")),
            declare_parameter("localization.slam_node", std::string("/slam_toolbox"))}) {
        if (node.empty()) throw std::invalid_argument("lifecycle node must not be empty");
        if (node.back() != '/') node += '/';
        LifecycleObservation observation;
        observation.client = create_client<lifecycle_msgs::srv::GetState>(node + "get_state");
        navigationLifecycle_.push_back(std::move(observation));
    }
    navigationSpeedSetClient_ = create_client<rcl_interfaces::srv::SetParametersAtomically>(
        controllerNode + "set_parameters_atomically");
    armExecutionEnabled_ = declare_parameter("arm.execution_enabled", false);
    {
        const auto current = std::filesystem::path(robotDataDir_) / "arm_pose_presets.json";
        const auto legacy = std::filesystem::path(mapsDir_) / "arm_pose_presets.json";
        std::error_code ec;
        const bool useLegacy = !std::filesystem::is_regular_file(current, ec) &&
                               std::filesystem::is_regular_file(legacy, ec);
        const auto source = useLegacy ? legacy : current;
        std::ifstream in(source);
        if (in) {
            try {
                json document;
                in >> document;
                if (!document.is_object() || !document.contains("presets") || !document["presets"].is_array())
                    throw json::type_error::create(302, "presets must be an array in an object", &document);
                const auto &presets = document["presets"];
                if (presets.is_array()) {
                    std::unordered_set<std::string> ids, names;
                    for (const auto &preset : presets) {
                        try {
                            if (!preset.is_object() || !preset.contains("id") || !preset["id"].is_string() ||
                                !preset.contains("name") || !preset["name"].is_string() ||
                                !preset.contains("positions") || !validArmPresetPositions(preset["positions"]) ||
                                (preset.contains("description") && !preset["description"].is_string()) ||
                                (preset.contains("archived") && !preset["archived"].is_boolean()) ||
                                (preset.contains("revision") && (!preset["revision"].is_number_integer() ||
                                    preset["revision"].get<int64_t>() < 1)))
                                throw std::invalid_argument("invalid preset fields");
                            const auto id = preset["id"].get<std::string>();
                            const auto name = preset["name"].get<std::string>();
                            const bool archived = preset.value("archived", false);
                            if (!validArmPresetId(id) || !validArmPresetText(name, preset.value("description", std::string{})) ||
                                !ids.insert(id).second || (!archived && !names.insert(name).second))
                                throw std::invalid_argument("invalid or duplicate preset");
                            json validated = preset;
                            if (!validated.contains("revision")) validated["revision"] = 1;
                            if (!validated.contains("archived")) validated["archived"] = false;
                            armPosePresets_.push_back(std::move(validated));
                        } catch (const std::exception &e) {
                            armPosePresetsLoadInvalid_ = true;
                            RCLCPP_WARN(get_logger(), "잘못된 팔 자세 항목을 건너뜁니다: %s", e.what());
                        }
                    }
                    if (useLegacy) {
                        std::string error;
                        if (saveArmPosePresets(&error))
                            RCLCPP_INFO(get_logger(), "팔 자세 프리셋을 %s로 이전했습니다", current.c_str());
                        else
                            RCLCPP_WARN(get_logger(), "팔 자세 프리셋 이전 실패: %s", error.c_str());
                    }
                } else armPosePresetsLoadInvalid_ = true;
            } catch (const json::exception &e) {
                armPosePresetsLoadInvalid_ = true;
                RCLCPP_WARN(get_logger(), "팔 자세 프리셋을 읽지 못했습니다: %s", e.what());
            }
        } else if (std::filesystem::exists(source, ec) || ec) {
            armPosePresetsLoadInvalid_ = true;
            RCLCPP_WARN(get_logger(), "팔 자세 파일을 열지 못했습니다: %s", source.c_str());
        }
    }
    const auto initialMap = declare_parameter("initial_map", std::string{});
    if (!initialMap.empty()) {
        const std::filesystem::path requested(initialMap);
        std::string mapId;
        if (!requested.is_absolute() || requested.filename() != "map.yaml") {
            RCLCPP_WARN(get_logger(),
                        "initial_map은 절대 경로의 map.yaml이어야 합니다: %s",
                        initialMap.c_str());
        } else {
            // colcon --symlink-install은 map.yaml만 원본 소스로 연결할 수 있다.
            // filesystem::relative는 심볼릭 링크를 따라가 mapsDir 밖의 파일로
            // 계산하므로, 구성에 적힌 경로의 구조를 그대로 비교한다.
            const auto relative = requested.lexically_normal().lexically_relative(
                std::filesystem::path(mapsDir_).lexically_normal());
            if (!relative.empty() && relative.filename() == "map.yaml"
                && relative.parent_path().filename() == relative.parent_path()) {
                mapId = relative.parent_path().string();
            }
            if (mapId.empty())
                RCLCPP_WARN(get_logger(),
                            "initial_map은 maps_dir 바로 아래 <지도 ID>/map.yaml이어야 합니다: %s",
                            initialMap.c_str());
        }
        std::string detail;
        if (!mapId.empty() && !loadMapBundle(mapId, &detail)) {
            RCLCPP_WARN(get_logger(), "시작 지도 %s 상태를 읽지 못했습니다: %s",
                        mapId.c_str(), detail.c_str());
        }
    }
    safetyCommandClient_ = create_client<shalom_interfaces::srv::SafetyCommand>(
        "/safety/command");
    safetyEventPub_ = create_publisher<shalom_interfaces::msg::SafetyEvent>("/safety/event", 20);
    authorityRequestClient_ = create_client<shalom_interfaces::srv::AuthorityRequest>(
        "/motion/authority/request");
    missionConfigureClient_ = create_client<shalom_interfaces::srv::ConfigureMission>(
        "/mission/configure");
    missionControlClient_ = create_client<shalom_interfaces::srv::MissionControl>(
        "/mission/control");

    batterySub_ = create_subscription<sensor_msgs::msg::BatteryState>(
        "battery_state", 10, [this](const sensor_msgs::msg::BatteryState::ConstSharedPtr &msg) {
            sendEnvelope(makePublish(kChBattery,
                                     json{{"soc", msg->percentage * 100.0},
                                          {"voltage", msg->voltage},
                                          {"current", msg->current},
                                          {"charging",
                                           msg->power_supply_status
                                               == sensor_msgs::msg::BatteryState::
                                                      POWER_SUPPLY_STATUS_CHARGING}}),
                         true);
        });

    jointSub_ = create_subscription<sensor_msgs::msg::JointState>(
        "fr3/joint_states", 10, [this](const sensor_msgs::msg::JointState::ConstSharedPtr &msg) {
            // JointState의 배열 순서는 발행자마다 다를 수 있다. HMI의 3D
            // 모델과 정지 명령은 j1..j6 순서를 전제로 하므로 이름으로 맞춘다.
            std::vector<double> ordered;
            std::vector<double> velocities;
            ordered.reserve(kArmJointNames.size());
            velocities.reserve(kArmJointNames.size());
            for (const auto &name : kArmJointNames) {
                const auto it = std::find(msg->name.begin(), msg->name.end(), name);
                if (it == msg->name.end())
                    return;  // 불완전한 상태를 실제 관절값처럼 표시하지 않는다.
                const auto index = std::size_t(std::distance(msg->name.begin(), it));
                if (index >= msg->position.size())
                    return;
                ordered.push_back(msg->position[index]);
                if (index < msg->velocity.size())
                    velocities.push_back(msg->velocity[index]);
            }
            if (std::any_of(ordered.begin(), ordered.end(), [](double value) { return !std::isfinite(value); })) {
                lastArmPositions_.clear();
                lastArmReceived_ = {};
                return;
            }
            lastArmPositions_ = ordered;
            lastArmReceived_ = std::chrono::steady_clock::now();
            // TODO(integration): 조작성 지수는 야코비안에서 계산해 함께 실어야 한다.
            // 관제는 표시만 하며 스스로 계산하지 않는다 (프로토콜 §4).
            sendEnvelope(makePublish(kChArm, json{{"positions", ordered},
                                                  {"velocities", velocities},
                                                  {"names", kArmJointNames}}),
                         true);
        });

    planSub_ = create_subscription<nav_msgs::msg::Path>(
        "plan", 10, [this](const nav_msgs::msg::Path::ConstSharedPtr &msg) {
            json points = json::array();
            for (const auto &pose : msg->poses)
                points.push_back({pose.pose.position.x, pose.pose.position.y});
            lastPlanPoints_ = points;
            sendEnvelope(makePublish(kChPlan, json{{"points", points}}), true);
        });

    // 맵은 크고 드물다. transient_local 로 걸어 관제가 늦게 붙어도 받는다.
    mapSub_ = create_subscription<nav_msgs::msg::OccupancyGrid>(
        "map", rclcpp::QoS(1).transient_local(),
        [this](const nav_msgs::msg::OccupancyGrid::ConstSharedPtr &msg) {
            if (mapTransitionUncertain_) return;
            const std::string png = encodeGridPng(*msg);
            if (png.empty()) {
                RCLCPP_WARN(get_logger(), "맵 %ux%u 를 PNG 로 만들지 못했다",
                            msg->info.width, msg->info.height);
                return;
            }
            lastMapMeta_ = json{{"width", msg->info.width},
                                {"height", msg->info.height},
                                {"resolution", msg->info.resolution},
                                {"origin",
                                 json{{"x", msg->info.origin.position.x},
                                      {"y", msg->info.origin.position.y},
                                      {"theta", 0.0}}},
                                {"map_id", pendingMapPublication_ ? pendingMapId_ : mapId_},
                                {"encoding", "png"}};
            lastMapPng_ = png;
            sendEnvelope(makePublish(kChMap, lastMapMeta_), false, png);
            RCLCPP_INFO_THROTTLE(get_logger(), *get_clock(), 10000,
                                 "맵 %ux%u, PNG %zu 바이트", msg->info.width,
                                 msg->info.height, png.size());
        });

    tfBuffer_ = std::make_unique<tf2_ros::Buffer>(get_clock());
    tfListener_ = std::make_shared<tf2_ros::TransformListener>(*tfBuffer_);

    // 포트를 열지 못해도 죽지 않는다.
    //
    // 예전에는 여기서 예외를 던졌고, launch 의 respawn=true 가 2 초마다 노드를
    // 되살렸다. 포트를 쥔 쪽이 살아 있는 한 결과는 매번 같으므로, 복구되지
    // 않는 조건을 무한히 재시도하며 FATAL 로그만 쌓였다. respawn 은 간헐적
    // 크래시를 위한 장치이지 이런 상태를 위한 것이 아니다.
    //
    // 대신 프로세스를 유지한 채 주기적으로 다시 시도한다. 포트를 쥔 쪽이
    // 사라지면 그때 올라오고, 프로세스가 갈리지 않으니 ROS 그래프도 흔들리지
    // 않는다.
    using namespace std::chrono_literals;

    if (!openControlPort())
        bindRetryTimer_ = create_wall_timer(5s, [this] {
            if (openControlPort())
                bindRetryTimer_->cancel();
        });

    // 본체 자세는 B2 드라이버의 Trigger 서비스를 부른다. 드라이버가 없으면
    // 클라이언트는 만들어지되 호출이 실패하고, 그 사유가 관제로 간다.
    for (const auto &name : {"stand_up", "stand_down", "balance_stand",
                             "recovery_stand", "damp"})
        postureClients_[name] = create_client<std_srvs::srv::Trigger>(name);
    postureDryRun_ = declare_parameter("base.posture_dry_run", false);
    if (postureDryRun_)
        RCLCPP_WARN(get_logger(),
                    "본체 자세 명령을 로그로만 처리합니다 (base.posture_dry_run). "
                    "실기에서는 반드시 꺼야 합니다.");

    // 모션 권한을 듣는다. 팔이 움직이는 중에는 자세 전환을 받지 않는다.
    // transient_local 이라 나중에 붙어도 마지막 값을 받는다.
    authoritySub_ = create_subscription<shalom_interfaces::msg::MotionAuthority>(
        "/motion/authority", rclcpp::QoS(1).transient_local(),
        [this](const shalom_interfaces::msg::MotionAuthority::SharedPtr msg) {
            const std::string next = authorityName(msg->state);
            authorityReceived_ = std::chrono::steady_clock::now();
            if (motionAuthority_ == next)
                return;
            motionAuthority_ = next;
            // 자세 버튼을 잠그고 푸는 근거가 이 값이다. 바뀐 것을 보내지
            // 않으면 화면은 팔이 멈춘 뒤에도 버튼을 잠근 채로 둔다. 관제가
            // 없을 때 쌓아 두지는 않는다 — 새로 붙는 화면에는 접속 시점에
            // 현재 값을 한 번 밀어 준다(publishHealth).
            if (server_.isConnected())
                publishBase();
        });
    safetyStateSub_ = create_subscription<shalom_interfaces::msg::SafetyState>(
        "/safety/state", rclcpp::QoS(1).transient_local(),
        [this](const shalom_interfaces::msg::SafetyState::SharedPtr msg) {
            const std::string next = safetyStateName(msg->state);
            const bool estop_active = msg->software_estop_active || msg->physical_estop_active;
            safetyReceived_ = std::chrono::steady_clock::now();
            const bool changed = safetyState_ != next || estopEngaged_ != estop_active ||
                safetyMotionPermitted_ != msg->motion_permitted || safetyReasonCode_ != msg->reason_code ||
                safetyDetail_ != msg->detail;
            safetyState_ = next;
            estopEngaged_ = estop_active;
            safetyMotionPermitted_ = msg->motion_permitted;
            safetyReasonCode_ = msg->reason_code;
            safetyDetail_ = msg->detail;
            if (!navPreparing_ && (next != "normal" || !msg->motion_permitted) &&
                (navStatus_ == "navigating" || navStatus_ == "accepting"))
                stopNavigation(true);
            if (!changed)
                return;
            publishSafety();
        });
    missionStateSub_ = create_subscription<shalom_interfaces::msg::MissionState>(
        "/mission/state", rclcpp::QoS(1).transient_local(),
        std::bind(&BridgeNode::onMissionState, this, std::placeholders::_1));
    amclPoseSub_ = create_subscription<geometry_msgs::msg::PoseWithCovarianceStamped>(
        "/amcl_pose", rclcpp::QoS(10), [this](
            const geometry_msgs::msg::PoseWithCovarianceStamped::ConstSharedPtr &message) {
            if (!trailAwaitingLocalization_ || message->header.frame_id != mapFrame_ ||
                rclcpp::Time(message->header.stamp) < trailInitialPoseAt_)
                return;
            const double x = message->pose.pose.position.x;
            const double y = message->pose.pose.position.y;
            if (!std::isfinite(x) || !std::isfinite(y))
                return;
            trailExpectedX_ = x;
            trailExpectedY_ = y;
            trailNewPoseReceived_ = true;
        });

    linkTimer_ = create_wall_timer(10ms, [this] { pollLink(); });
    poseTimer_ = create_wall_timer(100ms, [this] { publishPose(); });     // 10 Hz
    safetyTimer_ = create_wall_timer(50ms, [this] {
        tickSafety();
        tickArmGoal();
        publishSafety();
    });                                                                    // 20 Hz
    healthTimer_ = create_wall_timer(1s, [this] { publishHealth(); });
    navTimer_ = create_wall_timer(200ms, [this] {
        publishNav();
        // Nav2 uses a volatile subscription; reapply after lifecycle restarts.
        publishSpeedLimit();
        syncNavigationSpeed();
    });                                                               // 5 Hz
    systemTimer_ = create_wall_timer(1s, [this] {
        pollNavigationReadiness();
        publishSystem();
        publishNavigationSpeed();
        publishCaptureSpool();
    });
    trailTimer_ = create_wall_timer(500ms, [this] { publishTrail(); });   // 2 Hz

    // 팔은 이름으로 지시한다. 받는 쪽(시뮬레이터든 실기 드라이버든)이 자기
    // 순서를 쓰므로, 색인으로 보내면 언젠가 다른 축이 움직인다.
    // HMI arm goals are merely one source. joint_mux arbitrates them before
    // safety_gate; this node must never publish straight to an FR3 driver.
    armCmdPub_ = create_publisher<sensor_msgs::msg::JointState>(
        "/motion/arm/joint_command/manual_hold", 10);
    initialPosePub_ = create_publisher<geometry_msgs::msg::PoseWithCovarianceStamped>(
        "/initialpose", rclcpp::QoS(1));

    // 본체의 수동 제자리. 조작자가 수동으로 넘긴 동안 20 Hz 로 0 을 내보내
    // twist_mux 의 manual_hold(90) 자리를 잡아 둔다. 이것이 없으면 수동으로
    // 바꿔도 mux 가 nav(20) 로 내려가 로봇이 목표를 향해 계속 간다.
    baseHoldPub_ = create_publisher<geometry_msgs::msg::Twist>(
        "/motion/manual_hold/cmd_vel", 10);

    // ---- 촬영 -------------------------------------------------------------
    captureEnabled_ = declare_parameter("capture.enabled", false);
    if (captureEnabled_) {
        spoolDir_ = declare_parameter("capture.spool_dir",
                                      std::string("/mnt/nas/inspection"));
        captureMountPoint_ = declare_parameter("capture.mount_point", std::string("/mnt/nas"));
        captureRequireMount_ = declare_parameter("capture.require_mount", true);
        if (captureRequireMount_) {
            const auto root = std::filesystem::path(captureMountPoint_).lexically_normal().string();
            const auto folder = std::filesystem::path(spoolDir_).lexically_normal().string();
            if (root.empty() || root == "/" || root.front() != '/' ||
                (folder != root && folder.rfind(root + "/", 0) != 0)) {
                throw std::invalid_argument(
                    "capture.spool_dir must be under capture.mount_point");
            }
        }
        maxCaptureLinear_ = declare_parameter("capture.max_linear_speed", maxCaptureLinear_);
        maxCaptureAngular_ = declare_parameter("capture.max_angular_speed", maxCaptureAngular_);
        captureMaxImageAge_ = declare_parameter("capture.max_image_age_s", captureMaxImageAge_);
        captureMaxSyncDifference_ = declare_parameter("capture.max_image_sync_difference_s", captureMaxSyncDifference_);
        if (!std::isfinite(captureMaxImageAge_) || captureMaxImageAge_ <= 0 ||
            !std::isfinite(captureMaxSyncDifference_) || captureMaxSyncDifference_ < 0)
            throw std::invalid_argument("capture image age and synchronization limits must be valid");
        const auto colorTopic = declare_parameter(
            "capture.color_topic", std::string("/fr3/camera_2d/image_raw"));
        const auto depthTopic = declare_parameter(
            "capture.depth_topic",
            std::string("/fr3/camera_3d/image_raw"));

        colorSub_ = create_subscription<sensor_msgs::msg::Image>(
            colorTopic, rclcpp::SensorDataQoS(),
            [this](const sensor_msgs::msg::Image::ConstSharedPtr &msg) {
                lastColor_ = msg;
                lastColorReceived_ = std::chrono::steady_clock::now();
            });
        depthSub_ = create_subscription<sensor_msgs::msg::Image>(
            depthTopic, rclcpp::SensorDataQoS(),
            [this](const sensor_msgs::msg::Image::ConstSharedPtr &msg) {
                lastDepth_ = msg;
                lastDepthReceived_ = std::chrono::steady_clock::now();
            });
    }


    // 액션 이름은 Nav2 기본값이다. 다른 이름을 쓰는 스택에 붙일 때는 런치에서
    // 리매핑한다 — 여기에 파라미터를 하나 더 만들면 프로토콜 문서와 어긋날
    // 자리가 하나 더 생긴다.
    navClient_ = rclcpp_action::create_client<NavigateToPose>(this, "navigate_to_pose");
    mapLoadClient_ = create_client<nav2_msgs::srv::LoadMap>("map_server/load_map");
    localizationManagerClient_ = create_client<nav2_msgs::srv::ManageLifecycleNodes>(
        "lifecycle_manager_localization/manage_nodes");
    slamLifecycleClient_ = create_client<lifecycle_msgs::srv::ChangeState>(
        "slam_toolbox/change_state");

    // 감시할 센서 목록은 파라미터에서 온다 (bridge.yaml 의 sensors).
    for (const auto &id : declare_parameter<std::vector<std::string>>(
             "sensors", std::vector<std::string>{})) {
        Sensor sensor;
        sensor.id = id;
        sensor.name = declare_parameter<std::string>("sensor." + id + ".name", id);
        sensor.topic = declare_parameter<std::string>("sensor." + id + ".topic", "");
        sensor.type = declare_parameter<std::string>("sensor." + id + ".type", "");
        sensor.expectedHz = declare_parameter<double>("sensor." + id + ".expected_hz", 0.0);
        sensor.lastSeen = now();
        if (sensor.topic.empty() || sensor.type.empty()) {
            RCLCPP_WARN(get_logger(), "센서 %s: topic 또는 type 이 없어 건너뛴다", id.c_str());
            continue;
        }
        sensors_.push_back(std::move(sensor));
    }
    // 구독은 목록이 다 자란 뒤에 건다. 벡터가 커지면서 재할당되면 콜백이
    // 붙잡은 참조가 무효가 된다.
    for (auto &sensor : sensors_) {
        // 내용은 필요 없고 도착 시각만 필요하다. 제네릭 구독을 쓰면 센서마다
        // 메시지 타입을 컴파일 시점에 알 필요가 없어, 카메라가 늘어도 이
        // 노드는 그대로다.
        sensor.sub = create_generic_subscription(
            sensor.topic, sensor.type, rclcpp::SensorDataQoS(),
            [this, &sensor](std::shared_ptr<const rclcpp::SerializedMessage>) {
                markSeen(sensor);
            });
    }
    RCLCPP_INFO(get_logger(), "센서 %zu 개 감시", sensors_.size());
}

BridgeNode::~BridgeNode()
{
    server_.stop();
}

// ================= 링크 =================

void BridgeNode::pollLink()
{
    const LinkEvents events = server_.drain();
    if (events.empty())
        return;

    if (events.clientConnected) {
        ++linkGeneration_;
        RCLCPP_INFO(get_logger(), "관제 연결됨");
    }
    if (events.protocolError) {
        // 프레이밍이 어긋났다는 것은 링크나 양쪽 구현 중 하나에 문제가
        // 있다는 뜻이다. 조용히 넘기면 안 된다.
        RCLCPP_ERROR(get_logger(), "프로토콜 오류로 연결을 끊었습니다: %s",
                     events.protocolErrorDetail.c_str());
        // 이미 끊긴 뒤라 이 이벤트는 다음 연결에서야 전달된다. 그래도 보내는
        // 이유는, 재연결한 관제가 직전에 무슨 일이 있었는지 알아야 하기 때문이다.
        sendEnvelope(makeEvent(kChLog, json{{"code", "LINK_FRAME_CORRUPT"},
                                            {"level", "error"},
                                            {"msg", events.protocolErrorDetail}}));
    }
    if (events.clientDisconnected) {
        ++linkGeneration_;
        RCLCPP_WARN(get_logger(), "관제 연결 끊김");
        linkHold_ = true;
        linkMissionResumePending_ = false;
        requestSafetyStop("SAFETY_COMM_TIMEOUT_STOP", "일반 관제 연결이 끊겼습니다");
        stopNavigation(true);
        pauseMissionForManualTakeover();
        if (missionStartPending_) {
            ++missionStartGeneration_;
            finishMissionStart(false, err::kMode, "관제 연결 종료로 미션 시작을 취소했습니다");
        }
    }

    for (const auto &frame : events.frames)
        handleFrame(frame);
}

void BridgeNode::handleFrame(const inspection::Frame &frame)
{
    std::string err;
    const auto env = Envelope::fromHeader(frame.header, &err);
    if (!env) {
        // 버전 불일치를 포함한다. 어긋난 채로 운용하는 것이 가장 위험하다.
        RCLCPP_ERROR(get_logger(), "봉투 해석 실패: %s", err.c_str());
        return;
    }

    // 나를 지목하지 않은 프레임은 실행하지 않는다.
    //
    // 관제가 여러 로봇을 다루게 되면 주소를 잘못 적어 옆 로봇에 붙는 일이
    // 생긴다. 그 상태에서도 화면은 정상으로 보이므로 조작자는 알아채지
    // 못하고, 비상정지를 누르면 아무도 보고 있지 않은 로봇이 선다.
    //
    // 빈 값은 "이 연결에 있는 로봇" 이라는 뜻으로 통과시킨다. 관제는 식별자를
    // 듣기 전까지 비워 보내므로, 첫 명령이 식별자가 없다는 이유로 거절되는
    // 일은 없다.
    if (!env->robot.empty() && env->robot != robotId_) {
        if (env->t == mtype::kReq) {
            // 응답에는 우리 식별자가 찍혀 나가므로, 관제는 자기가 실제로
            // 어느 로봇에 닿았는지 알게 된다.
            respond(*env, false, err::kRobotMismatch,
                    "이 로봇은 " + robotId_ + " 입니다. " + env->robot
                        + " 로 보낸 명령은 실행하지 않습니다");
        }
        RCLCPP_WARN_THROTTLE(
            get_logger(), *get_clock(), 5000,
            "%s 로 보낸 %s 를 무시했다. 이 로봇은 %s 다", env->robot.c_str(),
            env->ch.empty() ? env->t.c_str() : env->ch.c_str(), robotId_.c_str());
        return;
    }

    if (env->t == mtype::kHb)
        handleHeartbeat(*env);
    else if (env->t == mtype::kReq) {
        try {
            handleRequest(*env);
        } catch (const json::exception &e) {
            RCLCPP_WARN(get_logger(), "잘못된 요청 JSON (%s): %s", env->ch.c_str(), e.what());
            respond(*env, false, err::kBadPayload, "요청 필드의 타입 또는 값이 올바르지 않습니다");
        } catch (const std::filesystem::filesystem_error &e) {
            RCLCPP_ERROR(get_logger(), "요청 파일 처리 실패 (%s): %s", env->ch.c_str(), e.what());
            respond(*env, false, err::kHardware, "로봇 파일을 처리하지 못했습니다");
        }
    }
    else if (env->t == mtype::kPub && env->ch == "cmd/cmd_vel")
        RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                             "TCP cmd/cmd_vel은 비활성입니다. 수동 조작은 UDP teleop_bridge를 사용합니다");
    else if (env->t == mtype::kSub)
        RCLCPP_INFO(get_logger(), "구독 요청 수신");
}

void BridgeNode::handleHeartbeat(const Envelope &heartbeat)
{
    // 같은 seq 를 돌려보내 관제가 왕복 지연을 잴 수 있게 한다.
    sendEnvelope(makeHeartbeat(heartbeat.p.value("seq", std::int64_t{0})));
}

void BridgeNode::sendEnvelope(const Envelope &env, bool lossy, const std::string &payload)
{
    // 나가는 모든 프레임에 자기 식별자를 찍는다. 한 곳에서 하는 이유는,
    // 채널을 새로 추가하는 사람이 이것을 기억해야 한다면 언젠가 빠지기
    // 때문이다 — 빠진 프레임은 관제 쪽에서 "아직 모르는 로봇" 으로 보여
    // 증상이 없다.
    Envelope stamped = env;
    stamped.robot = robotId_;
    server_.send(inspection::encodeFrame(stamped.toHeader(), payload), lossy);
}

void BridgeNode::respond(const Envelope &request, bool ok, const std::string &code,
                         const std::string &message)
{
    sendEnvelope(makeResponse(request, ok, code, message));
}

// ================= 명령 =================

bool BridgeNode::commandsAllowed(const Envelope &request)
{
    if (estopActive()) {
        respond(request, false, err::kEstopEngaged, "E-Stop 발동 상태입니다");
        return false;
    }
    return true;
}

void BridgeNode::handleRequest(const Envelope &request)
{
    if (mapTransitionUncertain_ && (request.ch == kCmdInitialPose || request.ch == kCmdCapture ||
        request.ch == kCmdMissionResume || request.ch == kCmdMissionReturnDock)) {
        respond(request, false, err::kHardware, "지도 전환 결과가 불명확합니다. 내비게이션과 브리지를 재시작하십시오");
        return;
    }
    if (missionStartPending_ && (request.ch == kCmdMapsSelect || request.ch == kCmdMapsRename ||
        request.ch == kCmdMapsDelete || request.ch == kCmdMissionsSave || request.ch == kCmdMissionsArchive ||
        request.ch == kCmdWaypointsSet || request.ch == kCmdLocationsSet || request.ch == kCmdMarkersSet)) {
        respond(request, false, err::kBusy, "미션 시작 요청이 끝난 뒤 지도 데이터를 변경하십시오");
        return;
    }
    // E-Stop은 일반 API 연결이 아니라 estop_bridge의 전용 TCP 포트만 쓴다.
    // 여기서 받아 버리면 일반 링크가 살아 있는 동안 독립 감시 채널이 죽어도
    // 정지하지 않는, 분리의 목적과 반대되는 구성이 된다.
    if (request.ch == kCmdEstop || request.ch == kCmdEstopRelease) {
        respond(request, false, err::kUnreachable,
                "E-Stop은 estop_bridge 전용 TCP 포트로 보내야 합니다");
        return;
    }
    // Stop requests remain available while the safety controller holds motion.
    if (request.ch == kCmdNavPause || request.ch == kCmdNavCancel) {
        if (request.ch == kCmdNavPause && !navigationBusy()) {
            respond(request, false, err::kMode, "일시정지할 목표 주행이 없습니다");
            return;
        }
        if (request.ch == kCmdNavPause && navStatus_ == "canceling") {
            respond(request, false, err::kBusy, "목표 주행을 취소 중입니다");
            return;
        }
        stopNavigation(request.ch == kCmdNavPause);
        respond(request, true);
        publishNav();
        return;
    }
    if (!commandsAllowed(request))
        return;

    if (request.ch == kCmdNavigationSpeedLimit || request.ch == kCmdNavigationSpeedSettings) {
        json settings;
        std::string error;
        if (!readNavigationSpeed(request.p, request.ch == kCmdNavigationSpeedSettings, &settings, &error)) {
            respond(request, false, err::kBadPayload, error);
            return;
        }
        if (!saveNavigationSpeed(settings, &error)) {
            respond(request, false, err::kUnreachable, error);
            return;
        }
        const bool valuesChanged = navigationSpeed_ != settings.at("speed_limit_mps").get<double>() ||
                                   navigationAngularSpeed_ != settings.at("angular_speed_limit_rps").get<double>();
        navigationSpeed_ = settings.at("speed_limit_mps").get<double>();
        navigationAngularSpeed_ = settings.at("angular_speed_limit_rps").get<double>();
        navigationMinSpeed_ = settings.at("min_speed_mps").get<double>();
        navigationMaxSpeed_ = settings.at("max_speed_mps").get<double>();
        navigationMinAngularSpeed_ = settings.at("min_angular_speed_rps").get<double>();
        navigationMaxAngularSpeed_ = settings.at("max_angular_speed_rps").get<double>();
        if (!valuesChanged) {
            respond(request, true);
            publishNavigationSpeed();
            return;
        }
        navigationSpeedApplied_ = false;
        // Cancel obsolete callbacks; an old response must not confirm the new limits.
        ++navigationSpeedGeneration_;
        if (navigationSpeedStateId_)
            navigationSpeedStateClient_->remove_pending_request(*navigationSpeedStateId_);
        if (navigationSpeedGetId_)
            navigationSpeedGetClient_->remove_pending_request(*navigationSpeedGetId_);
        if (navigationSpeedSetId_)
            navigationSpeedSetClient_->remove_pending_request(*navigationSpeedSetId_);
        navigationSpeedGetId_.reset();
        navigationSpeedStateId_.reset();
        navigationSpeedSetId_.reset();
        navigationSpeedSyncPending_ = false;
        navigationSpeedSyncAt_ = {};
        publishSpeedLimit();
        syncNavigationSpeed();
        respond(request, true);
        publishNavigationSpeed();
        return;
    }

    if (request.ch == kCmdMode) {
        manualMode_ = request.p.value("mode", std::string("auto")) == "manual";
        if (manualMode_) {
            pauseMissionForManualTakeover();
            // Entering manual mode is an explicit operator motion request.
            // Heartbeat and E-Stop guards remain enforced by safety_manager;
            // without this request, startup/link-recovery stays in controlled
            // stop forever and teleop velocity can never reach the base.
            requestSafetyResume();
            requestBaseAuthority();
            linkHold_ = false;
        }
        RCLCPP_INFO(get_logger(), "주행 모드: %s", manualMode_ ? "수동" : "자율");
        respond(request, true);
        publishSafety();
        return;
    }

    if (request.ch == kCmdBasePosture) {
        handleBasePosture(request);
        return;
    }

    if (request.ch == kCmdArmStop) {
        if (pendingArmGoal_)
            finishArmGoal(false, err::kBusy, "로봇팔 정지 요청으로 목표를 취소했습니다");
        if (!armExecutionEnabled_) {
            respond(request, false, err::kUnreachable,
                    "로봇팔 실행기가 연결되지 않았습니다");
            return;
        }
        // 지금 있는 자리를 그대로 목표로 준다. 명령을 끊는 것만으로는 팔이
        // 마지막 목표를 향해 계속 간다.
        sensor_msgs::msg::JointState hold;
        hold.header.stamp = now();
        hold.name = kArmJointNames;
        hold.position = lastArmPositions_;
        if (hold.position.size() != kArmJointNames.size() ||
            std::chrono::steady_clock::now() - lastArmReceived_ > std::chrono::seconds(1)) {
            respond(request, false, err::kUnreachable, "로봇팔 관절 피드백이 끊겼습니다");
            return;
        }
        armCmdPub_->publish(hold);
        respond(request, true);
        return;
    }

    if (request.ch == kCmdArmPreset) {
        const auto name = request.p.value("name", std::string{});
        const auto *preset = armPreset(name);
        if (preset == nullptr) {
            respond(request, false, err::kBadPayload, "알 수 없는 프리셋: " + name);
            return;
        }
        applyArmGoal(request, *preset);
        return;
    }

    if (request.ch == kCmdArmJointGoal) {
        if (!request.p.contains("positions") || !validArmPresetPositions(request.p["positions"])) {
            respond(request, false, err::kBadPayload, "관절 목표가 올바르지 않습니다");
            return;
        }
        std::vector<double> q;
        for (const auto &v : request.p.value("positions", json::array())) {
            if (!v.is_number()) {
                respond(request, false, err::kBadPayload, "관절 값이 숫자가 아닙니다");
                return;
            }
            q.push_back(v.get<double>());
        }
        applyArmGoal(request, q);
        return;
    }

    if (request.ch == kCmdArmEeGoal) {
        // 끝단 목표는 역기구학이 필요하고, 그 판단은 MoveIt2 의 몫이다
        // (프로토콜 §4). 아직 붙지 않았다는 사실을 조용히 넘기지 않는다 —
        // 조작자가 보내고 아무 일도 일어나지 않으면 원인을 짚을 수 없다.
        respond(request, false, err::kUnreachable,
                "끝단 목표는 아직 연결되지 않았습니다 (MoveIt2 미연동)");
        return;
    }

    if (request.ch == kCmdWaypointsSet) {
        if (!pendingMapId_.empty()) {
            respond(request, false, err::kBusy, "지도 전환이 끝난 뒤 웨이포인트를 변경하십시오");
            return;
        }
        if (haveMissionState_ && missionState_.state != shalom_interfaces::msg::MissionState::IDLE &&
            missionState_.state != shalom_interfaces::msg::MissionState::COMPLETED &&
            missionState_.state != shalom_interfaces::msg::MissionState::FAILED) {
            respond(request, false, err::kBusy, "미션이 끝난 뒤 점검 지점을 변경하십시오");
            return;
        }
        if (mapId_ == "live") {
            respond(request, false, err::kMode, "저장된 지도를 선택한 뒤 웨이포인트를 등록하십시오");
            return;
        }
        if (!request.p.contains("map_id") || !request.p["map_id"].is_string() ||
            request.p["map_id"].get<std::string>() != mapId_ ||
            !request.p.contains("expected_points") || request.p["expected_points"] != waypoints_) {
            respond(request, false, err::kBusy, "지도 또는 웨이포인트 목록이 변경되었습니다. 최신 목록에서 다시 편집하십시오");
            publishWaypoints();
            return;
        }
        json points = request.p.value("points", json::array());
        if (!points.is_array()) {
            respond(request, false, err::kBadPayload, "웨이포인트 목록이 올바르지 않습니다");
            return;
        }
        std::unordered_set<std::string> ids;
        for (auto &point : points) {
            if (!point.is_object() || !point.contains("id") || !point["id"].is_string()
                || !point.contains("x") || !point["x"].is_number()
                || !point.contains("y") || !point["y"].is_number()) {
                respond(request, false, err::kBadPayload, "웨이포인트 ID와 좌표를 확인하십시오");
                return;
            }
            const std::string id = point["id"].get<std::string>();
            if (id.empty() || !ids.insert(id).second) {
                respond(request, false, err::kBadPayload, "웨이포인트 ID가 비었거나 중복되었습니다");
                return;
            }
            // 기존 지도에는 이름/방향이 없는 지점도 있다. 목록을 다시
            // 저장할 때 명시적인 기본값으로 보완하되 기존 ID는 유지한다.
            if (!point.contains("name")) point["name"] = id;
            if (!point.contains("theta")) point["theta"] = 0.0;
            if (!point["name"].is_string() || !point["theta"].is_number()) {
                respond(request, false, err::kBadPayload, "웨이포인트 이름과 방향을 확인하십시오");
                return;
            }
            const auto name = point["name"].get<std::string>();
            if (name.empty() || name.size() > 120 || name.find('\n') != std::string::npos
                || name.find('\r') != std::string::npos
                || !std::isfinite(point["x"].get<double>())
                || !std::isfinite(point["y"].get<double>())
                || !std::isfinite(point["theta"].get<double>())) {
                respond(request, false, err::kBadPayload, "웨이포인트 이름·좌표·방향이 올바르지 않습니다");
                return;
            }
            point.erase("status");
            point.erase("captured_from");
            point.erase("localization_ok");
            point.erase("tag_id");
            point.erase("kind");
        }
        std::unordered_set<std::string> removedIds;
        for (const auto &point : waypoints_)
            if (point.is_object() && point.contains("id") && point["id"].is_string()) {
                const auto id = point["id"].get<std::string>();
                if (!ids.count(id)) removedIds.insert(id);
            }
        for (const auto &mission : missions_) {
            if (!mission.is_object() || mission.value("archived", false))
                continue;
            for (const auto &step : mission.value("steps", json::array())) {
                if (step.is_object() && step.value("type", std::string{}) == "navigate" &&
                    removedIds.count(step.value("location_id", std::string{}))) {
                    respond(request, false, err::kBusy,
                            "미션에서 사용하는 웨이포인트는 삭제할 수 없습니다: " + mission.value("name", std::string{}));
                    return;
                }
            }
        }
        if (!saveMapState("waypoints.json", points)) {
            respond(request, false, err::kHardware, "웨이포인트 파일을 저장하지 못했습니다");
            return;
        }
        waypoints_ = points;
        ++missionPlanRevision_;
        respond(request, true);
        publishWaypoints();
        publishMapCatalog();
        RCLCPP_INFO(get_logger(), "점검 지점 %zu 개 수신", waypoints_.size());
        return;
    }

    if (request.ch == kCmdMissionsList) {
        publishMissions();
        respond(request, true);
        return;
    }

    if (request.ch == kCmdArmPosePresetsList) {
        publishArmPosePresets();
        respond(request, true);
        return;
    }

    if (request.ch == kCmdArmPosePresetsSave) {
        const json preset = request.p.value("preset", json::object());
        if (!preset.is_object()) {
            respond(request, false, err::kBadPayload, "팔 자세 프리셋 형식이 올바르지 않습니다");
            return;
        }
        const std::string id = preset.value("id", std::string{});
        const std::string name = preset.value("name", std::string{});
        const std::string description = preset.value("description", std::string{});
        const auto positions = preset.value("positions", json::array());
        if (!validArmPresetId(id) || !validArmPresetText(name, description) ||
            !validArmPresetPositions(positions)) {
            respond(request, false, err::kBadPayload, "프리셋 이름 또는 관절값이 올바르지 않습니다");
            return;
        }
        if (std::any_of(armPosePresets_.begin(), armPosePresets_.end(), [&name, &id](const json &item) {
                return item.value("id", std::string{}) == id ||
                       (!item.value("archived", false) &&
                        item.value("name", std::string{}) == name);
            })) {
            respond(request, false, err::kBadPayload, "같은 이름 또는 ID의 프리셋이 이미 있습니다");
            return;
        }
        const json previous = armPosePresets_;
        armPosePresets_.push_back(json{{"id", id}, {"name", name},
                                       {"description", description}, {"positions", positions},
                                       {"revision", 1}, {"archived", false}});
        std::string error;
        if (!saveArmPosePresets(&error)) {
            armPosePresets_ = previous;
            respond(request, false, err::kHardware, error);
            return;
        }
        publishArmPosePresets();
        respond(request, true);
        return;
    }

    if (request.ch == kCmdArmPosePresetsUpdate) {
        const json preset = request.p.value("preset", json::object());
        if (!preset.is_object()) {
            respond(request, false, err::kBadPayload, "팔 자세 프리셋 형식이 올바르지 않습니다");
            return;
        }
        const std::string id = preset.value("id", std::string{});
        const std::string name = preset.value("name", std::string{});
        const std::string description = preset.value("description", std::string{});
        const json positions = preset.value("positions", json::array());
        const uint64_t expected = request.p.value("expected_revision", uint64_t{0});
        if (!validArmPresetId(id) || !validArmPresetText(name, description) ||
            !validArmPresetPositions(positions)) {
            respond(request, false, err::kBadPayload, "프리셋 이름·설명·관절값이 올바르지 않습니다");
            return;
        }
        auto found = std::find_if(armPosePresets_.begin(), armPosePresets_.end(), [&id](const json &item) {
            return item.value("id", std::string{}) == id;
        });
        if (found == armPosePresets_.end() || found->value("archived", false) ||
            found->value("revision", uint64_t{1}) != expected) {
            respond(request, false, err::kBusy, "팔 자세가 다른 화면에서 변경됐습니다. 다시 불러오십시오");
            publishArmPosePresets();
            return;
        }
        if (std::any_of(armPosePresets_.begin(), armPosePresets_.end(), [&name, &id](const json &item) {
                return item.value("id", std::string{}) != id &&
                       !item.value("archived", false) &&
                       item.value("name", std::string{}) == name;
            })) {
            respond(request, false, err::kBadPayload, "같은 이름의 프리셋이 이미 있습니다");
            return;
        }
        const json previous = armPosePresets_;
        (*found)["name"] = name;
        (*found)["description"] = description;
        (*found)["positions"] = positions;
        (*found)["revision"] = expected + 1;
        std::string error;
        if (!saveArmPosePresets(&error)) {
            armPosePresets_ = previous;
            respond(request, false, err::kHardware, error);
            return;
        }
        publishArmPosePresets();
        respond(request, true);
        return;
    }

    if (request.ch == kCmdArmPosePresetsArchive) {
        const std::string id = request.p.value("id", std::string{});
        const uint64_t expected = request.p.value("expected_revision", uint64_t{0});
        if (!validArmPresetId(id) || expected == 0) {
            respond(request, false, err::kBadPayload, "팔 자세 ID 또는 revision이 올바르지 않습니다");
            return;
        }
        auto found = std::find_if(armPosePresets_.begin(), armPosePresets_.end(),
            [&id](const json &item) { return item.value("id", std::string{}) == id; });
        if (found == armPosePresets_.end() || found->value("archived", false) ||
            found->value("revision", uint64_t{1}) != expected) {
            respond(request, false, err::kBusy, "팔 자세가 없거나 다른 화면에서 변경됐습니다");
            publishArmPosePresets();
            return;
        }
        std::string error;
        const auto referencedMap = mapUsingArmPose(mapsDir_, id, &error);
        if (!error.empty()) {
            respond(request, false, err::kHardware, error);
            return;
        }
        if (referencedMap) {
            respond(request, false, err::kBusy,
                    "지도 '" + *referencedMap + "'의 미션이 이 자세를 사용 중입니다");
            return;
        }
        const json previous = armPosePresets_;
        (*found)["archived"] = true;
        (*found)["revision"] = expected + 1;
        if (!saveArmPosePresets(&error)) {
            armPosePresets_ = previous;
            respond(request, false, err::kHardware, error);
            return;
        }
        publishArmPosePresets();
        respond(request, true);
        return;
    }

    if (request.ch == kCmdMissionsSave) {
        if (!pendingMapId_.empty()) {
            respond(request, false, err::kBusy, "지도 전환이 끝난 뒤 미션을 변경하십시오");
            return;
        }
        if (mapId_ == "live") {
            respond(request, false, err::kMode, "저장된 지도를 선택한 뒤 미션을 저장하십시오");
            return;
        }
        if (navigationBusy() || (haveMissionState_ &&
            missionState_.state != shalom_interfaces::msg::MissionState::IDLE &&
            missionState_.state != shalom_interfaces::msg::MissionState::COMPLETED &&
            missionState_.state != shalom_interfaces::msg::MissionState::FAILED)) {
            respond(request, false, err::kBusy, "주행 또는 미션 실행 중에는 미션을 편집할 수 없습니다");
            return;
        }
        const json mission = request.p.value("mission", json::object());
        const std::string id = mission.value("id", std::string{});
        const std::string name = mission.value("name", std::string{});
        const uint64_t expected = request.p.value("expected_revision", uint64_t{0});
        const auto valid_id = [](const std::string &value) {
            return !value.empty() && value.size() <= 96 &&
                std::all_of(value.begin(), value.end(), [](unsigned char c) {
                    return std::isalnum(c) || c == '-' || c == '_';
                });
        };
        if (!mission.is_object() || !valid_id(id) || name.empty() || name.size() > 120 ||
            name.find('\n') != std::string::npos || name.find('\r') != std::string::npos ||
            !mission.value("steps", json::array()).is_array()) {
            respond(request, false, err::kBadPayload, "미션 ID, 이름 또는 단계 목록이 올바르지 않습니다");
            return;
        }
        const auto &steps = mission.at("steps");
        std::unordered_set<std::string> stepIds;
        for (const auto &step : steps) {
            if (!step.is_object()) {
                respond(request, false, err::kBadPayload, "각 미션 단계는 객체여야 합니다");
                return;
            }
            const std::string stepId = step.value("id", std::string{});
            const std::string type = step.value("type", std::string{});
            if (!valid_id(stepId) || !stepIds.insert(stepId).second ||
                (type != "navigate" && type != "capture" &&
                 type != "arm_move" && type != "dock")) {
                respond(request, false, err::kBadPayload, "단계 ID가 중복되었거나 지원하지 않는 단계 유형입니다");
                return;
            }
            const char *reference = type == "navigate" ? "location_id"
                                    : type == "capture" ? "preset"
                                    : type == "arm_move" ? "pose" : nullptr;
            if (reference && (!step.contains(reference) || !step[reference].is_string() ||
                              !valid_id(step[reference].get<std::string>()))) {
                respond(request, false, err::kBadPayload, "단계에 유효한 위치·촬영 프리셋·팔 자세 참조가 필요합니다");
                return;
            }
            if (type == "navigate") {
                const auto locationId = step["location_id"].get<std::string>();
                if (std::none_of(waypoints_.begin(), waypoints_.end(), [&locationId](const json &point) {
                        return point.is_object() && point.value("id", std::string{}) == locationId;
                    })) {
                    respond(request, false, err::kBadPayload,
                            "주행 단계 '" + stepId + "'의 웨이포인트가 없습니다: " + locationId);
                    return;
                }
            }
            if (type == "arm_move") {
                const auto poseId = step["pose"].get<std::string>();
                if (std::none_of(armPosePresets_.begin(), armPosePresets_.end(), [&poseId](const json &preset) {
                        return preset.is_object() && preset.value("id", std::string{}) == poseId &&
                            !preset.value("archived", false);
                    })) {
                    respond(request, false, err::kBadPayload,
                        "로봇팔 단계 '" + stepId + "'의 자세가 없거나 보관되었습니다: " + poseId);
                    return;
                }
            }
        }
        if (mission.contains("map_id") && mission.value("map_id", std::string{}) != mapId_) {
            respond(request, false, err::kMode, "현재 선택된 지도와 미션의 지도가 다릅니다");
            return;
        }
        const json previous = missions_;
        auto found = std::find_if(missions_.begin(), missions_.end(), [&id](const json &item) {
            return item.value("id", std::string{}) == id;
        });
        if (found == missions_.end()) {
            if (expected != 0) {
                respond(request, false, err::kBusy, "미션이 이미 변경되었습니다. 목록을 새로 고치십시오");
                return;
            }
            json saved = mission;
            saved.erase("map_id");
            saved["revision"] = 1;
            saved["archived"] = false;
            missions_.push_back(std::move(saved));
        } else {
            const uint64_t current = found->value("revision", uint64_t{1});
            if (current != expected) {
                respond(request, false, err::kBusy, "미션 revision이 바뀌었습니다. 최신본을 다시 불러오십시오");
                return;
            }
            json saved = mission;
            saved.erase("map_id");
            saved["revision"] = current + 1;
            saved["archived"] = false;
            *found = std::move(saved);
        }
        std::string error;
        if (!saveMissions(&error)) {
            missions_ = previous;
            respond(request, false, err::kHardware, error);
            return;
        }
        publishMissions();
        respond(request, true);
        return;
    }

    if (request.ch == kCmdMissionsArchive) {
        if (mapId_ == "live" || !pendingMapId_.empty() || navigationBusy() || (haveMissionState_ &&
            missionState_.state != shalom_interfaces::msg::MissionState::IDLE &&
            missionState_.state != shalom_interfaces::msg::MissionState::COMPLETED &&
            missionState_.state != shalom_interfaces::msg::MissionState::FAILED)) {
            respond(request, false, err::kBusy, "저장된 지도에서 주행과 미션이 끝난 뒤 보관할 수 있습니다");
            return;
        }
        const std::string id = request.p.value("id", std::string{});
        const uint64_t expected = request.p.value("expected_revision", uint64_t{0});
        auto found = std::find_if(missions_.begin(), missions_.end(), [&id](const json &item) {
            return item.value("id", std::string{}) == id;
        });
        if (found == missions_.end() || found->value("revision", uint64_t{1}) != expected) {
            respond(request, false, err::kBusy, "미션이 없거나 revision이 바뀌었습니다");
            return;
        }
        const json previous = missions_;
        (*found)["archived"] = true;
        (*found)["revision"] = expected + 1;
        std::string error;
        if (!saveMissions(&error)) {
            missions_ = previous;
            respond(request, false, err::kHardware, error);
            return;
        }
        publishMissions();
        respond(request, true);
        return;
    }

    if (request.ch == kCmdLocationsSet) {
        if (haveMissionState_ && missionState_.state != shalom_interfaces::msg::MissionState::IDLE &&
            missionState_.state != shalom_interfaces::msg::MissionState::COMPLETED &&
            missionState_.state != shalom_interfaces::msg::MissionState::FAILED) {
            respond(request, false, err::kBusy, "미션이 끝난 뒤 위치를 변경하십시오");
            return;
        }
        auto locations = request.p.value("locations", json{});
        if (!locations.is_array()) {
            respond(request, false, err::kBadPayload, "위치 목록이 올바르지 않습니다");
            return;
        }
        std::unordered_set<std::string> kinds;
        for (auto &location : locations) {
            if (!location.is_object() || !location.contains("kind") || !location["kind"].is_string()) {
                respond(request, false, err::kBadPayload, "위치 종류가 올바르지 않습니다");
                return;
            }
            const auto kind = location["kind"].get<std::string>();
            if ((kind != "home" && kind != "dock") || !kinds.insert(kind).second) {
                respond(request, false, err::kBadPayload, "위치 종류가 중복되었거나 올바르지 않습니다");
                return;
            }
            for (const auto *field : {"x", "y", "theta"})
                if (!location.contains(field) || !location[field].is_number() || !std::isfinite(location[field].get<double>())) {
                    respond(request, false, err::kBadPayload, "위치 좌표가 올바르지 않습니다");
                    return;
                }
            if (location.is_object()) {
                location.erase("captured_from");
                location.erase("localization_ok");
                location.erase("tag_id");
            }
        }
        if (!saveMapState("locations.json", locations)) {
            respond(request, false, err::kHardware, "시작·충전 위치 파일을 저장하지 못했습니다");
            return;
        }
        locations_ = locations;
        ++missionPlanRevision_;
        respond(request, true);
        publishLocations();
        return;
    }

    if (request.ch == kCmdMarkersSet) {
        // 측량해 넣은 마커 자리다. 관제에서 통째로 갈아 끼우고, 로봇은 그것을
        // 그대로 들고 있다가 되돌려 준다 — 어느 태그가 어디 붙어 있는지는
        // 사람이 재어 오는 값이라 로봇이 스스로 정할 수 있는 것이 아니다.
        if (mapId_ == "live") {
            respond(request, false, err::kMode, "저장된 지도를 선택한 뒤 마커를 등록하십시오");
            return;
        }
        const json markers = request.p.value("markers", json{});
        if (!markers.is_array()) {
            respond(request, false, err::kBadPayload, "마커 목록이 올바르지 않습니다");
            return;
        }
        std::unordered_set<int> ids;
        for (const auto &marker : markers) {
            if (!marker.is_object() || !marker.contains("id") ||
                !marker["id"].is_number_integer() || !marker.contains("x") ||
                !marker["x"].is_number() || !marker.contains("y") ||
                !marker["y"].is_number()) {
                respond(request, false, err::kBadPayload, "마커 ID와 지도 좌표를 확인하십시오");
                return;
            }
            const auto id = marker["id"].get<long long>();
            if (id < 0 || id > 100000 || !ids.insert(static_cast<int>(id)).second ||
                !std::isfinite(marker["x"].get<double>()) ||
                !std::isfinite(marker["y"].get<double>())) {
                respond(request, false, err::kBadPayload, "마커 ID가 중복되었거나 좌표가 올바르지 않습니다");
                return;
            }
            // 구형 X/Y 전용 기록은 읽고 보존한다. 높이와 방향이 둘 다
            // 채워진 마커만 향후 3D 위치 보정에 사용할 수 있다.
            const bool hasZ = marker.contains("z");
            const bool hasYaw = marker.contains("yaw");
            if (hasZ != hasYaw || (hasZ &&
                (!marker["z"].is_number() || !marker["yaw"].is_number() ||
                 !std::isfinite(marker["z"].get<double>()) ||
                 !std::isfinite(marker["yaw"].get<double>()) ||
                 std::abs(marker["yaw"].get<double>()) > M_PI + 1e-6))) {
                respond(request, false, err::kBadPayload, "마커 높이와 앞면 방향을 확인하십시오");
                return;
            }
        }
        if (!saveMapState("markers.json", markers)) {
            respond(request, false, err::kHardware, "마커 파일을 저장하지 못했습니다");
            return;
        }
        markers_ = markers;
        respond(request, true);
        publishMarkers();
        RCLCPP_INFO(get_logger(), "마커 %zu개 등록", markers_.size());
        return;
    }

    if (request.ch == kCmdMapsList) {
        respond(request, true);
        publishMapCatalog();
        publishActiveMap();
        // HMI가 빠르게 다른 로봇으로 전환하면 publishHealth의 1초 폴링이
        // 연결 종료를 보지 못할 수 있다. 목록 요청은 연결 직후 항상 오므로
        // 현재 지도에 속한 자료도 함께 다시 발행한다.
        publishWaypoints();
        publishMissions();
        publishLocations();
        publishMarkers();
        if (!lastMapPng_.empty())
            sendEnvelope(makePublish(kChMap, lastMapMeta_), false, lastMapPng_);
        return;
    }

    if (request.ch == kCmdTrailSnapshot) {
        json points = json::array();
        for (const auto &pt : trailHistory_)
            points.push_back({pt.first, pt.second});
        sendEnvelope(makePublish(kChTrail, json{{"points", points}, {"reset", true}}));
        trailPending_.clear();
        trailReset_ = false;
        trailWasConnected_ = true;
        respond(request, true);
        return;
    }

    if (request.ch == kCmdMapsSelect) {
        if (mapTransitionUncertain_) {
            respond(request, false, err::kHardware, "지도 전환 결과가 불명확합니다. 내비게이션과 브리지를 재시작하십시오");
            return;
        }
        const std::string id = request.p.value("id", std::string{});
        if (!haveMissionState_ || (missionState_.state != shalom_interfaces::msg::MissionState::IDLE &&
            missionState_.state != shalom_interfaces::msg::MissionState::COMPLETED &&
            missionState_.state != shalom_interfaces::msg::MissionState::FAILED) ||
            navigationBusy() || !pendingMapId_.empty()) {
            respond(request, false, err::kBusy,
                    "주행 또는 점검이 끝난 뒤에 지도를 전환하십시오");
            return;
        }
        if (id.empty() || id == "." || id.find('/') != std::string::npos || id.find("..") != std::string::npos
            || !std::filesystem::is_regular_file(std::filesystem::path(mapsDir_) / id / "map.yaml")) {
            respond(request, false, err::kBadPayload, "유효하지 않거나 없는 지도입니다");
            return;
        }
        std::string detail;
        if (!loadMapBundle(id, &detail, true)) {
            respond(request, false, err::kBadPayload, detail);
            return;
        }
        pendingMapId_ = id;
        pendingMapRequest_ = request;
        pendingMapAt_ = std::chrono::steady_clock::now();
        ++mapTransitionGeneration_;
        if (mapId_ == "live")
            switchFromSlam(request, id);
        else
            loadSelectedMap(request, id, false);
        return;
    }

    if (request.ch == kCmdMapsRename) {
        const std::string id = request.p.value("id", std::string{});
        const std::string name = request.p.value("name", std::string{});
        if (!haveMissionState_ || (missionState_.state != shalom_interfaces::msg::MissionState::IDLE &&
            missionState_.state != shalom_interfaces::msg::MissionState::COMPLETED &&
            missionState_.state != shalom_interfaces::msg::MissionState::FAILED) ||
            navigationBusy() || !pendingMapId_.empty()) {
            respond(request, false, err::kBusy,
                    "주행 또는 점검이 끝난 뒤에 지도 이름을 바꾸십시오");
            return;
        }
        if (id.empty() || id == "." || id.find('/') != std::string::npos || id.find("..") != std::string::npos
            || !std::filesystem::is_regular_file(std::filesystem::path(mapsDir_) / id / "map.yaml")) {
            respond(request, false, err::kBadPayload, "유효하지 않거나 없는 지도입니다");
            return;
        }
        // 지도 이름은 실제 디렉터리 이름이다. 경로 구분자와 제어 문자는 허용하지 않는다.
        if (name.empty() || name.size() > 120 || name == "." || name.find("..") != std::string::npos ||
            name == "live" || name == ".trash" || name.front() == ' ' || name.back() == ' ' ||
            std::any_of(name.begin(), name.end(), [](unsigned char c) {
                return c == '/' || c == '\\' || c < 0x20 || c == 0x7f;
            })) {
            respond(request, false, err::kBadPayload, "지도 이름에 사용할 수 없는 문자가 있습니다");
            return;
        }
        if (name == id) {
            respond(request, true);
            return;
        }
        const std::filesystem::path root(mapsDir_);
        const std::filesystem::path dir = root / id;
        const std::filesystem::path destination = root / name;
        std::error_code ec;
        if (std::filesystem::exists(destination, ec) ||
            std::filesystem::is_symlink(destination) || ec) {
            respond(request, false, err::kBadPayload, "같은 이름의 지도가 이미 있습니다");
            return;
        }
        // 먼저 모든 JSON의 새 내용을 준비한다. 한 파일이라도 읽거나 쓸 수 없으면
        // 폴더를 옮기지 않는다. 지도별 파일의 구형 중복 필드도 정리한다.
        const std::array<const char *, 5> files{
            "metadata.json", "waypoints.json", "locations.json", "markers.json", "missions.json"};
        std::vector<std::filesystem::path> staged;
        const auto defaultMap = root / "default_map.json";
        std::filesystem::path stagedDefaultMap;
        const auto discardStaged = [&staged, &stagedDefaultMap]() {
            for (const auto &path : staged) {
                std::error_code ignored;
                std::filesystem::remove(path, ignored);
            }
            if (!stagedDefaultMap.empty()) {
                std::error_code ignored;
                std::filesystem::remove(stagedDefaultMap, ignored);
            }
        };
        try {
            for (const char *filename : files) {
                const auto source = dir / filename;
                if (std::string(filename) != "metadata.json" &&
                    !std::filesystem::exists(source))
                    continue;
                json document = json::object();
                if (std::filesystem::exists(source)) {
                    std::ifstream in(source);
                    if (!in || !(in >> document) || !document.is_object())
                        throw std::runtime_error(std::string(filename) + " 읽기 실패");
                }
                document.erase("schema_version");
                if (std::string(filename) == "metadata.json") {
                    document.erase("id");
                    document["name"] = name;
                    document.erase("map_yaml");
                } else {
                    document.erase("map_id");
                    if (std::string(filename) == "missions.json" &&
                        document.value("missions", json::array()).is_array()) {
                        for (auto &mission : document["missions"])
                            if (mission.is_object()) mission.erase("map_id");
                    }
                    if (std::string(filename) == "waypoints.json" &&
                        document.value("points", json::array()).is_array()) {
                        for (auto &point : document["points"])
                            if (point.is_object()) point.erase("kind");
                    }
                }
                const auto temporary = dir / (std::string(".") + filename + ".rename.tmp");
                staged.push_back(temporary);
                std::ofstream out(temporary, std::ios::trunc);
                if (!out || !(out << document.dump(2) << '\n'))
                    throw std::runtime_error(std::string(filename) + " 쓰기 실패");
                out.close();
                if (!out)
                    throw std::runtime_error(std::string(filename) + " 저장 실패");
            }
            if (std::filesystem::is_regular_file(defaultMap)) {
                std::ifstream in(defaultMap);
                json selected;
                if (!in || !(in >> selected) || !selected.is_object())
                    throw std::runtime_error("기본 지도 설정 읽기 실패");
                if (selected.value("map_id", std::string{}) == id) {
                    stagedDefaultMap = root / ".default_map.json.rename.tmp";
                    selected["map_id"] = name;
                    std::ofstream out(stagedDefaultMap, std::ios::trunc);
                    if (!out || !(out << selected.dump(2) << '\n'))
                        throw std::runtime_error("기본 지도 설정 쓰기 실패");
                    out.close();
                    if (!out)
                        throw std::runtime_error("기본 지도 설정 저장 실패");
                }
            }
        } catch (const std::exception &e) {
            discardStaged();
            respond(request, false, err::kHardware, std::string("지도 데이터 변경 준비 실패: ") + e.what());
            return;
        }
        std::filesystem::rename(dir, destination, ec);
        if (ec) {
            discardStaged();
            respond(request, false, err::kHardware, "지도 폴더 이름 변경 실패: " + ec.message());
            return;
        }
        for (const auto &temporary : staged) {
            std::string filename = temporary.filename().string();
            filename.erase(0, 1);
            filename.erase(filename.size() - std::string(".rename.tmp").size());
            const auto target = destination / filename;
            std::filesystem::rename(destination / temporary.filename(), target, ec);
            if (ec) {
                RCLCPP_ERROR(get_logger(), "지도 파일 갱신 실패 (%s): %s", target.c_str(), ec.message().c_str());
                respond(request, false, err::kHardware, "지도 폴더는 변경됐지만 일부 데이터 갱신에 실패했습니다");
                publishMapCatalog();
                return;
            }
        }
        if (!stagedDefaultMap.empty()) {
            std::filesystem::rename(stagedDefaultMap, defaultMap, ec);
            if (ec) {
                RCLCPP_ERROR(get_logger(), "기본 지도 설정 갱신 실패: %s", ec.message().c_str());
                respond(request, false, err::kHardware, "지도 이름은 바뀌었지만 기본 지도 설정 갱신에 실패했습니다");
                publishMapCatalog();
                return;
            }
        }
        if (get_parameter("initial_map").as_string() == (dir / "map.yaml").string())
            set_parameter(rclcpp::Parameter("initial_map", (destination / "map.yaml").string()));
        if (mapId_ == id) {
            mapId_ = name;
            lastMapMeta_["map_id"] = name;
        }
        publishMapCatalog();
        if (mapId_ == name) {
            publishActiveMap();
            publishWaypoints();
            publishMissions();
            publishLocations();
            publishMarkers();
            if (!lastMapPng_.empty())
                sendEnvelope(makePublish(kChMap, lastMapMeta_), false, lastMapPng_);
        }
        respond(request, true);
        return;
    }

    if (request.ch == kCmdMapsSetDefault) {
        const std::string id = request.p.value("id", std::string{});
        const auto root = std::filesystem::path(mapsDir_);
        if (!id.empty() && (id == "." || id.find('/') != std::string::npos ||
            id.find("..") != std::string::npos ||
            !std::filesystem::is_regular_file(root / id / "map.yaml"))) {
            respond(request, false, err::kBadPayload, "유효하지 않거나 없는 지도입니다");
            return;
        }
        const auto temporary = root / ".default_map.json.tmp";
        const auto destination = root / "default_map.json";
        std::ofstream out(temporary, std::ios::trunc);
        if (!out || !(out << json{{"map_id", id}}.dump(2) << '\n')) {
            respond(request, false, err::kHardware, "기본 지도 설정을 쓸 수 없습니다");
            return;
        }
        out.close();
        if (!out) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            respond(request, false, err::kHardware, "기본 지도 설정을 저장하지 못했습니다");
            return;
        }
        std::error_code ec;
        std::filesystem::rename(temporary, destination, ec);
        if (ec) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            respond(request, false, err::kHardware, "기본 지도 설정을 저장하지 못했습니다");
            return;
        }
        publishMapCatalog();
        respond(request, true);
        return;
    }

    if (request.ch == kCmdMapsDelete) {
        const std::string id = request.p.value("id", std::string{});
        if (!haveMissionState_ || (missionState_.state != shalom_interfaces::msg::MissionState::IDLE &&
            missionState_.state != shalom_interfaces::msg::MissionState::COMPLETED &&
            missionState_.state != shalom_interfaces::msg::MissionState::FAILED) ||
            navigationBusy() || !pendingMapId_.empty()) {
            respond(request, false, err::kBusy, "주행과 미션이 끝난 뒤 지도를 삭제하십시오");
            return;
        }
        if (id.empty() || id == "." || id.find('/') != std::string::npos || id.find("..") != std::string::npos ||
            id == mapId_) {
            respond(request, false, err::kMode, "현재 사용 중인 지도는 삭제할 수 없습니다");
            return;
        }
        const std::filesystem::path source = std::filesystem::path(mapsDir_) / id;
        if (!std::filesystem::is_regular_file(source / "map.yaml")) {
            respond(request, false, err::kBadPayload, "유효하지 않거나 없는 지도입니다");
            return;
        }
        if (id == readDefaultMapId(std::filesystem::path(mapsDir_))) {
            respond(request, false, err::kMode, "기본 지도를 변경하거나 해제한 뒤 삭제하십시오");
            return;
        }
        const auto startupMap = get_parameter("initial_map").as_string();
        if (!startupMap.empty() && std::filesystem::path(startupMap).lexically_normal() ==
                                       (source / "map.yaml").lexically_normal()) {
            respond(request, false, err::kMode,
                    "기동 설정에서 사용하는 지도입니다. 시작 지도 설정을 먼저 변경하십시오");
            return;
        }
        const std::filesystem::path trash = std::filesystem::path(mapsDir_) / ".trash";
        std::error_code ec;
        std::filesystem::create_directories(trash, ec);
        if (ec) {
            respond(request, false, err::kHardware, "지도 보관 폴더를 만들지 못했습니다: " + ec.message());
            return;
        }
        const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
        const auto destination = trash / (id + "-" + std::to_string(stamp));
        std::filesystem::rename(source, destination, ec);
        if (ec) {
            respond(request, false, err::kHardware, "지도를 보관 폴더로 옮기지 못했습니다: " + ec.message());
            return;
        }
        publishMapCatalog();
        respond(request, true);
        return;
    }

    if (request.ch == kCmdCapture) {
        handleCapture(request);
        return;
    }

    if (request.ch == kCmdMissionStart) {
        configureAndStartMission(request);
        return;
    }

    if (request.ch == kCmdMissionPause) {
        sendMissionControl(request, shalom_interfaces::srv::MissionControl::Request::PAUSE);
        return;
    }

    if (request.ch == kCmdMissionReturnDock) {
        if (manualMode_ || !pendingMapId_.empty()) {
            respond(request, false, err::kMode, "지도 전환 완료 후 자율 모드에서 충전소로 복귀하십시오");
            return;
        }
        if (haveMissionState_ && missionState_.state != shalom_interfaces::msg::MissionState::IDLE &&
            missionState_.state != shalom_interfaces::msg::MissionState::COMPLETED &&
            missionState_.state != shalom_interfaces::msg::MissionState::FAILED) {
            sendMissionControl(request, shalom_interfaces::srv::MissionControl::Request::RETURN_DOCK);
            return;
        }
        const int dock = findDock();
        if (dock < 0 || manualMode_) {
            respond(request, false, err::kMode, "자율 모드에서 등록된 충전 위치로 복귀할 수 있습니다");
            return;
        }
        Envelope goal = request;
        goal.p = locations_[dock];
        startNavigation(goal);
        return;
    }

    if (request.ch == kCmdMissionResume) {
        if (estopActive()) {
            respond(request, false, err::kMode,
                    "비상정지 상태입니다. 해제한 뒤 재개하십시오");
            return;
        }
        sendMissionControl(request, shalom_interfaces::srv::MissionControl::Request::RESUME);
        return;
    }

    if (request.ch == kCmdMissionStop) {
        const uint8_t operation = haveMissionState_ &&
            (missionState_.state == shalom_interfaces::msg::MissionState::COMPLETED ||
             missionState_.state == shalom_interfaces::msg::MissionState::FAILED)
            ? shalom_interfaces::srv::MissionControl::Request::RESET
            : shalom_interfaces::srv::MissionControl::Request::STOP;
        sendMissionControl(request, operation);
        return;
    }

    if (request.ch == kCmdPowerPolicy) {
        returnAtPct_ = request.p.value("return_at", returnAtPct_);
        departAtPct_ = request.p.value("depart_at", departAtPct_);
        respond(request, true);
        RCLCPP_INFO(get_logger(), "배터리 정책: 복귀 %.0f%%, 출발 %.0f%%",
                    returnAtPct_, departAtPct_);
        return;
    }

    if (request.ch == kCmdInitialPose) {
        if (!pendingMapId_.empty()) {
            respond(request, false, err::kBusy, "지도 전환이 끝난 뒤 초기 위치를 지정하십시오");
            return;
        }
        const double x = request.p.value("x", std::numeric_limits<double>::quiet_NaN());
        const double y = request.p.value("y", std::numeric_limits<double>::quiet_NaN());
        const double theta = request.p.value("theta", std::numeric_limits<double>::quiet_NaN());
        if (mapId_ == "live") {
            respond(request, false, err::kMode,
                    "저장된 지도를 선택해야 초기 위치를 지정할 수 있습니다");
            return;
        }
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(theta)) {
            respond(request, false, err::kBadPayload, "초기 위치 좌표가 올바르지 않습니다");
            return;
        }
        geometry_msgs::msg::PoseWithCovarianceStamped pose;
        pose.header.stamp = now();
        pose.header.frame_id = mapFrame_;
        pose.pose.pose.position.x = x;
        pose.pose.pose.position.y = y;
        pose.pose.pose.orientation.z = std::sin(theta * 0.5);
        pose.pose.pose.orientation.w = std::cos(theta * 0.5);
        // 초기 추정의 불확실성은 AMCL이 scan matching으로 좁혀 간다.
        // x/y 0.25 m², yaw 15°², z/roll/pitch 는 평면 로봇에 맞게 넓게 둔다.
        pose.pose.covariance[0] = 0.25;
        pose.pose.covariance[7] = 0.25;
        pose.pose.covariance[14] = 1.0e6;
        pose.pose.covariance[21] = 1.0e6;
        pose.pose.covariance[28] = 1.0e6;
        pose.pose.covariance[35] = 0.0685389;
        initialPosePub_->publish(pose);
        resetTrail();
        trailAwaitingLocalization_ = true;
        trailNewPoseReceived_ = false;
        trailInitialPoseAt_ = rclcpp::Time(pose.header.stamp);
        respond(request, true);
        sendEnvelope(makeEvent(kChLog, json{{"code", "LOCALIZATION_INITIAL_POSE_SET"},
                                            {"level", "info"},
                                            {"x", x}, {"y", y}, {"theta", theta}}));
        return;
    }

    if (request.ch == kCmdGoto || request.ch == kCmdNavResume) {
        if (!pendingMapId_.empty()) {
            respond(request, false, err::kBusy, "지도 전환이 끝난 뒤 목표를 지정하십시오");
            return;
        }
        if (manualMode_) {
            respond(request, false, err::kMode, "수동 모드에서는 자율 이동을 실행하지 않습니다");
            return;
        }
        if (haveMissionState_ &&
            missionState_.state != shalom_interfaces::msg::MissionState::IDLE &&
            missionState_.state != shalom_interfaces::msg::MissionState::COMPLETED &&
            missionState_.state != shalom_interfaces::msg::MissionState::FAILED) {
            respond(request, false, err::kBusy, "미션이 끝난 뒤 개별 목표를 지정하십시오");
            return;
        }
        const bool resume = request.ch == kCmdNavResume;
        if (resume && navStatus_ != "paused") {
            respond(request, false, err::kMode, "일시정지된 목표 주행이 없습니다");
            return;
        }
        Envelope goal = request;
        if (resume)
            goal.p = navGoalPoint_;
        startNavigation(goal, resume);
        return;
    }

    // 미지원 채널은 조용히 무시하지 않고 사유를 돌려준다. 관제가 새 기능을
    // 쓰려다 아무 반응이 없으면 원인을 짚을 수 없다.
    respond(request, false, err::kUnknownChannel, "지원하지 않는 채널: " + request.ch);
}

namespace {

/// First value in /proc/<file> matching `key`, in whatever unit the file uses.
double readMetric(const char *path, const char *key)
{
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind(key, 0) != 0)
            continue;
        const auto pos = line.find_first_of("0123456789");
        if (pos == std::string::npos)
            return 0.0;
        return std::strtod(line.c_str() + pos, nullptr);
    }
    return 0.0;
}

/// Hottest CPU-ish thermal zone, in Celsius.
///
/// Which zone is the CPU differs between boards, and several of them read
/// room temperature. Taking the maximum finds the one that will throttle
/// first, which is the number worth showing.
double cpuTemperature()
{
    double hottest = 0.0;
    for (int i = 0; i < 32; ++i) {
        std::ifstream f("/sys/class/thermal/thermal_zone" + std::to_string(i) + "/temp");
        if (!f)
            break;
        double milli = 0.0;
        f >> milli;
        hottest = std::max(hottest, milli / 1000.0);
    }
    return hottest;
}

}  // namespace

void BridgeNode::publishSystem()
{
    if (!server_.isConnected())
        return;

    // CPU is a delta against the previous second; one reading of /proc/stat
    // only gives the average since boot, which barely moves after an hour.
    double cpuPct = 0.0;
    {
        std::ifstream f("/proc/stat");
        std::string cpu;
        unsigned long long user = 0, nice = 0, sys = 0, idle = 0, iowait = 0,
                           irq = 0, softirq = 0, steal = 0;
        if (f >> cpu >> user >> nice >> sys >> idle >> iowait >> irq >> softirq >> steal) {
            const unsigned long long idleAll = idle + iowait;
            const unsigned long long total =
                user + nice + sys + idleAll + irq + softirq + steal;
            if (cpuTotal_ != 0 && total > cpuTotal_) {
                const double dTotal = double(total - cpuTotal_);
                const double dIdle = double(idleAll - cpuIdle_);
                cpuPct = 100.0 * (dTotal - dIdle) / dTotal;
            }
            cpuIdle_ = idleAll;
            cpuTotal_ = total;
        }
    }

    const double memTotal = readMetric("/proc/meminfo", "MemTotal:");
    const double memAvail = readMetric("/proc/meminfo", "MemAvailable:");
    const double memPct = memTotal > 0.0 ? 100.0 * (1.0 - memAvail / memTotal) : 0.0;

    // GPU is left null rather than guessed. On the Jetson it comes from the
    // tegra counters and on a desktop from NVML; neither is worth shelling out
    // to once a second, and a zero would read as "idle" instead of "unknown".
    sendEnvelope(makePublish(kChSystem,
                             json{{"cpu_pct", cpuPct},
                                  {"mem_pct", memPct},
                                  {"cpu_temp_c", cpuTemperature()},
                                  {"gpu_pct", nullptr},
                                  {"gpu_temp_c", nullptr},
                                  {"net_rtt_ms", nullptr},
                                  {"net_rssi", nullptr},
                                  {"capture_enabled", captureEnabled_},
                                  {"arm_execution_enabled", armExecutionEnabled_ &&
                                    authorityRequestClient_->service_is_ready() && safetyCommandClient_->service_is_ready() &&
                                    armCmdPub_->get_subscription_count() > 0 && count_subscribers("/motion/safe/arm/joint_command") > 0},
                                  {"robot_id", robotId_},
                                  {"robot_name", robotName_}}),
                 true);
}

const std::vector<double> *BridgeNode::armPreset(const std::string &name)
{
    // 관제의 hmi/src/RobotDef.h 와 같은 값이다. FAIRINO 의 URDF 를 훑어
    // 가동 한계·조작성·데크 간섭·LiDAR 간섭 네 조건을 만족하도록 고른 자세다.
    static const std::vector<double> kHome{0.0, -1.600, -0.800, -1.400, -1.571, 0.0};
    static const std::vector<double> kStandby{0.0, -1.200, -2.200, -1.800, -1.571, 0.0};
    static const std::vector<double> kStow{0.0, -2.600, -2.400, 0.400, -1.571, 0.0};
    if (name == "home") return &kHome;
    if (name == "standby") return &kStandby;
    if (name == "stow") return &kStow;
    return nullptr;
}

bool BridgeNode::applyArmGoal(const Envelope &request, const std::vector<double> &positions)
{
    if (!armExecutionEnabled_) {
        respond(request, false, err::kUnreachable,
                "로봇팔 실행기가 연결되지 않았습니다. 목표는 전송되지 않았습니다");
        return false;
    }
    if (pendingArmGoal_) {
        respond(request, false, err::kBusy, "이전 로봇팔 명령을 처리 중입니다");
        return false;
    }
    if (count_subscribers("/motion/safe/arm/joint_command") == 0 ||
        armCmdPub_->get_subscription_count() == 0 || !authorityRequestClient_->service_is_ready() ||
        !safetyCommandClient_->service_is_ready()) {
        respond(request, false, err::kUnreachable,
                "로봇팔 안전 출력에 실행기가 연결되지 않았습니다");
        return false;
    }
    if (!validArmPresetPositions(json(positions))) {
        respond(request, false, err::kBadPayload,
                "관절 수 또는 관절 범위가 올바르지 않습니다");
        return false;
    }
    if (lastArmPositions_.size() != kArmJointNames.size() ||
        std::chrono::steady_clock::now() - lastArmReceived_ > std::chrono::seconds(1)) {
        respond(request, false, err::kUnreachable, "로봇팔 관절 피드백이 끊겼습니다");
        return false;
    }
    if (haveMissionState_ && (missionState_.state == shalom_interfaces::msg::MissionState::RUNNING ||
                             missionState_.state == shalom_interfaces::msg::MissionState::RETURNING ||
                             missionState_.state == shalom_interfaces::msg::MissionState::RECOVERING ||
                             missionState_.state == shalom_interfaces::msg::MissionState::PAUSING)) {
        respond(request, false, err::kBusy, "미션을 일시정지한 뒤 로봇팔을 조작하십시오");
        return false;
    }
    pendingArmGoal_ = request;
    pendingArmPositions_ = positions;
    armGoalRequestedAt_ = std::chrono::steady_clock::now();
    armAuthorityAccepted_ = false;
    auto authority = std::make_shared<shalom_interfaces::srv::AuthorityRequest::Request>();
    authority->request_id = "hmi-arm-" + request.id;
    authority->requester = "hmi_bridge";
    authority->operation = shalom_interfaces::srv::AuthorityRequest::Request::REQUEST_ARM;
    const auto pending = authorityRequestClient_->async_send_request(authority,
        [this, id = request.id](rclcpp::Client<shalom_interfaces::srv::AuthorityRequest>::SharedFuture future) {
            if (!pendingArmGoal_ || pendingArmGoal_->id != id)
                return;
            const auto result = future.get();
            if (!result->accepted) {
                finishArmGoal(false, err::kBusy, result->detail);
                return;
            }
            armAuthorityAccepted_ = true;
            requestSafetyResume();
        });
    armAuthorityRequestId_ = pending.request_id;
    return true;
}

void BridgeNode::finishArmGoal(bool ok, const std::string &code, const std::string &message)
{
    if (!pendingArmGoal_)
        return;
    const auto request = *pendingArmGoal_;
    pendingArmGoal_.reset();
    pendingArmPositions_.clear();
    if (armAuthorityRequestId_) {
        authorityRequestClient_->remove_pending_request(*armAuthorityRequestId_);
        armAuthorityRequestId_.reset();
    }
    respond(request, ok, code, message);
}

void BridgeNode::tickArmGoal()
{
    if (!pendingArmGoal_)
        return;
    if (!server_.isConnected() || estopActive() ||
        std::chrono::steady_clock::now() - lastArmReceived_ > std::chrono::seconds(1)) {
        finishArmGoal(false, err::kBusy, "연결·안전 상태·팔 피드백을 확인하십시오");
        return;
    }
    if (std::chrono::steady_clock::now() - armGoalRequestedAt_ > std::chrono::seconds(2)) {
        finishArmGoal(false, err::kBusy, "로봇팔 제어 권한 또는 안전 상태 응답이 없습니다");
        return;
    }
    if (!armAuthorityAccepted_ || motionAuthority_ != "arm_active" || safetyState_ != "normal")
        return;
    if (armCmdPub_->get_subscription_count() == 0 || count_subscribers("/motion/safe/arm/joint_command") == 0) {
        finishArmGoal(false, err::kUnreachable, "로봇팔 실행기 연결이 끊겼습니다");
        return;
    }
    sensor_msgs::msg::JointState msg;
    msg.header.stamp = now();
    msg.name = kArmJointNames;
    msg.position = pendingArmPositions_;
    armCmdPub_->publish(msg);
    finishArmGoal(true);
}

void BridgeNode::publishWaypoints()
{
    // 위치 정의는 수정하지 않는다. 진행 상태는 현재 미션에서 발행할 때만 계산한다.
    json points = waypoints_;
    for (auto &point : points)
        if (point.is_object()) point["status"] = "todo";
    if (haveMissionState_ && missionState_.state != shalom_interfaces::msg::MissionState::IDLE) {
        const bool completed = missionState_.state == shalom_interfaces::msg::MissionState::COMPLETED ||
                               (missionState_.state == shalom_interfaces::msg::MissionState::RETURNING &&
                                missionState_.current_step < 0);
        const auto mission = std::find_if(missions_.begin(), missions_.end(), [this](const json &item) {
            return item.is_object() &&
                   item.value("id", std::string{}) == missionState_.mission_id;
        });
        if (mission != missions_.end() && mission->contains("steps") &&
            (*mission)["steps"].is_array()) {
            // 미션 단계 순서가 위치 목록의 순서와 다를 수 있다.
            std::size_t stepIndex = 0;
            for (const auto &step : mission->value("steps", json::array())) {
                if (!step.is_object() || step.value("type", std::string{}) != "navigate") {
                    ++stepIndex;
                    continue;
                }
                const std::string locationId = step.value("location_id", std::string{});
                const bool beforeCurrent = missionState_.current_step >= 0 &&
                    stepIndex < std::size_t(missionState_.current_step);
                const bool current = missionState_.current_step >= 0 &&
                    stepIndex == std::size_t(missionState_.current_step);
                for (auto &point : points)
                    if (point.value("id", std::string{}) == locationId)
                        point["status"] = completed || beforeCurrent ? "done"
                            : current && missionState_.state == shalom_interfaces::msg::MissionState::FAILED
                                ? "error" : current ? "current" : "todo";
                ++stepIndex;
            }
        } else {
            // 이전 전체 목록 기반 주행 계획을 위한 호환 경로.
            for (std::size_t i = 0; i < points.size(); ++i) {
                const bool beforeCurrent = missionState_.current_step >= 0 &&
                    i < std::size_t(missionState_.current_step);
                const bool current = missionState_.current_step >= 0 &&
                    i == std::size_t(missionState_.current_step);
                points[i]["status"] = completed || beforeCurrent ? "done"
                    : current && missionState_.state == shalom_interfaces::msg::MissionState::FAILED
                        ? "error" : current ? "current" : "todo";
            }
        }
    }
    sendEnvelope(makePublish(kChWaypoints, json{{"points", points}}));
}

void BridgeNode::publishMissions()
{
    sendEnvelope(makePublish(kChMissions,
                             json{{"map_id", mapId_}, {"missions", missions_}}));
}

void BridgeNode::publishArmPosePresets()
{
    sendEnvelope(makePublish(kChArmPosePresets,
                             json{{"presets", armPosePresets_}}));
}

void BridgeNode::publishLocations()
{
    sendEnvelope(makePublish(kChLocations, json{{"locations", locations_}}));
}

void BridgeNode::publishActiveMap()
{
    json active{{"id", mapId_}, {"name", mapId_}};
    sendEnvelope(makePublish(kChActiveMap, active));
}

void BridgeNode::loadSelectedMap(const Envelope &request, const std::string &id, bool from_slam)
{
    if (!mapLoadClient_->service_is_ready()) {
        if (from_slam) {
            restoreSlam(request, "map_server/load_map 서비스를 찾지 못했습니다");
        } else {
            finishMapTransition(false, "map_server/load_map 서비스를 찾지 못했습니다");
        }
        return;
    }
    auto load = std::make_shared<nav2_msgs::srv::LoadMap::Request>();
    load->map_url = (std::filesystem::path(mapsDir_) / id / "map.yaml").string();
    pendingMapPublication_ = true;
    const auto generation = mapTransitionGeneration_;
    mapLoadRequestId_ = mapLoadClient_->async_send_request(load, [this, request, id, from_slam, generation](
        rclcpp::Client<nav2_msgs::srv::LoadMap>::SharedFuture future) {
        if (!pendingMapRequest_ || generation != mapTransitionGeneration_) return;
        mapLoadRequestId_.reset();
        if (future.get()->result != nav2_msgs::srv::LoadMap::Response::RESULT_SUCCESS) {
            pendingMapPublication_ = false;
            if (from_slam) restoreSlam(request, "map_server가 지도를 불러오지 못했습니다");
            else {
                finishMapTransition(false, "map_server가 지도를 불러오지 못했습니다");
            }
            return;
        }
        std::string detail;
        if (!loadMapBundle(id, &detail)) {
            pendingMapPublication_ = false;
            if (from_slam) restoreSlam(request, detail);
            else {
                mapTransitionUncertain_ = true;
                linkHold_ = true;
                requestSafetyStop("NAV_MAP_BUNDLE_INVALID", "지도 적용 후 지도 데이터 읽기 실패");
                finishMapTransition(false, detail);
            }
            return;
        }
        pendingMapPublication_ = false;
        pendingMapId_.clear();
        publishActiveMap();
        publishMapCatalog();
        publishWaypoints();
        publishMissions();
        publishLocations();
        publishMarkers();
        sendEnvelope(makePublish(kChPlan, json{{"points", lastPlanPoints_}}), true);
        sendEnvelope(makePublish(kChTrail,
                                 json{{"points", json::array()}, {"reset", true}}));
        finishMapTransition(true);
    }).request_id;
}

void BridgeNode::finishMapTransition(bool ok, const std::string &detail)
{
    if (mapLoadRequestId_) mapLoadClient_->remove_pending_request(*mapLoadRequestId_);
    if (mapLocalizationRequestId_) localizationManagerClient_->remove_pending_request(*mapLocalizationRequestId_);
    if (mapSlamRequestId_) slamLifecycleClient_->remove_pending_request(*mapSlamRequestId_);
    mapLoadRequestId_.reset();
    mapLocalizationRequestId_.reset();
    mapSlamRequestId_.reset();
    pendingMapId_.clear();
    pendingMapPublication_ = false;
    ++mapTransitionGeneration_;
    if (pendingMapRequest_) respond(*pendingMapRequest_, ok, ok ? std::string{} : err::kHardware, detail);
    pendingMapRequest_.reset();
}

void BridgeNode::switchFromSlam(const Envelope &request, const std::string &id)
{
    if (!slamLifecycleClient_->service_is_ready() ||
        !localizationManagerClient_->service_is_ready()) {
        finishMapTransition(false, "SLAM 또는 위치 추정 전환 서비스를 찾지 못했습니다");
        return;
    }
    // 먼저 위치 추정 노드를 inactive까지 준비한다. 이 동안 SLAM의 지도와
    // map->odom은 그대로 살아 있어 전환 준비 실패가 주행 좌표계를 끊지 않는다.
    auto configure = std::make_shared<nav2_msgs::srv::ManageLifecycleNodes::Request>();
    configure->command = nav2_msgs::srv::ManageLifecycleNodes::Request::CONFIGURE;
    const auto generation = mapTransitionGeneration_;
    mapLocalizationRequestId_ = localizationManagerClient_->async_send_request(configure, [this, request, id, generation](
        rclcpp::Client<nav2_msgs::srv::ManageLifecycleNodes>::SharedFuture configured) {
        if (!pendingMapRequest_ || generation != mapTransitionGeneration_) return;
        mapLocalizationRequestId_.reset();
        if (!configured.get()->success) {
            finishMapTransition(false, "map_server·AMCL 준비에 실패했습니다. SLAM은 유지됩니다");
            return;
        }
        auto deactivate = std::make_shared<lifecycle_msgs::srv::ChangeState::Request>();
        deactivate->transition.id = lifecycle_msgs::msg::Transition::TRANSITION_DEACTIVATE;
        mapSlamRequestId_ = slamLifecycleClient_->async_send_request(deactivate, [this, request, id, generation](
            rclcpp::Client<lifecycle_msgs::srv::ChangeState>::SharedFuture stopped) {
            if (!pendingMapRequest_ || generation != mapTransitionGeneration_) return;
            mapSlamRequestId_.reset();
            if (!stopped.get()->success) {
                finishMapTransition(false, "SLAM을 중지하지 못해 지도를 전환하지 않았습니다");
                return;
            }
            auto resume = std::make_shared<nav2_msgs::srv::ManageLifecycleNodes::Request>();
            resume->command = nav2_msgs::srv::ManageLifecycleNodes::Request::RESUME;
            mapLocalizationRequestId_ = localizationManagerClient_->async_send_request(resume, [this, request, id, generation](
                rclcpp::Client<nav2_msgs::srv::ManageLifecycleNodes>::SharedFuture resumed) {
                if (!pendingMapRequest_ || generation != mapTransitionGeneration_) return;
                mapLocalizationRequestId_.reset();
                if (!resumed.get()->success) {
                    restoreSlam(request, "map_server·AMCL을 활성화하지 못했습니다");
                    return;
                }
                loadSelectedMap(request, id, true);
            }).request_id;
        }).request_id;
    }).request_id;
}

void BridgeNode::restoreSlam(const Envelope &request, const std::string &reason)
{
    pendingMapPublication_ = false;
    if (!localizationManagerClient_->service_is_ready()) {
        mapTransitionUncertain_ = true;
        finishMapTransition(false, reason + "; 위치 추정 종료 서비스를 찾지 못해 수동 복구가 필요합니다");
        return;
    }
    auto pause = std::make_shared<nav2_msgs::srv::ManageLifecycleNodes::Request>();
    pause->command = nav2_msgs::srv::ManageLifecycleNodes::Request::PAUSE;
    const auto generation = mapTransitionGeneration_;
    mapLocalizationRequestId_ = localizationManagerClient_->async_send_request(pause, [this, request, reason, generation](
        rclcpp::Client<nav2_msgs::srv::ManageLifecycleNodes>::SharedFuture pause_future) {
        if (!pendingMapRequest_ || generation != mapTransitionGeneration_) return;
        mapLocalizationRequestId_.reset();
        if (!pause_future.get()->success || !slamLifecycleClient_->service_is_ready()) {
            mapTransitionUncertain_ = true;
            finishMapTransition(false, reason + "; 위치 추정 종료 실패로 수동 복구가 필요합니다");
            return;
        }
        auto activate = std::make_shared<lifecycle_msgs::srv::ChangeState::Request>();
        activate->transition.id = lifecycle_msgs::msg::Transition::TRANSITION_ACTIVATE;
        mapSlamRequestId_ = slamLifecycleClient_->async_send_request(activate, [this, request, reason, generation](
            rclcpp::Client<lifecycle_msgs::srv::ChangeState>::SharedFuture activate_future) {
            if (!pendingMapRequest_ || generation != mapTransitionGeneration_) return;
            mapSlamRequestId_.reset();
            lastMapPng_.clear();
            lastMapMeta_ = json::object();
            const bool restored = activate_future.get()->success;
            mapTransitionUncertain_ = !restored;
            finishMapTransition(false, reason + (restored ? "; SLAM으로 복귀했습니다"
                : "; SLAM 복귀 실패로 수동 복구가 필요합니다"));
        }).request_id;
    }).request_id;
}

void BridgeNode::publishMapCatalog()
{
    json maps = json::array();
    std::error_code ec;
    const std::filesystem::path root(mapsDir_);
    const std::string defaultId = readDefaultMapId(root);
    for (const auto &entry : std::filesystem::directory_iterator(root, ec)) {
        if (ec || !entry.is_directory())
            continue;
        const auto dir = entry.path();
        if (!std::filesystem::is_regular_file(dir / "map.yaml"))
            continue;

        const std::string id = dir.filename().string();
        json item{{"id", id}, {"name", id}, {"active", id == mapId_},
                  {"default", id == defaultId},
                  {"waypoint_count", 0}};
        try {
            std::ifstream meta_in(dir / "metadata.json");
            json meta;
            if (meta_in >> meta) {
                if (meta.is_object() && meta.contains("created_at") &&
                    meta["created_at"].is_string())
                    item["created_at"] = meta["created_at"];
            }
            std::ifstream wp_in(dir / "waypoints.json");
            json waypoints;
            if (wp_in >> waypoints && waypoints["points"].is_array())
                item["waypoint_count"] = waypoints["points"].size();
        } catch (const json::exception &e) {
            RCLCPP_WARN(get_logger(), "지도 카탈로그 항목을 읽지 못했습니다 (%s): %s",
                        dir.c_str(), e.what());
        }
        maps.push_back(std::move(item));
    }
    // directory_iterator의 순서는 파일 시스템마다 달라 HMI 목록까지
    // 흔들리므로 폴더 이름으로 정렬한다.
    std::sort(maps.begin(), maps.end(), [](const json &left, const json &right) {
        return left.value("id", std::string{}) < right.value("id", std::string{});
    });
    sendEnvelope(makePublish(kChMaps, json{{"maps", maps}}));
}

bool BridgeNode::loadMapBundle(const std::string &map_id, std::string *error,
                               bool validate_only)
{
    if (map_id.empty() || map_id == "." || map_id.find('/') != std::string::npos || map_id.find("..") != std::string::npos) {
        if (error) *error = "유효하지 않은 지도 ID";
        return false;
    }
    const std::filesystem::path dir = std::filesystem::path(mapsDir_) / map_id;
    if (!std::filesystem::is_regular_file(dir / "map.yaml")) {
        if (error) *error = "지도 파일이 없습니다";
        return false;
    }

    std::ifstream metadata(dir / "metadata.json");
    if (metadata) {
        try {
            json document;
            metadata >> document;
            if (!document.is_object()) {
                if (error) *error = "metadata.json 형식이 올바르지 않습니다";
                return false;
            }
        } catch (const json::exception &e) {
            if (error) *error = std::string("metadata.json 읽기 실패: ") + e.what();
            return false;
        }
    }

    const auto read = [&dir, error](const char *name, const char *key) -> std::optional<json> {
        std::ifstream in(dir / name);
        if (!in)
            return json::array();
        json doc;
        try {
            in >> doc;
            if (!doc.is_object() || !doc.contains(key) || !doc[key].is_array()) {
                if (error) *error = std::string(name) + " 형식이 올바르지 않습니다";
                return std::nullopt;
            }
            return doc[key];
        } catch (const json::exception &e) {
            if (error) *error = std::string(name) + " 읽기 실패: " + e.what();
            return std::nullopt;
        }
    };
    const auto points = read("waypoints.json", "points");
    const auto locations = read("locations.json", "locations");
    const auto markers = read("markers.json", "markers");
    const auto missions = read("missions.json", "missions");
    if (!points || !locations || !markers || !missions)
        return false;
    if (std::any_of(points->begin(), points->end(), [](const json &point) {
            return !point.is_object();
        })) {
        if (error) *error = "waypoints.json에 올바르지 않은 지점이 있습니다";
        return false;
    }
    if (validate_only)
        return true;
    if (mapId_ != map_id) {
        trailPending_.clear();
        trailHistory_.clear();
        trailHasLast_ = false;
        trailReset_ = true;
        trailAwaitingLocalization_ = false;
        trailNewPoseReceived_ = false;
        lastPlanPoints_ = json::array();
    }
    waypoints_ = *points;
    for (auto &point : waypoints_)
        if (point.is_object()) {
            point.erase("status");
            point.erase("captured_from");
            point.erase("localization_ok");
            point.erase("tag_id");
            point.erase("kind");
        }
    locations_ = *locations;
    for (auto &location : locations_)
        if (location.is_object()) {
            location.erase("captured_from");
            location.erase("localization_ok");
            location.erase("tag_id");
        }
    markers_ = *markers;
    missions_ = *missions;
    for (auto &mission : missions_)
        if (mission.is_object()) mission.erase("map_id");
    mapId_ = map_id;
    return true;
}

bool BridgeNode::saveMapState(const char *filename, const json &state)
{
    if (mapId_ == "live")
        return false;
    const std::filesystem::path path = std::filesystem::path(mapsDir_) / mapId_ / filename;
    const std::filesystem::path temporary = path.parent_path() /
        (std::string(".") + filename + ".tmp");
    const std::string key = std::string(filename) == "waypoints.json" ? "points"
                            : std::string(filename) == "locations.json" ? "locations"
                                                                          : "markers";
    json document = json::object();
    document[key] = state;
    {
        std::ofstream out(temporary, std::ios::trunc);
        if (!out)
            return false;
        out << document.dump(2) << '\n';
        out.close();
        if (!out) {
            std::error_code ec;
            std::filesystem::remove(temporary, ec);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::error_code removeError;
        std::filesystem::remove(temporary, removeError);
        return false;
    }
    return true;
}

bool BridgeNode::saveMissions(std::string *error)
{
    const std::filesystem::path dir = std::filesystem::path(mapsDir_) / mapId_;
    const std::filesystem::path destination = dir / "missions.json";
    const std::filesystem::path temporary = dir / ".missions.json.tmp";
    const json document{{"missions", missions_}};
    {
        std::ofstream out(temporary, std::ios::trunc);
        if (!out) {
            if (error) *error = "미션 파일을 쓸 수 없습니다";
            return false;
        }
        out << document.dump(2) << '\n';
        if (!out) {
            std::error_code ec;
            std::filesystem::remove(temporary, ec);
            if (error) *error = "미션 파일 저장에 실패했습니다";
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temporary, destination, ec);
    if (ec) {
        // POSIX rename replaces an existing file atomically. On platforms that
        // do not, report the failure rather than deleting the last good copy.
        std::error_code remove_ec;
        std::filesystem::remove(temporary, remove_ec);
        if (error) *error = "저장된 미션 파일을 교체하지 못했습니다: " + ec.message();
        return false;
    }
    return true;
}

void BridgeNode::publishSpeedLimit()
{
    nav2_msgs::msg::SpeedLimit message;
    message.header.stamp = now();
    message.percentage = false;
    message.speed_limit = navigationSpeed_;
    speedLimitPub_->publish(message);
}

void BridgeNode::publishNavigationSpeed()
{
    if (server_.isConnected()) {
        auto settings = navigationSpeedSettings();
        settings["autonomous_applied"] = navigationSpeedApplied_;
        sendEnvelope(makePublish(kChNavigationSpeed, settings));
    }
}

bool BridgeNode::saveNavigationSpeed(const json &settings, std::string *error)
{
    const std::filesystem::path root(robotDataDir_);
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec) {
        *error = "주행 속도 저장 경로를 만들 수 없습니다: " + ec.message();
        return false;
    }
    const auto destination = root / "navigation_settings.json";
    const auto temporary = root / ".navigation_settings.json.tmp";
    {
        std::ofstream out(temporary, std::ios::trunc);
        if (!out) {
            *error = "주행 속도 설정 파일을 쓸 수 없습니다";
            return false;
        }
        out << settings.dump(2) << '\n';
        out.close();
        if (!out) {
            std::filesystem::remove(temporary, ec);
            *error = "주행 속도 설정 저장에 실패했습니다";
            return false;
        }
    }
    std::filesystem::rename(temporary, destination, ec);
    if (ec) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        *error = "주행 속도 설정 파일을 교체하지 못했습니다: " + ec.message();
        return false;
    }
    return true;
}

bool BridgeNode::saveArmPosePresets(std::string *error)
{
    if (armPosePresetsLoadInvalid_) {
        if (error) *error = "팔 자세 파일에 잘못된 항목이 있습니다. 파일을 수정하고 브리지를 다시 시작하십시오";
        return false;
    }
    const std::filesystem::path root(robotDataDir_);
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec) {
        if (error) *error = "팔 자세 프리셋 저장 경로를 만들 수 없습니다: " + ec.message();
        return false;
    }
    const auto destination = root / "arm_pose_presets.json";
    const auto temporary = root / ".arm_pose_presets.json.tmp";
    {
        std::ofstream out(temporary, std::ios::trunc);
        if (!out) {
            if (error) *error = "팔 자세 프리셋 파일을 쓸 수 없습니다";
            return false;
        }
        out << json{{"presets", armPosePresets_}}.dump(2) << '\n';
        out.close();
        if (!out) {
            std::filesystem::remove(temporary, ec);
            if (error) *error = "팔 자세 프리셋 저장에 실패했습니다";
            return false;
        }
    }
    std::filesystem::rename(temporary, destination, ec);
    if (ec) {
        std::error_code removeError;
        std::filesystem::remove(temporary, removeError);
        if (error) *error = "팔 자세 프리셋 파일을 교체하지 못했습니다: " + ec.message();
        return false;
    }
    return true;
}


bool BridgeNode::motionFeedbackFresh() const
{
    if (lastOdomAt_.nanoseconds() == 0 ||
        lastOdomReceived_ == std::chrono::steady_clock::time_point{})
        return false;
    const double sourceAge = (now() - lastOdomAt_).seconds();
    return sourceAge >= -0.1 && sourceAge <= 1.0 &&
        std::chrono::steady_clock::now() - lastOdomReceived_ <= std::chrono::seconds(1);
}

bool BridgeNode::isMoving() const
{
    // A stopped or disconnected TF source cannot confirm that the robot is stationary.
    if (!motionFeedbackFresh()) return true;
    return speedLinear_ > maxCaptureLinear_ || speedAngular_ > maxCaptureAngular_;
}

double BridgeNode::centreDistanceMm() const
{
    // 정렬된 깊이 이미지 한가운데 값. 대상이 화면 중앙에 있다는 가정인데,
    // Apriltag 보정이 붙기 전까지는 이보다 나은 근거가 없다. 과업지시서의
    // 필수 메타데이터에 촬영거리가 있어 빈 값으로 둘 수도 없다.
    if (!lastDepth_ || lastDepth_->encoding != "16UC1")
        return -1.0;
    const auto w = lastDepth_->width, h = lastDepth_->height;
    if (w == 0 || h == 0)
        return -1.0;
    const std::size_t offset = std::size_t(h / 2) * lastDepth_->step
                               + std::size_t(w / 2) * 2;
    if (offset + 1 >= lastDepth_->data.size())
        return -1.0;
    const std::uint16_t mm = std::uint16_t(lastDepth_->data[offset])
                             | std::uint16_t(lastDepth_->data[offset + 1] << 8);
    return mm == 0 ? -1.0 : double(mm);   // 0 은 "측정 못 함" 이다
}

void BridgeNode::publishCaptureSpool()
{
    std::error_code ec;
    const bool mounted = captureEnabled_ &&
        (!captureRequireMount_ || mountPointActive(captureMountPoint_));
    std::filesystem::space_info space{};
    if (mounted) {
        std::filesystem::create_directories(spoolDir_, ec);
        if (!ec)
            space = std::filesystem::space(captureRequireMount_ ? captureMountPoint_ : spoolDir_, ec);
    }
    sendEnvelope(makePublish(kChCaptureSpool,
                             json{{"nas_online", mounted && !ec},
                                  {"pending", 0},
                                  {"spool_free_mb",
                                   mounted && !ec
                                       ? double(space.available) / (1024.0 * 1024.0) : 0.0}}));
}

void BridgeNode::handleCapture(const Envelope &request)
{
    if (!captureEnabled_) {
        respond(request, false, err::kMode,
                "이 배포본에는 카메라 촬영 기능이 포함되어 있지 않습니다");
        return;
    }
    if (!lastColor_) {
        respond(request, false, err::kUnreachable,
                "카메라 영상이 없습니다. 카메라 연결을 확인하십시오");
        return;
    }
    const auto imageFresh = [this](const sensor_msgs::msg::Image::ConstSharedPtr &image,
            std::chrono::steady_clock::time_point received) {
        if (!image || received == std::chrono::steady_clock::time_point{}) return false;
        const rclcpp::Time stamp(image->header.stamp, get_clock()->get_clock_type());
        const double age = (now() - stamp).seconds();
        return stamp.nanoseconds() != 0 && age >= -0.1 && age <= captureMaxImageAge_ &&
            std::chrono::duration<double>(std::chrono::steady_clock::now() - received).count() <= captureMaxImageAge_;
    };
    if (!imageFresh(lastColor_, lastColorReceived_) || !imageFresh(lastDepth_, lastDepthReceived_)) {
        respond(request, false, err::kUnreachable, "RGB·Depth 영상이 없거나 오래되었습니다");
        return;
    }
    const auto rgbStamp = rclcpp::Time(lastColor_->header.stamp, get_clock()->get_clock_type());
    const auto depthStamp = rclcpp::Time(lastDepth_->header.stamp, get_clock()->get_clock_type());
    if (std::abs((rgbStamp - depthStamp).seconds()) > captureMaxSyncDifference_) {
        respond(request, false, err::kUnreachable, "RGB·Depth 영상의 촬영 시각이 일치하지 않습니다");
        return;
    }
    if (estopActive()) {
        respond(request, false, err::kMode, "비상정지 상태에서는 촬영하지 않습니다");
        return;
    }
    // 과업지시서 2.2.4: 반드시 정지 상태에서 촬영한다. 화면에서도 버튼을
    // 잠그지만, 규칙은 로봇이 지켜야 한다 — 화면만 막으면 다른 경로로 들어온
    // 요청은 그대로 통과한다.
    if (isMoving()) {
        respond(request, false, err::kMode,
                "이동 중에는 촬영하지 않습니다. 정지한 뒤 다시 시도하십시오");
        return;
    }

    if (captureRequireMount_ && !mountPointActive(captureMountPoint_)) {
        respond(request, false, err::kHardware,
                "촬영 저장소가 마운트되지 않았습니다: " + captureMountPoint_);
        return;
    }

    // 파일명은 과업지시서가 정한 형식이다.
    //   차량번호_량번호_포인트ID,YYYYMMDDHHMMSS.확장자
    const auto text = [&request](const char *key) {
        const auto value = request.p.find(key);
        return value != request.p.end() && value->is_string()
            ? value->get<std::string>() : std::string{};
    };
    const std::string vehicle = text("vehicle_number");
    const std::string train = text("train_number");
    const std::string car = text("car_number");
    const std::string point = text("point_id");
    const std::string safeVehicle = captureFilePart(vehicle);
    const std::string safeTrain = captureFilePart(train);
    const std::string safeCar = captureFilePart(car);
    const std::string safePoint = captureFilePart(point);
    if (safeVehicle.empty() || safeTrain.empty() || safeCar.empty() || safePoint.empty()) {
        respond(request, false, err::kBadPayload,
                "차량번호·편성번호·량번호·포인트ID를 입력하십시오");
        return;
    }

    const auto t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char stamp[16];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d%H%M%S", &tm);

    const std::string base = safeVehicle + "_" + safeCar + "_" + safePoint + "," + stamp;

    std::error_code ec;
    std::filesystem::create_directories(spoolDir_, ec);
    if (ec) {
        respond(request, false, err::kHardware,
                "저장 폴더를 만들지 못했습니다: " + spoolDir_);
        return;
    }

    const std::string png = encodeImagePng(*lastColor_);
    if (png.empty()) {
        respond(request, false, err::kHardware,
                "사진을 만들지 못했습니다. 카메라 형식이 " + lastColor_->encoding
                    + " 입니다");
        return;
    }

    const std::string imagePath = spoolDir_ + "/" + base + ".png";
    const std::string sidecarPath = spoolDir_ + "/" + base + ".json";
    const bool imageExists = std::filesystem::exists(imagePath, ec);
    if (!ec && (imageExists || std::filesystem::exists(sidecarPath, ec))) {
        respond(request, false, err::kBusy,
                "같은 지점의 촬영 파일이 이미 있습니다. 1초 뒤 다시 촬영하십시오");
        return;
    }
    if (ec) {
        respond(request, false, err::kHardware, "기존 촬영 파일을 확인하지 못했습니다");
        return;
    }
    {
        std::ofstream out(imagePath, std::ios::binary);
        if (!out) {
            respond(request, false, err::kHardware, "사진을 저장하지 못했습니다");
            return;
        }
        out.write(png.data(), std::streamsize(png.size()));
        out.flush();
        out.close();
        if (!out) {
            std::filesystem::remove(imagePath, ec);
            respond(request, false, err::kHardware, "사진을 끝까지 저장하지 못했습니다");
            return;
        }
    }

    // 사이드카. 파일명에 담기지 않는 값들이 여기 들어간다 — 과업지시서의
    // 필수 메타데이터 중 로봇좌표, 촬영거리, Apriltag ID 가 그것이다.
    double x = 0.0, y = 0.0, theta = 0.0;
    try {
        const auto tf = tfBuffer_->lookupTransform(mapFrame_, baseFrame_,
                                                   tf2::TimePointZero);
        x = tf.transform.translation.x;
        y = tf.transform.translation.y;
        // 기존 자세 발행부와 같은 방식으로 만든다. tf2::fromMsg 의 이 조합은
        // 헤더에 선언만 있고 구현이 없어 링크가 깨진다.
        const tf2::Quaternion q(tf.transform.rotation.x, tf.transform.rotation.y,
                                tf.transform.rotation.z, tf.transform.rotation.w);
        double roll = 0, pitch = 0, yaw = 0;
        tf2::Matrix3x3(q).getRPY(roll, pitch, yaw);
        theta = yaw;
    } catch (const tf2::TransformException &e) {
        // 자리를 모른 채로도 사진은 남긴다. 좌표가 빠진 사실은 사이드카에
        // 그대로 드러나고, 화면이 그것을 "메타데이터 누락" 으로 표시한다.
        RCLCPP_WARN(get_logger(), "촬영 시각의 자세를 읽지 못했다: %s", e.what());
    }
    const double distance = centreDistanceMm();
    const json meta{
        {"file", base + ".png"},
        {"vehicle_number", vehicle},
        {"train_number", train},
        {"car_number", car},
        {"point_id", point},
        {"captured_at", std::string(stamp)},
        {"robot", json{{"x", x}, {"y", y}, {"theta", theta}}},
        {"distance_mm", distance > 0.0 ? json(distance) : json(nullptr)},
        {"tag_id", request.p.contains("tag_id") ? request.p["tag_id"] : json(nullptr)},
        {"map_id", mapId_},
    };
    {
        std::ofstream out(sidecarPath);
        if (!out) {
            std::filesystem::remove(imagePath, ec);
            respond(request, false, err::kHardware, "촬영 메타데이터 파일을 만들지 못했습니다");
            return;
        }
        out << meta.dump(2);
        out.flush();
        out.close();
        if (!out) {
            std::filesystem::remove(sidecarPath, ec);
            std::filesystem::remove(imagePath, ec);
            respond(request, false, err::kHardware, "촬영 메타데이터를 저장하지 못했습니다");
            return;
        }
    }

    // 파일명은 실제 저장 성공 응답에 넣는다. 관제의 예상 파일명과 서버
    // 시각이 다를 수 있으므로, 화면과 이력은 이 값을 사용해야 한다.
    auto response = makeResponse(request, true);
    response.p["file"] = base + ".png";
    sendEnvelope(response);
    RCLCPP_INFO(get_logger(), "촬영 저장: %s (%ux%u %s, PNG %zu 바이트)",
                imagePath.c_str(), lastColor_->width, lastColor_->height,
                lastColor_->encoding.c_str(), png.size());

    // 미리보기는 방금 저장한 그 바이트를 그대로 보낸다. 다시 만들면 화면에
    // 보이는 것과 저장된 것이 다를 수 있다.
    sendEnvelope(makePublish(kChPreview, meta), false, png);
    publishCaptureSpool();
}

// ================= mission adapter =================

std::optional<shalom_interfaces::msg::MissionPlan> BridgeNode::makeMissionPlan(
    const json *mission, std::string *error)
{
    const auto fail = [error](const std::string &message)
        -> std::optional<shalom_interfaces::msg::MissionPlan> {
        if (error)
            *error = message;
        return std::nullopt;
    };
    json steps = json::array();
    bool explicitDock = false;
    if (mission) {
        if (!mission->is_object() || !(*mission)["steps"].is_array())
            return fail("미션 단계 목록이 올바르지 않습니다");
        for (std::size_t i = 0; i < (*mission)["steps"].size(); ++i) {
            const auto &step = (*mission)["steps"][i];
            if (!step.is_object())
                return fail("미션 단계 " + std::to_string(i + 1) + " 형식이 올바르지 않습니다");
            const std::string type = step.value("type", std::string{});
            if (type == "navigate") {
                const std::string locationId = step.value("location_id", std::string{});
                const auto findLocation = [&locationId](const json &list) -> const json * {
                    if (!list.is_array()) return nullptr;
                    for (const auto &item : list)
                        if (item.is_object() && item.value("id", std::string{}) == locationId)
                            return &item;
                    return nullptr;
                };
                const json *location = findLocation(waypoints_);
                if (!location) location = findLocation(locations_);
                if (!location || !location->contains("x") || !location->contains("y") ||
                    !(*location)["x"].is_number() || !(*location)["y"].is_number())
                    return fail("미션 단계 " + std::to_string(i + 1) +
                                "이 참조하는 위치를 찾지 못했습니다: " + locationId);
                json point = *location;
                point["id"] = step.value("id", locationId);
                steps.push_back(std::move(point));
            } else if (type == "dock") {
                if (i + 1 != (*mission)["steps"].size())
                    return fail("dock 단계는 현재 미션의 마지막 단계여야 합니다");
                const int dock = findDock();
                if (dock < 0)
                    return fail("충전 스테이션 위치가 등록되어 있지 않습니다");
                const std::string requested = step.value("location_id", std::string{});
                if (!requested.empty() && locations_[std::size_t(dock)].value("id", "dock") != requested)
                    return fail("dock 단계의 위치가 등록된 충전 스테이션과 다릅니다");
                explicitDock = true;
            } else {
                return fail("미션 단계 " + std::to_string(i + 1) + "의 " + type +
                            " executor가 현재 로봇에 연결되어 있지 않습니다");
            }
        }
    } else {
        steps = waypoints_;
    }
    if (!steps.is_array() || steps.empty())
        return fail("미션에 실행 가능한 navigate 단계가 없습니다");

    shalom_interfaces::msg::MissionPlan plan;
    plan.created_at = now();
    plan.revision = mission ? mission->value("revision", uint64_t{1})
                            : (missionPlanRevision_ == 0 ? ++missionPlanRevision_ : missionPlanRevision_);
    plan.map_id = mapId_.empty() ? "live" : mapId_;
    plan.mission_id = mission ? mission->value("id", plan.map_id + ":" + std::to_string(plan.revision))
                              : plan.map_id + ":" + std::to_string(plan.revision);

    for (std::size_t i = 0; i < steps.size(); ++i) {
        const auto &point = steps[i];
        if (!point.is_object() || !point.contains("x") || !point.contains("y") ||
            !point["x"].is_number() || !point["y"].is_number()) {
            return fail("점검포인트 " + std::to_string(i + 1) + "의 좌표가 올바르지 않습니다");
        }
        shalom_interfaces::msg::MissionWaypoint waypoint;
        waypoint.waypoint_id = point.value("id", "P" + std::to_string(i + 1));
        waypoint.target_pose.header.frame_id = mapFrame_;
        waypoint.target_pose.header.stamp = now();
        waypoint.target_pose.pose.position.x = point["x"].get<double>();
        waypoint.target_pose.pose.position.y = point["y"].get<double>();
        tf2::Quaternion q;
        q.setRPY(0.0, 0.0, point.value("theta", 0.0));
        waypoint.target_pose.pose.orientation.x = q.x();
        waypoint.target_pose.pose.orientation.y = q.y();
        waypoint.target_pose.pose.orientation.z = q.z();
        waypoint.target_pose.pose.orientation.w = q.w();
        waypoint.operation = shalom_interfaces::msg::MissionWaypoint::NAVIGATE_ONLY;
        plan.waypoints.push_back(std::move(waypoint));
    }

    const int dock = findDock();
    plan.return_to_dock = mission ? explicitDock : dock >= 0;
    plan.has_dock_approach = dock >= 0;
    if (dock >= 0) {
        const auto &location = locations_[std::size_t(dock)];
        if (!location.contains("x") || !location.contains("y") ||
            !location["x"].is_number() || !location["y"].is_number()) {
            return fail("충전 스테이션 접근 좌표가 올바르지 않습니다");
        }
        auto &waypoint = plan.dock_approach;
        waypoint.waypoint_id = location.value("id", std::string("dock"));
        waypoint.target_pose.header.frame_id = mapFrame_;
        waypoint.target_pose.header.stamp = now();
        waypoint.target_pose.pose.position.x = location["x"].get<double>();
        waypoint.target_pose.pose.position.y = location["y"].get<double>();
        tf2::Quaternion q;
        q.setRPY(0.0, 0.0, location.value("theta", 0.0));
        waypoint.target_pose.pose.orientation.x = q.x();
        waypoint.target_pose.pose.orientation.y = q.y();
        waypoint.target_pose.pose.orientation.z = q.z();
        waypoint.target_pose.pose.orientation.w = q.w();
        waypoint.operation = shalom_interfaces::msg::MissionWaypoint::NAVIGATE_ONLY;
    }
    return plan;
}

void BridgeNode::configureAndStartMission(const Envelope &request)
{
    if (mapTransitionUncertain_) {
        respond(request, false, err::kHardware, "지도 전환 결과가 불명확합니다. 내비게이션과 브리지를 재시작하십시오");
        return;
    }
    if (missionStartPending_ || !pendingMapId_.empty()) {
        respond(request, false, err::kBusy, "지도 전환 또는 미션 시작 요청을 처리 중입니다");
        return;
    }
    if (manualMode_) {
        respond(request, false, err::kMode, "자율 모드에서 미션을 시작하십시오");
        return;
    }
    publishSpeedLimit();
    if (!navigationSpeedApplied_) {
        respond(request, false, err::kBusy, "자율주행 속도 설정을 적용 중입니다");
        return;
    }
    if (estopActive()) {
        respond(request, false, err::kMode, "비상정지 상태입니다. 해제한 뒤 시작하십시오");
        return;
    }
    if (!missionConfigureClient_->service_is_ready() ||
        !missionControlClient_->service_is_ready()) {
        respond(request, false, err::kUnreachable, "Mission Manager가 준비되지 않았습니다");
        return;
    }
    if (navigationBusy()) {
        respond(request, false, err::kBusy, "개별 자율주행 목표가 끝난 뒤 미션을 시작하십시오");
        return;
    }
    std::string detail;
    const json *definition = nullptr;
    json selectedMission;
    const std::string missionId = request.p.value("mission_id", std::string{});
    if (!missionId.empty()) {
        const auto found = std::find_if(missions_.begin(), missions_.end(), [&missionId](const json &item) {
            return item.value("id", std::string{}) == missionId;
        });
        if (found == missions_.end() || found->value("archived", false)) {
            respond(request, false, err::kBadPayload, "저장되어 있지 않거나 보관된 미션입니다");
            return;
        }
        selectedMission = *found;
        definition = &selectedMission;
    }
    auto plan = makeMissionPlan(definition, &detail);
    if (!plan) {
        respond(request, false, err::kBadPayload, detail);
        return;
    }

    auto configure = std::make_shared<shalom_interfaces::srv::ConfigureMission::Request>();
    const std::string request_id = request.id.empty()
        ? "hmi-" + std::to_string(++rosRequestSequence_) : request.id;
    configure->request_id = request_id + ":configure";
    configure->operator_id = "hmi";
    configure->plan = *plan;
    const auto generation = ++missionStartGeneration_;
    const auto selectedMap = mapId_;
    missionStartPending_ = true;
    missionStartRequest_ = request;
    missionStartAt_ = std::chrono::steady_clock::now();
    missionConfigureRequestId_ = missionConfigureClient_->async_send_request(
        configure,
        [this, generation, selectedMap, request_id](
            rclcpp::Client<shalom_interfaces::srv::ConfigureMission>::SharedFuture future) {
            if (!missionStartPending_ || generation != missionStartGeneration_) return;
            missionConfigureRequestId_.reset();
            if (!server_.isConnected() || mapId_ != selectedMap || !pendingMapId_.empty() || manualMode_ || estopActive()) {
                finishMissionStart(false, err::kBusy, "미션 시작 전 로봇·지도 상태가 변경되었습니다");
                return;
            }
            const auto configured = future.get();
            if (!configured->accepted) {
                finishMissionStart(false, configured->reason_code.empty() ? err::kMode : configured->reason_code, configured->detail);
                return;
            }
            onMissionState(std::make_shared<shalom_interfaces::msg::MissionState>(configured->state));
            auto control = std::make_shared<shalom_interfaces::srv::MissionControl::Request>();
            control->request_id = request_id + ":start";
            control->operator_id = "hmi";
            control->mission_id = configured->state.mission_id;
            control->operation = shalom_interfaces::srv::MissionControl::Request::START;
            missionStartControlRequestId_ = missionControlClient_->async_send_request(
                control,
                [this, generation, selectedMap](
                    rclcpp::Client<shalom_interfaces::srv::MissionControl>::SharedFuture result) {
                    if (!missionStartPending_ || generation != missionStartGeneration_) return;
                    missionStartControlRequestId_.reset();
                    const auto started = result.get();
                    if (!server_.isConnected() || mapId_ != selectedMap || !pendingMapId_.empty()) {
                        requestSafetyStop("SAFETY_COMM_TIMEOUT_STOP", "미션 시작 중 관제·지도 상태 변경");
                        finishMissionStart(false, err::kBusy, "미션 시작 중 로봇·지도 상태가 변경되었습니다");
                        return;
                    }
                    onMissionState(std::make_shared<shalom_interfaces::msg::MissionState>(started->state));
                    if (started->accepted) linkMissionResumePending_ = true;
                    // READY 동안에는 안전·동작 권한을 기다릴 수 있다. 실제 RUNNING
                    // 전환은 onMissionState가 확인하고 그때 경로를 초기화한다.
                    finishMissionStart(started->accepted,
                        started->accepted ? std::string() : started->reason_code.empty() ? err::kMode : started->reason_code,
                        started->detail);
                }).request_id;
        }).request_id;
}

void BridgeNode::finishMissionStart(bool ok, const std::string &code, const std::string &detail)
{
    if (missionConfigureRequestId_) missionConfigureClient_->remove_pending_request(*missionConfigureRequestId_);
    if (missionStartControlRequestId_) missionControlClient_->remove_pending_request(*missionStartControlRequestId_);
    missionConfigureRequestId_.reset();
    missionStartControlRequestId_.reset();
    missionStartPending_ = false;
    ++missionStartGeneration_;
    if (missionStartRequest_) respond(*missionStartRequest_, ok, code, detail);
    missionStartRequest_.reset();
}

void BridgeNode::sendMissionControl(const Envelope &request, uint8_t operation)
{
    if (missionStartPending_ && (operation == shalom_interfaces::srv::MissionControl::Request::STOP ||
        operation == shalom_interfaces::srv::MissionControl::Request::PAUSE))
        finishMissionStart(false, err::kMode, "미션 시작 요청을 취소했습니다");
    if (!missionControlClient_->service_is_ready()) {
        respond(request, false, err::kUnreachable, "Mission Manager가 준비되지 않았습니다");
        return;
    }
    auto control = std::make_shared<shalom_interfaces::srv::MissionControl::Request>();
    control->request_id = (request.id.empty()
        ? "hmi-" + std::to_string(++rosRequestSequence_) : request.id) + ":control";
    control->operator_id = "hmi";
    control->mission_id = missionState_.mission_id;
    control->operation = operation;
    const auto linkGeneration = linkGeneration_;
    missionControlClient_->async_send_request(
        control,
        [this, request, operation, linkGeneration](rclcpp::Client<shalom_interfaces::srv::MissionControl>::SharedFuture future) {
            if (!server_.isConnected() || linkGeneration != linkGeneration_) return;
            const auto result = future.get();
            onMissionState(std::make_shared<shalom_interfaces::msg::MissionState>(result->state));
            if (result->accepted && (operation == shalom_interfaces::srv::MissionControl::Request::RESUME ||
                operation == shalom_interfaces::srv::MissionControl::Request::RETURN_DOCK))
                linkMissionResumePending_ = true;
            respond(request, result->accepted,
                    result->accepted ? std::string() : err::kMode, result->detail);
        });
}

void BridgeNode::pauseMissionForManualTakeover()
{
    if (!haveMissionState_ ||
        (missionState_.state != shalom_interfaces::msg::MissionState::RUNNING &&
         missionState_.state != shalom_interfaces::msg::MissionState::RETURNING &&
         missionState_.state != shalom_interfaces::msg::MissionState::READY))
        return;
    if (!missionControlClient_->service_is_ready()) {
        RCLCPP_ERROR(get_logger(), "수동 전환 중 Mission Manager에 일시정지를 요청하지 못했습니다");
        return;
    }
    auto control = std::make_shared<shalom_interfaces::srv::MissionControl::Request>();
    control->request_id = "hmi-manual-" + std::to_string(++rosRequestSequence_);
    control->operator_id = "hmi_manual_takeover";
    control->mission_id = missionState_.mission_id;
    control->operation = missionState_.state == shalom_interfaces::msg::MissionState::READY
        ? shalom_interfaces::srv::MissionControl::Request::STOP : shalom_interfaces::srv::MissionControl::Request::PAUSE;
    missionControlClient_->async_send_request(control);
}

void BridgeNode::onMissionState(
    const shalom_interfaces::msg::MissionState::SharedPtr message)
{
    // Service snapshots can arrive after a newer transient-local state report.
    // A manager restart may reset sequence, but has a newer source timestamp.
    if (haveMissionState_ && message->sequence != 0 && missionState_.sequence != 0 &&
        message->sequence < missionState_.sequence &&
        rclcpp::Time(message->stamp) <= rclcpp::Time(missionState_.stamp))
        return;
    const uint8_t previous = haveMissionState_ ? missionState_.state
                                               : shalom_interfaces::msg::MissionState::IDLE;
    if (message->state == shalom_interfaces::msg::MissionState::RUNNING &&
        (previous == shalom_interfaces::msg::MissionState::IDLE ||
         previous == shalom_interfaces::msg::MissionState::READY ||
         previous == shalom_interfaces::msg::MissionState::COMPLETED ||
         previous == shalom_interfaces::msg::MissionState::FAILED))
        resetTrail();
    missionState_ = *message;
    haveMissionState_ = true;
    const bool terminal = message->state == shalom_interfaces::msg::MissionState::IDLE ||
                          message->state == shalom_interfaces::msg::MissionState::COMPLETED ||
                          message->state == shalom_interfaces::msg::MissionState::FAILED;
    if (terminal && previous != message->state &&
        (previous != shalom_interfaces::msg::MissionState::IDLE ||
         message->state != shalom_interfaces::msg::MissionState::IDLE))
        clearPlan();
    publishWaypoints();
    publishMission();
    if (message->state == shalom_interfaces::msg::MissionState::COMPLETED &&
        previous != message->state) {
        sendEnvelope(makeEvent(kChLog, json{{"code", "MISSION_COMPLETE"}}));
    }
}

/// 등록된 위치에서 충전 스테이션을 찾는다. 없으면 -1.
int BridgeNode::findDock() const
{
    for (std::size_t i = 0; i < locations_.size(); ++i) {
        const auto &loc = locations_[i];
        if (loc.value("kind", std::string()) == "dock")
            return int(i);
    }
    return -1;
}

void BridgeNode::publishMission()
{
    const uint8_t state = haveMissionState_ ? missionState_.state
        : shalom_interfaces::msg::MissionState::IDLE;
    sendEnvelope(makePublish(kChMission,
                             json{{"state", missionStateName(state)},
                                  {"mission_id", haveMissionState_
                                      ? missionState_.mission_id : std::string()},
                                  {"index", haveMissionState_ ? missionState_.current_step : -1},
                                  {"total", haveMissionState_ ? missionState_.total_steps : 0},
                                  {"reason_code", haveMissionState_
                                      ? missionState_.reason_code : std::string()},
                                  {"detail", haveMissionState_ ? missionState_.detail : std::string()}}));
}






void BridgeNode::publishMarkers()
{
    sendEnvelope(makePublish(kChMarkers, json{{"markers", markers_}}));
}

void BridgeNode::clearPlan()
{
    lastPlanPoints_ = json::array();
    sendEnvelope(makePublish(kChPlan, json{{"points", lastPlanPoints_}}), true);
}

void BridgeNode::resetTrail()
{
    trailPending_.clear();
    trailHistory_.clear();
    trailHasLast_ = false;
    trailReset_ = true;
    // 재접속 시 보내는 스냅샷도 비어야 한다. 화면만 지우면 옛 경로가 되살아난다.
    if (server_.isConnected())
        sendEnvelope(makePublish(kChTrail,
                                 json{{"points", json::array()}, {"reset", true}}));
}

void BridgeNode::publishTrail()
{
    const bool connected = server_.isConnected();
    if (!connected)
        trailWasConnected_ = false;
    geometry_msgs::msg::TransformStamped tf;
    try {
        tf = tfBuffer_->lookupTransform(mapFrame_, baseFrame_, tf2::TimePointZero);
    } catch (const tf2::TransformException &) {
        return;
    }
    const double x = tf.transform.translation.x;
    const double y = tf.transform.translation.y;

    if (trailAwaitingLocalization_) {
        if (!trailNewPoseReceived_ ||
            std::hypot(x - trailExpectedX_, y - trailExpectedY_) > kTrailLocalizationMatchM)
            return;
        trailAwaitingLocalization_ = false;
        trailNewPoseReceived_ = false;
    }

    if (!trailHasLast_ || std::hypot(x - trailLastX_, y - trailLastY_) >= kTrailMinStepM) {
        trailPending_.emplace_back(x, y);
        trailHistory_.emplace_back(x, y);
        if (trailHistory_.size() > 4000)
            trailHistory_.erase(trailHistory_.begin(), trailHistory_.begin() +
                               (trailHistory_.size() - 4000));
        trailLastX_ = x;
        trailLastY_ = y;
        trailHasLast_ = true;
    }
    if (!connected) {
        trailPending_.clear();
        return;
    }
    if (!trailWasConnected_ || trailReset_) {
        json history = json::array();
        for (const auto &pt : trailHistory_)
            history.push_back({pt.first, pt.second});
        sendEnvelope(makePublish(kChTrail, json{{"points", history}, {"reset", true}}));
        trailPending_.clear();
        trailReset_ = false;
        trailWasConnected_ = true;
        return;
    }
    if (trailPending_.empty() && !trailReset_)
        return;

    json points = json::array();
    for (const auto &pt : trailPending_)
        points.push_back({pt.first, pt.second});
    sendEnvelope(makePublish(kChTrail, json{{"points", points}, {"reset", trailReset_}}), true);
    trailPending_.clear();
    trailReset_ = false;
}

namespace {

/// One PNG chunk: length, type, data, then CRC32 over type+data.
void pngChunk(std::string &out, const char type[4], const std::string &data)
{
    const auto be32 = [&out](std::uint32_t v) {
        out.push_back(char((v >> 24) & 0xFF));
        out.push_back(char((v >> 16) & 0xFF));
        out.push_back(char((v >> 8) & 0xFF));
        out.push_back(char(v & 0xFF));
    };
    be32(std::uint32_t(data.size()));
    const std::size_t crcStart = out.size();
    out.append(type, 4);
    out += data;
    const auto *from = reinterpret_cast<const Bytef *>(out.data() + crcStart);
    be32(std::uint32_t(crc32(crc32(0L, Z_NULL, 0), from, uInt(4 + data.size()))));
}

}  // namespace

std::string BridgeNode::encodeImagePng(const sensor_msgs::msg::Image &image)
{
    const int w = int(image.width);
    const int h = int(image.height);
    if (w <= 0 || h <= 0)
        return {};

    // 채널 수와 PNG 색 타입. 그 외 형식은 빈 문자열로 돌려보내 호출부가
    // 무슨 형식이었는지 조작자에게 말하게 한다 — 조용히 깨진 파일을 남기는
    // 것보다 낫다.
    int channels = 0;
    char colourType = 0;
    bool swapRb = false;
    if (image.encoding == "rgb8") {
        channels = 3; colourType = 2;
    } else if (image.encoding == "bgr8") {
        channels = 3; colourType = 2; swapRb = true;
    } else if (image.encoding == "mono8") {
        channels = 1; colourType = 0;
    } else {
        return {};
    }

    const std::size_t stride = image.step ? image.step : std::size_t(w) * channels;
    if (image.data.size() < stride * std::size_t(h))
        return {};

    // 필터 0(None) 한 가지만 쓴다. 행마다 최적 필터를 고르면 몇 퍼센트를 더
    // 줄이지만, 촬영은 정지 상태에서 한 장씩 하는 일이라 시간이 아니라
    // 단순함이 낫다.
    std::string raw;
    raw.reserve(std::size_t(h) * (std::size_t(w) * channels + 1));
    for (int y = 0; y < h; ++y) {
        raw.push_back('\0');
        const std::uint8_t *row = image.data.data() + std::size_t(y) * stride;
        if (!swapRb) {
            raw.append(reinterpret_cast<const char *>(row), std::size_t(w) * channels);
        } else {
            for (int x = 0; x < w; ++x) {
                const std::uint8_t *px = row + std::size_t(x) * 3;
                raw.push_back(char(px[2]));
                raw.push_back(char(px[1]));
                raw.push_back(char(px[0]));
            }
        }
    }

    // 지도와 달리 여기서는 Z_BEST_SPEED 를 쓰지 않는다. 이 파일은 NAS 로 가서
    // 점검 근거로 남으므로, 한 번 더 눌러 두는 편이 낫다.
    uLongf bound = compressBound(uLong(raw.size()));
    std::string deflated(bound, '\0');
    if (compress2(reinterpret_cast<Bytef *>(deflated.data()), &bound,
                  reinterpret_cast<const Bytef *>(raw.data()), uLong(raw.size()),
                  Z_DEFAULT_COMPRESSION) != Z_OK)
        return {};
    deflated.resize(bound);

    std::string ihdr;
    for (int v : {w, h}) {
        ihdr.push_back(char((v >> 24) & 0xFF));
        ihdr.push_back(char((v >> 16) & 0xFF));
        ihdr.push_back(char((v >> 8) & 0xFF));
        ihdr.push_back(char(v & 0xFF));
    }
    ihdr.push_back(8);           // bit depth
    ihdr.push_back(colourType);  // 2 = truecolour, 0 = greyscale
    ihdr.append(3, '\0');        // compression, filter, interlace

    std::string png("\x89PNG\r\n\x1a\n", 8);
    pngChunk(png, "IHDR", ihdr);
    pngChunk(png, "IDAT", deflated);
    pngChunk(png, "IEND", {});
    return png;
}

std::string BridgeNode::encodeGridPng(const nav_msgs::msg::OccupancyGrid &grid)
{
    const int w = int(grid.info.width);
    const int h = int(grid.info.height);
    if (w <= 0 || h <= 0 || grid.data.size() < std::size_t(w) * std::size_t(h))
        return {};

    // 8비트 그레이스케일, 필터 0. 데이터가 평평해서 필터를 써도 얻는 게 없다.
    std::string raw;
    raw.reserve(std::size_t(h) * (std::size_t(w) + 1));
    for (int y = h - 1; y >= 0; --y) {     // 행 뒤집기: 격자는 아래에서, 이미지는 위에서 시작
        raw.push_back('\0');
        const std::int8_t *row =
            reinterpret_cast<const std::int8_t *>(grid.data.data()) + std::size_t(y) * std::size_t(w);
        for (int x = 0; x < w; ++x) {
            const int v = row[x];
            // map_server 규약 — 저장한 .pgm 을 불러온 것과 같아 보이게 한다.
            raw.push_back(char(v < 0 ? 205 : (v >= 65 ? 0 : (v <= 25 ? 254 : 205))));
        }
    }

    uLongf bound = compressBound(uLong(raw.size()));
    std::string deflated(bound, '\0');
    if (compress2(reinterpret_cast<Bytef *>(deflated.data()), &bound,
                  reinterpret_cast<const Bytef *>(raw.data()), uLong(raw.size()),
                  Z_BEST_SPEED) != Z_OK)
        return {};
    deflated.resize(bound);

    std::string ihdr;
    for (int v : {w, h}) {
        ihdr.push_back(char((v >> 24) & 0xFF));
        ihdr.push_back(char((v >> 16) & 0xFF));
        ihdr.push_back(char((v >> 8) & 0xFF));
        ihdr.push_back(char(v & 0xFF));
    }
    ihdr.push_back(8);     // bit depth
    ihdr.push_back(0);     // colour type: greyscale
    ihdr.append(3, '\0');  // compression, filter, interlace

    std::string png("\x89PNG\r\n\x1a\n", 8);
    pngChunk(png, "IHDR", ihdr);
    pngChunk(png, "IDAT", deflated);
    pngChunk(png, "IEND", {});
    return png;
}

// ================= 자율주행 =================

bool BridgeNode::navigationBusy() const
{
    return navGoal_ || navGoalPending_ || navStatus_ == "paused" ||
           navStatus_ == "pausing" || navStatus_ == "canceling";
}

void BridgeNode::startNavigation(const Envelope &request, bool resume)
{
    if (mapTransitionUncertain_) {
        respond(request, false, err::kHardware, "지도 전환 결과가 불명확합니다. 내비게이션과 브리지를 재시작하십시오");
        return;
    }
    publishSpeedLimit();
    if (!request.p.contains("x") || !request.p.contains("y")
        || !request.p["x"].is_number() || !request.p["y"].is_number()
        || (request.p.contains("theta") && !request.p["theta"].is_number())
        || !std::isfinite(request.p["x"].get<double>())
        || !std::isfinite(request.p["y"].get<double>())
        || !std::isfinite(request.p.value("theta", 0.0))) {
        respond(request, false, err::kBadPayload, "목표 좌표가 올바르지 않습니다");
        return;
    }

    // Paused goals reserve their map and destination until resumed or canceled.
    if (navigationBusy() && !(resume && navStatus_ == "paused")) {
        respond(request, false, err::kBusy, "현재 목표 주행을 취소한 뒤 새 목표를 지정하십시오");
        return;
    }

    if (!navClient_->action_server_is_ready()) {
        respond(request, false, err::kUnreachable,
                "자율주행이 준비되지 않았습니다 (Nav2 응답 없음)");
        return;
    }
    if (!navigationSpeedApplied_) {
        respond(request, false, err::kBusy, "자율주행 속도 설정을 적용 중입니다");
        return;
    }

    if (!safetyCommandClient_->service_is_ready() || !authorityRequestClient_->service_is_ready()) {
        respond(request, false, err::kUnreachable, "안전·주행 권한 서비스가 준비되지 않았습니다");
        return;
    }
    const auto generation = ++navGeneration_;
    pendingGoto_ = request;
    pendingGotoAt_ = std::chrono::steady_clock::now();
    navGoalPoint_ = json{{"x", request.p["x"]}, {"y", request.p["y"]},
                         {"theta", request.p.value("theta", 0.0)}};
    navStatus_ = "accepting";
    navGoalPending_ = navPreparing_ = true;
    navPreparingResume_ = resume;
    navSafetyAccepted_ = navAuthorityAccepted_ = false;
    navReadyReason_ = "NAV_SAFETY_AUTHORITY_PENDING";
    navReadyDetail_ = "안전·주행 권한 승인 대기";
    navError_.clear();
    navDistance_ = navEta_ = navElapsed_ = navRecoveries_ = nullptr;
    auto safety = std::make_shared<shalom_interfaces::srv::SafetyCommand::Request>();
    safety->request_id = "hmi-nav-safety-" + std::to_string(++rosRequestSequence_);
    safety->operator_id = "hmi";
    safety->operation = shalom_interfaces::srv::SafetyCommand::Request::RESUME;
    auto authority = std::make_shared<shalom_interfaces::srv::AuthorityRequest::Request>();
    authority->request_id = "hmi-nav-authority-" + std::to_string(++rosRequestSequence_);
    authority->requester = "hmi_bridge";
    authority->operation = shalom_interfaces::srv::AuthorityRequest::Request::REQUEST_BASE;
    navSafetyRequestId_ = safetyCommandClient_->async_send_request(safety,
        [this, generation](rclcpp::Client<shalom_interfaces::srv::SafetyCommand>::SharedFuture future) {
            if (!navPreparing_ || generation != navGeneration_) return;
            navSafetyRequestId_.reset();
            try {
                const auto result = future.get();
                if (!result->accepted) {
                    finishNavigationPreparation(result->reason_code.empty() ? err::kMode : result->reason_code,
                        result->detail.empty() ? "안전 재개 요청이 거절되었습니다" : result->detail);
                    return;
                }
                navSafetyAccepted_ = true;
                tickNavigationPreparation();
            } catch (const std::exception &e) {
                finishNavigationPreparation(err::kUnreachable, std::string("안전 서비스 응답 실패: ") + e.what());
            }
        }).request_id;
    navAuthorityRequestId_ = authorityRequestClient_->async_send_request(authority,
        [this, generation](rclcpp::Client<shalom_interfaces::srv::AuthorityRequest>::SharedFuture future) {
            if (!navPreparing_ || generation != navGeneration_) return;
            navAuthorityRequestId_.reset();
            try {
                const auto result = future.get();
                if (!result->accepted) {
                    finishNavigationPreparation(result->reason_code.empty() ? err::kMode : result->reason_code,
                        result->detail.empty() ? "주행 권한 요청이 거절되었습니다" : result->detail);
                    return;
                }
                navAuthorityAccepted_ = true;
                tickNavigationPreparation();
            } catch (const std::exception &e) {
                finishNavigationPreparation(err::kUnreachable, std::string("주행 권한 응답 실패: ") + e.what());
            }
        }).request_id;
    publishNav();
}

void BridgeNode::clearNavigationPreparation()
{
    if (navSafetyRequestId_) safetyCommandClient_->remove_pending_request(*navSafetyRequestId_);
    if (navAuthorityRequestId_) authorityRequestClient_->remove_pending_request(*navAuthorityRequestId_);
    navSafetyRequestId_.reset();
    navAuthorityRequestId_.reset();
    navPreparing_ = false;
}

void BridgeNode::finishNavigationPreparation(const std::string &code, const std::string &detail)
{
    const bool resume = navPreparingResume_;
    clearNavigationPreparation();
    ++navGeneration_;
    navGoalPending_ = false;
    navStatus_ = resume ? "paused" : "rejected";
    navError_ = detail;
    navReadyReason_ = code;
    navReadyDetail_ = detail;
    if (!resume) navGoalPoint_ = nullptr;
    settleGoto(false, code, detail);
    publishNav();
}

void BridgeNode::tickNavigationPreparation()
{
    if (!navPreparing_) return;
    const auto wall = std::chrono::steady_clock::now();
    if (!server_.isConnected() || manualMode_ || estopActive()) {
        finishNavigationPreparation(err::kMode, "연결·주행 모드·비상정지 상태가 변경되었습니다");
        return;
    }
    const bool safetyReady = safetyReceived_ != std::chrono::steady_clock::time_point{} &&
        wall - safetyReceived_ <= std::chrono::seconds(1) && safetyState_ == "normal" && safetyMotionPermitted_;
    const bool authorityReady = authorityReceived_ != std::chrono::steady_clock::time_point{} &&
        wall - authorityReceived_ <= std::chrono::seconds(1) && motionAuthority_ == "base_active";
    if (wall - pendingGotoAt_ > kGoalAcceptTimeout) {
        finishNavigationPreparation(err::kUnreachable, navReadyDetail_.empty()
            ? "안전·주행 권한 확인 시간이 초과되었습니다" : navReadyDetail_ + ": 응답 시간 초과");
        return;
    }
    if (!navSafetyAccepted_ || !navAuthorityAccepted_) {
        navReadyReason_ = "NAV_SAFETY_AUTHORITY_PENDING";
        navReadyDetail_ = "안전·주행 권한 승인 대기";
    } else if (!safetyReady) {
        navReadyReason_ = "NAV_SAFETY_STATE_PENDING";
        navReadyDetail_ = "로봇 안전 상태 확인 대기";
    } else if (!authorityReady) {
        navReadyReason_ = "NAV_BASE_AUTHORITY_PENDING";
        navReadyDetail_ = "로봇 주행 권한 확인 대기";
    } else {
        const auto generation = navGeneration_;
        const bool resume = navPreparingResume_;
        clearNavigationPreparation();
        navReadyReason_.clear();
        navReadyDetail_.clear();
        if (!navigationSpeedApplied_ || !navClient_->action_server_is_ready() || !pendingMapId_.empty()) {
            navPreparing_ = true;
            finishNavigationPreparation(err::kBusy, "자율주행 준비 상태가 변경되었습니다");
            return;
        }
        linkHold_ = false;
        sendPreparedNavigation(generation, resume);
    }
}

void BridgeNode::sendPreparedNavigation(uint64_t generation, bool resume)
{
    if (!pendingGoto_ || generation != navGeneration_) return;
    const auto request = *pendingGoto_;

    NavigateToPose::Goal goal;
    goal.pose.header.frame_id = mapFrame_;
    goal.pose.header.stamp = now();
    goal.pose.pose.position.x = request.p["x"].get<double>();
    goal.pose.pose.position.y = request.p["y"].get<double>();

    tf2::Quaternion q;
    q.setRPY(0.0, 0.0, request.p.value("theta", 0.0));
    goal.pose.pose.orientation.x = q.x();
    goal.pose.pose.orientation.y = q.y();
    goal.pose.pose.orientation.z = q.z();
    goal.pose.pose.orientation.w = q.w();

    rclcpp_action::Client<NavigateToPose>::SendGoalOptions opts;

    opts.goal_response_callback = [this, generation, resume](NavGoalHandle::SharedPtr handle) {
        if (generation != navGeneration_) {
            if (handle)
                navClient_->async_cancel_goal(handle);
            return;
        }
        navGoalPending_ = false;
        if (!handle) {
            navError_ = "자율주행이 목표를 거부했습니다";
            navStatus_ = navStatus_ == "pausing" ? "paused" :
                         navStatus_ == "canceling" ? "canceled" :
                         resume ? "paused" : "rejected";
            if (navStatus_ != "paused")
                navGoalPoint_ = nullptr;
            clearPlan();
            settleGoto(false, err::kUnreachable, "자율주행이 목표를 거부했습니다");
            publishNav();
            return;
        }
        navGoal_ = handle;
        navGoalId_ = handle->get_goal_id();
        if (navStatus_ == "pausing" || navStatus_ == "canceling") {
            requestNavigationCancel();
            return;
        }
        navStatus_ = "navigating";
        if (!resume)
            resetTrail();
        settleGoto(true);
        publishNav();
    };

    opts.feedback_callback = [this, generation](NavGoalHandle::SharedPtr handle,
                                    const std::shared_ptr<const NavigateToPose::Feedback> fb) {
        if (generation != navGeneration_ || navStatus_ != "navigating" ||
            !handle || handle->get_goal_id() != navGoalId_)
            return;
        navDistance_ = fb->distance_remaining;
        navFeedbackAt_ = std::chrono::steady_clock::now();
        navElapsed_ = double(fb->navigation_time.sec) + double(fb->navigation_time.nanosec) * 1e-9;
        navRecoveries_ = fb->number_of_recoveries;

        // Nav2 leaves estimated_time_remaining at zero with the controllers we
        // run - it is filled in by some plugins and not others. Reporting the
        // zero would put "0 s" on the operator's screen for the whole drive,
        // which reads as "arriving now". Send null instead so the station can
        // show a dash, and only send a number when there is one.
        const double eta = double(fb->estimated_time_remaining.sec)
                           + double(fb->estimated_time_remaining.nanosec) * 1e-9;
        navEta_ = eta > 0.0 ? json(eta) : json(nullptr);
    };

    opts.result_callback = [this, generation](const NavGoalHandle::WrappedResult &result) {
        if (generation != navGeneration_ || result.goal_id != navGoalId_)
            return;
        switch (result.code) {
        case rclcpp_action::ResultCode::SUCCEEDED: navStatus_ = "succeeded"; break;
        case rclcpp_action::ResultCode::CANCELED:
            navStatus_ = navStatus_ == "pausing" ? "paused" : "canceled";
            break;
        default: navStatus_ = "failed"; break;
        }
        navGoal_.reset();
        navDistance_ = navStatus_ == "succeeded" ? json(0.0) : json(nullptr);
        navEta_ = nullptr;
        if (navStatus_ == "failed")
            navError_ = result.result && !result.result->error_msg.empty()
                ? result.result->error_msg : "목표 주행 실패";
        clearPlan();
        // 목표가 끝났다는 사실은 이벤트로도 한 번 보낸다. state/nav 는 손실을
        // 허용하는 스트림이라, 마지막 상태 한 프레임이 떨어지면 관제 화면에
        // 주행이 영영 끝나지 않은 것처럼 남는다.
        sendEnvelope(makeEvent(kChLog, json{{"code", navStatus_ == "paused" ? "NAV_PAUSED" : navResultCode(navStatus_)},
                                            {"goal", navGoalPoint_}}));
        if (navStatus_ != "paused")
            navGoalPoint_ = nullptr;
        publishNav();
    };

    navGoalPoint_ = json{{"x", goal.pose.pose.position.x},
                         {"y", goal.pose.pose.position.y},
                         {"theta", request.p.value("theta", 0.0)}};
    navStatus_ = "accepting";
    navDistance_ = nullptr;
    navEta_ = nullptr;
    navElapsed_ = nullptr;
    navRecoveries_ = nullptr;
    navError_.clear();
    navFeedbackAt_ = {};
    pendingGotoAt_ = std::chrono::steady_clock::now();
    navGoalPending_ = true;
    navClient_->async_send_goal(goal, opts);
}

void BridgeNode::stopNavigation(bool pause)
{
    if (!navigationBusy())
        return;
    if (navPreparing_) {
        clearNavigationPreparation();
        ++navGeneration_;
        navGoalPending_ = false;
    }
    navReadyReason_.clear();
    navReadyDetail_.clear();
    navStatus_ = pause ? "pausing" : "canceling";
    baseHoldPub_->publish(geometry_msgs::msg::Twist{});
    settleGoto(false, err::kMode, pause ? "목표 주행 일시정지 요청" : "목표 주행 취소 요청");
    clearPlan();
    if (navGoal_) {
        requestNavigationCancel();
    } else if (!navGoalPending_) {
        navStatus_ = pause ? "paused" : "canceled";
        if (!pause)
            navGoalPoint_ = nullptr;
    }
}

void BridgeNode::requestNavigationCancel()
{
    // A cancel ACK is not an action result. Keep holding zero until Nav2 has
    // terminated this particular goal; never cancel a mission's action goal.
    const auto generation = navGeneration_;
    navClient_->async_cancel_goal(navGoal_, [this, generation](auto response) {
        if (generation != navGeneration_ || !navGoal_)
            return;
        if (response->return_code != 0 || response->goals_canceling.empty()) {
            sendEnvelope(makeEvent(kChLog, json{{"code", "NAV_CANCEL_REJECTED"},
                {"level", "error"}, {"message", "주행 정지 확인 실패. 정지 상태를 유지합니다."}}));
        }
    });
}

void BridgeNode::settleGoto(bool ok, const std::string &code, const std::string &message)
{
    if (!pendingGoto_)
        return;
    respond(*pendingGoto_, ok, code, message);
    pendingGoto_.reset();
}

void BridgeNode::publishNav()
{
    // Goal acceptance must also time out when the simulation clock is paused.
    if (!navPreparing_ && pendingGoto_ && std::chrono::steady_clock::now() - pendingGotoAt_ > kGoalAcceptTimeout) {
        navError_ = "자율주행이 목표에 응답하지 않습니다";
        settleGoto(false, err::kUnreachable, "자율주행이 목표에 응답하지 않습니다");
        // A delayed acceptance must be canceled before any other motion is allowed.
        stopNavigation(false);
    }

    if (!server_.isConnected())
        return;

    const bool feedbackFresh = navStatus_ != "navigating" ||
        std::chrono::steady_clock::now() - navFeedbackAt_ < std::chrono::seconds(2);
    sendEnvelope(makePublish(kChNav,
                             json{{"status", navStatus_},
                                  {"goal", navGoalPoint_},
                                  {"distance_remaining_m", feedbackFresh ? navDistance_ : json(nullptr)},
                                  {"eta_s", navStatus_ == "navigating" && feedbackFresh ? navEta_ : json(nullptr)},
                                  {"elapsed_s", feedbackFresh ? navElapsed_ : json(nullptr)},
                                  {"recoveries", feedbackFresh ? navRecoveries_ : json(nullptr)},
                                  {"navigation_state", mapTransitionUncertain_ ? "unknown" : navigationReadiness()},
                                  {"localization_state", localizationReadiness()},
                                  {"error", navError_},
                                  {"ready_reason_code", navReadyReason_},
                                  {"ready_detail", navReadyDetail_},
                                  // 경유점 개념은 아직 이 노드에 없다. 필드를
                                  // 빼면 관제가 키 없음과 값 없음을 구분해야
                                  // 하므로 null 로 보낸다.
                                  {"current_waypoint_id", nullptr}}),
                 true);
}

// ================= 안전 =================

void BridgeNode::tickSafety()
{
    const auto wall = std::chrono::steady_clock::now();
    if (!navPreparing_ && (navStatus_ == "navigating" || navStatus_ == "accepting") &&
        (safetyReceived_ == std::chrono::steady_clock::time_point{} || wall - safetyReceived_ > std::chrono::seconds(1) ||
         authorityReceived_ == std::chrono::steady_clock::time_point{} || wall - authorityReceived_ > std::chrono::seconds(1)))
        stopNavigation(true);
    if (linkMissionResumePending_ && server_.isConnected() && haveMissionState_ &&
        (missionState_.state == shalom_interfaces::msg::MissionState::RUNNING ||
         missionState_.state == shalom_interfaces::msg::MissionState::RETURNING) &&
        safetyState_ == "normal" && safetyMotionPermitted_ && motionAuthority_ == "base_active" &&
        wall - safetyReceived_ <= std::chrono::seconds(1) && wall - authorityReceived_ <= std::chrono::seconds(1)) {
        linkHold_ = false;
        linkMissionResumePending_ = false;
    }
    if (pendingMapRequest_ && std::chrono::steady_clock::now() - pendingMapAt_ > std::chrono::seconds(10)) {
        mapTransitionUncertain_ = true;
        linkHold_ = true;
        requestSafetyStop("NAV_MAP_TRANSITION_TIMEOUT", "지도 전환 응답 시간 초과");
        finishMapTransition(false, "지도 전환 응답 시간이 초과되었습니다. 내비게이션과 브리지를 재시작하십시오");
    }
    tickNavigationPreparation();
    if (missionStartPending_ && std::chrono::steady_clock::now() - missionStartAt_ > std::chrono::seconds(3)) {
        linkHold_ = true;
        linkMissionResumePending_ = false;
        requestSafetyStop("MISSION_START_TIMEOUT", "미션 구성·시작 응답 시간 초과");
        pauseMissionForManualTakeover();
        finishMissionStart(false, err::kUnreachable, "미션 구성·시작 응답 시간이 초과되었습니다");
    }
    // 수동 모드인 동안 제자리 명령을 계속 내보낸다. 두 가지를 한꺼번에 한다 —
    // 로봇을 세워 두고, mux 에서 자율 출력이 선택되지 못하게 한다.
    //
    // 관제가 아니라 여기서 내보내는 이유는 링크다. 관제가 0 을 스트림하게
    // 하면 링크가 끊긴 순간 lease 가 만료되고, 수동 모드인데도 Nav2 가 로봇을
    // 몰기 시작한다. 모드를 아는 것은 이 노드이므로 여기서 잡는다.
    //
    // E-Stop 중에도 내보낸다. 멈추는 것은 안전 게이트가 하지만, 그 사이에
    // 자율 출력이 mux 에서 선택되어 있을 이유는 없다.
    if (linkHold_ || navPreparing_ || manualMode_ || estopEngaged_ || navStatus_ == "pausing" ||
        navStatus_ == "paused" || navStatus_ == "canceling")
        baseHoldPub_->publish(geometry_msgs::msg::Twist{});
}

bool BridgeNode::estopActive() const
{
    return estopEngaged_ || safetyState_ == "e_stop_latched";
}

void BridgeNode::requestBaseAuthority()
{
    if (!authorityRequestClient_->service_is_ready())
        return;
    auto request = std::make_shared<shalom_interfaces::srv::AuthorityRequest::Request>();
    request->request_id = "hmi-authority-" + std::to_string(++rosRequestSequence_);
    request->requester = "hmi_bridge";
    request->operation = shalom_interfaces::srv::AuthorityRequest::Request::REQUEST_BASE;
    authorityRequestClient_->async_send_request(request);
}

void BridgeNode::requestSafetyResume()
{
    if (!safetyCommandClient_->service_is_ready())
        return;
    auto request = std::make_shared<shalom_interfaces::srv::SafetyCommand::Request>();
    request->request_id = "hmi-safety-" + std::to_string(++rosRequestSequence_);
    request->operator_id = "hmi";
    request->operation = shalom_interfaces::srv::SafetyCommand::Request::RESUME;
    safetyCommandClient_->async_send_request(request);
}

void BridgeNode::publishSafety()
{
    const bool fresh = safetyReceived_ != std::chrono::steady_clock::time_point{} &&
        std::chrono::steady_clock::now() - safetyReceived_ <= std::chrono::seconds(1);
    sendEnvelope(makePublish(kChSafety,
                             json{{"estop", estopActive()},
                                  {"mode", manualMode_ ? "manual" : "auto"},
                                  {"state", safetyState_}, {"state_fresh", fresh},
                                  {"motion_permitted", fresh && safetyMotionPermitted_},
                                  {"reason_code", safetyReasonCode_}, {"detail", safetyDetail_}}));
}

void BridgeNode::requestSafetyStop(const std::string &reason, const std::string &detail)
{
    shalom_interfaces::msg::SafetyEvent event;
    event.stamp = now();
    event.sequence = ++rosRequestSequence_;
    event.event = shalom_interfaces::msg::SafetyEvent::REQUEST_STOP;
    event.source = "hmi_bridge";
    event.reason_code = reason;
    event.detail = detail;
    safetyEventPub_->publish(event);
    baseHoldPub_->publish(geometry_msgs::msg::Twist{});
}

// ================= 텔레메트리 =================

void BridgeNode::publishPose()
{
    if (mapTransitionUncertain_) return;
    geometry_msgs::msg::TransformStamped tf;
    try {
        tf = tfBuffer_->lookupTransform(mapFrame_, baseFrame_, tf2::TimePointZero);
    } catch (const tf2::TransformException &) {
        // 변환이 아직 없다. 로그를 매 주기 찍지 않는다 — 기동 직후에는 정상이다.
        return;
    }


    const rclcpp::Time sourceAt(tf.header.stamp, get_clock()->get_clock_type());
    const double sourceAge = (now() - sourceAt).seconds();
    if (sourceAt.nanoseconds() == 0 || sourceAge < -0.1 || sourceAge > 1.0 ||
        sourceAt.nanoseconds() == lastPoseAt_.nanoseconds())
        return;
    const double x = tf.transform.translation.x;
    const double y = tf.transform.translation.y;
    tf2::Quaternion q(tf.transform.rotation.x, tf.transform.rotation.y,
                      tf.transform.rotation.z, tf.transform.rotation.w);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(q.length2()) || q.length2() < 1e-12)
        return;
    q.normalize();
    double roll = 0, pitch = 0, yaw = 0;
    tf2::Matrix3x3(q).getRPY(roll, pitch, yaw);

    // Derive velocity from distinct, continuous source samples. The first
    // sample after startup or a clock/source interruption has no velocity.
    {
        const auto received = std::chrono::steady_clock::now();
        const double dt = lastPoseAt_.nanoseconds() == 0 ? 0.0 : (sourceAt - lastPoseAt_).seconds();
        const bool continuous = lastPoseReceived_ != std::chrono::steady_clock::time_point{} &&
            received - lastPoseReceived_ <= std::chrono::seconds(1);
        if (continuous && dt > 1e-3 && dt < 2.0) {
            const double v = std::hypot(x - lastPoseX_, y - lastPoseY_) / dt;
            double dth = yaw - lastPoseTheta_;
            while (dth > M_PI) dth -= 2 * M_PI;
            while (dth < -M_PI) dth += 2 * M_PI;
            const double w = std::abs(dth) / dt;
            speedLinear_ = 0.7 * speedLinear_ + 0.3 * v;
            speedAngular_ = 0.7 * speedAngular_ + 0.3 * w;
            lastOdomAt_ = sourceAt;
            lastOdomReceived_ = received;
        } else {
            speedLinear_ = speedAngular_ = 0.0;
            lastOdomAt_ = rclcpp::Time(0, 0, get_clock()->get_clock_type());
            lastOdomReceived_ = {};
        }
        lastPoseX_ = x;
        lastPoseY_ = y;
        lastPoseTheta_ = yaw;
        lastPoseAt_ = sourceAt;
        lastPoseReceived_ = received;
    }

    if (!server_.isConnected()) return;
    const bool odomFresh = motionFeedbackFresh();
    sendEnvelope(makePublish(kChPose,
                             json{{"speed", odomFresh ? json(speedLinear_) : json(nullptr)},
                                  {"yaw_rate", odomFresh ? json(speedAngular_) : json(nullptr)},
                                  {"moving", isMoving()},
                                  {"x", tf.transform.translation.x},
                                  {"y", tf.transform.translation.y},
                                  {"theta", yaw},
                                  {"frame", mapFrame_}},
                             ++seq_),
                 true);

}

void BridgeNode::markSeen(Sensor &sensor)
{
    const auto stamp = now();
    if (sensor.everSeen) {
        const double dt = (stamp - sensor.lastSeen).seconds();
        if (dt > 1e-6) {
            // 지수이동평균. 한 번의 간격만 보면 값이 심하게 튀어서 조작자가
            // 읽을 수 없다. 0.1 은 대략 최근 10 개 표본을 본다.
            const double hz = 1.0 / dt;
            sensor.measuredHz = sensor.measuredHz > 0.0
                                    ? 0.9 * sensor.measuredHz + 0.1 * hz
                                    : hz;
        }
    }
    sensor.lastSeen = stamp;
    sensor.everSeen = true;
}

/// 관제 포트를 연다. 이미 열려 있으면 아무것도 하지 않는다.
///
/// 실패 사유는 카탈로그의 LINK_PORT_BIND_FAIL 에 해당한다. 그 코드는 사람이
/// 해제해야 사라지는 것으로 분류돼 있어, 여기서도 한 번만 크게 알리고 이후는
/// 눌러서 찍는다 — 5 초마다 같은 줄이 쌓이면 다른 로그가 묻힌다.
bool BridgeNode::openControlPort()
{
    if (server_.isListening())
        return true;

    std::string err;
    // HMI 프로필에는 제어 포트 하나만 저장한다. 상태 확인 포트를 따로
    // 설정하게 하면 둘이 어긋나는 순간 목록이 영원히 빨갛게 보이므로,
    // 바로 다음 포트로 고정한다.
    if (port_ <= 0 || port_ >= 65535) {
        RCLCPP_ERROR(get_logger(), "관제 제어 포트가 올바르지 않습니다: %d", port_);
        return false;
    }
    if (server_.start(std::uint16_t(port_), 0, &err)) {
        RCLCPP_INFO(get_logger(), "관제 연결 대기 중 — TCP %d (제어·상태 확인)", port_);
        return true;
    }

    if (!bindFailed_) {
        bindFailed_ = true;
        RCLCPP_ERROR(get_logger(),
                     "관제 포트 %d 를 열지 못했습니다: %s. 5 초마다 다시 시도합니다 "
                     "(LINK_PORT_BIND_FAIL). 다른 브릿지가 떠 있는지 확인하십시오.",
                     port_, err.c_str());
    } else {
        RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 30000,
                             "관제 포트 %d 아직 열지 못함: %s", port_, err.c_str());
    }
    return false;
}

const char *BridgeNode::postureService(const std::string &posture)
{
    // 이름이 곧 서비스 이름이다. 목록을 여기 두는 이유는 관제가 보낸 문자열을
    // 그대로 서비스 이름으로 쓰지 않기 위해서다 — 그러면 임의의 서비스를
    // 부르게 할 수 있다.
    if (posture == "stand_up" || posture == "stand_down"
        || posture == "balance_stand" || posture == "recovery_stand"
        || posture == "damp")
        return "supported";
    return nullptr;
}

// 선행 조건 때문에 자세 전환을 거절할 때 쓴다. 응답은 명령을 보낸 화면만
// 보지만, 이벤트는 이력에 남는다 — 조작자가 버튼을 눌렀는데 아무 일도
// 일어나지 않은 것처럼 보이는 상황이 기록에 남지 않으면 나중에 짚을 수 없다.
void BridgeNode::refusePosture(const Envelope &request, const std::string &code,
                               const std::string &message)
{
    respond(request, false, code, message);
    sendEnvelope(makeEvent(kChLog, json{{"code", "BASE_POSTURE_BLOCKED"},
                                        {"level", "warn"},
                                        {"msg", message}}));
}

void BridgeNode::handleBasePosture(const Envelope &request)
{
    const std::string posture = request.p.value("posture", std::string());
    if (!postureService(posture)) {
        respond(request, false, err::kBadPayload,
                "알 수 없는 자세: " + posture);
        return;
    }

    if (estopActive()) {
        respond(request, false, err::kEstopEngaged,
                "비상정지 상태에서는 자세를 바꾸지 않습니다");
        return;
    }

    // 팔이 움직이는 중이면 받지 않는다. 팔이 펴진 채 앉으면 차체나 바닥에
    // 부딪힌다. 화면이 버튼을 잠그는 것과 별개로 판정은 로봇이 한다.
    if (motionAuthority_ == "arm_active" || motionAuthority_ == "arm_stopping") {
        refusePosture(request, err::kBusy,
                      "로봇팔이 동작 중입니다. 팔을 멈춘 뒤 다시 시도하십시오");
        return;
    }

    // 움직이는 중에 앉거나 힘을 빼면 넘어진다.
    if (posture == "stand_down" || posture == "damp") {
        // isMoving() 은 오도메트리가 끊긴 것도 "움직인다" 로 본다(정지를
        // 확인할 수 없으니 거절하는 편이 안전하다). 다만 그 이유를 "이동 중"
        // 이라고 말하면, 조작자는 멈춰 서 있는 로봇 앞에서 멈추라는 말을
        // 듣게 되고 다음에 무엇을 해야 하는지 알 수 없다.
        const bool odomStale = !motionFeedbackFresh();
        if (odomStale) {
            refusePosture(request, err::kHardware,
                          "주행 정보가 들어오지 않아 정지 상태를 확인할 수 없습니다");
            return;
        }
        if (isMoving()) {
            refusePosture(request, err::kMode,
                          "이동 중에는 이 자세로 바꾸지 않습니다. 정지한 뒤 시도하십시오");
            return;
        }
    }

    // damp 은 관절 힘을 빼는 것이라 서 있는 상태에서 누르면 주저앉는다.
    // 실수로 누르는 것을 막기 위해 확인 필드를 요구한다.
    if (posture == "damp" && !request.p.value("confirm", false)) {
        respond(request, false, err::kMode,
                "damp 은 로봇이 주저앉습니다. confirm: true 를 함께 보내십시오");
        return;
    }

    auto client = postureClients_[posture];
    const bool ready = client && client->service_is_ready();

    // 시뮬레이터에는 SportClient 서비스가 없다. 여기서 막아 버리면 관제 화면과
    // 연동 코드를 시뮬레이터에서 시험할 방법이 없으므로, 설정이 켜져 있으면
    // 로그에 남기고 상태만 갱신한다. 로봇이 실제로 움직인 것은 아니라는 사실을
    // 응답과 로그 양쪽에 남긴다.
    if (!ready && postureDryRun_) {
        basePosture_ = posture;
        RCLCPP_WARN(get_logger(), "[모의] 자세 전환 %s — 본체 드라이버 없음", posture.c_str());
        respond(request, true, std::string(), posture + " (모의)");
        publishBase();
        sendEnvelope(makeEvent(kChLog, json{{"code", "BASE_POSTURE_SIMULATED"},
                                            {"level", "warn"},
                                            {"msg", posture}}));
        return;
    }

    if (!ready) {
        const std::string msg = "본체 드라이버가 응답하지 않습니다 (" + posture + ")";
        respond(request, false, err::kHardware, msg);
        sendEnvelope(makeEvent(kChLog, json{{"code", "BASE_POSTURE_FAILED"},
                                            {"level", "error"},
                                            {"msg", msg}}));
        return;
    }

    // 응답은 비동기로 온다. 요청 봉투를 값으로 복사해 두는 이유는, 콜백이
    // 도는 시점에 원본이 이미 사라져 있기 때문이다.
    const Envelope pending = request;
    client->async_send_request(
        std::make_shared<std_srvs::srv::Trigger::Request>(),
        [this, pending, posture](
            rclcpp::Client<std_srvs::srv::Trigger>::SharedFuture future) {
            const auto result = future.get();
            if (result && result->success) {
                basePosture_ = posture;
                respond(pending, true, std::string(), posture);
                publishBase();
                sendEnvelope(makeEvent(kChLog, json{{"code", "BASE_POSTURE_CHANGED"},
                                                    {"level", "info"},
                                                    {"msg", posture}}));
            } else {
                const std::string msg =
                    result ? result->message : std::string("본체가 자세 전환을 거부했습니다");
                respond(pending, false, err::kHardware, msg);
                sendEnvelope(makeEvent(kChLog, json{{"code", "BASE_POSTURE_FAILED"},
                                                    {"level", "error"},
                                                    {"msg", msg}}));
            }
        });
}

void BridgeNode::publishBase()
{
    sendEnvelope(makePublish(kChBase, json{{"posture", basePosture_},
                                           {"motion_authority", motionAuthority_}}));
}

void BridgeNode::publishHealth()
{
    // 관제가 새로 붙으면 로봇이 들고 있던 목록을 한 번 밀어 준다. 예전에는
    // 관제가 설정을 보낼 때만 오갔던 탓에, 갓 접속한 화면에는 점검포인트도
    // 마커도 없는 빈 지도가 떴다 — 로봇은 알고 있는데 화면만 몰랐다.
    const bool connected = server_.isConnected();
    if (connected && !wasConnected_) {
        publishNavigationSpeed();
        publishMapCatalog();
        publishActiveMap();
        publishWaypoints();
        publishMissions();
        publishArmPosePresets();
        publishLocations();
        publishMarkers();
        // 지도도 다시 보낸다. 저장된 지도를 쓰면 map_server 가 한 번만
        // 발행하므로, 이것이 없으면 나중에 붙은 화면은 빈 지도를 본다.
        if (!lastMapPng_.empty())
            sendEnvelope(makePublish(kChMap, lastMapMeta_), false, lastMapPng_);
        if (!lastPlanPoints_.empty())
            sendEnvelope(makePublish(kChPlan, json{{"points", lastPlanPoints_}}), true);
        // 점검이 도는 중에 관제가 새로 붙을 수 있다. 상태를 안 보내면
        // 화면은 대기로 보고, 버튼이 "자율주행 시작" 인 채로 남는다.
        publishMission();
        // 자세도 바뀔 때만 보내는 채널이라 같은 문제가 있다. 없으면 갓 붙은
        // 화면의 자세 표시가 비어 있고, 조작자는 로봇이 서 있는지 앉아
        // 있는지를 화면에서 알 수 없다.
        publishBase();
    }
    wasConnected_ = connected;

    if (!connected)
        return;

    json list = json::array();
    for (const auto &sensor : sensors_) {
        const double age = (now() - sensor.lastSeen).seconds();
        // 기대 주기의 세 배를 놓치면 끊긴 것으로 본다. 한 번 놓친 것으로
        // 경고하면 조작자가 경고를 무시하는 법부터 배운다.
        const double stale = sensor.expectedHz > 0.0 ? 3.0 / sensor.expectedHz : 3.0;

        std::string state = "ok";
        std::string detail;
        if (!sensor.everSeen) {
            state = "lost";
            detail = "신호 없음";
        } else if (age > stale) {
            state = "lost";
            detail = "끊김";
        } else if (sensor.expectedHz > 0.0 && sensor.measuredHz < sensor.expectedHz * 0.6) {
            // 오고는 있는데 느리다. 조작자에게는 끊긴 것과 다른 상황이다 —
            // 화면은 나오는데 프레임이 흘러내리는 카메라가 여기에 해당한다.
            state = "degraded";
            detail = "주기 저하";
        }

        list.push_back(json{{"id", sensor.id},
                            {"name", sensor.name},
                            {"expected_hz", sensor.expectedHz},
                            {"actual_hz", sensor.everSeen ? sensor.measuredHz : 0.0},
                            {"last_seen_ms", sensor.everSeen ? json(int(age * 1000.0))
                                                             : json(nullptr)},
                            {"state", state},
                            {"detail", detail}});
    }

    json link{{"rx_bytes_per_s", server_.rxBytes()},
              {"tx_bytes_per_s", server_.txBytes()}};
    server_.resetByteCounters();

    sendEnvelope(makePublish(kChHealth, json{{"sensors", list}, {"link", link}}), true);
}

}  // namespace hmi_bridge
