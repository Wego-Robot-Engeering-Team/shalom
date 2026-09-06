// Capture metadata tests.
//
// The field set and the file name are fixed by the statement of work and are
// acceptance items. A capture saved with an incomplete record may be excluded
// at inspection, which means sending the robot back under the train, so the
// rules are pinned here rather than left to the form's validation.

#include <QTest>

#include "capture/CaptureMetadata.h"

using namespace gcs::capture;

namespace {

CaptureMetadata complete()
{
    CaptureMetadata m;
    m.trainNumber = QStringLiteral("1234");
    m.carNumber = QStringLiteral("05");
    m.pointId = QStringLiteral("C01-P03");
    m.tagId = 14;
    m.capturedAt = QDateTime(QDate(2026, 9, 6), QTime(14, 3, 21));
    m.robotX = 3.21;
    m.robotY = -1.04;
    m.robotTheta = 1.5708;
    m.distanceMm = 620;
    return m;
}

}  // namespace

class TestCaptureMeta : public QObject {
    Q_OBJECT

private slots:

    void vehicleNumber_joinsTrainAndCar()
    {
        QCOMPARE(complete().vehicleNumber(), QStringLiteral("1234.05"));
    }

    void vehicleNumber_emptyWhenIncomplete()
    {
        CaptureMetadata m = complete();
        m.carNumber.clear();
        QVERIFY(m.vehicleNumber().isEmpty());
    }

    /// 규정 형식: 차량번호_량번호_포인트ID,타임스탬프.확장자
    void fileName_followsMandatedForm()
    {
        QCOMPARE(complete().fileName(QStringLiteral("jpg")),
                 QStringLiteral("1234.05_05_C01-P03,20260906140321.jpg"));
    }

    void fileName_acceptsExtensionWithDot()
    {
        QCOMPARE(complete().fileName(QStringLiteral(".png")),
                 QStringLiteral("1234.05_05_C01-P03,20260906140321.png"));
    }

    /// 불완전한 기록이 그럴듯한 파일명을 만들어내면 안 된다.
    /// 검수에서 제외될 파일이 정상처럼 저장되는 것이 최악이다.
    void fileName_emptyWhenRecordIncomplete()
    {
        CaptureMetadata m = complete();
        m.pointId.clear();
        QVERIFY(m.fileName(QStringLiteral("jpg")).isEmpty());
    }

    void missingFields_listsWhatIsNeeded()
    {
        CaptureMetadata m;
        const auto missing = m.missingFields();
        QVERIFY(missing.contains(QStringLiteral("편성번호")));
        QVERIFY(missing.contains(QStringLiteral("량번호")));
        QVERIFY(missing.contains(QStringLiteral("점검포인트ID")));
        QVERIFY(missing.contains(QStringLiteral("촬영시간")));
    }

    void missingFields_ignoresWhitespaceOnlyInput()
    {
        CaptureMetadata m = complete();
        m.trainNumber = QStringLiteral("   ");
        QVERIFY(m.missingFields().contains(QStringLiteral("편성번호")));
    }

    /// Apriltag 인식 실패는 정상 범위 안에 있다. 저장을 막으면 조작자가
    /// 임의의 값을 채워 넣게 되고, 그쪽이 훨씬 나쁘다.
    void missingTag_doesNotBlockSave()
    {
        CaptureMetadata m = complete();
        m.tagId = -1;
        QVERIFY(m.isComplete());
        QVERIFY(!m.fileName(QStringLiteral("jpg")).isEmpty());
        QCOMPARE(m.toJson().value(QStringLiteral("tag_id")).toInt(), -1);
    }

    /// 파일명에 쓸 수 없는 문자는 거부가 아니라 치환한다.
    void illegalCharacters_areReplacedNotRejected()
    {
        CaptureMetadata m = complete();
        m.pointId = QStringLiteral("C01/P03:A");
        const QString name = m.fileName(QStringLiteral("jpg"));
        QVERIFY(!name.isEmpty());
        QVERIFY2(!name.contains(QLatin1Char('/')), qPrintable(name));
        QVERIFY2(!name.contains(QLatin1Char(':')), qPrintable(name));
        QCOMPARE(name, QStringLiteral("1234.05_05_C01-P03-A,20260906140321.jpg"));
    }

    void sanitise_handlesEveryWindowsIllegalCharacter()
    {
        QCOMPARE(sanitiseForFileName(QStringLiteral("a<b>c:d\"e/f\\g|h?i*j")),
                 QStringLiteral("a-b-c-d-e-f-g-h-i-j"));
    }

    /// 촬영시간 표기가 규정 형식이어야 한다.
    void capturedAtText_usesMandatedFormat()
    {
        QCOMPARE(complete().capturedAtText(), QStringLiteral("2026-09-06 14:03:21"));
    }

    void toJson_carriesEveryRequiredField()
    {
        const QJsonObject j = complete().toJson();
        for (const auto &key : {"vehicle_number", "point_id", "tag_id", "captured_at",
                                "robot_pose", "distance_mm"})
            QVERIFY2(j.contains(QLatin1String(key)), key);

        const QJsonObject pose = j.value(QStringLiteral("robot_pose")).toObject();
        QCOMPARE(pose.value(QStringLiteral("x")).toDouble(), 3.21);
        QVERIFY(qAbs(pose.value(QStringLiteral("theta_deg")).toDouble() - 90.0) < 0.01);
    }

    /// 보고서 생성이 파싱할 수 있는 형태도 함께 남아야 한다.
    void toJson_includesMachineReadableTimestamp()
    {
        const QJsonObject j = complete().toJson();
        const QDateTime parsed = QDateTime::fromString(
            j.value(QStringLiteral("captured_at_iso")).toString(), Qt::ISODate);
        QVERIFY(parsed.isValid());
        QCOMPARE(parsed, complete().capturedAt);
    }
};

QTEST_APPLESS_MAIN(TestCaptureMeta)
#include "test_capture_meta.moc"
