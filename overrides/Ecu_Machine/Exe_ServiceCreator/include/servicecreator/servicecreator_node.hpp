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
// Exe_ServiceCreator 의 ROS 2 측.
//
// docs/Exe_ServiceCreator.md "채널 구성" 의 16개 채널을 전부 개설한다.
//   입력 Events  11 — Subscriber      -> ApChannel::send()
//   입력 Methods  3 — Service Server  -> ApChannel::call()
//   출력 Events   3 — ApChannel 핸들러 -> Publisher
// AP 측 연결은 ApChannel 구현체가 담당한다. 이 클래스는 ara::com 을 모른다.
//
// 입력 채널의 모드·정책:
//   event  : 도착 즉시 ap_->send()
//   poll   : 콜백은 슬롯에 넣기만, wall timer 가 주기마다 꺼내 보낸다 (latest)
//   sticky : + kNoClients 면 보관·재시도, 성공하면 즉시 중단

#ifndef SERVICECREATOR__SERVICECREATOR_NODE_HPP_
#define SERVICECREATOR__SERVICECREATOR_NODE_HPP_

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <rclcpp/rclcpp.hpp>

#include "servicecreator/ap_channel.hpp"
#include "servicecreator/bridge_config.hpp"

namespace servicecreator
{

class ServiceCreator : public rclcpp::Node
{
public:
  explicit ServiceCreator(const rclcpp::NodeOptions & options = rclcpp::NodeOptions());

  // 채널 주입 생성자. 테스트에서 스텁 채널을 넣거나, ap_channel_para.cpp 대신
  // 다른 구현을 쓰고 싶을 때 사용한다. nullptr 이면 makeApChannel() 을 쓴다.
  ServiceCreator(
    std::unique_ptr<ApChannel> channel,
    const rclcpp::NodeOptions & options = rclcpp::NodeOptions());

  ~ServiceCreator() override;

  // 테스트 전용 — 송신 워커 pending 회계. 활동 중에는 증감
  // 인터리빙으로 일시 -1 을 관측할 수 있다 — 정지(정착) 상태에서만 0 을
  // 단언할 것. 프로덕션 코드는 부르지 않는다.
  int pendingCountForTest() const
  {
    int total = 0;
    for (const auto & w : workers_) {total += w->pending.load();}
    return total;
  }

private:
  struct SendWorker;  // 아래 정의 — InputChannel 이 포인터로 참조한다

  // 입력 채널 하나의 배선. poll 슬롯과 sticky 보관값을 가진다.
  // 지금은 두 콜백이 같은 executor 스레드에서 돌지만 mutex 로 보호한다 —
  // 나중에 MultiThreadedExecutor 나 송신 전용 스레드로 바꾸면 전제가 조용히
  // 깨지기 때문이며, 무경합 mutex 의 비용은 사실상 0 이다.
  template<typename MsgT>
  struct InputChannel
  {
    // 상태·mutex 를 구독/타이머보다 "앞"에 — 역순 파괴에서 콜백 소유자(구독·
    // 타이머)가 mutex 보다 먼저 죽는 순서다.
    std::mutex mutex;
    std::optional<MsgT> slot;                   // poll 슬롯. has_value() 가 dirty 플래그
    std::optional<MsgT> pending;                // 송신 워커로 넘길 최신 값
    std::optional<MsgT> sticky_value;           // kNoClients 로 무산된 마지막 값
    bool sticky_delivered{false};               // 지도는 성공 후에도 재전송한다
    bool sending{false};                        // 워커가 이 채널 send 진행 중 (재시도 억제)
    bool sticky_given_up{false};                // sticky 포기 확정 — 새 ROS 값에서만 해제
    std::chrono::steady_clock::time_point episode_start{};  // sticky 에피소드 시작
    std::uint64_t received{0};                  // [1] ROS 수신 건수
    std::uint64_t dispatched{0};                // ap_->send() 에 넘긴 건수
    // send 1회 비용(변환 + 16벌 fan-out 포함). 송신 워커 점유 시간이다
    // (워커 도입 후 executor 는 pending 복사만 부담한다).
    std::uint64_t send_ns_total{0};
    std::uint64_t send_ns_max{0};
    SendWorker * worker{nullptr};               // 이 채널 전용 송신 워커

