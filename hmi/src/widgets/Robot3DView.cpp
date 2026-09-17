// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include "widgets/Robot3DView.h"

#include <cmath>
#include <QFont>
#include <QMatrix4x4>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <limits>
#include <vector>

#include "RobotDef.h"
#include "robot/Kinematics.h"
#include "theme/Tokens.h"

namespace hmi::ui {

using namespace hmi::theme;

namespace {


/// 삼각형 하나. 좌표는 월드, 밝기는 코너별로 미리 계산해 둔다.
struct Tri3 {
    QVector3D a, b, c;
    float la, lb, lc;   ///< 코너별 밝기 (0~1)
    QColor color;
};

/// 깊이 버퍼를 가진 소프트웨어 래스터라이저.
///
/// 왜 이것이 필요한가: 이전에는 면을 카메라 거리로 정렬해 뒤에서 앞으로
/// 그렸다(화가 알고리즘). 그 방식은 두 가지를 못 한다 — 수백 면을 넘으면
/// 정렬이 비싸지고, 서로 파고드는 형상을 올바로 그리지 못한다. 두 번째
/// 제약 때문에 로봇의 각 마디를 볼록껍질로 뭉개서 그려야 했고, 화면의
/// 로봇은 실제와 눈에 띄게 달랐다.
///
/// 픽셀마다 깊이를 재면 두 제약이 함께 사라진다. OpenGL 이 하는 일이지만
/// 여기서는 직접 한다 — 납품 장비의 GPU 드라이버를 알 수 없고, 관제 화면이
/// 그것 하나 때문에 안 뜨면 안 된다(Robot3DView.h). QImage 에 쓰므로
/// QWidget::grab() 으로 찍는 현장 지원용 스크린샷에도 그대로 담긴다.
class DepthRaster {
public:
    DepthRaster(int w, int h)
        : w_(w), h_(h), img_(w, h, QImage::Format_ARGB32_Premultiplied),
          depth_(size_t(w) * size_t(h), std::numeric_limits<float>::max())
    {
        img_.fill(Qt::transparent);
    }

    /// 화면 좌표 (x, y) 와 NDC 깊이 z 를 받는다.
    ///
    /// z 를 화면 좌표에 대해 선형 보간해도 정확하다: 원근 투영에서 평면
    /// 삼각형의 NDC z 는 화면 x, y 의 아핀 함수다. 색까지 보간해야 한다면
    /// 1/w 로 나눠 줘야 하지만, 여기서는 면 단위 음영이라 필요 없다.
    /// 밝기까지 보간한다(Gouraud).
    ///
    /// 삼각형마다 하나의 밝기를 쓰면 면이 통째로 한 톤이 되어, 아무리 촘촘한
    /// 메시라도 각져 보인다. 코너 밝기를 보간하면 같은 삼각형 수로 곡면이
    /// 곡면처럼 보인다 — 이것은 OpenGL 이냐 아니냐와 무관한, 음영 방식의
    /// 문제다.
    void triangle(const QVector3D &p0, const QVector3D &p1, const QVector3D &p2,
                  float l0, float l1, float l2, const QColor &base)
    {
        const float det = (p1.y() - p2.y()) * (p0.x() - p2.x())
                          + (p2.x() - p1.x()) * (p0.y() - p2.y());
        if (std::abs(det) < 1e-9f)
            return;

        int x0 = int(std::floor(std::min({p0.x(), p1.x(), p2.x()})));
        int x1 = int(std::ceil(std::max({p0.x(), p1.x(), p2.x()})));
        int y0 = int(std::floor(std::min({p0.y(), p1.y(), p2.y()})));
        int y1 = int(std::ceil(std::max({p0.y(), p1.y(), p2.y()})));
        x0 = std::max(0, x0);
        y0 = std::max(0, y0);
        x1 = std::min(w_ - 1, x1);
        y1 = std::min(h_ - 1, y1);
        if (x1 < x0 || y1 < y0)
            return;

        // 무게중심 좌표는 화면 좌표의 아핀 함수다. 픽셀마다 다시 계산하는
        // 대신 x 로 한 칸 갈 때의 증분을 미리 구해 더한다 — 안쪽 루프에서
        // 곱셈 여섯 번이 사라진다. 디버그 빌드에서 특히 크게 차이 난다.
        const float inv = 1.0f / det;
        const float dadx = (p1.y() - p2.y()) * inv;
        const float dbdx = (p2.y() - p0.y()) * inv;
        const float dady = (p2.x() - p1.x()) * inv;
        const float dbdy = (p0.x() - p2.x()) * inv;
        const float dzda = p0.z() - p2.z();
        const float dzdb = p1.z() - p2.z();

        const float dlda = l0 - l2;
        const float dldb = l1 - l2;
        const int r = base.red(), g = base.green(), b_ = base.blue();

        float aRow = ((p1.y() - p2.y()) * (float(x0) + 0.5f - p2.x())
                      + (p2.x() - p1.x()) * (float(y0) + 0.5f - p2.y())) * inv;
        float bRow = ((p2.y() - p0.y()) * (float(x0) + 0.5f - p2.x())
                      + (p0.x() - p2.x()) * (float(y0) + 0.5f - p2.y())) * inv;

        for (int y = y0; y <= y1; ++y, aRow += dady, bRow += dbdy) {
            auto *row = reinterpret_cast<QRgb *>(img_.scanLine(y));
            float *drow = depth_.data() + size_t(y) * size_t(w_);
            float a = aRow;
            float b = bRow;
            for (int x = x0; x <= x1; ++x, a += dadx, b += dbdx) {
                if (a < 0.0f || b < 0.0f || a + b > 1.0f)
                    continue;
                const float z = p2.z() + a * dzda + b * dzdb;
                if (z >= drow[x])
                    continue;
                drow[x] = z;
                const float l = l2 + a * dlda + b * dldb;
                row[x] = qRgb(int(float(r) * l), int(float(g) * l), int(float(b_) * l));
            }
        }
    }

