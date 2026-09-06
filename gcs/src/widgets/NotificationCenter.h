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

/// The list itself: a rounded, frameless card that hangs under the bell.
///
/// Painted rather than assembled from styled widgets. A top-level Qt::Popup
/// picks up the platform's window frame, which came out as a hard black rule
/// around the panel; painting the card means the border is the theme's own
/// hairline on every platform.
class NotificationPopup : public QWidget {
    Q_OBJECT
public:
    explicit NotificationPopup(const QList<Notification> &items);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    /// Height the list needs, capped so a long backlog scrolls instead of
    /// growing past the bottom of the screen.
    static constexpr int kMaxListHeight = 380;
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
