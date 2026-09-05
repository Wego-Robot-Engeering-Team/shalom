// Header hygiene checks.
//
// These catch a class of mistake that compiles cleanly in the header itself and
// only fails later, in an unrelated translation unit, with a confusing message.
//
// The specific trap this guards against: writing
//
//     namespace gcs::ui {
//     class Widget { QLabel *label_; };   // with `class QLabel *label_;`
//     }
//
// declares a *new* type gcs::ui::QLabel rather than referring to Qt's QLabel.
// Any later `class Badge : public QLabel` inside the same namespace then
// inherits from that incomplete placeholder, and the error surfaces in a file
// that did nothing wrong. Qt forward declarations must sit at global scope,
// above the namespace.
//
// This bit us three times during the port before the check existed.

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTest>

class TestHeaders : public QObject {
    Q_OBJECT

private:
    static QStringList headerFiles()
    {
        QStringList out;
        QDir root(QStringLiteral(GCS_SOURCE_DIR "/src"));
        QDirIterator it(root.absolutePath(), {QStringLiteral("*.h")}, QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext())
            out << it.next();
        return out;
    }

    static QString readAll(const QString &path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }

private slots:

    void headersExist()
    {
        QVERIFY2(!headerFiles().isEmpty(),
                 "헤더를 하나도 찾지 못했다 — GCS_SOURCE_DIR 설정을 확인할 것");
    }

    /// 네임스페이스 안에서 `class QFoo *member;` 또는 `class QFoo;` 를 쓰면
    /// Qt 타입이 아니라 동명의 새 타입이 선언된다.
    void noElaboratedQtTypeInsideNamespace()
    {
        // 멤버 선언(`class QFoo *x;`)과 템플릿 인자(`QList<class QFoo *>`) 양쪽.
        const QRegularExpression bad(QStringLiteral(R"(\bclass\s+Q[A-Za-z]+\s*\*)"));

        QStringList offenders;
        for (const QString &path : headerFiles()) {
            const QString src = readAll(path);
            int nsDepth = 0;
            const QStringList lines = src.split(QLatin1Char('\n'));
            for (int i = 0; i < lines.size(); ++i) {
                const QString &line = lines.at(i);
                if (line.startsWith(QLatin1String("namespace ")))
                    ++nsDepth;
                if (nsDepth > 0 && bad.match(line).hasMatch()) {
                    offenders << QStringLiteral("%1:%2  %3")
                                     .arg(QFileInfo(path).fileName())
                                     .arg(i + 1)
                                     .arg(line.trimmed());
                }
            }
        }
        QVERIFY2(offenders.isEmpty(),
                 qPrintable(QStringLiteral(
                                "네임스페이스 안에서 Qt 타입을 전방 선언했다. "
                                "전역 스코프로 올릴 것:\n  %1")
                                .arg(offenders.join(QStringLiteral("\n  ")))));
    }

    /// 배포 대상 헤더는 영문 주석으로 유지한다(고객 배포용 API 레퍼런스).
    /// 구현부(.cpp)의 한국어 주석은 임치 요건이므로 검사 대상이 아니다.
    void publicHeadersAreEnglish()
    {
        // 한글 음절 영역 U+AC00..U+D7A3
        const QRegularExpression hangul(QStringLiteral("[\\x{AC00}-\\x{D7A3}]"));

        QStringList offenders;
        for (const QString &path : headerFiles()) {
            const auto m = hangul.match(readAll(path));
            if (m.hasMatch())
                offenders << QFileInfo(path).fileName();
        }
        QVERIFY2(offenders.isEmpty(),
                 qPrintable(QStringLiteral("헤더에 한글이 있다 (영문으로 옮길 것): %1")
                                .arg(offenders.join(QStringLiteral(", ")))));
    }

    /// 모든 헤더에 include 가드가 있어야 한다.
    void allHeadersHavePragmaOnce()
    {
        QStringList offenders;
        for (const QString &path : headerFiles()) {
            if (!readAll(path).contains(QStringLiteral("#pragma once")))
                offenders << QFileInfo(path).fileName();
        }
        QVERIFY2(offenders.isEmpty(),
                 qPrintable(QStringLiteral("#pragma once 누락: %1")
                                .arg(offenders.join(QStringLiteral(", ")))));
    }
};

QTEST_APPLESS_MAIN(TestHeaders)
#include "test_headers.moc"
