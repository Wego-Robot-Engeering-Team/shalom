#pragma once

// Event log panel. Statement of work 2.2.7 [5], item 3.
//
// Rows lead with the diagnostic code and a short title so the list stays
// scannable while the robot is moving. The full explanation sits behind the
// (i) affordance next to the code: hovering shows a summary, clicking opens a
// popup with the cause and the ordered remedial steps.
//
// Keeping the detail out of the row matters because this panel is read at a
// glance during operation; the operator needs to spot that something changed,
// not read a paragraph.

#include <QWidget>

#include "diag/CodeCatalog.h"

class QComboBox;
class QLineEdit;
class QListWidget;

namespace gcs::diag {
class LogStore;
struct LogEntry;
}

namespace gcs::ui {

class Badge;
class Card;

class EventLogPanel : public QWidget {
    Q_OBJECT
public:
    explicit EventLogPanel(gcs::diag::LogStore *store, QWidget *parent = nullptr);

    Card *card() const { return card_; }

private slots:
    void onAppended(const gcs::diag::LogEntry &e);
    void rebuild();
    void exportLog();

private:
    void addRow(const gcs::diag::LogEntry &e);
    void updateBadge();

    gcs::diag::LogStore *store_ = nullptr;
    Card *card_ = nullptr;
    QListWidget *list_ = nullptr;
    QComboBox *filter_ = nullptr;
    QLineEdit *search_ = nullptr;
    Badge *badge_ = nullptr;
};

}  // namespace gcs::ui
