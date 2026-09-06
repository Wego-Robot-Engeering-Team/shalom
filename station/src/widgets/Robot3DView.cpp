#include "widgets/Robot3DView.h"

#include <cmath>
#include <QFont>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QPolygonF>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>

#include "theme/Tokens.h"

namespace gcs::ui {

using namespace gcs::theme;

namespace {

/// FR3 수정 DH 파라미터 (Craig 표기).
/// i 번째 관절: a_{i-1}, d_i, alpha_{i-1}. 마지막 행은 플랜지다.
struct DhRow {
    double a, d, alpha;
};

constexpr DhRow kDh[8] = {
    {0.0,     0.333, 0.0},
    {0.0,     0.0,   -M_PI_2},
    {0.0,     0.316, M_PI_2},
    {0.0825,  0.0,   M_PI_2},
    {-0.0825, 0.384, -M_PI_2},
    {0.0,     0.0,   M_PI_2},
    {0.088,   0.0,   M_PI_2},
    {0.0,     0.107, 0.0},     // 플랜지
};

/// B2 몸통 치수 (m). 공개 사양 기준의 근사값이며, 실측 메시로 교체할 때
/// 함께 정정한다.
constexpr double kBodyLen = 1.10;
constexpr double kBodyWid = 0.50;
constexpr double kBodyHgt = 0.30;
constexpr double kStandHeight = 0.55;   ///< 서 있을 때 몸통 바닥 높이
constexpr double kUpperLeg = 0.35;

/// 화가 알고리즘으로 그릴 하나의 면.
struct Face {
    QList<QVector3D> pts;
    QColor color;
    double depth = 0.0;   ///< 카메라까지 거리. 큰 것부터 그린다.
};

QMatrix4x4 dhTransform(const DhRow &row, double theta)
{
    // T = Rx(alpha) * Tx(a) * Rz(theta) * Tz(d)
    QMatrix4x4 m;
    m.rotate(qRadiansToDegrees(row.alpha), 1, 0, 0);
    m.translate(float(row.a), 0, 0);
    m.rotate(qRadiansToDegrees(theta), 0, 0, 1);
    m.translate(0, 0, float(row.d));
    return m;
}

/// 축 정렬 상자의 여섯 면. center 는 중심, size 는 각 축 길이.
void appendBox(QList<Face> &out, const QMatrix4x4 &frame, const QVector3D &center,
               const QVector3D &size, const QColor &color)
{
    const QVector3D h = size / 2.0f;
    const QVector3D c[8] = {
        {center.x() - h.x(), center.y() - h.y(), center.z() - h.z()},
        {center.x() + h.x(), center.y() - h.y(), center.z() - h.z()},
        {center.x() + h.x(), center.y() + h.y(), center.z() - h.z()},
        {center.x() - h.x(), center.y() + h.y(), center.z() - h.z()},
        {center.x() - h.x(), center.y() - h.y(), center.z() + h.z()},
        {center.x() + h.x(), center.y() - h.y(), center.z() + h.z()},
        {center.x() + h.x(), center.y() + h.y(), center.z() + h.z()},
        {center.x() - h.x(), center.y() + h.y(), center.z() + h.z()},
    };
    static const int idx[6][4] = {
        {0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4},
        {2, 3, 7, 6}, {1, 2, 6, 5}, {0, 3, 7, 4},
    };
    // 면마다 밝기를 살짝 달리해 입체감을 준다. 조명 계산 대신이다.
    static const double shade[6] = {0.72, 1.00, 0.86, 0.78, 0.92, 0.82};

    for (int f = 0; f < 6; ++f) {
        Face face;
        for (int k = 0; k < 4; ++k)
            face.pts << frame.map(c[idx[f][k]]);
        face.color = QColor::fromHslF(color.hslHueF() < 0 ? 0 : color.hslHueF(),
                                      color.hslSaturationF(),
                                      qBound(0.0, color.lightnessF() * shade[f], 1.0));
        out << face;
    }
}

/// 두 점을 잇는 사각 단면 링크.
void appendSegment(QList<Face> &out, const QVector3D &a, const QVector3D &b,
                   double thickness, const QColor &color)
{
    const QVector3D dir = b - a;
    const float len = dir.length();
    if (len < 1e-5f)
        return;

    QMatrix4x4 frame;
    frame.translate(a);
    // +z 를 링크 방향으로 돌린다.
    const QVector3D z(0, 0, 1);
    const QVector3D axis = QVector3D::crossProduct(z, dir.normalized());
    const float dot = qBound(-1.0f, QVector3D::dotProduct(z, dir.normalized()), 1.0f);
    if (axis.length() > 1e-6f)
        frame.rotate(qRadiansToDegrees(std::acos(dot)), axis.normalized());
    else if (dot < 0)
        frame.rotate(180, 1, 0, 0);

    appendBox(out, frame, QVector3D(0, 0, len / 2),
              QVector3D(float(thickness), float(thickness), len), color);
}

}  // namespace

Robot3DView::Robot3DView(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(240);
    setCursor(Qt::OpenHandCursor);
    joints_ = {0.0, -0.785, 0.0, -2.356, 0.0, 1.571, 0.785};
    shown_ = joints_;

    // 표시용 보간. 실제 팔의 속도가 아니라 "움직였다" 를 읽히게 하는 것이
    // 목적이므로 실제 관절 속도보다 빠르게 잡는다.
    ease_ = new QTimer(this);
    ease_->setInterval(20);
    connect(ease_, &QTimer::timeout, this, [this] {
        const QList<double> &target = preview_.isEmpty() ? joints_ : preview_;
        if (target.size() != shown_.size()) {
            shown_ = target;
            ease_->stop();
            update();
            return;
        }

        constexpr double kStep = 0.055;   // rad per tick, 약 2.75 rad/s
        bool moving = false;
        for (int i = 0; i < shown_.size(); ++i) {
            const double d = target.at(i) - shown_.at(i);
            if (std::abs(d) <= kStep) {
                shown_[i] = target.at(i);
            } else {
                shown_[i] += d > 0 ? kStep : -kStep;
                moving = true;
            }
        }
        if (!moving)
            ease_->stop();
        update();
    });
}

void Robot3DView::setArmJoints(const QList<double> &q)
{
    if (q.size() < 7)
        return;
    joints_ = q;
    ease_->start();
}

void Robot3DView::setSingularWarning(bool warn)
{
    if (warn == singularWarn_)
        return;
    singularWarn_ = warn;
    update();
}

void Robot3DView::setStale(bool stale)
{
    if (stale == stale_)
        return;
    stale_ = stale;
    update();
}

void Robot3DView::resetCamera()
{
    azimuth_ = -0.9;
    elevation_ = 0.42;
    distance_ = 2.4;
    target_ = QVector3D(0, 0, 0.55);
    update();
}

QList<QVector3D> Robot3DView::jointOrigins(const QList<double> &q) const
{
    QList<QVector3D> origins;
    QMatrix4x4 t;
    origins << t.map(QVector3D(0, 0, 0));
    for (int i = 0; i < 8; ++i) {
        t *= dhTransform(kDh[i], i < 7 ? q.value(i) : 0.0);
        origins << t.map(QVector3D(0, 0, 0));
    }
    return origins;
}

void Robot3DView::setPreviewJoints(const QList<double> &q)
{
    if (preview_ == q)
        return;
    preview_ = q;
    ease_->start();
}

void Robot3DView::mousePressEvent(QMouseEvent *ev)
{
    lastMouse_ = ev->pos();
    setCursor(Qt::ClosedHandCursor);
    ev->accept();
}

void Robot3DView::mouseMoveEvent(QMouseEvent *ev)
{
    const QPoint d = ev->pos() - lastMouse_;
    if (d.isNull())
        return;
    lastMouse_ = ev->pos();

    // 가운데·오른쪽 버튼이나 Shift 를 누른 채 끌면 시점을 옮긴다.
    // 노트북 트랙패드에는 가운데 버튼이 없어 Shift 조합을 함께 둔다.
    const bool panning = (ev->buttons() & (Qt::MiddleButton | Qt::RightButton))
                         || (ev->modifiers() & Qt::ShiftModifier);

    if (panning) {
        // 화면 축을 월드 축으로 되돌린다. 카메라가 도는데 이동만 월드
        // 축으로 하면 끄는 방향과 움직이는 방향이 어긋나 조작이 안 된다.
        const QVector3D right(float(-std::sin(azimuth_)), float(std::cos(azimuth_)), 0.0f);
        const QVector3D up(
            float(-std::sin(elevation_) * std::cos(azimuth_)),
            float(-std::sin(elevation_) * std::sin(azimuth_)),
            float(std::cos(elevation_)));

        // 멀리서 볼수록 한 픽셀이 더 먼 거리에 해당한다.
        const float k = float(distance_ * 0.0016);
        target_ -= right * (d.x() * k);
        target_ += up * (d.y() * k);
        update();
        return;
    }

    if (!(ev->buttons() & Qt::LeftButton))
        return;
    azimuth_ -= d.x() * 0.01;
    // 위아래로 뒤집히지 않게 고도를 제한한다.
    elevation_ = qBound(-1.4, elevation_ + d.y() * 0.01, 1.4);
    update();
}

void Robot3DView::wheelEvent(QWheelEvent *ev)
{
    distance_ = qBound(1.2, distance_ * (ev->angleDelta().y() > 0 ? 1 / 1.12 : 1.12), 6.0);
    update();
}

void Robot3DView::enterEvent(QEnterEvent *)
{
    hovered_ = true;
    update();
}

void Robot3DView::leaveEvent(QEvent *)
{
    hovered_ = false;
    update();
}

void Robot3DView::paintEvent(QPaintEvent *)
{
    const Colors &C = colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    p.fillRect(rect(), QColor(C.isDark() ? C.bg : C.surfaceHi));

    // ---- 카메라 ----
    const QVector3D target = target_;
    const QVector3D eye(
        float(target.x() + distance_ * std::cos(elevation_) * std::cos(azimuth_)),
        float(target.y() + distance_ * std::cos(elevation_) * std::sin(azimuth_)),
        float(target.z() + distance_ * std::sin(elevation_)));

    QMatrix4x4 viewM;
    viewM.lookAt(eye, target, QVector3D(0, 0, 1));

    QMatrix4x4 projM;
    projM.perspective(38.0f, float(width()) / float(qMax(1, height())), 0.05f, 60.0f);

    const QMatrix4x4 mvp = projM * viewM;
    const auto project = [&](const QVector3D &v) {
        const QVector3D n = mvp.map(v);
        return QPointF((n.x() * 0.5 + 0.5) * width(), (1.0 - (n.y() * 0.5 + 0.5)) * height());
    };

    // ---- 바닥 격자 ----
    p.setPen(QPen(QColor(C.border), 1));
    for (int i = -3; i <= 3; ++i) {
        const double g = i * 0.5;
        p.drawLine(project({float(g), -1.5f, 0}), project({float(g), 1.5f, 0}));
        p.drawLine(project({-1.5f, float(g), 0}), project({1.5f, float(g), 0}));
    }

    // ---- 형상 수집 ----
    QList<Face> faces;

    const QColor bodyColor(C.isDark() ? QColor(0x3A, 0x42, 0x4D) : QColor(0x9A, 0xA4, 0xB0));
    const QColor legColor(C.isDark() ? QColor(0x2E, 0x35, 0x3E) : QColor(0x84, 0x8E, 0x9A));
    // 편집 중에는 보낼 자세를 그대로 그린다. 실제 자세 위에 반투명 팔을
    // 겹쳐 봤더니 로봇이 두 대로 보였다.
    const bool previewing = !preview_.isEmpty();
    const QColor armColor = singularWarn_ ? QColor(C.warning)
                            : previewing  ? QColor(C.accentHi)
                                          : QColor(C.accent);

    // B2 몸통
    QMatrix4x4 identity;
    appendBox(faces, identity, QVector3D(0, 0, float(kStandHeight + kBodyHgt / 2)),
              QVector3D(float(kBodyLen), float(kBodyWid), float(kBodyHgt)), bodyColor);

    // 다리 4개. 서 있는 자세를 고정으로 그린다 — 다리 관절값은 아직
    // 텔레메트리에 없고, 팔 자세 확인이 이 뷰의 목적이다.
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sy = -1; sy <= 1; sy += 2) {
            const QVector3D hip(float(sx * kBodyLen * 0.38), float(sy * kBodyWid * 0.5),
                                float(kStandHeight));
            const QVector3D knee(hip.x(), float(sy * kBodyWid * 0.62),
                                 float(kStandHeight - kUpperLeg * 0.8));
            const QVector3D foot(hip.x(), knee.y(), 0.0f);
            appendSegment(faces, hip, knee, 0.07, legColor);
            appendSegment(faces, knee, foot, 0.055, legColor);
        }
    }

    // FR3 — 몸통 위에 얹혀 있다.
    QMatrix4x4 armBase;
    armBase.translate(0.0f, 0.0f, float(kStandHeight + kBodyHgt));
    const auto origins = jointOrigins(shown_);

    appendBox(faces, armBase, QVector3D(0, 0, 0.04f), QVector3D(0.18f, 0.18f, 0.08f),
              armColor.darker(140));

    for (int i = 0; i + 1 < origins.size(); ++i) {
        const QVector3D a = armBase.map(origins.at(i));
        const QVector3D b = armBase.map(origins.at(i + 1));
        // 뒤로 갈수록 가늘게. 실제 FR3 도 손목이 가늘다.
        const double thick = 0.085 - i * 0.006;
        appendSegment(faces, a, b, qMax(0.035, thick), armColor);
    }

    // 엔드이펙터(카메라) 표시
    if (origins.size() >= 2) {
        QMatrix4x4 eeFrame;
        eeFrame.translate(armBase.map(origins.last()));
        appendBox(faces, eeFrame, QVector3D(0, 0, 0), QVector3D(0.09f, 0.06f, 0.05f),
                  QColor(C.success));
    }

    // ---- 화가 알고리즘: 카메라에서 먼 면부터 ----
    for (auto &f : faces) {
        QVector3D c;
        for (const auto &v : f.pts)
            c += v;
        c /= float(f.pts.size());
        f.depth = (c - eye).length();
    }
    std::sort(faces.begin(), faces.end(),
              [](const Face &a, const Face &b) { return a.depth > b.depth; });

    for (const auto &f : faces) {
        QPolygonF poly;
        for (const auto &v : f.pts)
            poly << project(v);
        QColor col = f.color;
        if (stale_)
            col = QColor(C.textMute);
        p.setPen(QPen(col.darker(125), 0.8));
        p.setBrush(col);
        p.drawPolygon(poly);
    }

    // ---- 조작 안내 ----
    // 한 번 읽으면 그만인 문구다. 늘 띄워두면 화면만 지저분해지므로
    // 마우스를 올렸을 때만 보여준다.
    if (hovered_) {
        QFont f;
        f.setPointSize(8);
        p.setFont(f);
        p.setPen(QColor(C.textMute));
        p.drawText(rect().adjusted(8, 0, -8, -6), Qt::AlignLeft | Qt::AlignBottom,
                   QStringLiteral("끌어서 회전 · Shift+끌기로 이동 · 휠로 확대"));
    }

    // 미리보기 문구는 지웠다. 팔 색이 바뀌는 것으로 이미 드러나고, 문장이
    // 늘 떠 있으면 3D 뷰를 가린다.

    if (singularWarn_) {
        p.setPen(QColor(C.warning));
        p.drawText(rect().adjusted(8, 6, -8, 0), Qt::AlignRight | Qt::AlignTop,
                   QStringLiteral("특이자세 근접"));
    }
}

}  // namespace gcs::ui
