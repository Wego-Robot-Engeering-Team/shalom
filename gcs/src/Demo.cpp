#include "Demo.h"

#include <QRandomGenerator>
#include <QtMath>

namespace gcs::demo {

using gcs::map::MapInfo;

namespace {
constexpr double kResolution = 0.05;   // 5 cm/셀
constexpr int kW = 720;                // 36 m
constexpr int kH = 420;                // 21 m

inline void fillRect(QList<qint8> &g, int x0, int y0, int x1, int y1, qint8 v)
{
    for (int y = qMax(0, y0); y < qMin(kH, y1); ++y)
        for (int x = qMax(0, x0); x < qMin(kW, x1); ++x)
            g[qsizetype(y) * kW + x] = v;
}
}  // namespace

MapData buildMap()
{
    QList<qint8> g(qsizetype(kW) * kH, qint8(-1));   // 미탐색으로 시작

    fillRect(g, 30, 30, kW - 30, kH - 30, 0);        // 검수고 내부

    // 외벽
    fillRect(g, 30, 30, kW - 30, 38, 100);
    fillRect(g, 30, kH - 38, kW - 30, kH - 30, 100);
    fillRect(g, 30, 30, 38, kH - 30, 100);
    fillRect(g, kW - 38, 30, kW - 30, kH - 30, 100);

    // 열차 하부 점검 피트 — 좌우 벽 두 줄. 그 사이가 로봇 주행로.
    const int pitTop = 150, pitBottom = 270;
    fillRect(g, 90, pitTop, kW - 90, pitTop + 6, 100);
    fillRect(g, 90, pitBottom, kW - 90, pitBottom + 6, 100);

    // 량 구분 기둥
    for (int cx = 120; cx < kW - 100; cx += 118) {
        fillRect(g, cx, pitTop - 26, cx + 10, pitTop, 100);
        fillRect(g, cx, pitBottom + 6, cx + 10, pitBottom + 32, 100);
    }

    // 충전 스테이션
    fillRect(g, 60, 60, 120, 96, 100);
    fillRect(g, 66, 66, 114, 90, 0);

    // SLAM 특유의 지저분한 경계를 약간만 재현한다.
    auto *rng = QRandomGenerator::global();
    for (qsizetype i = 0; i < g.size(); ++i)
        if (g[i] == 0 && rng->generateDouble() < 0.0012)
            g[i] = qint8(rng->bounded(25, 70));

    const auto info = MapInfo::create(kW, kH, kResolution,
                                      -kW * kResolution / 2, -kH * kResolution / 2,
                                      0.0, QStringLiteral("gtxa_pit_demo"));
    return {*info, g};
}

QList<QVariantMap> buildWaypoints()
{
    QList<QVariantMap> wps;
    int n = 0;
    for (int car = 1; car <= 3; ++car) {
        for (int pt = 1; pt <= 4; ++pt) {
            QVariantMap w;
            w["id"] = QStringLiteral("C%1-P%2")
                          .arg(car, 2, 10, QLatin1Char('0'))
                          .arg(pt, 2, 10, QLatin1Char('0'));
            w["name"] = QStringLiteral("%1량 P%2").arg(car).arg(pt);
            w["x"] = -14.0 + n * 2.35;
            w["y"] = 0.0;
            w["theta"] = 0.0;
            w["tag_id"] = 10 + n;
            w["status"] = QStringLiteral("todo");
            wps << w;
            ++n;
        }
    }
    return wps;
}

QList<QVariantMap> buildTags(const QList<QVariantMap> &waypoints)
{
    QList<QVariantMap> tags;
    for (const auto &w : waypoints) {
        const int id = w.value("tag_id").toInt();
        // 각 포인트 양옆 벽에 마커가 붙는다 (지시서 2.2.2 로봇암 제어 항목).
        tags << QVariantMap{{"id", id}, {"x", w.value("x")}, {"y", 3.0}};
        tags << QVariantMap{{"id", id + 100}, {"x", w.value("x")}, {"y", -3.0}};
    }
    return tags;
}

Feed::Feed(QList<QVariantMap> waypoints) : waypoints_(std::move(waypoints)) {}

Frame Feed::step(double dt)
{
    t_ += dt;

    // 피트를 따라 천천히 왕복한다.
    constexpr double span = 26.0;
    const double x = -13.0 + span * (0.5 - 0.5 * std::cos(t_ * 0.09));
    const double y = 0.35 * std::sin(t_ * 0.5);
    const double theta = std::atan2(0.35 * 0.5 * std::cos(t_ * 0.5),
                                    span * 0.5 * 0.09 * std::sin(t_ * 0.09) + 1e-6);

    trail_ << QPointF(x, y);
    if (trail_.size() > 900)
        trail_.removeFirst();

    Frame f;
    f.x = x;
    f.y = y;
    f.theta = theta;
    f.trail = trail_;

    // 지나온 포인트를 완료 처리하고, 현재 포인트만 진행 표시한다.
    f.plan << QPointF(x, y);
    for (auto &w : waypoints_) {
        const double wx = w.value("x").toDouble();
        if (wx < x - 0.6) {
            w["status"] = QStringLiteral("done");
        } else if (qAbs(wx - x) <= 0.6) {
            w["status"] = QStringLiteral("current");
        } else {
            w["status"] = QStringLiteral("todo");
            f.plan << QPointF(wx, w.value("y").toDouble());
        }
    }
    // 인식 실패 사례가 화면에 하나는 보이도록 고정 배치한다.
    if (waypoints_.size() > 7 && waypoints_[7].value("status") == QStringLiteral("done"))
        waypoints_[7]["status"] = QStringLiteral("error");

    soc_ = qMax(8.0, soc_ - dt * 0.045);
    f.soc = soc_;

    // 로봇팔: 촬영 자세를 오가며 조작성 지수가 오르내리게 한다.
    const double phase = std::sin(t_ * 0.35);
    f.joints = {0.35 * phase,
                -0.785 + 0.55 * phase,
                0.12 * std::sin(t_ * 0.22),
                -2.356 + 0.95 * qAbs(phase),   // 팔꿈치가 펴지면 특이자세에 근접
                0.10 * phase,
                1.571 + 0.40 * phase,
                0.785};

    // 4축이 펴질수록(0 에 가까울수록) 조작성이 급감하는 팔꿈치 특이자세 모사.
    const double elbow = qAbs(f.joints[3] + 0.1518) / 2.89;
    f.manipulability = qMax(0.004, 0.115 * std::pow(elbow, 0.7));
    f.sigmaMin = qMax(0.002, 0.085 * std::pow(elbow, 0.8));

    if (std::fmod(t_, 7.0) < 3.2) {
        for (const auto &w : waypoints_)
            if (w.value("status") == QStringLiteral("current"))
                f.seenTags.insert(w.value("tag_id").toInt());
    }

    auto *rng = QRandomGenerator::global();
    f.cpu = 34 + 22 * qAbs(std::sin(t_ * 0.3));
    f.mem = 51 + 8 * qAbs(std::sin(t_ * 0.17));
    f.cpuTemp = 58 + 14 * qAbs(std::sin(t_ * 0.11));
    f.gpuTemp = 62 + 16 * qAbs(std::sin(t_ * 0.13));
    f.rtt = 18 + 14 * rng->generateDouble();
    return f;
}

}  // namespace gcs::demo
