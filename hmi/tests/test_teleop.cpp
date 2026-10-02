// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include <QApplication>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QVBoxLayout>

#include "panels/TeleopPanel.h"

using hmi::ui::TeleopPanel;

namespace {
struct Fixture {
    QWidget window;
    QVBoxLayout layout{&window};
    TeleopPanel *panel = new TeleopPanel;
    QLineEdit *entry = new QLineEdit;

    Fixture() {
        layout.addWidget(panel);
        layout.addWidget(entry);
        window.show();
        window.activateWindow();
        window.setFocus();
        panel->setJogEnabled(true);
        QTest::qWait(10);
    }
};

bool hasMotion(const QSignalSpy &commands) {
    for (const auto &command : commands)
        if (command.at(0).toDouble() != 0 || command.at(1).toDouble() != 0 ||
            command.at(2).toDouble() != 0)
            return true;
    return false;
}
}  // namespace

class TestTeleop : public QObject {
    Q_OBJECT
private slots:
    void focusChangeStopsHeldKey() {
        Fixture f;
        QSignalSpy commands(f.panel, &TeleopPanel::cmdVel);
        QTest::keyPress(&f.window, Qt::Key_Up);
        QVERIFY(hasMotion(commands));
        commands.clear();
        f.entry->setFocus();
        QTest::keyRelease(f.entry, Qt::Key_Up);
        QTest::qWait(160);
        QVERIFY(!commands.isEmpty());
        QVERIFY(!hasMotion(commands));
        commands.clear();
        QTest::qWait(160);
        QVERIFY(commands.isEmpty());
    }

    void windowDeactivationStopsKeyboardAndMouse_data() {
        QTest::addColumn<bool>("mouse");
        QTest::newRow("keyboard") << false;
        QTest::newRow("mouse") << true;
    }

    void windowDeactivationStopsKeyboardAndMouse() {
        QFETCH(bool, mouse);
        Fixture f;
        QSignalSpy commands(f.panel, &TeleopPanel::cmdVel);
        if (mouse) {
            QPushButton *forward = nullptr;
            for (auto *button : f.panel->findChildren<QPushButton *>())
                if (button->text() == QStringLiteral("▲")) forward = button;
            QVERIFY(forward);
            QTest::mousePress(forward, Qt::LeftButton);
        } else {
            QTest::keyPress(&f.window, Qt::Key_Up);
        }
        QVERIFY(hasMotion(commands));
        commands.clear();
        QEvent event(QEvent::WindowDeactivate);
        QApplication::sendEvent(&f.window, &event);
        QTest::qWait(160);
        QVERIFY(!commands.isEmpty());
        QVERIFY(!hasMotion(commands));
        for (auto *button : f.panel->findChildren<QPushButton *>())
            QVERIFY(!button->isDown());
    }

    void applicationDeactivationStopsHeldKey() {
        Fixture f;
        QSignalSpy commands(f.panel, &TeleopPanel::cmdVel);
        QTest::keyPress(&f.window, Qt::Key_Up);
        QVERIFY(hasMotion(commands));
        commands.clear();
        QEvent event(QEvent::ApplicationDeactivate);
        QApplication::sendEvent(qApp, &event);
        QTest::qWait(160);
        QVERIFY(!commands.isEmpty());
        QVERIFY(!hasMotion(commands));
    }

    void hidingPanelStopsHeldKey() {
        Fixture f;
        QSignalSpy commands(f.panel, &TeleopPanel::cmdVel);
        QTest::keyPress(&f.window, Qt::Key_Up);
        QVERIFY(hasMotion(commands));
        commands.clear();
        f.panel->hide();
        QTest::qWait(160);
        QVERIFY(!commands.isEmpty());
        QVERIFY(!hasMotion(commands));
    }

    void typingDoesNotStartJog() {
        Fixture f;
        f.entry->setFocus();
        QSignalSpy commands(f.panel, &TeleopPanel::cmdVel);
        QTest::keyClick(f.entry, Qt::Key_Q);
        QTest::keyClick(f.entry, Qt::Key_Up);
        QTest::qWait(60);
        QVERIFY(!hasMotion(commands));
        QCOMPARE(f.entry->text(), QStringLiteral("q"));
    }

    void keysInAnotherWindowDoNotStartJog() {
        Fixture f;
        QWidget other;
        QSignalSpy commands(f.panel, &TeleopPanel::cmdVel);
        QTest::keyPress(&other, Qt::Key_Up);
        QTest::keyRelease(&other, Qt::Key_Up);
        QVERIFY(!hasMotion(commands));
    }
};

QTEST_MAIN(TestTeleop)
#include "test_teleop.moc"
