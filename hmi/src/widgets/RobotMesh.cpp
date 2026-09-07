#include "widgets/RobotMesh.h"

#include <QByteArray>
#include <QDataStream>
#include <QFile>
#include <QLoggingCategory>

#include <cstring>

namespace hmi::ui::mesh {

namespace {

constexpr char kMagic[8] = {'B', '2', 'M', 'S', 'H', '2', '\0', '\0'};
constexpr int kMaxParts = 256;
constexpr quint32 kMaxVertices = 2'000'000;
constexpr quint32 kMaxFaces = 2'000'000;

/// Reads one part, or returns false and leaves the model to be discarded.
///
/// Every count is checked before it is used to size an allocation. The blob
/// ships inside the binary and is not attacker-controlled, but a truncated
/// build artefact is a real way for this to go wrong, and a bad length field
/// would otherwise be a multi-gigabyte reserve.
bool readPart(QDataStream &in, Part &out)
{
    quint16 nameLen = 0;
    in >> nameLen;
    if (in.status() != QDataStream::Ok || nameLen == 0 || nameLen > 256)
        return false;

    QByteArray name(nameLen, '\0');
    if (in.readRawData(name.data(), nameLen) != nameLen)
        return false;
    out.name = QString::fromUtf8(name);

    quint8 group = 0, reserved = 0;
    in >> group >> reserved;
    out.group = int(group);

    quint32 vertexCount = 0;
    in >> vertexCount;
    if (in.status() != QDataStream::Ok || vertexCount == 0 || vertexCount > kMaxVertices)
        return false;
    out.vertices.resize(vertexCount);
    for (auto &v : out.vertices) {
        float x = 0, y = 0, z = 0;
        in >> x >> y >> z;
        v = QVector3D(x, y, z);
    }

    quint32 faceCount = 0;
    in >> faceCount;
    if (in.status() != QDataStream::Ok || faceCount == 0 || faceCount > kMaxFaces)
        return false;
    out.faces.resize(faceCount);
    for (auto &f : out.faces) {
        in >> f.a >> f.b >> f.c;
        if (f.a >= vertexCount || f.b >= vertexCount || f.c >= vertexCount)
            return false;
    }

    out.normals.resize(faceCount);
    for (auto &n : out.normals) {
        for (auto *v : {&n.a, &n.b, &n.c}) {
            float x = 0, y = 0, z = 0;
            in >> x >> y >> z;
            *v = QVector3D(x, y, z);
        }
    }
    return in.status() == QDataStream::Ok;
}

Model load()
{
    Model m;

    QFile file(QStringLiteral(":/robot_mesh.bin"));
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("robot_mesh.bin 을 열 수 없다 — 3D 뷰는 형상 없이 뜬다");
        return {};
    }

    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);
    in.setFloatingPointPrecision(QDataStream::SinglePrecision);

    char magic[8] = {};
    if (in.readRawData(magic, 8) != 8 || std::memcmp(magic, kMagic, 8) != 0) {
        qWarning("robot_mesh.bin 의 형식이 맞지 않는다 — tools/make_robot_mesh.py 로 다시 생성하라");
        return {};
    }

    float mx = 0, my = 0, mz = 0;
    in >> m.baseHeight >> mx >> my >> mz;
    m.armMount = QVector3D(mx, my, mz);

    quint32 partCount = 0;
    in >> partCount;
    if (in.status() != QDataStream::Ok || partCount == 0 || partCount > kMaxParts) {
        qWarning("robot_mesh.bin 의 부품 수가 이상하다: %u", partCount);
        return {};
    }

    for (quint32 i = 0; i < partCount; ++i) {
        Part p;
        if (!readPart(in, p)) {
            qWarning("robot_mesh.bin 이 %u 번째 부품에서 끊겼다", i);
            return {};
        }
        (p.group == 2 ? m.fr3 : m.b2).push_back(std::move(p));
    }
    return m;
}

}  // namespace

int Model::faceCount() const
{
    int n = 0;
    for (const auto *group : {&b2, &fr3})
        for (const auto &p : *group)
            n += int(p.faces.size());
    return n;
}

const Model &model()
{
    // Parsed once. The blob is a build artefact, so re-reading it could never
    // produce a different answer, and the 3D view asks for it every frame.
    static const Model cached = load();
    return cached;
}

}  // namespace hmi::ui::mesh
