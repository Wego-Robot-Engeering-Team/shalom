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

class QDoubleSpinBox;
class QTabWidget;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;

namespace gcs::ui {

class SettingsDialog : public QWidget {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

    /// Selects a tab by index. Used by the screenshot path during development.
    void setCurrentTab(int index);

signals:
    /// Raised when a change requires the stylesheet to be rebuilt.
    void appearanceChanged();

private:
    QWidget *buildConnectionTab();

    /// Lists this machine's usable IPv4 addresses, and warns when the bridge
    /// address is not on any of their subnets - the most common way an
    /// air-gapped install fails to connect.
    void refreshNetworkInfo();

    /// Attempts a TCP connection to the configured bridge and reports the
    /// outcome. Read-only: it opens a socket and closes it.
    void testConnection();
    QWidget *buildAppearanceTab();
    QWidget *buildOperationTab();
    QWidget *buildStorageTab();
    QWidget *buildSafetyTab();

    void load();

    QTabWidget *tabs_ = nullptr;
    QLineEdit *host_ = nullptr;
    QSpinBox *port_ = nullptr;
    QSpinBox *cameraPort_ = nullptr;
    QSlider *scale_ = nullptr;
    QLabel *scaleValue_ = nullptr;
    QDoubleSpinBox *linear_ = nullptr;
    QDoubleSpinBox *angular_ = nullptr;
    QLineEdit *logDir_ = nullptr;
    QSpinBox *retention_ = nullptr;
    QLineEdit *nasPath_ = nullptr;
    QLabel *interfaces_ = nullptr;
    QLabel *subnetWarning_ = nullptr;
    QLabel *testResult_ = nullptr;
    QPushButton *testButton_ = nullptr;
};

}  // namespace gcs::ui
