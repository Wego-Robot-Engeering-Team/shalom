// 프레이밍 단위 테스트.
//
// docs/bridge_protocol.md §1.3 이 "전부 처리해야 한다"고 못박은 항목을
// 하나씩 대응시킨다. 여기가 깨지면 현장에서 재현 안 되는 프레임 어긋남이 난다.

#include <QTest>
#include <QtEndian>

#include "net/Framing.h"

using namespace gcs::net;

namespace {

QByteArray withMagic(std::uint32_t magic, std::uint32_t bodyLen, const QByteArray &body)
{
    QByteArray out;
    char b[4];
    qToLittleEndian(magic, reinterpret_cast<uchar *>(b));
    out.append(b, 4);
    qToLittleEndian(bodyLen, reinterpret_cast<uchar *>(b));
    out.append(b, 4);
    out.append(body);
    return out;
}

}  // namespace

class TestFraming : public QObject {
    Q_OBJECT

private slots:

    // ---- 기본 왕복 -----------------------------------------------------
    void roundTrip_headerOnly()
    {
        const QByteArray header = R"({"v":1,"t":"hb","ts":1.0})";
        FrameDecoder dec;
        dec.append(encodeFrame(header));

        Frame f;
        QCOMPARE(dec.next(f), FrameDecoder::Status::Ok);
        QCOMPARE(f.header, header);
        QVERIFY(f.payload.isEmpty());
        QCOMPARE(dec.buffered(), qsizetype(0));
    }

    void roundTrip_withPayload()
    {
        const QByteArray header = R"({"v":1,"t":"pub","ch":"map/occupancy"})";
        QByteArray payload(4096, '\0');
        for (int i = 0; i < payload.size(); ++i)
            payload[i] = char(i & 0xFF);

        FrameDecoder dec;
        dec.append(encodeFrame(header, payload));

        Frame f;
        QCOMPARE(dec.next(f), FrameDecoder::Status::Ok);
        QCOMPARE(f.header, header);
        QCOMPARE(f.payload, payload);
    }

    void roundTrip_utf8Header()
    {
        // 한글 메타데이터가 봉투에 들어간다 (차량번호, 포인트명 등).
        const QByteArray header = QString(R"({"p":{"name":"1량 P3 검수고"}})").toUtf8();
        FrameDecoder dec;
        dec.append(encodeFrame(header));

        Frame f;
        QCOMPARE(dec.next(f), FrameDecoder::Status::Ok);
        QCOMPARE(f.header, header);
    }

    // ---- §1.3-1 부분 수신 ----------------------------------------------
    void partialDelivery_byteByByte()
    {
        const QByteArray header = R"({"v":1,"t":"pub","ch":"state/pose"})";
        const QByteArray payload("0123456789", 10);
        const QByteArray wire = encodeFrame(header, payload);

        FrameDecoder dec;
        Frame f;
        // 마지막 바이트 직전까지는 계속 NeedMore 여야 한다.
        for (qsizetype i = 0; i < wire.size() - 1; ++i) {
            dec.append(wire.mid(i, 1));
            QCOMPARE(dec.next(f), FrameDecoder::Status::NeedMore);
        }
        dec.append(wire.right(1));
        QCOMPARE(dec.next(f), FrameDecoder::Status::Ok);
        QCOMPARE(f.header, header);
        QCOMPARE(f.payload, payload);
    }

    void partialDelivery_splitInsidePrefix()
    {
        // 접두사 8 바이트가 3 + 5 로 쪼개져 도착하는 경우.
        const QByteArray wire = encodeFrame(R"({"t":"hb"})");
        FrameDecoder dec;
        Frame f;

        dec.append(wire.left(3));
        QCOMPARE(dec.next(f), FrameDecoder::Status::NeedMore);
        dec.append(wire.mid(3, 5));
        QCOMPARE(dec.next(f), FrameDecoder::Status::NeedMore);
        dec.append(wire.mid(8));
        QCOMPARE(dec.next(f), FrameDecoder::Status::Ok);
    }

    // ---- §1.3-2 병합 수신 ----------------------------------------------
    void coalescedDelivery_threeFramesOneChunk()
    {
        QByteArray wire;
        wire += encodeFrame(R"({"n":1})");
        wire += encodeFrame(R"({"n":2})", QByteArray("abc", 3));
        wire += encodeFrame(R"({"n":3})");

        FrameDecoder dec;
        dec.append(wire);

        QStringList seen;
        Frame f;
        for (;;) {
            const auto st = dec.next(f);
            if (st == FrameDecoder::Status::NeedMore)
                break;
            QCOMPARE(st, FrameDecoder::Status::Ok);
            seen << QString::fromUtf8(f.header);
        }
        QCOMPARE(seen.size(), 3);
        QCOMPARE(seen.at(1), QString(R"({"n":2})"));
        QCOMPARE(dec.buffered(), qsizetype(0));
    }

