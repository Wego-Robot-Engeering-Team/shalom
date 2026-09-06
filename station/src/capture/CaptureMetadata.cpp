#include "capture/CaptureMetadata.h"

#include <QRegularExpression>
#include <QtMath>

namespace gcs::capture {
namespace {

/// 과업지시서가 정한 촬영시간 표기.
constexpr auto kTimeFormat = "yyyy-MM-dd HH:mm:ss";

/// 파일명에 쓰는 타임스탬프. 구분자 없이 붙여 정렬이 시간순이 되게 한다.
constexpr auto kStampFormat = "yyyyMMddHHmmss";

}  // namespace

QString sanitiseForFileName(const QString &value)
{
    // Windows 가 금지하는 문자 집합이 Linux 보다 넓으므로 그쪽에 맞춘다.
    // 거부하지 않고 치환하는 이유는, 조작자가 량번호에 슬래시를 넣었을 때
    // 해석할 수 없는 오류 대신 쓸 수 있는 파일을 받아야 하기 때문이다.
    static const QRegularExpression illegal(QStringLiteral(R"([<>:"/\\|?*\x00-\x1F])"));
    QString out = value;
    out.replace(illegal, QStringLiteral("-"));
    return out.trimmed();
}

QString CaptureMetadata::vehicleNumber() const
{
    if (trainNumber.isEmpty() || carNumber.isEmpty())
        return {};
    return QStringLiteral("%1.%2").arg(trainNumber, carNumber);
}

QStringList CaptureMetadata::missingFields() const
{
    QStringList missing;
    if (trainNumber.trimmed().isEmpty())
        missing << QStringLiteral("편성번호");
    if (carNumber.trimmed().isEmpty())
        missing << QStringLiteral("량번호");
    if (pointId.trimmed().isEmpty())
        missing << QStringLiteral("점검포인트ID");
    if (!capturedAt.isValid())
        missing << QStringLiteral("촬영시간");
    // Apriltag ID 는 필수 항목이지만 인식 실패가 정상 범위 안에 있다.
    // 없으면 기록에 남기되 저장은 막지 않는다 — 여기서 막으면 조작자가
    // 임의의 값을 넣어 채우게 되고, 그쪽이 훨씬 나쁘다.
    return missing;
}

QString CaptureMetadata::capturedAtText() const
{
    return capturedAt.isValid() ? capturedAt.toString(QLatin1String(kTimeFormat))
                                : QString();
}

QString CaptureMetadata::fileName(const QString &extension) const
{
    if (!isComplete())
        return {};

    // 규정 형식: 차량번호_량번호_포인트ID,타임스탬프.확장자
    QString ext = extension;
    while (ext.startsWith(QLatin1Char('.')))
        ext.remove(0, 1);

    return QStringLiteral("%1_%2_%3,%4.%5")
        .arg(sanitiseForFileName(vehicleNumber()),
             sanitiseForFileName(carNumber),
             sanitiseForFileName(pointId),
             capturedAt.toString(QLatin1String(kStampFormat)),
             sanitiseForFileName(ext));
}

QJsonObject CaptureMetadata::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("vehicle_number")] = vehicleNumber();
    o[QStringLiteral("train_number")] = trainNumber;
    o[QStringLiteral("car_number")] = carNumber;
    o[QStringLiteral("point_id")] = pointId;
    o[QStringLiteral("tag_id")] = tagId;
    o[QStringLiteral("captured_at")] = capturedAtText();
    // ISO 8601 도 함께 남긴다. 보고서 생성이 파싱하기 쉽다.
    o[QStringLiteral("captured_at_iso")] = capturedAt.toString(Qt::ISODate);

    QJsonObject pose;
    pose[QStringLiteral("x")] = robotX;
    pose[QStringLiteral("y")] = robotY;
    pose[QStringLiteral("theta")] = robotTheta;
    pose[QStringLiteral("theta_deg")] = qRadiansToDegrees(robotTheta);
    o[QStringLiteral("robot_pose")] = pose;

    o[QStringLiteral("distance_mm")] = distanceMm;
    return o;
}

}  // namespace gcs::capture
