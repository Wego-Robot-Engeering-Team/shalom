// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include <QApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTest>
#include <QtMath>

#include "panels/ArmPanel.h"
#include "panels/WaypointPanel.h"
#include "theme/Style.h"
#include "theme/Tokens.h"
#include "widgets/CatalogRow.h"
#include "widgets/IconButton.h"
#include "widgets/Robot3DView.h"
#include "widgets/ValueSlider.h"

using namespace hmi;

namespace {
QVariantMap waypoint(int index = 0)
{
    return {{"id", QStringLiteral("point-%1").arg(index)},
            {"name", QStringLiteral("Waypoint %1").arg(index)},
            {"x", 1.23456789}, {"y", -2.3456789}, {"theta", 0.12345678}};
}

QVariantMap pose(int index = 0)
{
    return {{"id", QStringLiteral("pose-%1").arg(index)},
            {"name", QStringLiteral("Pose %1").arg(index)},
            {"description", "Original description"}, {"revision", 1}, {"archived", false},
            {"positions", QVariantList{0.25, -0.785, 0.0, -2.356, 0.0, 1.571}}};
}

ui::CatalogRow *row(QListWidget *list, int index = 0)
{
    return static_cast<ui::CatalogRow *>(list->itemWidget(list->item(index)));
}

QListWidget *showPanel(QWidget *panel, bool arm, int width = 360)
{
    panel->setFixedSize(width, 760);
    if (arm) {
        auto *armPanel = static_cast<ui::ArmPanel *>(panel);
        armPanel->setControlsEnabled(true);
        armPanel->setPosePresets({pose()});
        panel->findChild<QTabWidget *>("ArmSections")->setCurrentIndex(1);
    } else {
        auto *waypointPanel = static_cast<ui::WaypointPanel *>(panel);
        waypointPanel->setEditingEnabled(true);
        waypointPanel->setWaypoints({waypoint()});
    }
    panel->show();
    panel->activateWindow();
    QTest::qWait(1);
    return panel->findChild<QListWidget *>(arm ? "SavedArmPosePresets" : QString{});
}
}