    typename rclcpp::Subscription<MsgT>::SharedPtr sub;
    rclcpp::TimerBase::SharedPtr poll_timer;    // poll 모드일 때만
    rclcpp::TimerBase::SharedPtr retry_timer;   // sticky 채널일 때만. 상시 구동 틱 —
                                                //   워커발 reset 은 잠든 executor 를
                                                //   못 깨운다
  };

  // 채널 전용 송신 워커. 상태 전이는 stop/pending 이
  // 원자이되, CV 술어를 참으로 만드는 갱신은 반드시 mutex 아래에서 한다
  // (통지 유실 방지).
  struct SendWorker
  {
    std::thread thread;
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<int> pending{0};
    std::atomic<bool> stop{false};
    std::atomic<bool> done{false};
    std::function<bool()> drain;
    const char * name{""};
  };

  void setupInputEvents();
  void setupInputMethods();
  void setupOutputEvents();
  void logChannelSummary();  // [1] 요약 — 소멸 시 한 줄씩

  template<typename MsgT>
  void setupInputChannel(
    InputChannel<MsgT> & ch, ChannelKey key,
    const std::string & topic, const rclcpp::QoS & qos, std::uint32_t event_id);

  // ap_->send() 는 송신 워커 스레드에서만 부른다. 콜백·타이머는
  // pending 에 넣기만 한다(락 밖 송신 원칙은 워커 쪽에서 지켜진다).
  template<typename MsgT>
  void enqueueSend(InputChannel<MsgT> & ch, const MsgT & msg);
  template<typename MsgT>
  bool drainOne(InputChannel<MsgT> & ch, ChannelKey key);
  void sendWorkerLoop(SendWorker & worker);
  void stopSendWorkers();
  template<typename MsgT>
  void handleStickyOutcome(
    InputChannel<MsgT> & ch, ChannelKey key, const MsgT & msg, SendResult outcome);
  template<typename MsgT>
  void onStickyRetry(InputChannel<MsgT> & ch, ChannelKey key);

  // 입력 Method 하나를 서버로 열어 ApChannel 로 넘긴다.
  template<typename SrvT>
  typename rclcpp::Service<SrvT>::SharedPtr serveInputMethod(
    const std::string & service, std::uint32_t method_id);

  BridgeConfig config_;
  std::unique_ptr<ApChannel> ap_;
  // ap_ "뒤" 선언 — 역순 파괴에서 타이머(람다가 ap_->maintain 참조)가 ap_
  // 보다 먼저 죽는다(workers_ 와 같은 원칙).
  rclcpp::TimerBase::SharedPtr maintenance_timer_;

  std::vector<std::unique_ptr<SendWorker>> workers_;

  // 입력 Events (11) — ChannelKey 와 1:1
  InputChannel<rosgraph_msgs::msg::Clock> clock_ch_;
  InputChannel<geometry_msgs::msg::AccelWithCovarianceStamped> accel_ch_;
  InputChannel<nav_msgs::msg::Odometry> kinematic_state_ch_;
  InputChannel<autoware_map_msgs::msg::LaneletMapBin> vector_map_ch_;
  InputChannel<autoware_perception_msgs::msg::PredictedObjects> objects_ch_;
  InputChannel<sensor_msgs::msg::PointCloud2> obstacle_pointcloud_ch_;
  InputChannel<nav_msgs::msg::OccupancyGrid> occupancy_grid_ch_;
  InputChannel<autoware_perception_msgs::msg::TrafficLightGroupArray> traffic_signals_ch_;
  InputChannel<tier4_planning_msgs::msg::LateralOffset> lateral_offset_ch_;
  InputChannel<tier4_planning_msgs::msg::VelocityLimit> max_velocity_ch_;
  InputChannel<autoware_adapi_v1_msgs::msg::OperationModeState> operation_mode_ch_;

