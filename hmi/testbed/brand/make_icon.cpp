// 브랜드 SVG 로 실행 파일 아이콘 자산을 만든다.
//
// 빌드할 때마다 도는 것이 아니라, 로고가 바뀔 때 사람이 한 번 돌리는
// 도구다. 결과물(.ico 와 PNG 들)은 저장소에 커밋한다 — 빌드 환경에 SVG
// 래스터라이저를 요구하지 않기 위해서다. 에어갭에서 재빌드해야 하는
// 납품물이라, 빌드에 필요한 것은 적을수록 좋다.
//
// Qt 로 그린다. rsvg 나 ImageMagick 을 쓰면 그 도구가 없는 기계에서는
// 자산을 다시 만들 수 없는데, Qt 는 어차피 이 프로젝트의 의존성이다.
//
// 사용법:
//     make_brand_icon <logo.svg> <출력 폴더>

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

#include <array>

namespace {

/// 윈도우 탐색기와 작업 표시줄이 고르는 크기들. 256 은 큰 아이콘 보기용이다.
constexpr std::array<int, 7> kSizes{16, 24, 32, 48, 64, 128, 256};

QImage render(QSvgRenderer &svg, int size)
{
    QImage img(size, size, QImage::Format_ARGB32);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    svg.render(&p, QRectF(0, 0, size, size));
    return img;
}

QByteArray pngBytes(const QImage &img)
{
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return out;
}

void writeLe16(QByteArray &out, quint16 v)
{
    out.append(char(v & 0xFF));
    out.append(char((v >> 8) & 0xFF));
}

void writeLe32(QByteArray &out, quint32 v)
{
    for (int i = 0; i < 4; ++i)
        out.append(char((v >> (8 * i)) & 0xFF));
}

/// ICO 컨테이너. 헤더 6 바이트, 항목마다 16 바이트, 그 뒤에 PNG 를 이어 붙인다.
/// 요즘 윈도우는 항목 자체가 PNG 인 것을 읽으므로 BMP 로 풀어 쓸 필요가 없다.
QByteArray packIco(const QList<QByteArray> &frames, const QList<int> &sizes)
{
    QByteArray out;
    writeLe16(out, 0);                       // reserved
    writeLe16(out, 1);                       // type: icon
    writeLe16(out, quint16(frames.size()));

    quint32 offset = 6 + 16 * quint32(frames.size());
    for (int i = 0; i < frames.size(); ++i) {
        // 256 은 0 으로 적는다. 이 자리가 1 바이트라 256 이 들어가지 않는다.
        out.append(char(sizes.at(i) >= 256 ? 0 : sizes.at(i)));
        out.append(char(sizes.at(i) >= 256 ? 0 : sizes.at(i)));
        out.append(char(0));                 // 팔레트 없음
        out.append(char(0));                 // reserved
        writeLe16(out, 1);                   // color planes
        writeLe16(out, 32);                  // bits per pixel
        writeLe32(out, quint32(frames.at(i).size()));
        writeLe32(out, offset);
        offset += quint32(frames.at(i).size());
    }
    for (const auto &f : frames)
        out.append(f);
    return out;
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning("쓸 수 없습니다: %s (%s)", qPrintable(path), qPrintable(f.errorString()));
        return false;
    }
    f.write(data);
    return true;
}

}  // namespace

int main(int argc, char *argv[])
{
    // 화면 없이 도는 도구지만 글꼴·이미지 플러그인이 필요해 GUI 앱으로 둔다.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    const QStringList args = QGuiApplication::arguments();
    if (args.size() != 3) {
        qWarning("사용법: make_brand_icon <logo.svg> <출력 폴더>");
        return 2;
    }

    QSvgRenderer svg(args.at(1));
    if (!svg.isValid()) {
        qWarning("SVG 를 읽을 수 없습니다: %s", qPrintable(args.at(1)));
        return 1;
    }

    const QString outDir = args.at(2);
    if (!QDir().mkpath(outDir)) {
        qWarning("출력 폴더를 만들 수 없습니다: %s", qPrintable(outDir));
        return 1;
    }

    QList<QByteArray> frames;
    QList<int> sizes;
    for (int size : kSizes) {
        const QImage img = render(svg, size);
        frames << pngBytes(img);
        sizes << size;

        // 리눅스 아이콘 테마가 쓰는 낱장. .desktop 이 이것을 가리킨다.
        if (size == 48 || size == 128 || size == 256) {
            if (!writeFile(QStringLiteral("%1/app-%2.png").arg(outDir).arg(size), frames.last()))
                return 1;
        }
    }

    if (!writeFile(outDir + QStringLiteral("/app.ico"), packIco(frames, sizes)))
        return 1;

    qInfo("%s/app.ico  (%lld 크기)", qPrintable(outDir), qint64(kSizes.size()));
    return 0;
}
