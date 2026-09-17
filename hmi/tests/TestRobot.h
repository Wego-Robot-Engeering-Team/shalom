// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#pragma once

// UI unit tests need a RobotLink instance to paint MainWindow, but must not
// embed an alternative simulator. This fixture deliberately has no motion,
// map, timer, or robot behaviour: the real simulator is reached through the
// TCP bridge in simulation/.

#include "robot/RobotLink.h"

namespace hmi::test {

class TestRobot final : public hmi::robot::RobotLink {
public:
    using hmi::robot::RobotLink::RobotLink;

    void setCmdVel(double, double, double) override {}
    void requestGoal(double, double, double) override {}
    void cancelNav() override {}
    void setWaypoints(const QList<QVariantMap> &waypoints) override { waypoints_ = waypoints; }
    void setLocations(const QList<QVariantMap> &) override {}
    void setMarkers(const QList<QVariantMap> &) override {}
    void setBatteryPolicy(double, double) override {}
    QList<QVariantMap> waypoints() const override { return waypoints_; }

    void missionStart() override { mission_ = hmi::robot::MissionState::Running; }
    void missionPause() override { mission_ = hmi::robot::MissionState::Paused; }
    void missionResume() override { mission_ = hmi::robot::MissionState::Running; }
    void missionStop() override { mission_ = hmi::robot::MissionState::Idle; }
    hmi::robot::MissionState missionState() const override { return mission_; }

    void engageEstop() override { estop_ = true; }
    void releaseEstop() override { estop_ = false; }
    bool estopEngaged() const override { return estop_; }
    void setMode(hmi::robot::DriveMode mode) override { mode_ = mode; }
    hmi::robot::DriveMode mode() const override { return mode_; }

    void setArmJointGoal(const QList<double> &) override {}
    void setArmPreset(const QString &) override {}
    void stopArm() override {}

    bool isConnected() const override { return true; }
    QString describe() const override { return QStringLiteral("test robot"); }

private:
    QList<QVariantMap> waypoints_;
    hmi::robot::MissionState mission_ = hmi::robot::MissionState::Idle;
    hmi::robot::DriveMode mode_ = hmi::robot::DriveMode::Auto;
    bool estop_ = false;
};

}  // namespace hmi::test
