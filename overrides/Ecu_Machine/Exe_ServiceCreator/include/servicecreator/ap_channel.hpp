///////////////////////////////////////////////////////////////////////////////////////////////////////////
///
/// Copyright, 2026 PopcornSAR Co., Ltd. All rights reserved.
/// This software is copyright protected and proprietary to PopcornSAR Co., Ltd.
/// PopcornSAR Co., Ltd. grants to you only those rights as set out in the license conditions.
///
/// HAND-WRITTEN — NOT GENERATED.
/// 이 파일은 사람이 작성한 것이며 AutoSAR.io 생성 대상이 아니다. 같은 디렉터리의
/// include/oss/** 와 manifest/** 는 생성물이므로 고치지 않는다.
///
///////////////////////////////////////////////////////////////////////////////////////////////////////////
// ApChannel — ROS 2 측과 AUTOSAR Adaptive(ara::com) 측의 경계.
//
// ROS 2 쪽 코드(servicecreator_node)는 이 인터페이스만 안다. ara::com 연결 코드는
// 이 인터페이스를 구현하기만 하면 되고, ROS 2 코드를 건드릴 필요가 없다.
//
// 대응 관계 — docs/Exe_ServiceCreator.md "채널 구성"
//   send(...)                 2장 입력 Events (11)  : ROS 2 수신 -> AP Skeleton 송신
//   call(...)                 3장 입력 Methods (3)  : ROS 2 요청 -> AP Proxy 호출
//   setTrajectoryHandler(...) / setRouteStateHandler(...) / setRouteHandler(...)
//                                                 출력 Events (3)
//
// 모든 가상 함수에 기본 구현(no-op)이 있으므로, 일부만 구현한 채널도 컴파일된다.
// 단계적으로 채워 넣을 수 있다.

#ifndef SERVICECREATOR__AP_CHANNEL_HPP_
#define SERVICECREATOR__AP_CHANNEL_HPP_

#include <functional>
#include <memory>

#include "servicecreator/send_result.hpp"

#include <rosgraph_msgs/msg/clock.hpp>
#include <geometry_msgs/msg/accel_with_covariance_stamped.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <nav_msgs/msg/occupancy_grid.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>
#include <autoware_map_msgs/msg/lanelet_map_bin.hpp>
#include <autoware_perception_msgs/msg/predicted_objects.hpp>
#include <autoware_perception_msgs/msg/traffic_light_group_array.hpp>
#include <autoware_adapi_v1_msgs/msg/operation_mode_state.hpp>
#include <tier4_planning_msgs/msg/lateral_offset.hpp>
#include <tier4_planning_msgs/msg/velocity_limit.hpp>
#include <tier4_planning_msgs/msg/route_state.hpp>
#include <tier4_planning_msgs/srv/clear_route.hpp>
#include <tier4_planning_msgs/srv/set_lanelet_route.hpp>
#include <tier4_planning_msgs/srv/set_waypoint_route.hpp>
#include <autoware_planning_msgs/msg/trajectory.hpp>
#include <autoware_planning_msgs/msg/lanelet_route.hpp>
#include <autoware_vehicle_msgs/msg/turn_indicators_command.hpp>
#include <autoware_vehicle_msgs/msg/hazard_lights_command.hpp>
#include <autoware_internal_msgs/msg/mission_remaining_distance_time.hpp>
#include <tier4_metric_msgs/msg/metric_array.hpp>

namespace servicecreator
{

class ApChannel
{
public:
  virtual ~ApChannel() = default;

  // AP 런타임 초기화 — Skeleton offer, Proxy find 를 여기서 한다.
  // 실패해도 ROS 2 측은 계속 동작한다(로그만 남는다).
  virtual bool start() {return false;}
  virtual void stop() {}

  // 이름. 로그에만 쓴다.
  virtual const char * name() const {return "none";}

  // 주기 유지보수 틱(executor 스레드) — 배선 실패의 자가 회복 등.
  // AP 콜백(find 통지)은 서비스 상태가 변할 때만 다시 오므로, 콜백 안에서
  // 실패한 배선은 여기서 재시도해야 영구 고착이 안 된다.
  virtual void maintain() {}

