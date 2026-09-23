// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include <QElapsedTimer>
#include <QPixmap>
#include <QTest>

#include <limits>

#include "RobotDef.h"
#include "robot/Kinematics.h"
#include "robot/PoseCheck.h"
#include "widgets/Robot3DView.h"

using namespace hmi;

class TestRobotView : public QObject {
    Q_OBJECT

private slots:
    void endEffectorMarker_agreesWithTheReadout_data()
    {
        QTest::addColumn<QList<double>>("q");
        QTest::newRow("home") << QList<double>{robot::kArmHome.begin(), robot::kArmHome.end()};
        QTest::newRow("standby") << QList<double>{robot::kArmStandby.begin(), robot::kArmStandby.end()};
        QTest::newRow("stow") << QList<double>{robot::kArmStow.begin(), robot::kArmStow.end()};
        QTest::newRow("zero") << QList<double>{0, 0, 0, 0, 0, 0};
        QTest::newRow("arbitrary") << QList<double>{0.4, -1.3, -1.9, -1.0, -1.2, 0.9};
    }

    void endEffectorMarker_agreesWithTheReadout()
    {
        QFETCH(QList<double>, q);
        const robot::EePose p = robot::forwardKinematics(q);
        const QVector3D shown = robot::jointFrames(q).last().map(QVector3D(0, 0, 0));
        const QVector3D read(float(p.x), float(p.y), float(p.z));
        QVERIFY2(double((shown - read).length()) < 1e-4, "end-effector pose drift");
    }

    void presets_raiseNoWarning_data()
    {
        QTest::addColumn<QList<double>>("joints");
        QTest::newRow("home") << QList<double>{robot::kArmHome.begin(), robot::kArmHome.end()};
        QTest::newRow("standby") << QList<double>{robot::kArmStandby.begin(), robot::kArmStandby.end()};
        QTest::newRow("stow") << QList<double>{robot::kArmStow.begin(), robot::kArmStow.end()};
    }

    void presets_raiseNoWarning()
    {
        QFETCH(QList<double>, joints);
        const auto w = robot::checkArmPose(joints);
        QVERIFY2(w.isEmpty(), qPrintable(QStringLiteral("[%1] %2").arg(w.severity, w.text)));
    }

    void proceduralGeometry_paintsQuickly()
    {
        ui::Robot3DView view;
        view.resize(640, 480);
        view.setArmJoints({robot::kArmHome.begin(), robot::kArmHome.end()});
        QVERIFY(!view.grab().isNull());

        constexpr int kFrames = 20;
        qint64 best = std::numeric_limits<qint64>::max();
        for (int i = 0; i < kFrames; ++i) {
            QElapsedTimer timer;
            timer.start();
            const QPixmap px = view.grab();
            best = qMin(best, timer.nsecsElapsed());
            QVERIFY(!px.isNull());
        }

#ifdef NDEBUG
        constexpr double kBudgetMs = 16.0;
#else
        constexpr double kBudgetMs = 40.0;
#endif
        QVERIFY2(double(best) / 1e6 < kBudgetMs,
                 qPrintable(QStringLiteral("3D pose view took %1 ms").arg(double(best) / 1e6)));
    }
};

QTEST_MAIN(TestRobotView)
#include "test_robotview.moc"
