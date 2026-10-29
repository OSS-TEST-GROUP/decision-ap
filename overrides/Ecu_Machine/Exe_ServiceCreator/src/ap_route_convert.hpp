#ifndef SERVICECREATOR__AP_ROUTE_CONVERT_HPP_
#define SERVICECREATOR__AP_ROUTE_CONVERT_HPP_

#include <algorithm>
#include <utility>

#include <autoware_planning_msgs/msg/lanelet_route.hpp>
#include <oss/type/autoware_planning_msgs/msg/impl_type_laneletroute.h>

namespace servicecreator
{
inline autoware_planning_msgs::msg::LaneletRoute toRosRoute(
  const oss::type::autoware_planning_msgs::msg::LaneletRoute & in)
{
  autoware_planning_msgs::msg::LaneletRoute out;
  out.header.stamp.sec = in.header.stamp.sec;
  out.header.stamp.nanosec = in.header.stamp.nanosec;
  out.header.frame_id.assign(in.header.frame_id.data(), in.header.frame_id.size());
  const auto copy_pose = [](const auto & src, auto & dst) {
      dst.position.x = src.position.x;
      dst.position.y = src.position.y;
      dst.position.z = src.position.z;
      dst.orientation.x = src.orientation.x;
      dst.orientation.y = src.orientation.y;
      dst.orientation.z = src.orientation.z;
      dst.orientation.w = src.orientation.w;
    };
  copy_pose(in.start_pose, out.start_pose);
  copy_pose(in.goal_pose, out.goal_pose);
  out.segments.reserve(in.segments.size());
  for (const auto & segment : in.segments) {
    autoware_planning_msgs::msg::LaneletSegment dst;
    // PARA's generated field name differs from the ROS message field name.
    dst.preferred_primitive.id = segment.primitive_type.id;
    dst.preferred_primitive.primitive_type.assign(
      segment.primitive_type.primitive_type.data(),
      segment.primitive_type.primitive_type.size());
    dst.primitives.reserve(segment.primitives.size());
    for (const auto & primitive : segment.primitives) {
      autoware_planning_msgs::msg::LaneletPrimitive item;
      item.id = primitive.id;
      item.primitive_type.assign(primitive.primitive_type.data(), primitive.primitive_type.size());
      dst.primitives.push_back(std::move(item));
    }
    out.segments.push_back(std::move(dst));
  }
  std::copy(in.uuid.uuid.begin(), in.uuid.uuid.end(), out.uuid.uuid.begin());
  out.allow_modification = in.allow_modification;
  return out;
}
}  // namespace servicecreator

#endif
