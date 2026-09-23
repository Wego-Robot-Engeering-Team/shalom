// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#pragma once

// The only symbols exported from the shared SDK library.
#if defined(_WIN32)
#  if defined(SHALOM_SDK_BUILD)
#    define SHALOM_SDK_API __declspec(dllexport)
#  else
#    define SHALOM_SDK_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define SHALOM_SDK_API __attribute__((visibility("default")))
#else
#  define SHALOM_SDK_API
#endif
