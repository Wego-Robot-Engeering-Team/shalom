// 좌표 변환 테스트.
//
// 부호 하나가 틀리면 지도 클릭 목표점이 엉뚱한 곳으로 가고, 로봇이 실제로
// 그리로 이동한다. 육안으로는 "대충 맞아 보이는" 경우가 많아 반드시 수치로 잡는다.

#include <QTest>
#include <QtMath>

#include "mapview/MapInfo.h"

using namespace gcs::map;

class TestMapInfo : public QObject {
    Q_OBJECT

private:
    static MapInfo demo()
    {
        // 400x300 셀, 5cm/셀 → 20m x 15m. 원점을 중앙에 둔다.
        return *MapInfo::create(400, 300, 0.05, -10.0, -7.5);
    }

private slots:

    // ---- 검증 ----------------------------------------------------------
    void create_rejectsRotatedMap()
    {
        QString err;
        QVERIFY(!MapInfo::create(10, 10, 0.05, 0, 0, 0.3, {}, &err).has_value());
        QVERIFY(err.contains(QStringLiteral("v1 미지원")));
    }

    void create_rejectsBadResolution()
    {
        QVERIFY(!MapInfo::create(10, 10, 0.0, 0, 0).has_value());
        QVERIFY(!MapInfo::create(10, 10, -0.05, 0, 0).has_value());
    }

    void create_rejectsBadSize()
    {
        QVERIFY(!MapInfo::create(0, 10, 0.05, 0, 0).has_value());
        QVERIFY(!MapInfo::create(10, -1, 0.05, 0, 0).has_value());
    }

    // ---- 모서리 대응 ---------------------------------------------------
    void origin_mapsToSceneBottomLeft()
    {
        const auto mi = demo();
        // origin 은 격자 좌하단. 씬에서는 좌하단(= y 최대).
        const QPointF p = mi.toScene(-10.0, -7.5);
        QVERIFY(qFuzzyIsNull(p.x()));
        QCOMPARE(p.y(), 300.0);
    }

    void topRightCorner_mapsToSceneTopRight()
    {
        const auto mi = demo();
        const QPointF p = mi.toScene(-10.0 + 400 * 0.05, -7.5 + 300 * 0.05);
        QCOMPARE(p.x(), 400.0);
        QVERIFY(qFuzzyIsNull(p.y()));
    }

    void center_mapsToSceneCenter()
    {
        const auto mi = demo();
        const QPointF p = mi.toScene(0.0, 0.0);
        QCOMPARE(p.x(), 200.0);
        QCOMPARE(p.y(), 150.0);
    }

    // ---- 왕복 ----------------------------------------------------------
    void roundTrip_worldSceneWorld()
    {
        const auto mi = demo();
        const QList<QPointF> pts{{0, 0}, {3.21, -1.04}, {-9.9, 7.0}, {5.5, 5.5}, {-10.0, -7.5}};
        for (const auto &w : pts) {
            const QPointF s = mi.toScene(w.x(), w.y());
            double bx = 0, by = 0;
            mi.toWorld(s.x(), s.y(), bx, by);
            QVERIFY2(qAbs(bx - w.x()) < 1e-9 && qAbs(by - w.y()) < 1e-9,
                     qPrintable(QStringLiteral("왕복 불일치: (%1,%2) → (%3,%4)")
                                    .arg(w.x()).arg(w.y()).arg(bx).arg(by)));
        }
    }

    // ---- y 축 방향 -----------------------------------------------------
    void yAxis_isFlipped()
    {
        const auto mi = demo();
        // world 에서 위로 가면 씬에서는 아래로 가야 한다(= sy 감소).
        const QPointF lo = mi.toScene(0.0, 0.0);
        const QPointF hi = mi.toScene(0.0, 5.0);
        QVERIFY2(hi.y() < lo.y(), "world +y 가 씬에서 -y 로 매핑되어야 한다");
        QCOMPARE(lo.y() - hi.y(), 100.0);   // 5m / 0.05 = 100 셀
    }

    void xAxis_isNotFlipped()
    {
        const auto mi = demo();
        QVERIFY(mi.toScene(5.0, 0.0).x() > mi.toScene(0.0, 0.0).x());
    }

    // ---- 회전 부호 -----------------------------------------------------
    void rotation_northPointsUpOnScreen()
    {
        // world +90도(북쪽)를 아이템 회전으로 바꾸면, Qt 가 로컬 +x 를
        // 씬의 -y(화면 위)로 보내야 한다.
        const double theta = M_PI / 2;
        const double a = qDegreesToRadians(MapInfo::thetaToItemRotation(theta));
        const double vx = std::cos(a);
        const double vy = std::sin(a);   // Qt 가 (1,0) 을 보내는 곳
        QVERIFY(qAbs(vx) < 1e-9);
        QVERIFY2(qAbs(vy + 1.0) < 1e-9, "씬 -y = 화면 위 여야 한다");
    }

    void rotation_eastPointsRight()
    {
        const double a = qDegreesToRadians(MapInfo::thetaToItemRotation(0.0));
        QVERIFY(qAbs(std::cos(a) - 1.0) < 1e-9);
        QVERIFY(qAbs(std::sin(a)) < 1e-9);
    }

    void rotation_roundTrip()
    {
        for (double th : {0.0, 0.5, -1.2, M_PI / 2, -M_PI / 2, 3.0}) {
            const double deg = MapInfo::thetaToItemRotation(th);
            QVERIFY(qAbs(MapInfo::itemRotationToTheta(deg) - th) < 1e-12);
        }
    }

    // ---- 스케일 --------------------------------------------------------
    void metersToPx_matchesResolution()
    {
        const auto mi = demo();
        QCOMPARE(mi.metersToPx(1.0), 20.0);     // 1m / 0.05 = 20 셀
        QCOMPARE(mi.extentXMeters(), 20.0);
        QCOMPARE(mi.extentYMeters(), 15.0);
    }

    // ---- 각도 정규화 ---------------------------------------------------
    void normalizeAngle_wraps()
    {
        QVERIFY(qAbs(normalizeAngle(0.5) - 0.5) < 1e-12);
        QVERIFY(qAbs(normalizeAngle(2 * M_PI + 0.5) - 0.5) < 1e-9);
        QVERIFY(qAbs(normalizeAngle(-2 * M_PI + 0.5) - 0.5) < 1e-9);
    }

    /// 치역이 문서화한 (-pi, pi] 를 실제로 지키는지.
    /// atan2 는 경계에서 -pi 를 낼 수 있어 별도로 못박는다.
    void normalizeAngle_rangeIsHalfOpen()
    {
        for (double a : {M_PI, -M_PI, 3 * M_PI, -3 * M_PI, 5 * M_PI}) {
            const double r = normalizeAngle(a);
            QVERIFY2(r > -M_PI && r <= M_PI + 1e-12,
                     qPrintable(QStringLiteral("치역 위반: %1 → %2").arg(a).arg(r)));
        }
        QCOMPARE(normalizeAngle(-M_PI), M_PI);
    }
};

QTEST_APPLESS_MAIN(TestMapInfo)
#include "test_mapinfo.moc"
