// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#pragma once

// Mission run monitoring and controls, beside the saved mission editor.
//
// It shows progress reported by the robot's active mission plan. Waypoints
// are reusable locations; their catalog order is not a mission order.

#include <QStringList>
#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;

namespace hmi::ui {

class Badge;
class Card;

class MissionPanel : public QWidget {
    Q_OBJECT
public:
    explicit MissionPanel(QWidget *parent = nullptr);

    void setProgress(const QString &missionName, int index, int total,
                     const QStringList &stepLabels);

    /// Robot-owned lifecycle, with distinct failure and safety-stop states.
    void setMissionState(const QString &state);

    /// Whether the charging station has a known pose. Without one there is
    /// nowhere to send the robot, and a button that quietly does nothing is
    /// worse than one that is visibly unavailable.
    void setDockKnown(bool known);

signals:
    void missionSelectionRequested();

    /// Pause keeps the robot's active plan and resume continues its step index.
    void missionPause();
    void missionResume();

    /// Stop ends the run and discards progress; selecting a mission starts anew.
    void missionStop();

    /// Send the robot back to the charging station. Always available: getting
    /// the robot out from under a train is not something to make conditional
    /// on what the run happens to be doing. During a run it pauses first, so
    /// resume still picks up where the robot left off.
    void returnToDock();

private:
    void refresh();

    /// The run button opens mission selection while idle, then pauses/resumes.
    void onRunClicked();

    /// Cancelling is not undoable and a run can be an hour of driving. Asked
    /// only when there is progress to lose; the emergency stop is the control
    /// for stopping in a hurry and it never asks.
    void confirmStop();

    Card *card_ = nullptr;
    Badge *state_ = nullptr;
    QProgressBar *bar_ = nullptr;
    QLabel *count_ = nullptr;
    QLabel *current_ = nullptr;
    QLabel *next_ = nullptr;

    /// One button for the run itself: start, then pause, then resume. Three
    /// labels on one control rather than three controls of which two are
    /// always greyed out.
    QPushButton *run_ = nullptr;

    /// Ending the run and sending the robot home are different things, so they
    /// get their own buttons in fixed places. Swapping one for the other made
    /// the row read as arbitrary.
    QPushButton *stop_ = nullptr;
    QPushButton *dock_ = nullptr;

    QString missionName_;
    QStringList stepLabels_;
    int index_ = -1;
    int total_ = 0;
    QString missionState_ = QStringLiteral("idle");
    bool dockKnown_ = false;
};

}  // namespace hmi::ui
