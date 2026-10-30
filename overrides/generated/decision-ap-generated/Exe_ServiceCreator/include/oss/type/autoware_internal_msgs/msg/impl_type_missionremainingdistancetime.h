// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <ara/core/structure.h>
namespace oss { namespace type { namespace autoware_internal_msgs { namespace msg {
struct MissionRemainingDistanceTime {
  double remaining_distance = 0.0;
  double remaining_time = 0.0;
  PARA_STRUCTURE(MissionRemainingDistanceTime, remaining_distance, remaining_time);
};
}}}}
