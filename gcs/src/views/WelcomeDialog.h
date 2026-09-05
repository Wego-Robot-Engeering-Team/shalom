#pragma once

// Startup screen: identifies the operator before the control station opens.
//
// This is not decoration. Every privileged action - releasing the emergency
// stop, teaching a location, changing a setting - is written to the event log
// with the name entered here, which is what makes the log usable as an audit
// trail during the warranty period.
//
// On first launch the dialog switches to setup mode and requires an
// administrator password to be chosen. Running with no credential configured
// would leave every privileged action open, so it is not an option that can be
// skipped.

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace gcs::ui {

class WelcomeDialog : public QDialog {
    Q_OBJECT
public:
    explicit WelcomeDialog(QWidget *parent = nullptr);

private:
    void buildSetupMode();
    void buildSignInMode();
    void submitSetup();
    void submitSignIn();
    void showError(const QString &message);

    bool setupMode_ = false;

    QLineEdit *name_ = nullptr;
    QComboBox *role_ = nullptr;
    QLineEdit *password_ = nullptr;
    QLineEdit *passwordConfirm_ = nullptr;
    QLabel *passwordLabel_ = nullptr;
    QLabel *confirmLabel_ = nullptr;
    QLabel *error_ = nullptr;
    QPushButton *submit_ = nullptr;
};

}  // namespace gcs::ui