    const QImage &image() const { return img_; }

private:
    int w_, h_;
    QImage img_;
    std::vector<float> depth_;
};

float illumination(const QVector3D &normal, const QVector3D &light)
{
    return 0.42f + 0.58f * std::abs(QVector3D::dotProduct(normal.normalized(), light));
}

void appendTriangle(std::vector<Tri3> &tris, const QVector3D &a, const QVector3D &b,
                    const QVector3D &c, const QColor &color, const QVector3D &light)
{
    const QVector3D n = QVector3D::crossProduct(b - a, c - a);
    if (n.lengthSquared() < 1e-10f)
        return;
    const float l = illumination(n, light);
    tris.push_back({a, b, c, l, l, l, color});
}

void appendBox(std::vector<Tri3> &tris, const QMatrix4x4 &xf, const QVector3D &half,
               const QColor &color, const QVector3D &light)
{
    const QVector3D local[8] = {
        {-half.x(), -half.y(), -half.z()}, {half.x(), -half.y(), -half.z()},
        {half.x(), half.y(), -half.z()},   {-half.x(), half.y(), -half.z()},
        {-half.x(), -half.y(), half.z()},  {half.x(), -half.y(), half.z()},
        {half.x(), half.y(), half.z()},    {-half.x(), half.y(), half.z()},
    };
    QVector3D p[8];
    for (int i = 0; i < 8; ++i)
        p[i] = xf.map(local[i]);

    static constexpr int quads[6][4] = {
        {0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4},
        {2, 3, 7, 6}, {1, 2, 6, 5}, {0, 4, 7, 3},
    };
    for (const auto &q : quads) {
        appendTriangle(tris, p[q[0]], p[q[1]], p[q[2]], color, light);
        appendTriangle(tris, p[q[0]], p[q[2]], p[q[3]], color, light);
    }
}

// A low-poly tapered cylinder between two world-space points. This is
// deliberately ordinary primitive geometry authored here, rather than a
// conversion of a vendor CAD or mesh asset.
void appendCylinder(std::vector<Tri3> &tris, const QVector3D &from, const QVector3D &to,
                    float fromRadius, float toRadius, const QColor &color,
                    const QVector3D &light, int sides = 12)
{
    const QVector3D axis = to - from;
    if (axis.lengthSquared() < 1e-8f || sides < 3)
        return;

    const QVector3D w = axis.normalized();
    const QVector3D ref = std::abs(w.z()) < 0.85f ? QVector3D(0, 0, 1)
                                                   : QVector3D(1, 0, 0);
    const QVector3D u = QVector3D::crossProduct(w, ref).normalized();
    const QVector3D v = QVector3D::crossProduct(w, u).normalized();
    constexpr float kTau = 6.28318530718f;

    for (int i = 0; i < sides; ++i) {
        const float a0 = kTau * float(i) / float(sides);
        const float a1 = kTau * float(i + 1) / float(sides);
        const QVector3D r0 = u * std::cos(a0) + v * std::sin(a0);
        const QVector3D r1 = u * std::cos(a1) + v * std::sin(a1);
        const QVector3D p0 = from + r0 * fromRadius;
        const QVector3D p1 = from + r1 * fromRadius;
        const QVector3D q0 = to + r0 * toRadius;
        const QVector3D q1 = to + r1 * toRadius;

        appendTriangle(tris, p0, p1, q1, color, light);
        appendTriangle(tris, p0, q1, q0, color, light);
        appendTriangle(tris, from, p1, p0, color, light);
        appendTriangle(tris, to, q0, q1, color, light);
    }
}

QMatrix4x4 translated(const QVector3D &p)
{
    QMatrix4x4 xf;
    xf.translate(p);
    return xf;
}

void appendMobileBase(std::vector<Tri3> &tris, const QColor &bodyColor,
                      const QColor &legColor, const QVector3D &light)
{
    // This is an intentionally generic quadruped silhouette, not a model of
    // a particular vendor robot. It frames the arm and makes ground contact
    // readable in the operator view.
    appendBox(tris, translated({0, 0, 0.49f}), {0.36f, 0.22f, 0.11f}, bodyColor, light);
    appendBox(tris, translated({0, 0, 0.62f}), {0.25f, 0.15f, 0.035f}, bodyColor, light);

    for (const float x : {-0.27f, 0.27f}) {
        for (const float y : {-0.17f, 0.17f}) {
            const QVector3D hip(x, y, 0.43f);
            const QVector3D knee(x + (x < 0 ? -0.055f : 0.055f), y, 0.22f);
            const QVector3D foot(x, y, 0.055f);
            appendCylinder(tris, hip, knee, 0.034f, 0.029f, legColor, light, 10);
            appendCylinder(tris, knee, foot, 0.029f, 0.022f, legColor, light, 10);
            appendBox(tris, translated({foot.x(), foot.y(), 0.028f}),
                      {0.07f, 0.048f, 0.022f}, legColor, light);
        }
    }
}

void appendArm(std::vector<Tri3> &tris, const QMatrix4x4 &armBase,
               const QList<QMatrix4x4> &frames, const QColor &shellColor,
               const QColor &jointColor, const QVector3D &light)
{
    if (frames.isEmpty())
        return;

    std::vector<QMatrix4x4> worldFrames;
    std::vector<QVector3D> origins;
    worldFrames.reserve(size_t(frames.size()));
    origins.reserve(size_t(frames.size()));
    for (const auto &frame : frames) {
        worldFrames.push_back(armBase * frame);
        origins.push_back(worldFrames.back().map(QVector3D()));
    }

    const QVector3D base = origins.front();
    appendCylinder(tris, base - QVector3D(0, 0, 0.055f), base + QVector3D(0, 0, 0.055f),
                   0.087f, 0.079f, shellColor, light);

    static constexpr float kLinkRadius[] = {0.071f, 0.066f, 0.058f,
                                              0.052f, 0.046f, 0.040f};
    static constexpr float kJointRadius[] = {0.078f, 0.074f, 0.065f,
                                               0.058f, 0.052f, 0.046f};

    for (int i = 1; i < int(origins.size()); ++i) {
        const QVector3D axis = (worldFrames[size_t(i)].map(QVector3D(0, 0, 1))
                                - origins[size_t(i)]).normalized();
        const int ri = qMin(i - 1, 5);
        appendCylinder(tris, origins[size_t(i)] - axis * 0.026f,
                       origins[size_t(i)] + axis * 0.026f, kJointRadius[ri],
                       kJointRadius[ri], jointColor, light, 12);

        if ((origins[size_t(i)] - origins[size_t(i - 1)]).length() > 0.012f) {
            appendCylinder(tris, origins[size_t(i - 1)], origins[size_t(i)],
                           kLinkRadius[ri], kLinkRadius[qMin(ri + 1, 5)],
                           shellColor, light, 12);
        }
    }

    const QVector3D toolAxis = (worldFrames.back().map(QVector3D(0, 0, 1))
                                - origins.back()).normalized();
    appendCylinder(tris, origins.back(), origins.back() + toolAxis * 0.055f,
                   0.034f, 0.027f, shellColor, light, 12);
}

}  // namespace

Robot3DView::Robot3DView(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(240);
    setCursor(Qt::OpenHandCursor);
    joints_ = {robot::kArmHome.begin(), robot::kArmHome.end()};
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
    if (q.size() < robot::kArmJointCount)
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
    elevation_ = 0.30;
    distance_ = 3.2;
    target_ = QVector3D(0, 0, 0.75);
    update();
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

    // ---- Wego 절차 생성 형상 ----
    const QColor bodyColor(C.isDark() ? QColor(0x3A, 0x42, 0x4D) : QColor(0x9A, 0xA4, 0xB0));
    const QColor legColor(C.isDark() ? QColor(0x2E, 0x35, 0x3E) : QColor(0x84, 0x8E, 0x9A));
    // 편집 중에는 보낼 자세를 그대로 그린다. 실제 자세 위에 반투명 팔을
    // 겹쳐 봤더니 로봇이 두 대로 보였다.
    const bool previewing = !preview_.isEmpty();
    const QColor armColor = singularWarn_ ? QColor(C.warning)
                            : previewing  ? QColor(C.accentHi)
                                          : QColor(C.accent);

    QMatrix4x4 armBase;
    armBase.translate(float(robot::kArmMountX), float(robot::kArmMountY),
                      0.50f + float(robot::kArmMountZ));
    const auto frames = robot::jointFrames(shown_);

    std::vector<Tri3> tris;
    tris.reserve(1200);

    const QVector3D light = QVector3D(0.4f, -0.5f, 0.8f).normalized();
    appendMobileBase(tris, bodyColor, legColor, light);
    const QColor jointColor(C.isDark() ? QColor(0x1D, 0x2A, 0x38)
                                       : QColor(0x3E, 0x5B, 0x70));
    appendArm(tris, armBase, frames, armColor, jointColor, light);

    // 끝단(카메라) 표시. 팔 색과 구분되는 한 덩이라, 조작자가 "지금 어디를
    // 보고 있나" 를 형상 속에서 찾지 않아도 된다. 깊이 버퍼로 넘어오면서
    // 한 번 빠뜨렸는데, 없으면 끝단 탭의 좌표가 그림의 어디를 말하는지
    // 알 수 없다.
    if (!frames.isEmpty()) {
        const QMatrix4x4 ee = armBase * frames.last();
        const QVector3D h(0.045f, 0.030f, 0.025f);
        const QVector3D c[8] = {{-h.x(), -h.y(), -h.z()}, {h.x(), -h.y(), -h.z()},
                                {h.x(), h.y(), -h.z()},   {-h.x(), h.y(), -h.z()},
                                {-h.x(), -h.y(), h.z()},  {h.x(), -h.y(), h.z()},
                                {h.x(), h.y(), h.z()},    {-h.x(), h.y(), h.z()}};
        static const int quads[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4},
                                        {2, 3, 7, 6}, {1, 2, 6, 5}, {0, 4, 7, 3}};
        const QColor eeColor(C.success);
        for (const auto &q : quads) {
            const QVector3D p0 = ee.map(c[q[0]]), p1 = ee.map(c[q[1]]);
            const QVector3D p2 = ee.map(c[q[2]]), p3 = ee.map(c[q[3]]);
            const QVector3D n = QVector3D::crossProduct(p1 - p0, p2 - p0).normalized();
            const float l = illumination(n, light);
            tris.push_back({p0, p1, p2, l, l, l, eeColor});
            tris.push_back({p0, p2, p3, l, l, l, eeColor});
        }
    }

