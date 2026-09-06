#include "data/InspectionRecord.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>

namespace hmi::data {
namespace {

/// 규정 파일명: 차량번호_량번호_포인트ID,타임스탬프.확장자
/// 차량번호에 점이 들어가므로(편성.량) 언더스코어로만 나눈다.
const QRegularExpression &nameRegex()
{
    static const QRegularExpression re(
        QStringLiteral(R"(^([^_]+)_([^_]+)_([^,]+),(\d{14})\.([A-Za-z0-9]+)$)"));
    return re;
}

constexpr auto kStampFormat = "yyyyMMddHHmmss";

/// 이미지로 취급할 확장자. 사이드카(.json)와 기타 파일은 제외한다.
bool looksLikeImage(const QString &extension)
{
    static const QStringList kImage{QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                    QStringLiteral("png"), QStringLiteral("ply"),
                                    QStringLiteral("pcd")};
    return kImage.contains(extension.toLower());
}

}  // namespace

QStringList InspectionRecord::missingFields() const
{
    QStringList missing;
    if (vehicleNumber.isEmpty())
        missing << QStringLiteral("차량번호");
    if (pointId.isEmpty())
        missing << QStringLiteral("점검포인트");
    if (!capturedAt.isValid())
        missing << QStringLiteral("촬영시간");
    // Apriltag 는 인식 실패가 정상 범위 안에 있으므로 누락으로 세지 않는다.
    // 다만 사이드카가 아예 없으면 좌표와 촬영거리를 알 수 없다.
    if (!hasSidecar())
        missing << QStringLiteral("메타데이터 파일");
    return missing;
}

bool InspectionRecord::isComplete() const
{
    return missingFields().isEmpty();
}

std::optional<InspectionRecord> parseFileName(const QString &fileName)
{
    const auto match = nameRegex().match(fileName);
    if (!match.hasMatch())
        return std::nullopt;
    if (!looksLikeImage(match.captured(5)))
        return std::nullopt;

    InspectionRecord r;
    r.fileName = fileName;
    r.vehicleNumber = match.captured(1);
    r.carNumber = match.captured(2);
    r.pointId = match.captured(3);
    r.capturedAt = QDateTime::fromString(match.captured(4), QLatin1String(kStampFormat));
    return r;
}

ScanResult scanDirectory(const QString &directory, int maxFiles)
{
    ScanResult result;

    const QDir root(directory);
    if (!root.exists()) {
        result.error = QStringLiteral("저장 위치를 찾을 수 없습니다: %1").arg(directory);
        return result;
    }

    QDirIterator it(root.absolutePath(), QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    int seen = 0;
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);

        // 사이드카는 이미지에 딸려 읽으므로 따로 목록에 올리지 않는다.
        if (info.suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) == 0)
            continue;

        if (++seen > maxFiles) {
            // 공유 폴더가 예상보다 크면 무한정 훑지 않는다. 화면이 멈추는 것보다
            // 일부만 보여주고 그 사실을 알리는 편이 낫다.
            result.error = QStringLiteral("파일이 %1개를 넘어 일부만 표시합니다").arg(maxFiles);
            break;
        }

        auto record = parseFileName(info.fileName());
        if (!record) {
            result.unrecognised << info.fileName();
            continue;
        }

        record->filePath = info.absoluteFilePath();
        record->fileSize = info.size();

        // 사이드카는 같은 이름의 .json 이다. 있으면 좌표·촬영거리·태그를 채운다.
        const QString sidecarPath =
            info.absolutePath() + QLatin1Char('/') + info.completeBaseName()
            + QStringLiteral(".json");
        QFile sidecar(sidecarPath);
        if (sidecar.open(QIODevice::ReadOnly)) {
            const auto doc = QJsonDocument::fromJson(sidecar.readAll());
            if (doc.isObject()) {
                const QJsonObject o = doc.object();
                record->sidecar = o;
                if (o.contains(QStringLiteral("tag_id")))
                    record->tagId = o.value(QStringLiteral("tag_id")).toInt(-1);
            }
        }

        result.records << *record;
    }

    // 최신순. 조작자가 방금 찍은 것을 먼저 본다.
    std::sort(result.records.begin(), result.records.end(),
              [](const InspectionRecord &a, const InspectionRecord &b) {
                  return a.capturedAt > b.capturedAt;
              });
    return result;
}

}  // namespace hmi::data
