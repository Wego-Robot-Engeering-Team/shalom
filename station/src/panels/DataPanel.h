#pragma once

// Inspection history browser. Statement of work 2.2.7 [5] item 13.
//
// Browses and downloads previously captured images from the NAS share over the
// internal network. The control station only reads: the robot writes the
// evidence directly to the share, so an operator's machine being off never
// costs an inspection.
//
// Records whose metadata is incomplete are listed and marked rather than
// hidden. A gap in the evidence that nobody can see is worse than one that is
// obvious, because it surfaces at acceptance instead of during the run.

#include <QList>
#include <QWidget>

#include "data/InspectionRecord.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace gcs::ui {

class Badge;
class Card;
class PreviewView;

class DataPanel : public QWidget {
    Q_OBJECT
public:
    explicit DataPanel(QWidget *parent = nullptr);

    /// Directory to browse. Normally the mounted NAS share from settings.
    void setDirectory(const QString &path);

signals:
    /// Raised for anything the operator should see in the event log.
    void notice(const QString &severity, const QString &message);

private:
    void rescan();
    void applyFilter();
    void showRecord(const gcs::data::InspectionRecord &record);
    void downloadSelected();

    Card *card_ = nullptr;
    Badge *count_ = nullptr;
    QLabel *pathLabel_ = nullptr;
    QLineEdit *filter_ = nullptr;
    QListWidget *list_ = nullptr;
    QPushButton *download_ = nullptr;
    QLabel *warning_ = nullptr;

    PreviewView *preview_ = nullptr;
    QLabel *details_ = nullptr;

    QString directory_;
    QList<gcs::data::InspectionRecord> records_;
};

}  // namespace gcs::ui
