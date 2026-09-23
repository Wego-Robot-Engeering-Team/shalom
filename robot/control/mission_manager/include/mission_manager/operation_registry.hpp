// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "mission_manager/bt/status.hpp"

namespace mission_manager::execution {

// Maps a wire-level waypoint operation to its runtime implementation.  The
// registry deliberately contains only implemented executors; declaring a
// capability in deployment configuration cannot enable unfinished behavior.
class OperationRegistry {
public:
  enum class FailurePolicy { Pause, Fail };

  struct Result {
    bt::Status status;
    FailurePolicy failure_policy{FailurePolicy::Fail};
    std::string reason_code;
  };

  struct Executor {
    std::string name;
    std::vector<std::string> required_capabilities;
    std::function<Result(const std::string &)> tick;
    std::function<void()> halt;
  };

  bool add(uint8_t operation, Executor executor) {
    if (executor.name.empty() || !executor.tick || !executor.halt) return false;
    for (const auto & capability : executor.required_capabilities) {
      if (capability.empty()) return false;
    }
    return executors_.emplace(operation, std::move(executor)).second;
  }

  [[nodiscard]] const Executor * find(uint8_t operation) const {
    const auto it = executors_.find(operation);
    return it == executors_.end() ? nullptr : &it->second;
  }

private:
  std::unordered_map<uint8_t, Executor> executors_;
};

}  // namespace mission_manager::execution
