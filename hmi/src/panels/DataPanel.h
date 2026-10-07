// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#pragma once

// Inspection history browser. Statement of work 2.2.7 [5] item 13.
//
// Browses and downloads previously captured images from the mounted NAS share.
// Both the robot and HMI must mount the same share; the robot writes originals
// there and the control station only reads them.
//
// Records whose metadata is incomplete are listed and marked rather than
// hidden. A gap in the evidence that nobody can see is worse than one that is
// obvious, because it surfaces at acceptance instead of during the run.

#include <QList>
#include <QElapsedTimer>
#include <QWidget>

#include "data/InspectionRecord.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QProcess;
class QTimer;

namespace hmi::ui {

class Badge;
class Card;
class PreviewView;

class DataPanel : public QWidget {
    Q_OBJECT
public:
    explicit DataPanel(QWidget *parent = nullptr);
    ~DataPanel() override;

    /// Directory to browse. Normally the mounted NAS share from settings.
    void setDirectory(const QString &path);
    void refresh();
    void refreshIfStale();
    bool isScanning() const { return !directory_.isEmpty() && (scanBusy_ || rescanRequested_); }
    QList<hmi::data::InspectionRecord> recordsForPoint(const QString &id) const;

signals:
    /// Raised for anything the operator should see in the event log.
    void notice(const QString &severity, const QString &message);
    void recordsChanged();

private:
    void rescan();
    void cancelScan();
    void finishScan(QProcess *process, quint64 generation, const QString &failure = {});
    void applyFilter();
    void showRecord(const hmi::data::InspectionRecord &record, bool refreshImage = true);
    void downloadSelected();

    Card *card_ = nullptr;
    Badge *count_ = nullptr;
    QLabel *pathLabel_ = nullptr;
    QLineEdit *filter_ = nullptr;
    QListWidget *list_ = nullptr;
    QPushButton *download_ = nullptr;
    QPushButton *refresh_ = nullptr;
    QLabel *warning_ = nullptr;

    PreviewView *preview_ = nullptr;
    QLabel *details_ = nullptr;

    QString directory_;
    QString lastScanError_;
    QList<hmi::data::InspectionRecord> records_;
    quint64 directoryGeneration_ = 0;
    quint64 previewGeneration_ = 0;
    QProcess *scanProcess_ = nullptr;
    QTimer *scanTimeout_ = nullptr;
    QTimer *refreshTimer_ = nullptr;
    QElapsedTimer cacheAge_;
    QByteArray scanOutput_;
    bool scanBusy_ = false;
    bool rescanRequested_ = false;
    bool downloadPending_ = false;
};

}  // namespace hmi::ui
