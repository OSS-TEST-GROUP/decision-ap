// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <ara/core/structure.h>
#include "oss/type/builtin_interfaces/msg/impl_type_time.h"
#include <cstdint>
namespace oss { namespace type { namespace adaptive_planning { namespace msg {
struct HazardCommand {
  oss::type::builtin_interfaces::msg::Time stamp;
  std::uint8_t command = 0;
  PARA_STRUCTURE(HazardCommand, stamp, command);
};
}}}}
