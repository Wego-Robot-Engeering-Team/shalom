// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#pragma once

#include "data/InspectionRecord.h"

namespace hmi::data {

/// Internal worker protocol. Filesystem reads run outside the UI process so
/// an unavailable NAS cannot hold up directory changes or application exit.
QByteArray encodeScanResult(const ScanResult &result);
std::optional<ScanResult> decodeScanResult(const QByteArray &bytes);
int runRecordScanWorker(const QString &directory);

}  // namespace hmi::data
