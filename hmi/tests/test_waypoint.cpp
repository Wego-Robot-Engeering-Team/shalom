// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include <QtTest>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>

#include "panels/WaypointPanel.h"
#include "widgets/CatalogRow.h"
#include "widgets/IconButton.h"

using namespace hmi;

class TestWaypoint : public QObject {
    Q_OBJECT

    static QVariantMap point()
    {
        return {{"id", "a"}, {"name", "Original"}, {"description", "Keep this description"},
                {"x", 1.2345678901234}, {"y", -2.3456789012345}, {"theta", 6.406641307179586}};
    }

    static ui::CatalogRow *row(ui::WaypointPanel &panel)
    {
        auto *list = panel.findChild<QListWidget *>();
        return static_cast<ui::CatalogRow *>(list->itemWidget(list->item(0)));
    }

private slots:
    void nameOnlySaveKeepsExactCoordinates()
    {
        ui::WaypointPanel panel;
        const auto original = point();
        panel.setWaypoints({original});
        panel.setEditingEnabled(true);
        row(panel)->editButton()->click();
        row(panel)->findChild<QLineEdit *>(QStringLiteral("WaypointRowName"))->setText(QStringLiteral("Renamed"));
        panel.setWaypoints({original});
        QSignalSpy writes(&panel, &ui::WaypointPanel::updateRequested);
        row(panel)->findChild<QPushButton *>(QStringLiteral("WaypointRowSave"))->click();
        QCOMPARE(writes.size(), 1);
        auto saved = writes.first().at(1).toMap();
        for (const auto *field : {"x", "y", "theta", "description"})
            QCOMPARE(saved.value(QLatin1String(field)), original.value(QLatin1String(field)));
        saved.remove(QStringLiteral("_expected_point"));
        panel.setWaypoints({saved});
        QVERIFY(!row(panel)->isEditing());
        QVERIFY(!row(panel)->property("draftActive").toBool());
    }

    void editingOneCoordinateKeepsTheOthers()
    {
        ui::WaypointPanel panel;
        const auto original = point();
        panel.setWaypoints({original});
        panel.setEditingEnabled(true);
        row(panel)->editButton()->click();
        row(panel)->findChild<QDoubleSpinBox *>(QStringLiteral("WaypointRowX"))->setValue(9.5);
        panel.setWaypoints({original});
        QSignalSpy writes(&panel, &ui::WaypointPanel::updateRequested);
        row(panel)->findChild<QPushButton *>(QStringLiteral("WaypointRowSave"))->click();
        QCOMPARE(writes.size(), 1);
        const auto saved = writes.first().at(1).toMap();
        QCOMPARE(saved.value("x").toDouble(), 9.5);
        QCOMPARE(saved.value("y"), original.value("y"));
        QCOMPARE(saved.value("theta"), original.value("theta"));
    }

    void revertingAnEditRestoresOriginalPrecision()
    {
        ui::WaypointPanel panel;
        const auto original = point();
        panel.setWaypoints({original});
        panel.setEditingEnabled(true);
        row(panel)->editButton()->click();
        auto *x = row(panel)->findChild<QDoubleSpinBox *>(QStringLiteral("WaypointRowX"));
        const double displayed = x->value();
        x->setValue(9.5);
        x->setValue(displayed);
        QSignalSpy writes(&panel, &ui::WaypointPanel::updateRequested);
        row(panel)->findChild<QPushButton *>(QStringLiteral("WaypointRowSave"))->click();
        QCOMPARE(writes.size(), 1);
        QCOMPARE(writes.first().at(1).toMap().value("x"), original.value("x"));
    }

    void cancelThenEditUsesConfirmedCoordinates()
    {
        ui::WaypointPanel panel;
        const auto original = point();
        panel.setWaypoints({original});
        panel.setEditingEnabled(true);
        row(panel)->editButton()->click();
        row(panel)->findChild<QDoubleSpinBox *>(QStringLiteral("WaypointRowX"))->setValue(9.5);
        row(panel)->findChild<QPushButton *>(QStringLiteral("WaypointRowCancel"))->click();
        row(panel)->editButton()->click();
        QSignalSpy writes(&panel, &ui::WaypointPanel::updateRequested);
        row(panel)->findChild<QPushButton *>(QStringLiteral("WaypointRowSave"))->click();
        QCOMPARE(writes.size(), 1);
        const auto saved = writes.first().at(1).toMap();
        for (const auto *field : {"x", "y", "theta"})
            QCOMPARE(saved.value(QLatin1String(field)), original.value(QLatin1String(field)));
    }
};

QTEST_MAIN(TestWaypoint)
#include "test_waypoint.moc"
