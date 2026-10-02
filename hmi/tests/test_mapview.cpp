// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

// 지도 오버레이. 조작자가 지도에서 무엇을 짚을 수 있는지가 여기 걸려 있다.

#include <cmath>
#include <QGraphicsScene>
#include <QGraphicsPathItem>
#include <QSignalSpy>
#include <QTest>

#include "mapview/MapItems.h"
#include "mapview/MapView.h"

using namespace hmi;
using namespace hmi::map;

namespace {

MapInfo testMap()
{
    MapInfo info;
    info.mapId = QStringLiteral("t");
    info.width = 400;
    info.height = 200;
    info.resolution = 0.05;
    info.originX = -10.0;
    info.originY = -5.0;
    return info;
}

QImage testImage()
{
    QImage img(400, 200, QImage::Format_RGB32);
    img.fill(Qt::white);
    return img;
}

int countStations(const MapView &v)
{
    int n = 0;
    for (auto *it : v.scene()->items())
        if (dynamic_cast<StationMarker *>(it))
            ++n;
    return n;
}

}  // namespace

class TestMapView : public QObject {
    Q_OBJECT

private slots:

    void sameMapUpdatePreservesViewport()
    {
        MapView v;
        v.resize(640, 360);
        v.show();
        QTest::qWait(1);
        v.setMap(testMap(), testImage());
        v.scale(3, 3);
        v.centerOn(100, 80);
        const QTransform transform = v.transform();
        const QPointF center = v.mapToScene(v.viewport()->rect().center());
        v.setMap(testMap(), testImage());
        QCOMPARE(v.transform(), transform);
        QCOMPARE(v.mapToScene(v.viewport()->rect().center()), center);
    }

    void growingMapPreservesWorldViewportAndOverlays()
    {
        MapView v;
        v.resize(640, 360);
        v.show();
        QTest::qWait(1);
        const MapInfo original = testMap();
        v.setMap(original, testImage());
        v.scale(3, 3);
        v.centerOn(100, 80);
        double x = 0, y = 0;
        const QPointF center = v.mapToScene(v.viewport()->rect().center());
        original.toWorld(center.x(), center.y(), x, y);
        const double pixelsPerMetre = v.transform().m11() / original.resolution;
        v.setRobotPose(1, 2, 0);
        v.setGoal(3, 4, 0);
        v.setDraftGoal({{"x", 1.0}, {"y", 2.0}, {"theta", 0.0}});
        v.setWaypoints({QVariantMap{{"id", "a"}, {"x", 1.0}, {"y", 2.0}}});
        v.setTags({QVariantMap{{"id", 1}, {"x", 1.0}, {"y", 2.0}}});
        v.setTrail({QPointF(1, 2), QPointF(3, 4)});

        MapInfo next = original;
        next.width = 1000;
        next.height = 600;
        next.resolution = 0.025;
        next.originX = -12;
        next.originY = -7;
        QImage image(next.width, next.height, QImage::Format_RGB32);
        image.fill(Qt::white);
        v.setMap(next, image);
        const QPointF updated = v.mapToScene(v.viewport()->rect().center());
        double newX = 0, newY = 0;
        next.toWorld(updated.x(), updated.y(), newX, newY);
        QVERIFY(std::hypot(newX - x, newY - y) < 0.02);
        QVERIFY(std::abs(v.transform().m11() / next.resolution - pixelsPerMetre) < 1e-6);
        const QPointF expected = next.toScene(1, 2);
        for (auto *item : v.scene()->items()) {
            if (dynamic_cast<RobotMarker *>(item) || dynamic_cast<WaypointMarker *>(item) ||
                dynamic_cast<AprilTagMarker *>(item))
                QCOMPARE(item->pos(), expected);
            if (auto *path = dynamic_cast<QGraphicsPathItem *>(item); path && !path->path().isEmpty())
                QCOMPARE(path->path().pointAtPercent(0), expected);
        }
    }

    void differentMapFitsViewport()
    {
        MapView v;
        v.resize(640, 360);
        v.show();
        QTest::qWait(1);
        v.setMap(testMap(), testImage());
        const double fitted = v.transform().m11();
        v.scale(3, 3);
        MapInfo next = testMap();
        next.mapId = QStringLiteral("next");
        v.setMap(next, testImage());
        QVERIFY(std::abs(v.transform().m11() - fitted) < 1e-6);
    }

