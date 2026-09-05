#include "diag/CodeInfoPopup.h"

#include <QApplication>
#include <QFrame>
#include <QGuiApplication>
#include <QLabel>
#include <QScreen>
#include <QVBoxLayout>

#include "diag/CodeCatalog.h"
#include "theme/Tokens.h"
#include "widgets/Primitives.h"

namespace gcs::diag {

using namespace gcs::theme;
using gcs::ui::Badge;

namespace {

/// 심각도 → Badge tone. 카탈로그의 5단계를 UI 의 4색으로 접는다.
QString toneFor(Severity s)
{
    switch (s) {
    case Severity::Ok: return QStringLiteral("ok");
    case Severity::Warn: return QStringLiteral("warn");
    case Severity::Error:
    case Severity::Critical: return QStringLiteral("danger");
    case Severity::Info: break;
    }
    return QStringLiteral("info");
}

QString esc(const QString &s)
{
    return s.toHtmlEscaped();
}

}  // namespace

CodeInfoPopup::CodeInfoPopup(const CodeEntry &e, QWidget *parent)
    : QWidget(parent, Qt::Popup)
{
    setObjectName(QStringLiteral("InfoPopup"));
    setAttribute(Qt::WA_DeleteOnClose);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(metrics::s4, metrics::s3, metrics::s4, metrics::s4);
    lay->setSpacing(metrics::s2);

    // ---- 코드 + 심각도 ----
    auto *head = new QHBoxLayout;
    head->setSpacing(metrics::s2);
    auto *codeLbl = ui::readout(e.code);
    head->addWidget(codeLbl);
    head->addWidget(new Badge(severityLabel(e.severity), toneFor(e.severity)));
    head->addStretch(1);
    lay->addLayout(head);

    // ---- 제목 ----
    auto *title = new QLabel(e.title);
    title->setObjectName(QStringLiteral("CardTitle"));
    title->setWordWrap(true);
    lay->addWidget(title);

    lay->addWidget(new ui::HLine);

    // ---- 원인 ----
    lay->addWidget(ui::sectionLabel(QStringLiteral("원인")));
    auto *cause = new QLabel(e.cause);
    cause->setWordWrap(true);
    lay->addWidget(cause);

    // ---- 조치 ----
    if (!e.actions.isEmpty()) {
        lay->addSpacing(metrics::s1);
        lay->addWidget(ui::sectionLabel(QStringLiteral("조치")));
        QString html = QStringLiteral("<ol style='margin:0 0 0 16px; padding:0;'>");
        for (const auto &a : e.actions)
            html += QStringLiteral("<li style='margin-bottom:3px;'>%1</li>").arg(esc(a));
        html += QStringLiteral("</ol>");
        auto *actions = new QLabel(html);
        actions->setTextFormat(Qt::RichText);
        actions->setWordWrap(true);
        lay->addWidget(actions);
    }

    // ---- 관련 채널 ----
    if (!e.channel.isEmpty() && e.channel != QLatin1String("-")) {
        lay->addSpacing(metrics::s1);
        auto *ch = ui::readout(QStringLiteral("채널  %1").arg(e.channel));
        ch->setObjectName(QStringLiteral("Mono"));
        lay->addWidget(ch);
    }

    setFixedWidth(380);
    adjustSize();
}

void CodeInfoPopup::showFor(const QString &code, const QPoint &globalPos, QWidget *parent)
{
    const CodeEntry *e = CodeCatalog::instance().find(code);
    if (!e)
        return;

    auto *popup = new CodeInfoPopup(*e, parent);

    // 화면 밖으로 나가지 않도록 앵커를 접는다. 로그 패널은 보통 화면 가장자리에
    // 붙어 있어서 그대로 띄우면 잘린다.
    QPoint pos = globalPos + QPoint(12, 12);
    if (const QScreen *scr = QGuiApplication::screenAt(globalPos)) {
        const QRect avail = scr->availableGeometry();
        const QSize sz = popup->size();
        if (pos.x() + sz.width() > avail.right())
            pos.setX(globalPos.x() - sz.width() - 12);
        if (pos.y() + sz.height() > avail.bottom())
            pos.setY(qMax(avail.top(), globalPos.y() - sz.height() - 12));
    }
    popup->move(pos);
    popup->show();
}

QString CodeInfoPopup::tooltipHtml(const QString &code)
{
    const CodeEntry *e = CodeCatalog::instance().find(code);
    if (!e) {
        // 카탈로그에 없는 코드도 감춘다면 조작자가 신고할 단서를 잃는다.
        return QStringLiteral("<b>%1</b><br/><i>카탈로그에 없는 코드입니다. "
                              "버전 불일치일 수 있습니다.</i>").arg(esc(code));
    }

    QString html = QStringLiteral("<div style='max-width:320px;'>"
                                  "<b>%1</b> — %2<br/><br/>%3")
                       .arg(esc(e->code), esc(e->title), esc(e->cause));
    if (!e->actions.isEmpty())
        html += QStringLiteral("<br/><br/><b>조치</b><br/>· %1").arg(esc(e->actions.first()));
    html += QStringLiteral("<br/><br/><i>클릭하면 전체 내용</i></div>");
    return html;
}

}  // namespace gcs::diag
