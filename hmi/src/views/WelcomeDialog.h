#pragma once

// Sign-in shown before the main window.
//
// It exists so that the audit trail has a name in it. The credential is a
// placeholder (see auth/Session.h) and this screen is not a security control.
//
// The footer repeats where the authority for stopping the robot actually lies,
// because this is the one screen every operator reads at the start of a shift.

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;

namespace hmi::ui {

class WelcomeDialog : public QDialog {
    Q_OBJECT
public:
    explicit WelcomeDialog(QWidget *parent = nullptr);

private:
    void submit();

    QLineEdit *id_ = nullptr;
    QLineEdit *password_ = nullptr;
    QLabel *error_ = nullptr;
    QPushButton *submit_ = nullptr;
};

}  // namespace hmi::ui
