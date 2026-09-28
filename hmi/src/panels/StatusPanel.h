// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#pragma once

// Base-driving state only. Robot-wide health belongs in the top bar and
// diagnostics; arm state belongs on the arm screen.

#include <QWidget>

class QLabel;

namespace hmi::ui {

class Card;
class Badge;

class StatusPanel : public QWidget {
    Q_OBJECT
public:
    explicit StatusPanel(QWidget *parent = nullptr);

    /// estop overrides mode: while engaged, the drive mode is not what the
    /// operator needs to see.
    void setMode(const QString &mode, bool estop);

    /// Base movement, in m/s. Shown as words first, number second - "is it
    /// moving" is the question, the speed is the detail.
    void setMotion(double speedMps);

    /// Map-frame pose. Degrees, so the operator never sees radians.
    void setPose(double x, double y, double thetaDeg);

private:
    /// One "label ....... value" row. Returns the value label to update.
    QLabel *addRow(const QString &label);

    Card *card_ = nullptr;
    Badge *mode_ = nullptr;
    QLabel *motion_ = nullptr;
    QLabel *pose_ = nullptr;
};

}  // namespace hmi::ui