    void tagPlacementKeepsFacingDirection()
    {
        MapView v;
        v.resize(640, 360);
        v.setMap(testMap(), testImage());
        v.show();
        QTest::qWait(1);
        QSignalSpy placed(&v, &MapView::tagPlaced);
        const QPoint start = v.viewport()->rect().center();

        v.setMode(MapMode::AddTag);
        QTest::mousePress(v.viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(v.viewport(), start + QPoint(40, 0));
        QTest::mouseRelease(v.viewport(), Qt::LeftButton, Qt::NoModifier,
                            start + QPoint(40, 0));
        QCOMPARE(placed.size(), 1);
        QVERIFY(placed.at(0).at(3).toBool());
        QVERIFY(std::abs(placed.at(0).at(2).toDouble()) < 0.01);

        v.setMode(MapMode::AddTag);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QCOMPARE(placed.size(), 2);
        QVERIFY(!placed.at(1).at(3).toBool());
    }

    void tagOverlayShowsHeightAndFacingDirection()
    {
        MapView v;
        v.setMap(testMap(), testImage());
        v.setTags({QVariantMap{{"id", 5}, {"x", 1.0}, {"y", 1.0},
                               {"z", 1.2}, {"yaw", 1.57}}});
        bool found = false;
        for (auto *item : v.scene()->items()) {
            if (auto *tag = dynamic_cast<AprilTagMarker *>(item)) {
                found = true;
                QVERIFY(tag->toolTip().contains(QStringLiteral("1.20 m")));
                QVERIFY(tag->toolTip().contains(QStringLiteral("방향")));
            }
        }
        QVERIFY(found);
    }

    /// 충전소와 시작 위치가 지도에 떠야 한다. 순회 목록 밖이라 웨이포인트
    /// 오버레이로는 그려지지 않는 자리다.
    void stations_appearOnTheMap()
    {
        MapView v;
        v.setMap(testMap(), testImage());
        QCOMPARE(countStations(v), 0);
        v.setDock(QVariantMap{{"x", 1.0}, {"y", 1.0}});
        QCOMPARE(countStations(v), 1);
        v.setHome(QVariantMap{{"x", 2.0}, {"y", 2.0}});
        QCOMPARE(countStations(v), 2);
    }

    /// 지도보다 위치가 먼저 와도 떠야 한다. 둘 다 로봇이 보내는 것이라
    /// 도착 순서가 정해져 있지 않은데, 예전에는 순서가 어긋나면 마커가
    /// 영영 안 떴다.
    void stations_surviveArrivingBeforeTheMap()
    {
        MapView v;
        v.setDock(QVariantMap{{"x", 1.0}, {"y", 1.0}});
        QCOMPARE(countStations(v), 0);   // 지도가 없으니 아직 놓을 수 없다
        v.setMap(testMap(), testImage());
        QCOMPARE(countStations(v), 1);
    }

    /// 자리를 지우면 마커도 사라져야 한다. 남겨 두면 조작자는 지우지 못한
    /// 줄 알고, 로봇은 이미 잊은 자리를 화면만 가리키고 있게 된다.
    void stations_clearWhenTheLocationIsUnset()
    {
        MapView v;
        v.setMap(testMap(), testImage());
        v.setDock(QVariantMap{{"x", 1.0}, {"y", 1.0}});
        QCOMPARE(countStations(v), 1);
        v.setDock(QVariantMap{});
        QCOMPARE(countStations(v), 0);
    }

    /// 목록에서 고른 포인트가 지도에서도 표시돼야 한다.
    void selection_followsTheList()
    {
        MapView v;
        v.setMap(testMap(), testImage());
        v.setWaypoints({QVariantMap{{"id", QStringLiteral("A")}, {"x", 1.0}, {"y", 1.0}},
                        QVariantMap{{"id", QStringLiteral("B")}, {"x", 2.0}, {"y", 2.0}}});
        QVERIFY(v.selectedWaypoint().isEmpty());
        v.setSelectedWaypoint(QStringLiteral("B"));
        QCOMPARE(v.selectedWaypoint(), QStringLiteral("B"));
    }

    /// 순서를 바꾸거나 하나를 지우면 마커를 새로 만든다. 그때 고른 자리가
    /// 풀리면 조작자는 방금 짚은 것을 다시 찾아야 한다.
    void selection_survivesAListRebuild()
    {
        MapView v;
        v.setMap(testMap(), testImage());
        const QVariantMap a{{"id", QStringLiteral("A")}, {"x", 1.0}, {"y", 1.0}};
        const QVariantMap b{{"id", QStringLiteral("B")}, {"x", 2.0}, {"y", 2.0}};
        v.setWaypoints({a, b});
        v.setSelectedWaypoint(QStringLiteral("B"));
        v.setWaypoints({b, a});                       // 순서만 바꾼다
        QCOMPARE(v.selectedWaypoint(), QStringLiteral("B"));
    }

    /// 고른 포인트가 목록에서 사라지면 선택도 풀려야 한다. 없는 것을
    /// 가리키는 채로 두면 다음 선택이 조용히 무시된다.
    void selection_clearsWhenTheWaypointIsDeleted()
    {
        MapView v;
        v.setMap(testMap(), testImage());
        v.setWaypoints({QVariantMap{{"id", QStringLiteral("A")}, {"x", 1.0}, {"y", 1.0}},
                        QVariantMap{{"id", QStringLiteral("B")}, {"x", 2.0}, {"y", 2.0}}});
        v.setSelectedWaypoint(QStringLiteral("B"));
        v.setWaypoints({QVariantMap{{"id", QStringLiteral("A")}, {"x", 1.0}, {"y", 1.0}}});
        QVERIFY(v.selectedWaypoint().isEmpty());
    }

    void changingMapClearsPreviousWaypoints()
    {
        MapView v;
        v.setMap(testMap(), testImage());
        v.setWaypoints({QVariantMap{{"id", "old"}, {"x", 1.0}, {"y", 1.0}}});
        v.setSelectedWaypoint(QStringLiteral("old"));
        MapInfo next = testMap();
        next.mapId = QStringLiteral("next");
        v.setMap(next, testImage());
        QVERIFY(v.selectedWaypoint().isEmpty());
        for (auto *item : v.scene()->items())
            QVERIFY(!dynamic_cast<WaypointMarker *>(item));
    }

    void clearMap_removesPreviousRobotOverlays()
    {
        MapView v;
        v.setMap(testMap(), testImage());
        v.setTrail({QPointF(0, 0), QPointF(1, 1)});
        v.setPlan({QPointF(1, 1), QPointF(2, 2)});
        v.setGoal(2, 2, 0);
        v.setRobotPose(1, 1, 0);
        v.clearMap();
        QVERIFY(v.mapInfo() == nullptr);
        for (auto *item : v.scene()->items()) {
            if (auto *path = dynamic_cast<QGraphicsPathItem *>(item))
                QVERIFY(path->path().isEmpty());
            if (auto *goal = dynamic_cast<GoalMarker *>(item))
                QVERIFY(!goal->isVisible());
        }
    }
};

QTEST_MAIN(TestMapView)
#include "test_mapview.moc"
