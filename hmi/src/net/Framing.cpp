#include "net/Framing.h"

namespace hmi::net {
namespace {

QByteArray toQt(const std::string &s)
{
    return QByteArray(s.data(), qsizetype(s.size()));
}

std::string toStd(const QByteArray &b)
{
    return std::string(b.constData(), std::size_t(b.size()));
}

}  // namespace

QByteArray encodeFrame(const QByteArray &header, const QByteArray &payload)
{
    return toQt(inspection::encodeFrame(toStd(header), toStd(payload)));
}

void FrameDecoder::append(const QByteArray &chunk)
{
    inner_.append(chunk.constData(), std::size_t(chunk.size()));
}

FrameDecoder::Status FrameDecoder::next(Frame &out)
{
    inspection::Frame frame;
    switch (inner_.next(frame)) {
    case inspection::DecodeStatus::Ok:
        out.header = toQt(frame.header);
        out.payload = toQt(frame.payload);
        return Status::Ok;
    case inspection::DecodeStatus::NeedMore:
        return Status::NeedMore;
    case inspection::DecodeStatus::BadMagic:
        return Status::BadMagic;
    case inspection::DecodeStatus::TooLarge:
        break;
    }
    return Status::TooLarge;
}

}  // namespace hmi::net
