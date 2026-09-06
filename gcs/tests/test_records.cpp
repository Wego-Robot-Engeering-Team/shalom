// Inspection record parsing tests.
//
// The file name is the only thing that identifies a capture once it is on the
// share, and its form is fixed by the statement of work. Parsing it back has
// to be exact, and it has to fail loudly on names that do not match rather
// than inventing plausible values.

#include <QDir>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

#include "data/InspectionRecord.h"

using namespace gcs::data;

namespace {

void writeFile(const QString &path, const QString &content = QStringLiteral("x"))
{
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly), qPrintable(path));
    QTextStream(&f) << content;
}

}  // namespace

class TestRecords : public QObject {
    Q_OBJECT

private slots:

    void parses_mandatedFileName()
    {
        const auto r = parseFileName(QStringLiteral("1234.05_05_C01-P03,20260906140321.jpg"));
        QVERIFY(r.has_value());
        QCOMPARE(r->vehicleNumber, QStringLiteral("1234.05"));
        QCOMPARE(r->carNumber, QStringLiteral("05"));
        QCOMPARE(r->pointId, QStringLiteral("C01-P03"));
        QCOMPARE(r->capturedAt, QDateTime(QDate(2026, 9, 6), QTime(14, 3, 21)));
    }

    /// 차량번호에 점이 들어가므로(편성.량) 점으로 나누면 안 된다.
    void parses_vehicleNumberContainingDot()
    {
        const auto r = parseFileName(QStringLiteral("A1.B2.C3_07_P1,20260101000000.png"));
        QVERIFY(r.has_value());
        QCOMPARE(r->vehicleNumber, QStringLiteral("A1.B2.C3"));
        QCOMPARE(r->carNumber, QStringLiteral("07"));
    }

    void accepts_knownImageExtensions()
    {
        for (const auto &ext : {"jpg", "jpeg", "png", "ply", "pcd", "JPG"}) {
            const QString name =
                QStringLiteral("1234.05_05_P1,20260906140321.%1").arg(QLatin1String(ext));
            QVERIFY2(parseFileName(name).has_value(), ext);
        }
    }

    /// 사이드카나 잡파일을 이미지로 세면 목록이 두 배로 부풀어 보인다.
    void rejects_nonImageFiles()
    {
        QVERIFY(!parseFileName(QStringLiteral("1234.05_05_P1,20260906140321.json")));
        QVERIFY(!parseFileName(QStringLiteral("1234.05_05_P1,20260906140321.txt")));
    }

    /// 규정에 맞지 않는 이름은 지어내지 말고 실패해야 한다.
    void rejects_malformedNames()
    {
        for (const auto *name : {"random.jpg", "1234.05_05_P1.jpg",
                                 "1234.05_05_P1,2026.jpg", "_05_P1,20260906140321.jpg",
                                 "1234.05__P1,20260906140321.jpg"}) {
            QVERIFY2(!parseFileName(QLatin1String(name)).has_value(), name);
        }
    }

    void missingSidecar_isReportedNotHidden()
    {
        auto r = parseFileName(QStringLiteral("1234.05_05_P1,20260906140321.jpg"));
        QVERIFY(r.has_value());
        QVERIFY(!r->hasSidecar());
        QVERIFY(!r->isComplete());
        QVERIFY(r->missingFields().contains(QStringLiteral("메타데이터 파일")));
    }

    // ---- 디렉터리 스캔 ---------------------------------------------------

    void scan_findsImagesAndAttachesSidecars()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString base = dir.path();

        writeFile(base + QStringLiteral("/1234.05_05_C01-P01,20260906140000.jpg"));
        {
            QFile f(base + QStringLiteral("/1234.05_05_C01-P01,20260906140000.json"));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(R"({"tag_id":17,"distance_mm":620})");
        }

        const ScanResult result = scanDirectory(base);
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.records.size(), 1);
        QVERIFY(result.records.first().hasSidecar());
        QCOMPARE(result.records.first().tagId, 17);
        QVERIFY(result.records.first().isComplete());
    }

    /// 사이드카는 목록에 따로 올라오면 안 된다. 이미지 수가 두 배로 보인다.
    void scan_doesNotListSidecarsSeparately()
    {
        QTemporaryDir dir;
        const QString base = dir.path();
        writeFile(base + QStringLiteral("/1234.05_05_P1,20260906140000.jpg"));
        writeFile(base + QStringLiteral("/1234.05_05_P1,20260906140000.json"),
                  QStringLiteral("{}"));

        const ScanResult result = scanDirectory(base);
        QCOMPARE(result.records.size(), 1);
        QVERIFY(result.unrecognised.isEmpty());
    }

    /// 공유 폴더에 예상 못 한 파일이 있으면 조용히 버리지 말고 알린다.
    void scan_reportsUnrecognisedFiles()
    {
        QTemporaryDir dir;
        const QString base = dir.path();
        writeFile(base + QStringLiteral("/1234.05_05_P1,20260906140000.jpg"));
        writeFile(base + QStringLiteral("/메모.txt"));
        writeFile(base + QStringLiteral("/IMG_0042.jpg"));

        const ScanResult result = scanDirectory(base);
        QCOMPARE(result.records.size(), 1);
        QCOMPARE(result.unrecognised.size(), 2);
    }

    void scan_recursesIntoSubdirectories()
    {
        QTemporaryDir dir;
        const QString base = dir.path();
        QVERIFY(QDir(base).mkpath(QStringLiteral("1234/05")));
        writeFile(base + QStringLiteral("/1234/05/1234.05_05_P1,20260906140000.jpg"));

        QCOMPARE(scanDirectory(base).records.size(), 1);
    }

    /// 최신순이어야 한다. 조작자는 방금 찍은 것을 먼저 본다.
    void scan_sortsNewestFirst()
    {
        QTemporaryDir dir;
        const QString base = dir.path();
        writeFile(base + QStringLiteral("/1234.05_05_P1,20260101000000.jpg"));
        writeFile(base + QStringLiteral("/1234.05_05_P2,20260906140000.jpg"));
        writeFile(base + QStringLiteral("/1234.05_05_P3,20260501000000.jpg"));

        const auto records = scanDirectory(base).records;
        QCOMPARE(records.size(), 3);
        QCOMPARE(records.at(0).pointId, QStringLiteral("P2"));
        QCOMPARE(records.at(2).pointId, QStringLiteral("P1"));
    }

    void scan_reportsMissingDirectory()
    {
        const ScanResult result =
            scanDirectory(QStringLiteral("/nonexistent/shalom/share"));
        QVERIFY(!result.error.isEmpty());
        QVERIFY(result.records.isEmpty());
    }

    /// 공유 폴더가 거대할 때 화면이 멈추면 안 된다. 일부만 보여주되 알린다.
    void scan_stopsAtLimitAndSaysSo()
    {
        QTemporaryDir dir;
        const QString base = dir.path();
        for (int i = 0; i < 12; ++i) {
            writeFile(base
                      + QStringLiteral("/1234.05_05_P%1,2026090614%2.jpg")
                            .arg(i)
                            .arg(i, 4, 10, QLatin1Char('0')));
        }
        const ScanResult result = scanDirectory(base, 5);
        QVERIFY2(!result.error.isEmpty(), "상한에 걸린 사실을 알려야 한다");
        QVERIFY(result.records.size() <= 5);
    }
};

QTEST_MAIN(TestRecords)
#include "test_records.moc"
