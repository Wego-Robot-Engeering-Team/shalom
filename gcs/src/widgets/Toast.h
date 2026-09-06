#pragma once

// Non-modal alert overlay.
//
// The statement of work (2.2.7 [5] item 3) requires a popup warning when a
// person is detected, an unmapped obstacle appears, communication drops or the
// battery runs low.
//
// Implemented as a non-modal overlay rather than a dialog, deliberately. A
// modal dialog would take focus and cover part of the window, and the one
// control that must never be covered or blocked is the emergency stop. A
// warning that prevents the operator from stopping the robot is worse than no
// warning at all.
//
// For the same reason the toasts appear along the bottom of the window, away
// from the emergency stop in the top right, and they never grab keyboard
// focus.

#include <QWidget>

class QTimer;

namespace gcs::ui {

/// One message. Owned by ToastHost; not created directly.
class Toast : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double appear READ appear WRITE setAppear)
public:
    Toast(const QString &title, const QString &detail, const QString &severity,
          QWidget *parent);

    double appear() const { return appear_; }
    void setAppear(double v);

    void dismiss();

signals:
    void dismissed(Toast *self);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    QString title_;
    QString detail_;
    QString severity_;
    double appear_ = 0.0;
    QTimer *life_ = nullptr;
};

/// Places toasts along the bottom of its parent and stacks them upward.
class ToastHost : public QObject {
    Q_OBJECT
public:
    explicit ToastHost(QWidget *host);

    /// severity is one of: info, ok, warn, error, critical.
    void show(const QString &title, const QString &detail, const QString &severity);

    /// Repositions after the host resizes.
    void relayout();

private:
    /// Beyond this many at once the screen is more alert than information.
    /// The oldest is dropped rather than queued: during an incident the newest
    /// message is the one that matters.
    static constexpr int kMaxVisible = 4;

    QWidget *host_ = nullptr;
    QList<Toast *> toasts_;
};

}  // namespace gcs::ui
