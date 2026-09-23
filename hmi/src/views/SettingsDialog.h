// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#pragma once

// Settings window.
//
// Non-modal on purpose: an operator may want to raise the interface scale or
// check the bridge address while watching the robot, and a modal dialog would
// hide the map to do it.
//
// The safety tab is read-only. Emergency stop response, the communication-loss
// stop and the jog deadman are enforced by the robot's safety node (protocol
// section 4); presenting them as editable here would imply the control station
// can weaken them, which it cannot and must not appear to.

#include <QWidget>

#include "Config.h"

class QDoubleSpinBox;
class QCloseEvent;
class QTabWidget;
class QLabel;
class QLineEdit;
class QTreeWidget;
class QPushButton;
class QSlider;
class QSpinBox;

namespace hmi::ui {

class SettingsDialog : public QWidget {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    /// Selects a tab by index. Used by the screenshot path during development.
    void setCurrentTab(int index);
    /// Re-read saved settings before showing this non-modal window again.
    void reload();

    /// How many tabs there are, so a test can walk all of them. Hard-coding the
    /// count in the test left the safety tab unpainted when a sixth was added.
    int tabCount() const;

signals:
    /// The battery thresholds changed. The window must push them to the robot:
    /// a setting the robot never hears about is a number on a screen, not a
    /// rule the machine follows.
    void batteryPolicyChanged();
    /// Saved connection profiles changed. A deleted active profile must also
    /// clear the main window's live connection state.
    void robotProfilesChanged();
    /// Appearance is previewed immediately, but is not persisted until save.
    void appearancePreviewChanged(const QString &theme, double scale);

private:
    QWidget *buildConnectionTab();

    /// Redraws the list widget from the pending (not yet persisted) robots.
    void reloadRobotList();

    /// Lists this machine's usable IPv4 addresses, and warns when the bridge
    /// address is not on any of their subnets - the most common way an
    /// air-gapped install fails to connect.
    void refreshNetworkInfo();

    /// Attempts a TCP connection to the configured bridge and reports the
    /// outcome. Read-only: it opens a socket and closes it.
    void testConnection();
    QWidget *buildAppearanceTab();
    QWidget *buildOperationTab();
    QWidget *buildPowerTab();
    QWidget *buildStorageTab();
    QWidget *buildSafetyTab();
    QWidget *buildAboutTab();

    void load();
    void save();
    void discardChanges();
    void loadDefaults();
    void previewAppearance();

    void closeEvent(QCloseEvent *event) override;

    /// Keeps the two battery thresholds in a workable order. Departing below
    /// the return threshold means leaving the dock and turning straight back.
    void applyBatteryBounds();

    /// Says whether a configured directory is there and can be written to.
    /// A path that is only a typo looks exactly like a correct one until the
    /// day a log has to be exported or a photo fetched.
    void refreshPathStatus();

    QTabWidget *tabs_ = nullptr;
    QTreeWidget *robotList_ = nullptr;
    QPushButton *addRobotButton_ = nullptr;
    QLineEdit *name_ = nullptr;
    QLineEdit *host_ = nullptr;
    QSpinBox *port_ = nullptr;
    QSlider *scale_ = nullptr;
    QLabel *scaleValue_ = nullptr;
    QDoubleSpinBox *linear_ = nullptr;
    QDoubleSpinBox *angular_ = nullptr;
    QLineEdit *logDir_ = nullptr;
    QSpinBox *retention_ = nullptr;
    QSpinBox *returnPct_ = nullptr;
    QSpinBox *departPct_ = nullptr;
    QLineEdit *nasPath_ = nullptr;
    QLabel *interfaces_ = nullptr;
    QLabel *subnetWarning_ = nullptr;
    QLabel *testResult_ = nullptr;
    QPushButton *testButton_ = nullptr;
    QLabel *logDirStatus_ = nullptr;
    QLabel *nasStatus_ = nullptr;
    QPushButton *lightBtn_ = nullptr;
    QPushButton *darkBtn_ = nullptr;
    QList<hmi::RobotEntry> pendingRobots_;
    int pendingCurrentRobot_ = -1;
    bool loading_ = false;
};

}  // namespace hmi::ui