class TestSavedListsUx : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qApp->setStyleSheet(theme::buildQss());
    }

    void waypointEditorSelectsItsWaypoint()
    {
        ui::WaypointPanel panel;
        panel.setEditingEnabled(true);
        panel.setWaypoints({waypoint(), waypoint(1)});
        auto *list = panel.findChild<QListWidget *>();
        list->setCurrentRow(1);
        QSignalSpy selected(&panel, &ui::WaypointPanel::waypointSelected);
        row(list)->editButton()->click();
        QCOMPARE(list->currentRow(), 0);
        QCOMPARE(selected.size(), 1);
        QCOMPARE(selected.first().first().toString(), QStringLiteral("point-0"));
    }

    void collapsedArmDraftCannotExecuteSavedPose()
    {
        ui::ArmPanel panel;
        panel.setControlsEnabled(true);
        panel.setExecutionAvailable(true);
        panel.setArmState({0.0, -0.785, 0.0, -2.356, 0.0, 1.571}, 1.0, 1.0, "idle");
        panel.setPosePresets({pose(), pose(1)});
        auto *list = panel.findChild<QListWidget *>("SavedArmPosePresets");
        row(list)->editButton()->click();
        row(list)->findChild<QDoubleSpinBox *>("PoseRowJoint1")->setValue(35.0);
        row(list, 1)->editButton()->click();
        QVERIFY(!row(list)->isEditing());
        list->setCurrentRow(0);
        const auto preview = panel.findChild<ui::ValueSlider *>("Joint1")->command();
        QVERIFY(qAbs(preview - qDegreesToRadians(35.0)) < 0.004);
        QVERIFY(!row(list)->applyButton()->isEnabled());
        QCOMPARE(row(list)->findChild<QLabel *>("CatalogRowStatus")->text(), QStringLiteral("초안"));
        QSignalSpy goals(&panel, &ui::ArmPanel::jointGoal);
        row(list)->applyButton()->click();
        QCOMPARE(goals.size(), 0);
        row(list)->editButton()->click();
        row(list)->findChild<QPushButton *>("PoseRowCancel")->click();
        QVERIFY(row(list)->applyButton()->isEnabled());
        row(list)->applyButton()->click();
        QCOMPARE(goals.size(), 1);
        QCOMPARE(qvariant_cast<QList<double>>(goals.first().first()).first(), 0.25);
    }

    void refreshKeepsTextInput_data()
    {
        QTest::addColumn<bool>("arm");
        QTest::newRow("waypoint") << false;
        QTest::newRow("arm") << true;
    }

    void refreshKeepsTextInput()
    {
        QFETCH(bool, arm);
        ui::ArmPanel armPanel;
        ui::WaypointPanel waypointPanel;
        auto *list = showPanel(arm ? static_cast<QWidget *>(&armPanel) : &waypointPanel, arm);
        row(list)->editButton()->click();
        const QString field = arm ? "PoseRowName" : "WaypointRowName";
        auto *name = row(list)->findChild<QLineEdit *>(field);
        name->setText(QStringLiteral("Draft name"));
        name->setFocus();
        name->setSelection(2, 4);
        QTRY_VERIFY(name->hasFocus());
        if (arm)
            armPanel.setPosePresets({pose()});
        else
            waypointPanel.setWaypoints({waypoint()});
        name = row(list)->findChild<QLineEdit *>(field);
        QTRY_VERIFY(name->hasFocus());
        QCOMPARE(name->text(), QStringLiteral("Draft name"));
        QCOMPARE(name->selectionStart(), 2);
        QCOMPARE(name->selectedText(), QStringLiteral("aft "));
        QTest::keyClicks(name, "X");
        QCOMPARE(name->text(), QStringLiteral("DrXname"));
        name->setSelection(6, -4);
        if (arm)
            armPanel.setPosePresets({pose()});
        else
            waypointPanel.setWaypoints({waypoint()});
        name = row(list)->findChild<QLineEdit *>(field);
        QTRY_VERIFY(name->hasFocus());
        QCOMPARE(name->cursorPosition(), 2);
        QCOMPARE(name->selectionStart(), 2);
        QCOMPARE(name->selectedText(), QStringLiteral("Xnam"));
    }

    void refreshKeepsNumericInput_data()
    {
        refreshKeepsTextInput_data();
    }

    void refreshKeepsNumericInput()
    {
        QFETCH(bool, arm);
        ui::ArmPanel armPanel;
        ui::WaypointPanel waypointPanel;
        auto *list = showPanel(arm ? static_cast<QWidget *>(&armPanel) : &waypointPanel, arm);
        row(list)->editButton()->click();
        const QString field = arm ? "PoseRowJoint1" : "WaypointRowX";
        auto *spin = row(list)->findChild<QDoubleSpinBox *>(field);
        spin->setFocus();
        auto *input = spin->findChild<QLineEdit *>();
        input->setText(QStringLiteral("12."));
        input->setCursorPosition(3);
        const QString text = input->text();
        QTRY_VERIFY(spin->hasFocus() || input->hasFocus());
        if (arm)
            armPanel.setPosePresets({pose()});
        else
            waypointPanel.setWaypoints({waypoint()});
        spin = row(list)->findChild<QDoubleSpinBox *>(field);
        input = spin->findChild<QLineEdit *>();
        QTRY_VERIFY(spin->hasFocus() || input->hasFocus());
        QCOMPARE(input->text(), text);
        QCOMPARE(input->cursorPosition(), 3);
    }

    void refreshKeepsScrollPosition_data()
    {
        refreshKeepsTextInput_data();
    }

    void refreshKeepsScrollPosition()
    {
        QFETCH(bool, arm);
        ui::ArmPanel armPanel;
        ui::WaypointPanel waypointPanel;
        auto *list = showPanel(arm ? static_cast<QWidget *>(&armPanel) : &waypointPanel, arm);
        QList<QVariantMap> entries;
        for (int i = 0; i < 40; ++i)
            entries << (arm ? pose(i) : waypoint(i));
        const auto refresh = [&] {
            if (arm)
                armPanel.setPosePresets(entries);
            else
                waypointPanel.setWaypoints(entries);
        };
        refresh();
        list->scrollToItem(list->item(30), QAbstractItemView::PositionAtTop);
        QTest::qWait(1);
        const int previous = list->verticalScrollBar()->value();
        QVERIFY(previous > 0);
        refresh();
        QTest::qWait(1);
        QCOMPARE(list->verticalScrollBar()->value(), previous);
    }

    void editorsFitNarrowViewport_data()
    {
        QTest::addColumn<bool>("arm");
        QTest::addColumn<int>("width");
        QTest::addColumn<QString>("themeName");
        for (const auto &themeName : {QStringLiteral("light"), QStringLiteral("dark")}) {
            const auto add = [&](const char *name, bool arm, int width) {
                QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(QLatin1String(name), themeName)))
                    << arm << width << themeName;
            };
            add("waypoint-320", false, 320);
            add("waypoint-360", false, 360);
            add("arm-320", true, 320);
            add("arm-360", true, 360);
        }
    }

    void editorDoesNotGrowArmPanel_data()
    {
        QTest::addColumn<int>("height");
        QTest::newRow("regular") << 760;
        QTest::newRow("short") << 650;
    }

    void editorDoesNotGrowArmPanel()
    {
        QFETCH(int, height);
        ui::ArmPanel panel;
        auto *list = showPanel(&panel, true, 320);
        panel.setFixedHeight(height);
        QTest::qWait(1);
        auto *card = panel.findChild<QWidget *>("Card");
        auto *view = panel.findChild<ui::Robot3DView *>();
        QVERIFY(card && view);
        const auto minimum = panel.minimumSizeHint();
        const auto panelSize = panel.size();
        const auto cardBounds = card->geometry();
        const auto viewBounds = QRect(view->mapTo(&panel, QPoint(0, 0)), view->size());
        row(list)->editButton()->click();
        QTest::qWait(1);
        QCOMPARE(panel.minimumSizeHint().height(), minimum.height());
        QVERIFY(panel.minimumSizeHint().width() <= panel.width());
        QCOMPARE(panel.size(), panelSize);
        QCOMPARE(card->geometry(), cardBounds);
        QCOMPARE(QRect(view->mapTo(&panel, QPoint(0, 0)), view->size()), viewBounds);
    }

    void editorsFitNarrowViewport()
    {
        QFETCH(bool, arm);
        QFETCH(int, width);
        QFETCH(QString, themeName);
        theme::setTheme(themeName);
        qApp->setStyleSheet(theme::buildQss());
        ui::ArmPanel armPanel;
        ui::WaypointPanel waypointPanel;
        auto *panel = arm ? static_cast<QWidget *>(&armPanel) : &waypointPanel;
        auto *list = showPanel(panel, arm, width);
        const QString shotDirectory = qEnvironmentVariable("HMI_SAVED_LISTS_SHOT_DIR");
        const QString shotPrefix = QStringLiteral("%1-%2-%3")
            .arg(arm ? "arm" : "waypoint").arg(width).arg(themeName);
        if (!shotDirectory.isEmpty()) {
            QVERIFY(QDir().mkpath(shotDirectory));
            QVERIFY(panel->grab().save(QDir(shotDirectory).filePath(shotPrefix + "-saved.png")));
        }
        row(list)->editButton()->click();
        QTest::qWait(1);
        if (!shotDirectory.isEmpty())
            QVERIFY(panel->grab().save(QDir(shotDirectory).filePath(shotPrefix + "-editor.png")));
        auto *catalogRow = row(list);
        QCOMPARE(list->horizontalScrollBar()->maximum(), 0);
        QVERIFY(catalogRow->width() <= list->viewport()->width());
        for (auto *field : catalogRow->findChildren<QWidget *>()) {
            if (!field->isVisible() ||
                (!qobject_cast<QLineEdit *>(field) && !qobject_cast<QDoubleSpinBox *>(field) &&
                 !qobject_cast<QPushButton *>(field) && !qobject_cast<QLabel *>(field)))
                continue;
            const QPoint topLeft = field->mapTo(catalogRow, QPoint(0, 0));
            QVERIFY2(topLeft.x() >= 0 && topLeft.x() + field->width() <= catalogRow->width(),
                     qPrintable(QStringLiteral("%1 exceeds row width: %2 + %3 > %4")
                         .arg(field->objectName()).arg(topLeft.x()).arg(field->width()).arg(catalogRow->width())));
            QVERIFY2(topLeft.y() + field->height() <= catalogRow->height(),
                     qPrintable(QStringLiteral("%1 clipped below row: %2 + %3 > %4")
                         .arg(field->objectName()).arg(topLeft.y()).arg(field->height()).arg(catalogRow->height())));
        }
        if (arm) {
            list->scrollToBottom();
            QTest::qWait(1);
            auto *save = catalogRow->findChild<QPushButton *>("PoseRowSave");
            const QPoint savePosition = save->mapTo(list->viewport(), QPoint(0, 0));
            QVERIFY(savePosition.y() + save->height() <= list->viewport()->height());
            auto *preview = panel->findChild<QPushButton *>("ArmSavePreviewPose");
            auto *actual = panel->findChild<QPushButton *>("ArmSaveCurrentPose");
            auto *stop = panel->findChild<QPushButton *>("ArmStopButton");
            QVERIFY(preview && actual && stop);
            QVERIFY(actual->y() >= preview->geometry().bottom());
            QCOMPARE(actual->y(), stop->y());
            QVERIFY(actual->geometry().right() < stop->geometry().left());
            QVERIFY(preview->width() > actual->width());
            for (const auto *button : {preview, actual, stop}) {
                QVERIFY(button->width() >= button->fontMetrics().horizontalAdvance(button->text()) + 16);
                const QPoint position = button->mapTo(panel, QPoint(0, 0));
                QVERIFY(position.y() + button->height() <= panel->height());
            }
            if (!shotDirectory.isEmpty())
                QVERIFY(panel->grab().save(QDir(shotDirectory).filePath(shotPrefix + "-editor-actions.png")));
        }
        const int expandedHeight = catalogRow->height();
        panel->setFixedWidth(600);
        QTest::qWait(1);
        QVERIFY(row(list)->height() <= expandedHeight);
    }
};

QTEST_MAIN(TestSavedListsUx)
#include "test_saved_lists_ux.moc"
