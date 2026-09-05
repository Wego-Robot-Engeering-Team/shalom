#include "net/Framing.h"

#include <QtEndian>

namespace gcs::net {
namespace {

constexpr qsizetype kPrefixLen = 8;   // magic + body_len
constexpr qsizetype kBodyMinLen = 4;  // header_len 필드 자체

std::uint32_t readU32(const char *p)
{
    return qFromLittleEndian<std::uint32_t>(reinterpret_cast<const uchar *>(p));
}

void appendU32(QByteArray &out, std::uint32_t v)
{
    char b[4];
    qToLittleEndian(v, reinterpret_cast<uchar *>(b));
    out.append(b, 4);
}

}  // namespace

QByteArray encodeFrame(const QByteArray &header, const QByteArray &payload)
{
    const auto bodyLen = static_cast<std::uint32_t>(kBodyMinLen + header.size() + payload.size());

    QByteArray out;
    out.reserve(kPrefixLen + bodyLen);
    appendU32(out, kMagic);
    appendU32(out, bodyLen);
    appendU32(out, static_cast<std::uint32_t>(header.size()));
    out.append(header);
    out.append(payload);
    return out;
}

void FrameDecoder::append(const QByteArray &chunk)
{
    buf_.append(chunk);
}

void FrameDecoder::reset()
{
    buf_.clear();
    offset_ = 0;
}

void FrameDecoder::compact()
{
    if (offset_ == 0)
        return;
    buf_.remove(0, offset_);
    offset_ = 0;
}

FrameDecoder::Status FrameDecoder::next(Frame &out)
{
    // 접두사 8 바이트조차 여러 번에 나뉘어 도착할 수 있다 (명세 §1.3-1).
    if (buffered() < kPrefixLen)
        return Status::NeedMore;

    const char *base = buf_.constData() + offset_;

    if (readU32(base) != kMagic)
        return Status::BadMagic;

    const std::uint32_t bodyLen = readU32(base + 4);
    if (bodyLen < kBodyMinLen || bodyLen > kMaxBodyLen)
        return Status::TooLarge;

    if (buffered() < kPrefixLen + static_cast<qsizetype>(bodyLen))
        return Status::NeedMore;

    const char *body = base + kPrefixLen;
    const std::uint32_t headerLen = readU32(body);

    // header_len 이 본문 밖을 가리키면 프레임이 모순이다. 재동기화하지 않는다.
    if (static_cast<qsizetype>(kBodyMinLen) + headerLen > static_cast<qsizetype>(bodyLen))
        return Status::TooLarge;

    const qsizetype payloadLen = bodyLen - kBodyMinLen - headerLen;
    out.header = QByteArray(body + kBodyMinLen, static_cast<qsizetype>(headerLen));
    out.payload = payloadLen > 0 ? QByteArray(body + kBodyMinLen + headerLen, payloadLen)
                                 : QByteArray();

    offset_ += kPrefixLen + static_cast<qsizetype>(bodyLen);

    // 매번 자르면 O(n^2) 이 된다. 소비분이 쌓였을 때만 실제로 압축한다.
    if (offset_ >= kCompactThreshold || offset_ == buf_.size())
        compact();

    return Status::Ok;
}

}  // namespace gcs::net
