#include "mapview/MapInfo.h"

#include <QtMath>

#include <cmath>

namespace gcs::map {

std::optional<MapInfo> MapInfo::create(int width, int height, double resolution,
                                       double originX, double originY, double originTheta,
                                       const QString &mapId, QString *err)
{
    const auto fail = [err](const QString &m) -> std::optional<MapInfo> {
        if (err)
            *err = m;
        return std::nullopt;
    };

    if (width <= 0 || height <= 0)
        return fail(QStringLiteral("격자 크기가 양수여야 한다: %1x%2").arg(width).arg(height));
    if (!(resolution > 0.0))
        return fail(QStringLiteral("resolution 은 양수여야 한다: %1").arg(resolution));
    if (std::abs(originTheta) > 1e-6) {
        // v1 미지원. 브릿지가 회전을 흡수해서 발행해야 한다 (명세 §2.2).
        return fail(QStringLiteral("origin_theta=%1 — 회전된 맵은 프로토콜 v1 미지원")
                        .arg(originTheta));
    }

    MapInfo mi;
    mi.width = width;
    mi.height = height;
    mi.resolution = resolution;
    mi.originX = originX;
    mi.originY = originY;
    mi.originTheta = 0.0;
    mi.mapId = mapId;
    return mi;
}

double MapInfo::thetaToItemRotation(double theta)
{
    return -qRadiansToDegrees(theta);
}

double MapInfo::itemRotationToTheta(double deg)
{
    return -qDegreesToRadians(deg);
}

double normalizeAngle(double a)
{
    // atan2 의 치역은 [-pi, pi] 라서 경계에서 -pi 가 나올 수 있다
    // (예: -3pi → atan2(-0.0, -1.0) = -pi). 문서화한 치역 (-pi, pi] 를
    // 지키기 위해 -pi 만 +pi 로 접는다. 각도로는 같은 값이지만, 비교나
    // 표시에서 부호가 튀는 것을 막는다.
    const double r = std::atan2(std::sin(a), std::cos(a));
    return (r <= -M_PI + 1e-12) ? M_PI : r;
}

}  // namespace gcs::map
