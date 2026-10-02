// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include <QtTest>
#include <QAbstractButton>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QtMath>

#include "panels/ArmPanel.h"
#include "widgets/CatalogRow.h"
#include "widgets/IconButton.h"
#include "widgets/ValueSlider.h"

using namespace hmi;

class TestArm : public QObject {
    Q_OBJECT

    static QVariantMap pose(const QString &id, double joint = 0.1234567890123)
    {
        return {{"id", id}, {"name", id}, {"description", "Original description"},
                {"revision", 1}, {"archived", false},
                {"positions", QVariantList{joint, -0.4567890123456, 0.5678901234567,
                                           -1.2345678901234, 0.2345678901234, 0.3456789012345}}};
    }

    static ui::CatalogRow *row(ui::ArmPanel &panel, int index = 0)
    {
        auto *list = panel.findChild<QListWidget *>(QStringLiteral("SavedArmPosePresets"));
        return static_cast<ui::CatalogRow *>(list->itemWidget(list->item(index)));
    }

private slots:
    void deleteConfirmationRevalidatesCatalog_data()
    {
        QTest::addColumn<QString>("change");
        QTest::addColumn<int>("requests");
        QTest::newRow("disconnect") << QStringLiteral("disconnect") << 0;
        QTest::newRow("disconnect-reconnect-same-pose") << QStringLiteral("reconnect") << 0;
        QTest::newRow("changed-revision") << QStringLiteral("changed") << 0;
        QTest::newRow("deleted") << QStringLiteral("deleted") << 0;
        QTest::newRow("archived") << QStringLiteral("archived") << 0;
        QTest::newRow("unchanged-refresh") << QStringLiteral("unchanged") << 1;
    }

    void deleteConfirmationRevalidatesCatalog()
    {
        QFETCH(QString, change);
        QFETCH(int, requests);
        ui::ArmPanel panel;
        panel.setControlsEnabled(true);
        const auto original = pose(QStringLiteral("a"));
        panel.setPosePresets({original});
        auto *oldRow = row(panel);
        oldRow->editButton()->click();
        QPointer<ui::CatalogRow> previousRow(oldRow);
        QSignalSpy archives(&panel, &ui::ArmPanel::archivePosePresetRequested);
        bool rowDestroyed = false;
        QTimer::singleShot(0, &panel, [&] {
            if (change == QLatin1String("disconnect") || change == QLatin1String("reconnect")) {
                panel.setControlsEnabled(false);
                panel.clearReportedState();
                panel.setPosePresets({});
                if (change == QLatin1String("reconnect")) {
                    panel.setControlsEnabled(true);
                    panel.setPosePresets({original});
                }
            } else if (change == QLatin1String("deleted")) {
                panel.setPosePresets({});
            } else {
                auto refreshed = original;
                if (change == QLatin1String("changed"))
                    refreshed["revision"] = 2;
                else if (change == QLatin1String("archived"))
                    refreshed["archived"] = true;
                panel.setPosePresets({refreshed});
            }
            QTimer::singleShot(20, &panel, [&] {
                rowDestroyed = previousRow.isNull();
                for (auto *widget : QApplication::topLevelWidgets())
                    if (auto *box = qobject_cast<QMessageBox *>(widget))
                        box->button(QMessageBox::Yes)->click();
            });
        });
        oldRow->findChild<QPushButton *>(QStringLiteral("PoseRowDelete"))->click();
        QVERIFY(rowDestroyed);
        QCOMPARE(archives.size(), requests);
        if (requests) {
            QCOMPARE(archives.first().at(0).toString(), QStringLiteral("a"));
            QCOMPARE(archives.first().at(1).toULongLong(), quint64{1});
            QVERIFY(row(panel)->isPending());
        }
    }

    void openingEditorPreviewsItsDraft()
    {
        ui::ArmPanel panel;
        panel.setControlsEnabled(true);
        panel.setPosePresets({pose(QStringLiteral("a"), 0.3), pose(QStringLiteral("b"), 1.0)});
        auto *list = panel.findChild<QListWidget *>(QStringLiteral("SavedArmPosePresets"));
        list->setCurrentRow(1);
        auto *joint = panel.findChild<ui::ValueSlider *>(QStringLiteral("Joint1"));
        QVERIFY(qAbs(joint->command() - 1.0) < 0.004);
        row(panel)->editButton()->click();
        QCOMPARE(list->currentRow(), 0);
        QVERIFY(qAbs(joint->command() - 0.3) < 0.004);
        row(panel)->findChild<QDoubleSpinBox *>(QStringLiteral("PoseRowJoint1"))->setValue(30.0);
        QVERIFY(qAbs(joint->command() - qDegreesToRadians(30.0)) < 0.004);
        row(panel, 1)->editButton()->click();
        QVERIFY(qAbs(joint->command() - 1.0) < 0.004);
        row(panel)->editButton()->click();
        QVERIFY(qAbs(joint->command() - qDegreesToRadians(30.0)) < 0.004);
        list->setCurrentRow(0);
        QVERIFY(qAbs(joint->command() - qDegreesToRadians(30.0)) < 0.004);
    }

    void nameOnlySaveKeepsExactJointValues()
    {
        ui::ArmPanel panel;
        panel.setControlsEnabled(true);
        const auto original = pose(QStringLiteral("a"));
        panel.setPosePresets({original});
        row(panel)->editButton()->click();
        row(panel)->findChild<QLineEdit *>(QStringLiteral("PoseRowName"))->setText(QStringLiteral("Renamed"));
        // Reports may rebuild the editor while its draft is open.
        panel.setPosePresets({original});
        QSignalSpy writes(&panel, &ui::ArmPanel::updatePosePresetRequested);
        row(panel)->findChild<QPushButton *>(QStringLiteral("PoseRowSave"))->click();
        QCOMPARE(writes.size(), 1);
        QCOMPARE(writes.first().at(0).toMap().value("positions"), original.value("positions"));
        QCOMPARE(writes.first().at(1).toULongLong(), quint64{1});
    }

    void editingOneJointKeepsOtherJointPrecision()
    {
        ui::ArmPanel panel;
        panel.setControlsEnabled(true);
        const auto original = pose(QStringLiteral("a"));
        panel.setPosePresets({original});
        row(panel)->editButton()->click();
        row(panel)->findChild<QDoubleSpinBox *>(QStringLiteral("PoseRowJoint1"))->setValue(30.0);
        panel.setPosePresets({original});
        QSignalSpy writes(&panel, &ui::ArmPanel::updatePosePresetRequested);
        row(panel)->findChild<QPushButton *>(QStringLiteral("PoseRowSave"))->click();
        QCOMPARE(writes.size(), 1);
        const auto positions = writes.first().at(0).toMap().value("positions").toList();
        QCOMPARE(positions.first().toDouble(), qDegreesToRadians(30.0));
        for (int i = 1; i < positions.size(); ++i)
            QCOMPARE(positions.at(i), original.value("positions").toList().at(i));
    }
};

QTEST_MAIN(TestArm)
#include "test_arm.moc"
