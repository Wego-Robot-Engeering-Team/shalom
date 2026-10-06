// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#pragma once

// The only symbols exported from the shared SDK library.
#if defined(_WIN32)
#  if defined(ROBOT_SDK_BUILD)
#    define ROBOT_SDK_API __declspec(dllexport)
#  else
#    define ROBOT_SDK_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) || defined(__clang__)
#  define ROBOT_SDK_API __attribute__((visibility("default")))
#else
#  define ROBOT_SDK_API
#endif
