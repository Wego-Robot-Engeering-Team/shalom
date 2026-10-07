// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "data/RecordScanWorker.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <cstdio>

namespace hmi::data {

QByteArray encodeScanResult(const ScanResult &result)
{
    QJsonArray records;
    for (const auto &record : result.records) {
        QJsonObject item{{"path", record.filePath}, {"name", record.fileName},
                         {"size", double(record.fileSize)}, {"vehicle", record.vehicleNumber},
                         {"car", record.carNumber}, {"point", record.pointId},
                         {"stamp", record.capturedAt.toString(Qt::ISODateWithMs)},
                         {"tag", record.tagId}};
        if (record.sidecar)
            item[QStringLiteral("sidecar")] = *record.sidecar;
        records << item;
    }
    return QJsonDocument(QJsonObject{{"records", records}, {"error", result.error},
        {"unrecognised", QJsonArray::fromStringList(result.unrecognised)}}).toJson(QJsonDocument::Compact);
}

std::optional<ScanResult> decodeScanResult(const QByteArray &bytes)
{
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;
    const auto root = document.object();
    if (!root.value("records").isArray() || !root.value("error").isString() ||
        !root.value("unrecognised").isArray())
        return std::nullopt;
    ScanResult result;
    result.error = root.value("error").toString();
    for (const auto &value : root.value("unrecognised").toArray()) {
        if (!value.isString()) return std::nullopt;
        result.unrecognised << value.toString();
    }
    for (const auto &value : root.value("records").toArray()) {
        if (!value.isObject()) return std::nullopt;
        const auto item = value.toObject();
        for (const auto *key : {"path", "name", "vehicle", "car", "point", "stamp"})
            if (!item.value(QLatin1String(key)).isString()) return std::nullopt;
        if (!item.value("size").isDouble() || !item.value("tag").isDouble() ||
            (item.contains("sidecar") && !item.value("sidecar").isObject()))
            return std::nullopt;
        InspectionRecord record;
        record.filePath = item.value("path").toString();
        record.fileName = item.value("name").toString();
        record.fileSize = item.value("size").toInteger();
        record.vehicleNumber = item.value("vehicle").toString();
        record.carNumber = item.value("car").toString();
        record.pointId = item.value("point").toString();
        record.capturedAt = QDateTime::fromString(item.value("stamp").toString(), Qt::ISODateWithMs);
        record.tagId = item.value("tag").toInt(-1);
        if (item.contains("sidecar")) record.sidecar = item.value("sidecar").toObject();
        result.records << record;
    }
    return result;
}

int runRecordScanWorker(const QString &directory)
{
    const auto bytes = encodeScanResult(scanDirectory(directory));
    QFile output;
    if (!output.open(stdout, QIODevice::WriteOnly) || output.write(bytes) != bytes.size())
        return 1;
    return output.flush() ? 0 : 1;
}

}  // namespace hmi::data
