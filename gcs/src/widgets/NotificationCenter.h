#pragma once

// Notification bell for the top bar, and the list behind it.
//
// Toasts are for the moment something happens; they leave on their own so the
// screen does not fill with stale alerts. Anything that toasted is kept here,
// so an operator who was looking at the robot instead of the screen can still
// find out what they missed without searching the event log.
//
// The unread count is the point of the control. It is cleared by opening the
// list, not by time passing.

#include <QDateTime>
#include <QList>
#include <QWidget>

namespace gcs::ui {

/// One entry in the notification list.
struct Notification {
    QDateTime at;
    QString title;
    QString detail;
    QString severity;   ///< info | ok | warn | error | critical
};

/// Bell glyph with an unread count. Clicking opens the list below it.
class NotificationBell : public QWidget {
    Q_OBJECT
public:
    explicit NotificationBell(QWidget *parent = nullptr);

    void add(const Notification &n);

    /// Entries kept. Older ones are dropped: during an incident the recent
    /// ones are what the operator needs.
    static constexpr int kMaxKept = 50;

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    void openPopup();

    QList<Notification> items_;
    int unread_ = 0;
    bool hover_ = false;
    QWidget *popup_ = nullptr;
};

}  // namespace gcs::ui
