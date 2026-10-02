// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include <QTest>
#include <limits>

#include "panels/LocationPanel.h"

using namespace hmi::ui;

class LocationCaptureTest : public QObject {
    Q_OBJECT
private slots:
    void fixedLocationsRequireKnownStationarySpeed()
    {
        RobotSnapshot snapshot;
        snapshot.poseFresh = true;
        snapshot.speed = std::numeric_limits<double>::quiet_NaN();
        for (const auto &kind : {QStringLiteral("dock"), QStringLiteral("home")}) {
            const auto check = LocationPanel::checkCapture(snapshot, kind);
            QVERIFY(!check.allowed);
            QVERIFY(!check.reason.isEmpty());
        }
        // Waypoint capture intentionally works while moving and does not need
        // a velocity estimate, provided the source position is current.
        QVERIFY(LocationPanel::checkCapture(snapshot, QStringLiteral("inspection")).allowed);
        snapshot.speed = 0.0;
        QVERIFY(LocationPanel::checkCapture(snapshot, QStringLiteral("dock")).allowed);
        snapshot.speed = 0.2;
        QVERIFY(!LocationPanel::checkCapture(snapshot, QStringLiteral("dock")).allowed);
        QVERIFY(LocationPanel::checkCapture(snapshot, QStringLiteral("inspection")).allowed);
        snapshot.poseFresh = false;
        QVERIFY(!LocationPanel::checkCapture(snapshot, QStringLiteral("inspection")).allowed);
    }
};

QTEST_APPLESS_MAIN(LocationCaptureTest)
#include "test_location_capture.moc"
