#include "theme/Style.h"

#include <QColor>
#include <QList>
#include <QPair>
#include <QStyle>
#include <QWidget>

#include <algorithm>

#include "theme/Tokens.h"

namespace gcs::theme {

QString rgba(const QString &hexColor, double alpha)
{
    const QColor c(hexColor);
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(c.red()).arg(c.green()).arg(c.blue())
        .arg(alpha, 0, 'f', 3);
}

void repolish(QWidget *w)
{
    if (!w)
        return;
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

namespace {

// 플레이스홀더 치환.
//
// %1..%N 을 수십 개 쓰면 순서가 어긋나도 컴파일이 통과해서 조용히 망가진다.
// 이름표를 쓰면 QSS 원문이 그대로 읽힌다.
//
// ⚠️ 반드시 **긴 이름부터** 치환한다. @accent 를 먼저 치환하면
// @accentSoft 가 "rgba(...)Soft" 로 깨진다. 이름 간 접두사 관계를 피하는
// 규율에 기대지 않고 순서로 강제한다 — 토큰이 늘어날수록 규율은 무너진다.
QString substitute(QString qss, QList<QPair<QString, QString>> vars)
{
    std::sort(vars.begin(), vars.end(),
              [](const auto &a, const auto &b) { return a.first.size() > b.first.size(); });
    for (const auto &[key, value] : vars)
        qss.replace(key, value);
    return qss;
}

const char *kTemplate = R"QSS(
/* ===================== 전역 ===================== */
* {
    font-family: @ui;
    font-size: @fsMd;
    color: @text;
    outline: none;
}
QWidget { background: transparent; }
QMainWindow, #Root { background: @bg; }

QToolTip {
    background: @surfaceHi;
    color: @text;
    border: 1px solid @borderHi;
    border-radius: @rSm;
    padding: 3px 7px;
}
QMessageBox { background: @surface; }

/* ===================== 면 ===================== */
/* 그림자 없음. 경계선 한 줄로만 층을 나눈다. */
#Card, #TopBar {
    background: @surface;
    border: 1px solid @border;
    border-radius: @rLg;
}
/* 떠 있는 팝업은 아래 내용과 겹치므로 경계선을 한 단계 강하게 준다.
   그림자를 쓰지 않는 대신 테두리 대비로 층을 구분한다. */
#InfoPopup {
    background: @surface;
    border: 1px solid @textMute;
    border-radius: @rLg;
}

#CardHeader {
    background: transparent;
    border: none;
    border-bottom: 1px solid @border;
}
#CardTitle { font-size: @fsMd; font-weight: 600; color: @text; }
#SectionLabel { font-size: @fsSm; color: @textMute; }
#Hint { color: @textMute; font-size: @fsSm; }
#HLine { background: @border; border: none; }
#AppTitle { font-size: @fsXl; font-weight: 650; color: @text; }
#AppSubtitle { font-size: @fsSm; color: @textMute; }

/* ===================== 버튼 ===================== */
QPushButton {
    background: @surfaceHi;
    border: 1px solid @borderHi;
    border-radius: @rMd;
    padding: 0 10px;
    min-height: @ctlH;
    font-weight: 500;
    color: @text;
}
QPushButton:hover   { background: @surfaceHover; }
QPushButton:pressed { background: @border; }
QPushButton:disabled {
    background: transparent;
    color: @textMute;
    border-color: @border;
}
QPushButton:checked {
    background: @accentSoft;
    border-color: @accent;
    color: @accent;
}
QPushButton[variant="primary"] {
    background: @accent; border-color: @accent;
    color: @textOnAccent; font-weight: 600;
}
QPushButton[variant="primary"]:hover   { background: @accentHi; border-color: @accentHi; }
QPushButton[variant="primary"]:pressed { background: @accentLo; }
QPushButton[variant="danger"] {
    background: transparent; border-color: @danger;
    color: @danger; font-weight: 600;
}
QPushButton[variant="danger"]:hover {
    background: @danger; border-color: @danger; color: @textOnAccent;
}
QPushButton[variant="ghost"] {
    background: transparent; border-color: transparent; color: @textDim;
}
QPushButton[variant="ghost"]:hover { background: @surfaceHi; color: @text; }
QPushButton[size="sm"] { min-height: @ctlHSm; padding: 0 8px; font-size: @fsSm; }

/* ===================== 입력 ===================== */
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit {
    background: @surfaceHi;
    border: 1px solid @borderHi;
    border-radius: @rMd;
    padding: 0 7px;
    min-height: @ctlHSm;
    font-family: @mono;
    selection-background-color: @accent;
    selection-color: @textOnAccent;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
    border-color: @accent;
}
QComboBox { font-family: @ui; }
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView {
    background: @surface;
    border: 1px solid @borderHi;
    selection-background-color: @accentSoft;
    padding: 2px;
}
QSpinBox::up-button, QSpinBox::down-button,
QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; border: none; }

/* ===================== 슬라이더 ===================== */
QSlider::groove:horizontal { height: 3px; background: @borderHi; border-radius: 1px; }
QSlider::sub-page:horizontal { background: @accent; border-radius: 1px; }
QSlider::handle:horizontal {
    background: @surface;
    border: 2px solid @accent;
    width: 10px; height: 10px; border-radius: 7px; margin: -6px 0;
}
QSlider::handle:horizontal:hover { background: @accent; }
QSlider::groove:horizontal:disabled   { background: @border; }
QSlider::sub-page:horizontal:disabled { background: @textMute; }
QSlider::handle:horizontal:disabled   { border-color: @textMute; }
QSlider[warn="true"]::sub-page:horizontal { background: @warning; }
QSlider[warn="true"]::handle:horizontal   { border-color: @warning; }