    // ---- 래스터화 ----
    //
    // 위젯보다 2 배로 그린 뒤 줄인다. 깊이 버퍼는 픽셀 단위로 잘라내므로
    // 가장자리가 계단으로 남는데, 이렇게 하면 QPainter 의 안티앨리어싱과
    // 비슷한 결과를 4 배의 채우기 비용으로 얻는다.
    constexpr int kSuperSample = 2;
    const int rw = width() * kSuperSample;
    const int rh = height() * kSuperSample;
    DepthRaster raster(rw, rh);

    const QColor staleColor(C.textMute);

    for (const auto &t : tris) {
        const QVector3D pts[3] = {mvp.map(t.a), mvp.map(t.b), mvp.map(t.c)};
        // NDC 를 벗어난 삼각형은 버린다. 카메라 뒤로 넘어간 정점은 투영이
        // 뒤집혀 화면 반대편에 거대한 삼각형을 그린다.
        bool clipped = false;
        for (const auto &q : pts)
            clipped |= (q.z() < -1.0f || q.z() > 1.0f);
        if (clipped)
            continue;

        QVector3D sp[3];
        for (int k = 0; k < 3; ++k) {
            sp[k] = QVector3D(float((pts[k].x() * 0.5 + 0.5) * rw),
                              float((1.0 - (pts[k].y() * 0.5 + 0.5)) * rh),
                              pts[k].z());
        }
        // 뒤를 향한 면은 그리지 않는다. 닫힌 형상이라 결과는 같고 채우기는
        // 절반이 된다.
        const float area = (sp[1].x() - sp[0].x()) * (sp[2].y() - sp[0].y())
                           - (sp[2].x() - sp[0].x()) * (sp[1].y() - sp[0].y());
        if (area <= 0.0f)
            continue;

        raster.triangle(sp[0], sp[1], sp[2], t.la, t.lb, t.lc,
                        stale_ ? staleColor : t.color);
    }

    p.drawImage(rect(), raster.image());

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

}  // namespace hmi::ui
