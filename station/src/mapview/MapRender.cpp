#include "mapview/MapRender.h"

#include <QColor>

#include "theme/Tokens.h"

namespace gcs::map {

using namespace gcs::theme;

namespace {

struct Rgb {
    int r, g, b;
};

Rgb toRgb(const QLatin1String &hex)
{
    const QColor c{QString(hex)};
    return {c.red(), c.green(), c.blue()};
}

}  // namespace

QImage occupancyToImage(const QList<qint8> &grid, int width, int height,
                        int occupiedThreshold)
{
    if (width <= 0 || height <= 0 || grid.size() < qsizetype(width) * height)
        return {};

    const Colors &C = colors();
    const Rgb unknown = toRgb(C.mapUnknown);
    const Rgb free = toRgb(C.mapFree);
    const Rgb occupied = toRgb(C.mapOccupied);

    QImage img(width, height, QImage::Format_RGB888);

    for (int y = 0; y < height; ++y) {
        uchar *line = img.scanLine(y);
        for (int x = 0; x < width; ++x) {
            const int v = grid.at(qsizetype(y) * width + x);
            Rgb c;
            if (v < 0) {
                c = unknown;
            } else if (v >= occupiedThreshold) {
                c = occupied;
            } else if (v > 20) {
                // 중간 점유도는 자유↔점유 사이를 보간해 SLAM 신뢰도를 드러낸다.
                // 이분법으로 칠하면 경계가 실제보다 단단해 보여, 조작자가
                // 통과 가능한 틈을 벽으로 오인한다.
                const double t = double(v - 20) / (occupiedThreshold - 20);
                c = {int(free.r + (occupied.r - free.r) * t),
                     int(free.g + (occupied.g - free.g) * t),
                     int(free.b + (occupied.b - free.b) * t)};
            } else {
                c = free;
            }
            line[x * 3 + 0] = uchar(c.r);
            line[x * 3 + 1] = uchar(c.g);
            line[x * 3 + 2] = uchar(c.b);
        }
    }
    return img;
}

}  // namespace gcs::map
