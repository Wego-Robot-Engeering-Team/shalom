// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary
#pragma once
#include <QWidget>
#include "robot/RobotTypes.h"

class QLabel;
class QPushButton;
namespace hmi::ui {
class Badge;
class StatusPanel : public QWidget {
    Q_OBJECT
public:
    explicit StatusPanel(QWidget *parent = nullptr);
    QPushButton *goalButton() const { return goal_; }
    QPushButton *startButton() const { return start_; }
    QPushButton *navPauseButton() const { return pause_; }
    QPushButton *navCancelButton() const { return cancel_; }
    void setMode(const QString &mode, bool estop);
    void setTelemetry(const robot::Telemetry &telemetry, bool connected);
    void setGoalState(const QString &state, const QVariantMap &goal, bool draft,
                      bool pending, bool mission, const QString &missionLabel, const QString &error);
    void setActions(bool select, bool start, bool pause, bool cancel, const QString &reason);
signals:
    void missionRequested();
private:
    QString safetyState_;
    bool safetyFresh_ = false;
    bool safetyMotionPermitted_ = false;
    QLabel *navigation_, *localization_, *target_, *distance_, *eta_, *reason_, *error_;
    QLabel *pose_, *linearVelocity_, *angularVelocity_, *elapsed_, *recoveries_;
    Badge *state_;
    QWidget *goalArea_;
    QPushButton *goal_, *start_, *pause_, *cancel_, *mission_;
    bool manual_ = false;
    bool estop_ = false;
    bool draft_ = false;
    bool missionActive_ = false;
};
}
