#include "widgets/Toast.h"

#include <QEasingCurve>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QTimer>

#include "theme/Tokens.h"

namespace gcs::ui {

using namespace gcs::theme;

namespace {

constexpr int kWidth = 380;
constexpr int kMargin = 16;
constexpr int kGap = 8;

/// 마우스를 올렸을 때 남겨 주는 최소 시간.
constexpr int kHoverGraceMs = 2000;

/// 표시 시간. 짧게 스쳐 지나가게 두고, 놓친 것은 상단 바의 알림함에서
/// 다시 본다. 예전에는 최대 20 초씩 떠 있어 화면을 가렸고, 마우스를 올린
/// 채로 두면 사라지지도 않았다.
int lifetimeMs(const QString &severity)
{
    if (severity == QLatin1String("critical"))
        return 6000;
    if (severity == QLatin1String("error"))
        return 5000;
    if (severity == QLatin1String("warn"))
        return 4000;
    return 3000;
}

QColor severityColor(const QString &severity)
{
    const Colors &C = colors();
    if (severity == QLatin1String("ok"))
        return QColor(C.success);
    if (severity == QLatin1String("warn"))
        return QColor(C.warning);
    if (severity == QLatin1String("error") || severity == QLatin1String("critical"))
        return QColor(C.danger);
    return QColor(C.accent);
}

}  // namespace

Toast::Toast(const QString &title, const QString &detail, const QString &severity,
             QWidget *parent)
    : QWidget(parent), title_(title), detail_(detail), severity_(severity)
{
    // 포커스를 절대 가져가지 않는다. 조작 중에 키 입력이 토스트로 새면
    // 조작자가 눌렀다고 생각한 것이 눌리지 않는다.
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setCursor(Qt::PointingHandCursor);

    QFont f;
    f.setPointSize(10);
    const QFontMetrics fm(f);
    const int detailHeight =
        detail_.isEmpty()
            ? 0
            : fm.boundingRect(0, 0, kWidth - 48, 1000, Qt::TextWordWrap, detail_).height();
    setFixedSize(kWidth, 34 + detailHeight + (detail_.isEmpty() ? 0 : 6));

    life_ = new QTimer(this);
    life_->setSingleShot(true);
    life_->setInterval(lifetimeMs(severity_));
    connect(life_, &QTimer::timeout, this, &Toast::dismiss);
    life_->start();

    auto *anim = new QPropertyAnimation(this, "appear", this);
    anim->setDuration(180);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void Toast::setAppear(double v)
{
    appear_ = v;
    update();
}

void Toast::dismiss()
{
    life_->stop();
    emit dismissed(this);
    deleteLater();
}

void Toast::mousePressEvent(QMouseEvent *)
{
    dismiss();
}

void Toast::enterEvent(QEnterEvent *ev)
{
    // 읽는 동안 시간을 조금 벌어 준다. 예전처럼 타이머를 멈춰 버리면
    // 마우스가 그 위에 놓인 채로 있는 한 알림이 영영 사라지지 않았다.
    if (life_->remainingTime() < kHoverGraceMs)
        life_->start(kHoverGraceMs);
    QWidget::enterEvent(ev);
}

void Toast::leaveEvent(QEvent *ev)
{
    QWidget::leaveEvent(ev);
}

void Toast::paintEvent(QPaintEvent *)
{
    const Colors &C = colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 등장할 때 살짝 떠오른다. 갑자기 나타나면 조작자가 놓친다.
    p.translate(0, (1.0 - appear_) * 8);
    p.setOpacity(appear_);

    const QRectF box = rect().adjusted(0, 0, -1, -1);
    const QColor accent = severityColor(severity_);

    p.setPen(QPen(QColor(C.borderHi), 1));
    p.setBrush(QColor(C.surface));
    p.drawRoundedRect(box, metrics::rMd, metrics::rMd);

    // 좌측 심각도 막대
    p.setPen(Qt::NoPen);
    p.setBrush(accent);
    p.drawRoundedRect(QRectF(box.left() + 1, box.top() + 1, 3, box.height() - 2), 1.5, 1.5);

    QFont ft;
    ft.setPointSize(11);
    ft.setWeight(QFont::DemiBold);
    p.setFont(ft);
    p.setPen(accent);
    p.drawText(QRectF(16, 8, width() - 32, 18), Qt::AlignLeft | Qt::AlignVCenter, title_);

    if (!detail_.isEmpty()) {
        QFont fd;
        fd.setPointSize(10);
        p.setFont(fd);
        p.setPen(QColor(C.textDim));
        p.drawText(QRectF(16, 28, width() - 32, height() - 34),
                   Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, detail_);
    }
}

// ============================ ToastHost ============================

ToastHost::ToastHost(QWidget *host) : QObject(host), host_(host) {}

void ToastHost::setBottomAnchor(int pixelsFromBottom)
{
    anchorFromBottom_ = pixelsFromBottom;
    relayout();
}

void ToastHost::show(const QString &title, const QString &detail, const QString &severity)
{
    auto *toast = new Toast(title, detail, severity, host_);
    connect(toast, &Toast::dismissed, this, [this](Toast *t) {
        toasts_.removeOne(t);
        relayout();
    });

    toasts_.append(toast);
    // 사건이 몰릴 때 화면이 알림으로만 덮이면 정작 지도를 못 본다.
    // 큐에 쌓지 않고 오래된 것을 버린다 — 지금 중요한 것은 최신 소식이다.
    while (toasts_.size() > kMaxVisible)
        toasts_.takeFirst()->dismiss();

    toast->show();
    toast->raise();
    relayout();
}

void ToastHost::relayout()
{
    if (!host_)
        return;

    // 이벤트 로그 위쪽에 띄운다. 로그 바로 위에 겹치면 같은 내용이 두 번
    // 보이고, 그중 하나는 곧 사라져서 어느 쪽을 봐야 하는지 헷갈린다.
    // 비상정지는 우상단이므로 어느 경우에도 가리지 않는다.
    int y = host_->height() - kMargin - anchorFromBottom_;
    for (int i = toasts_.size() - 1; i >= 0; --i) {
        Toast *t = toasts_.at(i);
        y -= t->height();
        t->move((host_->width() - t->width()) / 2, y);
        t->raise();
        y -= kGap;
    }
}

}  // namespace gcs::ui
