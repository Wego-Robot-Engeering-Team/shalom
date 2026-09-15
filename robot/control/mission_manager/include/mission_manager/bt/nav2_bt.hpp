#pragma once

#include <string>

#include "mission_manager/bt/status.hpp"

namespace mission_manager::bt {

class Nav2Runtime {
public:
  virtual ~Nav2Runtime() = default;
  virtual Status navigate_to(const std::string & goal_id) = 0;
  virtual void cancel_navigation() = 0;
};

// One Nav2 goal, used for inspection waypoints and the dock goal.
class Nav2Bt {
public:
  Status tick(Nav2Runtime & runtime, const std::string & goal_id);
  void halt(Nav2Runtime & runtime);

private:
  std::string active_goal_id_;
};

}  // namespace mission_manager::bt
