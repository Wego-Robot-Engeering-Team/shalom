#include "theme/Tokens.h"

namespace gcs::theme {

const Colors kDark{
    QLatin1String("dark"),
    QLatin1String("#101317"), QLatin1String("#161A1F"), QLatin1String("#1D2228"),
    QLatin1String("#252B32"), QLatin1String("#0C0F12"),
    QLatin1String("#252A31"), QLatin1String("#333A43"),
    QLatin1String("#E3E6EA"), QLatin1String("#98A0AA"), QLatin1String("#69727C"),
    QLatin1String("#FFFFFF"),
    QLatin1String("#4A8FE7"), QLatin1String("#66A2EF"), QLatin1String("#3A76C4"),
    QLatin1String("#3FA46A"), QLatin1String("#D2963C"),
    QLatin1String("#DC5B53"), QLatin1String("#E87068"), QLatin1String("#B4453F"),
    QLatin1String("#3FA46A"), QLatin1String("#4A8FE7"), QLatin1String("#69727C"),
    QLatin1String("#DC5B53"),
    QLatin1String("#232830"), QLatin1String("#5B6672"), QLatin1String("#14181C"),
    QLatin1String("#4A8FE7"), QLatin1String("#7B858F"), QLatin1String("#D2963C"),
};

const Colors kLight{
    QLatin1String("light"),
    QLatin1String("#F4F5F7"), QLatin1String("#FFFFFF"), QLatin1String("#F0F2F5"),
    QLatin1String("#E6E9ED"), QLatin1String("#FFFFFF"),
    QLatin1String("#E1E4E9"), QLatin1String("#C8CDD4"),
    QLatin1String("#1B1F24"), QLatin1String("#586069"), QLatin1String("#868E96"),
    QLatin1String("#FFFFFF"),
    QLatin1String("#2C6FD1"), QLatin1String("#3E82E4"), QLatin1String("#245BAC"),
    QLatin1String("#1D8A52"), QLatin1String("#B0741A"),
    QLatin1String("#C33E36"), QLatin1String("#D24C43"), QLatin1String("#9E2F29"),
    QLatin1String("#1D8A52"), QLatin1String("#2C6FD1"), QLatin1String("#868E96"),
    QLatin1String("#C33E36"),
    QLatin1String("#FFFFFF"), QLatin1String("#6E7883"), QLatin1String("#E3E6EA"),
    QLatin1String("#2C6FD1"), QLatin1String("#8A929B"), QLatin1String("#B0741A"),
};

namespace {
// 기본값은 라이트. 검수고 조명 환경과 인쇄 보고서 캡처를 고려한 선택이며,
// 관제실이 어두우면 상단바 토글로 즉시 전환한다.
const Colors *g_current = &kLight;
}  // namespace

const Colors &colors()
{
    return *g_current;
}

const Colors &setTheme(const QString &name)
{
    g_current = (name == QLatin1String("dark")) ? &kDark : &kLight;
    return *g_current;
}

const Colors &toggleTheme()
{
    return setTheme(g_current->isDark() ? QStringLiteral("light") : QStringLiteral("dark"));
}

QString monoFamily()
{
    QString first = QString::fromLatin1(type::mono).section(QLatin1Char(','), 0, 0).trimmed();
    return first.remove(QLatin1Char('"'));
}

}  // namespace gcs::theme