  // ── 2장. 입력 서비스 · Events (11) ─────────────────────────────────
  // 괄호 안은 AP Event ID. 반환값은 fan-out 16벌의 최악값이며
  // 노드의 sticky 재시도가 이것을 트리거로 쓴다.
  virtual SendResult send(const rosgraph_msgs::msg::Clock &) {return SendResult::Ok;}                          // 1
  virtual SendResult send(const geometry_msgs::msg::AccelWithCovarianceStamped &) {return SendResult::Ok;}     // 4001
  virtual SendResult send(const nav_msgs::msg::Odometry &) {return SendResult::Ok;}                            // 4002
  virtual SendResult send(const autoware_map_msgs::msg::LaneletMapBin &) {return SendResult::Ok;}              // 6001
  virtual SendResult send(const autoware_perception_msgs::msg::PredictedObjects &) {return SendResult::Ok;}    // 30101
  virtual SendResult send(const sensor_msgs::msg::PointCloud2 &) {return SendResult::Ok;}                      // 30201
  virtual SendResult send(const nav_msgs::msg::OccupancyGrid &) {return SendResult::Ok;}                       // 30301
  virtual SendResult send(const autoware_perception_msgs::msg::TrafficLightGroupArray &) {return SendResult::Ok;}  // 30401
  virtual SendResult send(const tier4_planning_msgs::msg::LateralOffset &) {return SendResult::Ok;}            // 11629
  virtual SendResult send(const tier4_planning_msgs::msg::VelocityLimit &) {return SendResult::Ok;}            // 12503
  virtual SendResult send(const autoware_adapi_v1_msgs::msg::OperationModeState &) {return SendResult::Ok;}    // 6101

  // ── 3장. 입력 서비스 · Methods (3) ─────────────────────────────────
  // AP Service 1817 (SCr_SI_missionplanner). 성공 시 true 를 돌려주고 response 를 채운다.
  // false 면 ROS 2 측이 status.success=false 로 응답한다.
  virtual bool call(
    const tier4_planning_msgs::srv::ClearRoute::Request &,
    tier4_planning_msgs::srv::ClearRoute::Response &) {return false;}              // 10801
  virtual bool call(
    const tier4_planning_msgs::srv::SetLaneletRoute::Request &,
    tier4_planning_msgs::srv::SetLaneletRoute::Response &) {return false;}         // 10809
  virtual bool call(
    const tier4_planning_msgs::srv::SetWaypointRoute::Request &,
    tier4_planning_msgs::srv::SetWaypointRoute::Response &) {return false;}        // 10812

  // ── 4장. 출력 서비스 · Events (3) ──────────────────────────────────
  // AP Service 2317 (SCr_SI_planningvalidator) 의 Event 를 받으면 이 핸들러를 부른다.
  // 핸들러는 ROS 2 Publisher 로 그대로 흘린다. 스레드는 AP 런타임 것이므로
  // 구현 측에서 재진입 안전성을 보장할 것.
  using TrajectoryHandler =
    std::function<void (const autoware_planning_msgs::msg::Trajectory &)>;
  virtual void setTrajectoryHandler(TrajectoryHandler) {}
  virtual void setScenarioTrajectoryHandler(TrajectoryHandler) {}
  using TurnHandler = std::function<void (const autoware_vehicle_msgs::msg::TurnIndicatorsCommand &)>;
  using HazardHandler = std::function<void (const autoware_vehicle_msgs::msg::HazardLightsCommand &)>;
  virtual void setTurnHandler(TurnHandler) {}
  virtual void setHazardHandler(HazardHandler) {}
  using RemainingHandler = std::function<void (const autoware_internal_msgs::msg::MissionRemainingDistanceTime &)>;
  using MetricsHandler = std::function<void (const tier4_metric_msgs::msg::MetricArray &)>;
  virtual void setRemainingHandler(RemainingHandler) {}
  virtual void setMetricsHandler(MetricsHandler) {}
  using RouteStateHandler =
    std::function<void (const tier4_planning_msgs::msg::RouteState &)>;
  virtual void setRouteStateHandler(RouteStateHandler) {}
  using RouteHandler =
    std::function<void (const autoware_planning_msgs::msg::LaneletRoute &)>;
  virtual void setRouteHandler(RouteHandler) {}
};

// 빌드 구성에 맞는 채널을 만든다.
//   SERVICECREATOR_WITH_PARA=OFF -> src/ap_channel_null.cpp  (아무것도 하지 않음)
//   SERVICECREATOR_WITH_PARA=ON  -> src/ap_channel_para.cpp  (ara::com 연결)
std::unique_ptr<ApChannel> makeApChannel();

}  // namespace servicecreator

#endif  // SERVICECREATOR__AP_CHANNEL_HPP_