/* ===================== 리스트 ===================== */
QListWidget, QTreeWidget, QTableWidget { background: transparent; border: none; }
QListWidget::item { border: none; }
QHeaderView::section {
    background: transparent; border: none;
    border-bottom: 1px solid @border;
    color: @textMute; font-size: @fsSm;
    padding: 3px 6px;
}

/* ===================== 스크롤바 ===================== */
QScrollArea { background: transparent; border: none; }
QScrollBar:vertical   { background: transparent; width: 9px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 9px; margin: 0; }
QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
    background: @borderHi; border-radius: 4px; min-height: 24px; min-width: 24px;
}
QScrollBar::handle:hover { background: @textMute; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* ===================== 스플리터 ===================== */
QSplitter::handle { background: transparent; }
QSplitter::handle:horizontal { width: @s3; }
QSplitter::handle:vertical   { height: @s3; }

/* ===================== 배지 ===================== */
/* 남용하지 않는다. 상태가 '변할 때'만 의미가 있는 자리에만 쓴다. */
#Badge {
    background: transparent;
    border: 1px solid @borderHi;
    border-radius: @rSm;
    padding: 1px 6px;
    font-size: @fsSm;
    font-weight: 500;
    color: @textDim;
}
#Badge[tone="ok"]     { color: @success; border-color: @successFaint; }
#Badge[tone="warn"]   { color: @warning; border-color: @warningFaint; }
#Badge[tone="danger"] { color: @danger;  border-color: @dangerFaint; }
#Badge[tone="info"]   { color: @accent;  border-color: @accentFaint; }

/* ===================== 수치 ===================== */
#Mono      { font-family: @mono; color: @textDim; font-size: @fsSm; }
#Readout   { font-family: @mono; color: @text; font-size: @fsMd; font-weight: 500; }
#ReadoutLg { font-family: @mono; color: @text; font-size: @fsLg; font-weight: 600; }

/* ===================== 지도 오버레이 ===================== */
#MapOverlay {
    background: @overlaySoft;
    border: 1px solid @borderHi;
    border-radius: @rMd;
}
#MapReadout {
    background: @overlaySoft;
    border: 1px solid @borderHi;
    border-radius: @rMd;
    padding: 3px 8px;
    font-family: @mono;
    font-size: @fsSm;
    color: @textDim;
}
)QSS";

QString px(int v)
{
    return QString::number(v) + QStringLiteral("px");
}

}  // namespace

QString buildQss(const Colors *pal)
{
    const Colors &C = pal ? *pal : colors();

    // 접두사 관계가 있는 이름들이 섞여 있다(@accent / @accentSoft / @accentHi …).
    // substitute() 가 길이 내림차순으로 치환하므로 여기 순서는 무관하다.
    QList<QPair<QString, QString>> v{
        {QStringLiteral("@ui"), QString::fromLatin1(type::ui)},
        {QStringLiteral("@mono"), QString::fromLatin1(type::mono)},

        {QStringLiteral("@fsXs"), px(type::xs)},
        {QStringLiteral("@fsSm"), px(type::sm)},
        {QStringLiteral("@fsMd"), px(type::md)},
        {QStringLiteral("@fsLg"), px(type::lg)},
        {QStringLiteral("@fsXl"), px(type::xl)},

        {QStringLiteral("@rSm"), px(metrics::rSm)},
        {QStringLiteral("@rMd"), px(metrics::rMd)},
        {QStringLiteral("@rLg"), px(metrics::rLg)},
        {QStringLiteral("@s3"), px(metrics::s3)},
        {QStringLiteral("@ctlHSm"), px(metrics::ctlHSm)},
        {QStringLiteral("@ctlH"), px(metrics::ctlH)},

        {QStringLiteral("@bg"), C.bg},
        {QStringLiteral("@surfaceHover"), C.surfaceHover},
        {QStringLiteral("@surfaceHi"), C.surfaceHi},
        {QStringLiteral("@surface"), C.surface},
        {QStringLiteral("@overlaySoft"), rgba(C.overlay, 0.90)},
        {QStringLiteral("@borderHi"), C.borderHi},
        {QStringLiteral("@border"), C.border},
        {QStringLiteral("@textOnAccent"), C.textOnAccent},
        {QStringLiteral("@textDim"), C.textDim},
        {QStringLiteral("@textMute"), C.textMute},
        {QStringLiteral("@text"), C.text},
        {QStringLiteral("@accentSoft"), rgba(C.accent, 0.12)},
        {QStringLiteral("@accentFaint"), rgba(C.accent, 0.40)},
        {QStringLiteral("@accentHi"), C.accentHi},
        {QStringLiteral("@accentLo"), C.accentLo},
        {QStringLiteral("@accent"), C.accent},
        {QStringLiteral("@successFaint"), rgba(C.success, 0.40)},
        {QStringLiteral("@success"), C.success},
        {QStringLiteral("@warningFaint"), rgba(C.warning, 0.40)},
        {QStringLiteral("@warning"), C.warning},
        {QStringLiteral("@dangerFaint"), rgba(C.danger, 0.40)},
        {QStringLiteral("@dangerHi"), C.dangerHi},
        {QStringLiteral("@dangerLo"), C.dangerLo},
        {QStringLiteral("@danger"), C.danger},
    };

    return substitute(QString::fromUtf8(kTemplate), v);
}

}  // namespace gcs::theme
