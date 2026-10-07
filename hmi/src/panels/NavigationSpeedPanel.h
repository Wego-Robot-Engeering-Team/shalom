// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#pragma once

#include <QWidget>
#include <array>

class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class QTimer;

namespace hmi::ui {

/// Edits the robot-owned speed setting shared by navigation and manual jog.
class NavigationSpeedPanel : public QWidget {
    Q_OBJECT
public:
    explicit NavigationSpeedPanel(QWidget *parent = nullptr, bool editRanges = false);
    void reset();
    void discardDraft();
    void setReportedLimits(double linear, double minimum, double maximum,
                           double angular, double angularMinimum, double angularMaximum,
                           bool autonomousApplied);
    void handleCommandResult(const QString &channel, bool ok, const QString &code,
                             const QString &message);

signals:
    void speedLimitsRequested(double linear, double angular);
    void speedRangesRequested(double minimum, double maximum,
                              double angularMinimum, double angularMaximum);

private:
    void refresh();
    void updateDirty();
    void restoreReported();
    void settlePending();
    QSlider *slider_ = nullptr;
    QDoubleSpinBox *value_ = nullptr;
    QSlider *angularSlider_ = nullptr;
    QDoubleSpinBox *angularValue_ = nullptr;
    QDoubleSpinBox *minimum_ = nullptr;
    QDoubleSpinBox *maximum_ = nullptr;
    QDoubleSpinBox *angularMinimum_ = nullptr;
    QDoubleSpinBox *angularMaximum_ = nullptr;
    QPushButton *apply_ = nullptr;
    QPushButton *revert_ = nullptr;
    QTimer *confirmationTimeout_ = nullptr;
    QLabel *reported_ = nullptr;
    QLabel *error_ = nullptr;
    QLabel *adjustment_ = nullptr;
    double limit_ = 0.0;
    double angularLimit_ = 0.0;
    double reportedMinimum_ = 0.10;
    double reportedMaximum_ = 0.60;
    double reportedAngularMinimum_ = 0.05;
    double reportedAngularMaximum_ = 0.80;
    bool editRanges_ = false;
    bool known_ = false;
    bool dirty_ = false;
    bool pending_ = false;
    bool pendingAccepted_ = false;
    bool pendingReported_ = false;
    bool autonomousApplied_ = false;
    std::array<double, 4> submitted_{};
};

}  // namespace hmi::ui
