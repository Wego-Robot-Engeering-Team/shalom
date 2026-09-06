#include "mapview/MapFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>
#include <QTextStream>

namespace gcs::map {

namespace {

/// "key: value" 한 줄에서 값만 떼어낸다. 주석과 따옴표를 걷어낸다.
QString valueOf(const QString &line)
{
    const int colon = line.indexOf(QLatin1Char(':'));
    if (colon < 0)
        return {};
    QString v = line.mid(colon + 1);

    // '#' 이 따옴표 안에 있을 수도 있으므로 따옴표를 먼저 본다.
    if (!v.trimmed().startsWith(QLatin1Char('"')) && !v.trimmed().startsWith(QLatin1Char('\'')))
        v = v.section(QLatin1Char('#'), 0, 0);

    v = v.trimmed();
    if (v.size() >= 2 && (v.front() == QLatin1Char('"') || v.front() == QLatin1Char('\''))
        && v.back() == v.front())
        v = v.mid(1, v.size() - 2);
    return v;
}

bool truthy(const QString &v)
{
    return v == QLatin1String("1") || v.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
}

}  // namespace

std::optional<MapMeta> parseMapYaml(const QString &text, QString *err)
{
    const auto fail = [err](const QString &why) -> std::optional<MapMeta> {
        if (err)
            *err = why;
        return std::nullopt;
    };

    MapMeta m;
    bool haveImage = false;
    bool haveResolution = false;
    bool haveOrigin = false;

    const auto lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        const QString key = line.section(QLatin1Char(':'), 0, 0).trimmed();
        const QString value = valueOf(line);

        if (key == QLatin1String("image")) {
            m.image = value;
            haveImage = !value.isEmpty();
        } else if (key == QLatin1String("resolution")) {
            m.resolution = value.toDouble(&haveResolution);
        } else if (key == QLatin1String("origin")) {
            // "[x, y, theta]" 또는 "x, y, theta"
            QString inner = value;
            inner.remove(QLatin1Char('[')).remove(QLatin1Char(']'));
            const auto parts = inner.split(QLatin1Char(','), Qt::SkipEmptyParts);
            if (parts.size() < 2)
                return fail(QStringLiteral("origin 값이 [x, y, theta] 형식이 아닙니다"));
            bool okX = false, okY = false;
            m.originX = parts.at(0).trimmed().toDouble(&okX);
            m.originY = parts.at(1).trimmed().toDouble(&okY);
            if (parts.size() > 2)
                m.originTheta = parts.at(2).trimmed().toDouble();
            haveOrigin = okX && okY;
        } else if (key == QLatin1String("negate")) {
            m.negate = truthy(value);
        } else if (key == QLatin1String("occupied_thresh")) {
            m.occupiedThresh = value.toDouble();
        } else if (key == QLatin1String("free_thresh")) {
            m.freeThresh = value.toDouble();
        }
    }

    if (!haveImage)
        return fail(QStringLiteral("image 항목이 없습니다"));
    if (!haveResolution || m.resolution <= 0.0)
        return fail(QStringLiteral("resolution 값이 올바르지 않습니다"));
    if (!haveOrigin)
        return fail(QStringLiteral("origin 값이 없습니다"));
    if (m.freeThresh > m.occupiedThresh)
        return fail(QStringLiteral("free_thresh 가 occupied_thresh 보다 큽니다"));

    return m;
}

std::optional<LoadedMap> loadRosMap(const QString &yamlPath, QString *err)
{
    const auto fail = [err](const QString &why) -> std::optional<LoadedMap> {
        if (err)
            *err = why;
        return std::nullopt;
    };

    QFile f(yamlPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return fail(QStringLiteral("지도 설명 파일을 열 수 없습니다: %1").arg(f.errorString()));

    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    const auto meta = parseMapYaml(in.readAll(), err);
    if (!meta)
        return std::nullopt;

    // 이미지 경로는 YAML 이 있는 폴더 기준이다. map_server 와 같은 규칙.
    const QFileInfo yamlInfo(yamlPath);
    QString imagePath = meta->image;
    if (QFileInfo(imagePath).isRelative())
        imagePath = yamlInfo.absoluteDir().filePath(imagePath);

    QImage img;
    if (!img.load(imagePath))
        return fail(QStringLiteral("지도 이미지를 읽을 수 없습니다: %1").arg(imagePath));
    if (img.isNull() || img.width() <= 0 || img.height() <= 0)
        return fail(QStringLiteral("지도 이미지가 비어 있습니다"));

    img = img.convertToFormat(QImage::Format_Grayscale8);

    const int w = img.width();
    const int h = img.height();

    auto info = MapInfo::create(w, h, meta->resolution, meta->originX, meta->originY,
                                meta->originTheta, yamlInfo.completeBaseName());
    if (!info)
        return fail(QStringLiteral("지도 정보가 올바르지 않습니다 (크기·해상도·원점을 확인하십시오)"));

    LoadedMap out;
    out.info = *info;
    out.grid.resize(qsizetype(w) * h);

    // map_server 규약: negate 가 0 이면 어두울수록 점유다.
    //   p = (255 - pixel) / 255
    // 그 사이 값은 미탐색으로 둔다. 임계 밖을 임의로 자유나 점유로 밀면
    // 조작자가 "지나갈 수 있다"고 읽는 곳이 실제로는 확인되지 않은 곳이 된다.
    for (int y = 0; y < h; ++y) {
        const uchar *line = img.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            const double shade = line[x] / 255.0;
            const double p = meta->negate ? shade : 1.0 - shade;

            qint8 v = -1;
            if (p > meta->occupiedThresh)
                v = 100;
            else if (p < meta->freeThresh)
                v = 0;
            out.grid[qsizetype(y) * w + x] = v;
        }
    }
    return out;
}

}  // namespace gcs::map