    void coalescedDelivery_lastFrameTruncated()
    {
        // 두 프레임 + 세 번째 프레임의 앞부분만 도착.
        QByteArray wire = encodeFrame(R"({"n":1})") + encodeFrame(R"({"n":2})");
        const QByteArray third = encodeFrame(R"({"n":3})");
        wire += third.left(5);

        FrameDecoder dec;
        dec.append(wire);

        Frame f;
        QCOMPARE(dec.next(f), FrameDecoder::Status::Ok);
        QCOMPARE(dec.next(f), FrameDecoder::Status::Ok);
        QCOMPARE(dec.next(f), FrameDecoder::Status::NeedMore);

        dec.append(third.mid(5));
        QCOMPARE(dec.next(f), FrameDecoder::Status::Ok);
        QCOMPARE(f.header, QByteArray(R"({"n":3})"));
    }

    // ---- §1.3-3 magic 불일치 → 종료 (재동기화 금지) ---------------------
    void badMagic_rejected()
    {
        FrameDecoder dec;
        dec.append(withMagic(0xDEADBEEF, 4, QByteArray(4, '\0')));

        Frame f;
        QCOMPARE(dec.next(f), FrameDecoder::Status::BadMagic);
        // 재호출해도 계속 BadMagic — 스스로 복구를 시도하지 않는다.
        QCOMPARE(dec.next(f), FrameDecoder::Status::BadMagic);
    }

    // ---- §1.3-4 길이 가드 ----------------------------------------------
    void bodyLenAboveCap_rejected()
    {
        FrameDecoder dec;
        dec.append(withMagic(kMagic, kMaxBodyLen + 1, QByteArray()));

        Frame f;
        // 본문을 기다리지 않고 즉시 거부해야 한다. 아니면 32MiB 를 버퍼링한다.
        QCOMPARE(dec.next(f), FrameDecoder::Status::TooLarge);
    }

    void bodyLenTooSmall_rejected()
    {
        FrameDecoder dec;
        dec.append(withMagic(kMagic, 3, QByteArray(3, '\0')));

        Frame f;
        QCOMPARE(dec.next(f), FrameDecoder::Status::TooLarge);
    }

    void headerLenOverrunsBody_rejected()
    {
        // body_len = 8 인데 header_len = 999 라고 주장하는 모순 프레임.
        QByteArray body;
        char b[4];
        qToLittleEndian(std::uint32_t(999), reinterpret_cast<uchar *>(b));
        body.append(b, 4);
        body.append(4, 'x');

        FrameDecoder dec;
        dec.append(withMagic(kMagic, 8, body));

        Frame f;
        QCOMPARE(dec.next(f), FrameDecoder::Status::TooLarge);
    }

    // ---- §1.3-6 재연결 시 버퍼 초기화 -----------------------------------
    void reset_clearsPartialFrame()
    {
        const QByteArray wire = encodeFrame(R"({"n":1})");
        FrameDecoder dec;
        dec.append(wire.left(6));           // 끊긴 연결의 잔여물
        QVERIFY(dec.buffered() > 0);

        dec.reset();
        QCOMPARE(dec.buffered(), qsizetype(0));

        dec.append(wire);                   // 새 연결
        Frame f;
        QCOMPARE(dec.next(f), FrameDecoder::Status::Ok);
        QCOMPARE(f.header, QByteArray(R"({"n":1})"));
    }

    // ---- 버퍼 압축이 실제로 동작하는지 (O(n^2) 방지) ---------------------
    void manyFrames_bufferDoesNotGrow()
    {
        FrameDecoder dec;
        const QByteArray one = encodeFrame(R"({"v":1,"t":"pub","ch":"state/pose"})",
                                           QByteArray(256, 'p'));
        // 10Hz 스트림 100초분에 해당하는 양을 한 번에 밀어 넣는다.
        QByteArray wire;
        for (int i = 0; i < 1000; ++i)
            wire += one;
        dec.append(wire);

        int count = 0;
        Frame f;
        while (dec.next(f) == FrameDecoder::Status::Ok)
            ++count;

        QCOMPARE(count, 1000);
        QCOMPARE(dec.buffered(), qsizetype(0));
    }

    void emptyHeader_isValid()
    {
        FrameDecoder dec;
        dec.append(encodeFrame(QByteArray()));
        Frame f;
        QCOMPARE(dec.next(f), FrameDecoder::Status::Ok);
        QVERIFY(f.header.isEmpty());
        QVERIFY(f.payload.isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestFraming)
#include "test_framing.moc"
