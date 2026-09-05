// 테마/스타일시트 테스트.
//
// QSS 는 파싱 실패해도 예외가 없다. 규칙 하나가 조용히 무시될 뿐이라
// 육안 검수 전까지 안 드러난다. 그래서 기계로 잡을 수 있는 것은 여기서 잡는다.

#include <QColor>
#include <QRegularExpression>
#include <QTest>

#include "theme/Style.h"
#include "theme/Tokens.h"

using namespace gcs::theme;

class TestTheme : public QObject {
    Q_OBJECT

private slots:

    /// 치환 순서 회귀 테스트.
    /// @accent 가 @accentSoft 보다 먼저 치환되면 "rgba(...)Soft" 같은 잔재가 남는다.
    /// 남은 플레이스홀더가 하나라도 있으면 실패다.
    void qss_noPlaceholderLeftover()
    {
        for (const Colors *p : {&kLight, &kDark}) {
            const QString qss = buildQss(p);
            const QRegularExpression leftover(QStringLiteral("@[A-Za-z]"));
            const auto m = leftover.match(qss);
            QVERIFY2(!m.hasMatch(),
                     qPrintable(QStringLiteral("팔레트 %1 에 미치환 토큰: %2")
                                    .arg(p->name, qss.mid(m.capturedStart(), 24))));
        }
    }

    /// 접두사 관계인 토큰이 잘려나가지 않았는지 직접 확인.
    void qss_prefixTokensSubstitutedWhole()
    {
        const QString qss = buildQss(&kLight);
        // 잘린 잔재 형태들
        for (const auto &frag : {QStringLiteral("Soft"), QStringLiteral("Faint")}) {
            // 색상값 바로 뒤에 남은 접미사가 없어야 한다.
            const QRegularExpression re(QStringLiteral("\\)%1|#[0-9A-Fa-f]{6}%1").arg(frag));
            QVERIFY2(!re.match(qss).hasMatch(),
                     qPrintable(QStringLiteral("치환 잔재 '%1' 발견").arg(frag)));
        }
    }

    /// QSS 의 8자리 hex 는 #AARRGGBB 다. CSS 습관대로 #RRGGBBAA 를 쓰면
    /// 알파가 적색 채널로 들어가 흰색이 연노랑이 된다. rgba() 로만 쓴다.
    void qss_noEightDigitHex()
    {
        for (const Colors *p : {&kLight, &kDark}) {
            const QRegularExpression re(QStringLiteral("#[0-9A-Fa-f]{8}\\b"));
            QVERIFY2(!re.match(buildQss(p)).hasMatch(),
                     "8자리 hex 색상 발견 — rgba() 를 쓸 것 (#AARRGGBB 혼동 방지)");
        }
    }

    void rgba_channelOrder()
    {
        // 흰색 + 알파가 실제로 흰색으로 남아야 한다(연노랑이 되면 안 된다).
        QCOMPARE(rgba(QStringLiteral("#FFFFFF"), 0.9), QStringLiteral("rgba(255, 255, 255, 0.900)"));
        QCOMPARE(rgba(QStringLiteral("#2C6FD1"), 0.12), QStringLiteral("rgba(44, 111, 209, 0.120)"));
    }

    void themeSwitch_roundTrip()
    {
        QCOMPARE(setTheme(QStringLiteral("light")).name, QLatin1String("light"));
        QVERIFY(!colors().isDark());
        QCOMPARE(toggleTheme().name, QLatin1String("dark"));
        QVERIFY(colors().isDark());
        QCOMPARE(toggleTheme().name, QLatin1String("light"));
        setTheme(QStringLiteral("light"));
    }

    /// 과업지시서 2.2.7 [1] ② 규정: 완료(녹색)/현재(파랑)/미완료(회색)/오류(빨강).
    /// 두 테마 모두에서 색상 의미가 유지되는지 색조(hue)로 확인한다.
    void waypointColors_matchSpecSemantics()
    {
        for (const Colors *p : {&kLight, &kDark}) {
            const int doneH = QColor(p->wpDone).hslHue();
            const int currentH = QColor(p->wpCurrent).hslHue();
            const int errorH = QColor(p->wpError).hslHue();
            const int todoS = QColor(p->wpTodo).hslSaturation();

            QVERIFY2(doneH > 90 && doneH < 170, "완료는 녹색 계열이어야 한다");
            QVERIFY2(currentH > 190 && currentH < 250, "현재는 파랑 계열이어야 한다");
            QVERIFY2(errorH < 20 || errorH > 340, "오류는 빨강 계열이어야 한다");
            QVERIFY2(todoS < 60, "미완료는 무채색(회색)이어야 한다");
        }
    }

    /// 두 팔레트의 필드가 하나도 비어 있으면 안 된다.
    /// 새 토큰을 추가하고 한쪽 팔레트에만 채우는 실수를 잡는다.
    void palettes_allFieldsPopulated()
    {
        for (const Colors *p : {&kLight, &kDark}) {
            const QLatin1String fields[] = {
                p->bg, p->surface, p->surfaceHi, p->surfaceHover, p->overlay,
                p->border, p->borderHi, p->text, p->textDim, p->textMute, p->textOnAccent,
                p->accent, p->accentHi, p->accentLo, p->success, p->warning,
                p->danger, p->dangerHi, p->dangerLo,
                p->wpDone, p->wpCurrent, p->wpTodo, p->wpError,
                p->mapFree, p->mapOccupied, p->mapUnknown, p->plan, p->trail, p->tag,
            };
            for (const auto &f : fields) {
                QVERIFY2(!f.isEmpty(), qPrintable(QStringLiteral("%1 에 빈 토큰").arg(p->name)));
                QVERIFY2(QColor(f).isValid(),
                         qPrintable(QStringLiteral("%1 에 잘못된 색상: %2").arg(p->name, f)));
            }
        }
    }
};

QTEST_MAIN(TestTheme)
#include "test_theme.moc"