  // 입력 Methods (3): legacy mission_planner 이름과 route_selector/main 별칭.
  rclcpp::Service<tier4_planning_msgs::srv::ClearRoute>::SharedPtr clear_route_srv_;
  rclcpp::Service<tier4_planning_msgs::srv::SetLaneletRoute>::SharedPtr set_lanelet_route_srv_;
  rclcpp::Service<tier4_planning_msgs::srv::SetWaypointRoute>::SharedPtr set_waypoint_route_srv_;
  rclcpp::Service<tier4_planning_msgs::srv::ClearRoute>::SharedPtr main_clear_route_srv_;
  rclcpp::Service<tier4_planning_msgs::srv::SetLaneletRoute>::SharedPtr main_set_lanelet_route_srv_;
  rclcpp::Service<tier4_planning_msgs::srv::SetWaypointRoute>::SharedPtr main_set_waypoint_route_srv_;

  // 출력 Events (2)
  rclcpp::Publisher<autoware_planning_msgs::msg::Trajectory>::SharedPtr trajectory_pub_;
  rclcpp::Publisher<autoware_planning_msgs::msg::Trajectory>::SharedPtr scenario_trajectory_pub_;
  rclcpp::Publisher<autoware_vehicle_msgs::msg::TurnIndicatorsCommand>::SharedPtr turn_pub_;
  rclcpp::Publisher<autoware_vehicle_msgs::msg::HazardLightsCommand>::SharedPtr hazard_pub_;
  rclcpp::Publisher<autoware_internal_msgs::msg::MissionRemainingDistanceTime>::SharedPtr remaining_pub_;
  rclcpp::Publisher<tier4_metric_msgs::msg::MetricArray>::SharedPtr metrics_pub_;
  rclcpp::Publisher<tier4_planning_msgs::msg::RouteState>::SharedPtr route_state_pub_;
  rclcpp::Publisher<autoware_planning_msgs::msg::LaneletRoute>::SharedPtr route_pub_;
  rclcpp::Publisher<autoware_planning_msgs::msg::LaneletRoute>::SharedPtr main_route_pub_;

  // 채널 전용 송신 워커(입력 이벤트당 1개, 고정 11).
  // 단일 워커로 전 채널을 직렬화하면 대형 send 1건(약 0.9s)이
  // 다른 모든 채널의 송신을 그만큼 지연시킨다. 채널별 1워커는:
  //   - 같은 Event 인스턴스의 Send 는 자기 워커 하나가 직렬화 — SWS_CM_00012 유지
  //   - 서로 다른 Event 의 동시 Send 는 같은 skeleton 이라도 다른 멤버 함수 —
  //     SWS_CM_00009 허용 범위
  //   - 스레드 수 제약: 피해야 하는 것은 이벤트×소비자 곱만큼 스레드가
  //     느는 것(177)이다. 이벤트당 1개 고정 상한(11, fan-out 은 그 스레드 안에서
  //     직렬)은 곱 증식이 아니다.
  //
  // 의미 유지: event 모드도 자기 워커가 밀리면 pending 이 최신값으로 덮여
  // latest 로 붕괴한다 — 소비자 캐시가 1칸이라 원래 무해한 붕괴다.
  // (선언 위치는 아래 채널 멤버들 "앞" — 역순 파괴에서 workers_ 가 채널보다
  // 오래 살아 ch.worker 비소유 포인터의 댕글링 구간을 없앤다.)
};

}  // namespace servicecreator

#endif  // SERVICECREATOR__SERVICECREATOR_NODE_HPP_
