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
// SERVICECREATOR_WITH_PARA=ON 일 때의 ApChannel — ara::com 연결.
//
// PlanningAA 입력 서비스 4308 하나를 offer 한다.
//
// 생성물(Ecu_Machine)의 skeleton 헤더와 타입은 그대로 쓰고, 구현부(aa/**)는
// 쓰지 않는다.

#include "servicecreator/ap_channel.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <ara/core/initialization.h>
#include <ara/core/instance_specifier.h>
#include <ara/com/com_error_domain.h>
#include <ara/exec/exec_error_domain.h>
#include <ara/exec/execution_client.h>

#include <rclcpp/clock.hpp>
#include <rclcpp/logging.hpp>

#include "servicecreator/bridge_config.hpp"
#include "servicecreator/handler_gate.hpp"
#include "servicecreator/send_aggregate.hpp"

#include "ap_input_convert.hpp"
#include "ap_trajectory_convert.hpp"
#include "ap_route_convert.hpp"

// PlanningAA 입력용 P-Port: Service 4308.
#include <oss/srv/SCr2MP/mp_si_servicecreator_skeleton.h>

// 출력 R-Port. 매니페스트 etc/interface/Exe_ServiceCreator/AA/RPort_PV2SCr.json 과
// 대응한다 — Service 2317 (SCr_SI_planningvalidator) / Event 12506 / SOME/IP.
#include <oss/srv/PV2SCr/scr_si_planningvalidator_proxy.h>
#include <oss/srv/SS2VS/vs_si_scenarioselector_proxy.h>
#include <oss/srv/AA2SCrTurn/scr_si_turncommand_proxy.h>
#include <oss/srv/AA2SCrHazard/scr_si_hazardcommand_proxy.h>
#include <oss/srv/AA2SCrMetrics/scr_si_planningmetrics_proxy.h>
#include <oss/srv/AA2SCrRemaining/scr_si_remainingdistance_proxy.h>

// Method R-Port. 매니페스트 etc/interface/Exe_ServiceCreator/AA/RPort_MP2SCr.json 과
// 대응한다 — Service 1817 (SCr_SI_missionplanner) / Method 10801·10809·10812.
#include <oss/srv/MP2SCr/scr_si_missionplanner_proxy.h>
#include <oss/srv/MP2RS/rs_si_missionplanner_proxy.h>

// 내부 planning 모듈은 PlanningAA 프로세스에 있으므로 입력 서비스는 하나다.
#define SERVICECREATOR_FOR_EACH_PPORT(X) X(MP)

#define SERVICECREATOR_SKELETON_TYPE(TAG) \
  oss::srv::SCr2 ## TAG::skeleton::TAG ## _SI_ServiceCreatorSkeleton

// 입력 채널 ↔ 스켈레톤 이벤트 멤버 ↔ AP 샘플 타입.
// 매니페스트 PPort_SCr2*.json 의 이벤트 11개와 1:1 대응한다.
#define SERVICECREATOR_FOR_EACH_INPUT_EVENT(X) \
  X(Clock, clock, oss::type::rosgraph_msgs::msg::Clock) \
  X(Accel, localization_acceleration, \
    oss::type::geometry_msgs::msg::AccelWithCovarianceStamped) \
  X(KinematicState, localization_kinematicstate, oss::type::nav_msgs::msg::Odometry) \
  X(VectorMap, map_vectormap, oss::type::autoware_map_msgs::msg::LaneletMapBin) \
  X(Objects, perception_objectrecognition_objects, \
    oss::type::autoware_perception_msgs::msg::PredictedObjects) \
  X(PointCloud, perception_obstaclesegmentation_pointcloud, \
    oss::type::sensor_msgs::msg::PointCloud2) \
  X(OccupancyGrid, perception_occupancygridmap_map, oss::type::nav_msgs::msg::OccupancyGrid) \
  X(TrafficSignals, perception_trafficlightrecognition_trafficsignals, \
    oss::type::autoware_msgs::autoware_perception_msgs::msg::TrafficLightGroupArray) \
  X(LateralOffset, \
    planning_scenarioplanning_lanedriving_behaviorplanning_behaviorpathplanner_input_lateraloffset, \
    oss::type::tier4_planning_msgs::msg::LateralOffset) \
  X(MaxVelocity, planning_scenarioplanning_maxvelocitydefault, \
    oss::type::tier4_planning_msgs::msg::VelocityLimit) \
  X(OperationMode, system_operationmode_state, \
    oss::type::autoware_adapi_v1_msgs::msg::OperationModeState)

namespace servicecreator
{
namespace
{

rclcpp::Logger logger()
{
  return rclcpp::get_logger("servicecreator.ap");
}

// ErrorCode 의 메시지는 StringView 라 널 종단이 보장되지 않는다.
std::string toString(const ara::core::ErrorCode & ec)
{
  const auto sv = ec.Message();
  return std::string(sv.data(), sv.size());
}

// "아직 아무도 안 듣는다" 와 진짜 장애를 가른다.
// 전자는 정상 기동 과정에서 반드시 지나가므로 매 호출을 찍으면 로그가 폭주한다.
bool isNoSubscriber(const ara::core::ErrorCode & ec)
{
  return ec == ara::com::ComErrc::kNoClients ||
         ec == ara::com::ComErrc::kServiceNotOffered;
}


// 채널별 송신 통계 — AP 측 [2] 지점. ROS 측 [1] 은 노드가 남긴다.
struct ChannelStats
{
  std::uint64_t attempted{0};   // send() 호출 횟수
  std::uint64_t sent{0};        // SCr2MP 전송 성공 횟수
  std::uint64_t no_subscriber{0};
  std::uint64_t failed{0};
  SendState last_state{SendState::Ok};
  bool state_known{false};
};

// 출력 채널(Event 12506)의 proxy 타입.
using TrajectoryProxy = oss::srv::PV2SCr::proxy::SCr_SI_planningvalidatorProxy;
using ScenarioProxy = oss::srv::SS2VS::proxy::VS_SI_scenarioselectorProxy;
using TurnProxy = oss::srv::AA2SCrTurn::proxy::SCr_SI_turncommandProxy;
using HazardProxy = oss::srv::AA2SCrHazard::proxy::SCr_SI_hazardcommandProxy;
using MetricsProxy = oss::srv::AA2SCrMetrics::proxy::SCr_SI_planningmetricsProxy;
using RemainingProxy = oss::srv::AA2SCrRemaining::proxy::SCr_SI_remainingdistanceProxy;
using RouteStateProxy = oss::srv::MP2RS::proxy::RS_SI_missionplannerProxy;

// Method 채널(Service 1817)의 proxy 타입.
using MethodProxy = oss::srv::MP2SCr::proxy::SCr_SI_missionplannerProxy;

// 타임아웃 취소(Future 파괴)는 서버로 전파되지 않으므로(SWS_CM_00194)
// 연속 타임아웃은 서버에 작업을 쌓는다. 연속 실패 횟수에 비례해 재호출을 미룬다.
// 원자 멤버 — 단일 executor 전제(무락 RMW)를
// 코드에서 제거해, MultiThreadedExecutor 전환이 조용한 데이터 레이스가 되지
// 않게 한다. 두 필드의 교차는 최악 "백오프 1회 더/덜"로 무해하다(정확성이
// 아니라 부하 완화 장치라서).
struct MethodBackoff
{
  std::atomic<int> consecutive{0};
  std::atomic<std::chrono::steady_clock::rep> last_ns{0};
};

// 구 proxy 소각 전담.
// ~ProxyVsomeipAdapter 는 vsomeip 앱 stop + 전용 스레드 join 을 수행한다
// (SDK 내부 동작). 이 소멸자가 executor(메서드 호출 스냅샷의
// 마지막 해제)나 SDK 수신/find 스레드(churn 의 replaced 해제)에서 돌면 그
// 스레드가 그 시간만큼 멈춘다 — executor 면 ROS 콜백 전면 정지. 파괴를 전담
// 스레드 1개로 모은다(고정 1, 곱 증식 아님. 첫 은퇴 때 lazy 기동).
class ProxyReaper
{
public:
  void enqueue(std::shared_ptr<void> retired)
  {
    if (!retired) {
      return;
    }
    // 파괴도 작업의 한 형태로 큐잉한다 — 실행(=해제)은 소각 스레드에서.
    enqueueJob([retired = std::move(retired)]() mutable {retired.reset();});
  }

  // 범용 배경 작업(proxy 생성자에 시한부 대기
  // 최대 ~20s 가 내장돼 있어, maintain 재배선을 executor 에서 하면 그 시간
  // ROS 콜백 전체가 선다. 이 스레드로 이관한다). stop 후에는 실행하지 않고
  // 버린다 — 종료 국면의 재배선·잔여 파괴는 stop()/호출자 몫.
  void enqueueJob(std::function<void()> job)
  {
    if (!job) {
      return;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!stopped_) {
        queue_.push_back(std::move(job));
        ensureThreadNoLock();
        cv_.notify_one();
        return;
      }
    }
    // stop 이후 — job 은 실행 없이 여기서 소멸(캡처 자원은 호출 스레드 해제).
  }

  // 스레드를 세우고 잔여를 호출 스레드에서 파괴한다 — stop() 이 Deinitialize() 전에 부른다.
  void stopAndDrain()
  {
    std::vector<std::function<void()>> rest;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopped_ = true;
      rest.swap(queue_);
    }
    cv_.notify_all();
    if (thread_.joinable()) {
      thread_.join();
    }
    // 잔여 파괴 작업만 호출 스레드에서 실행. 재배선류 작업도 잔여로 남았다면 실행 대신
    // 소멸시켜야 하지만, 큐에는 둘이 섞이므로 실행한다 — 재배선 작업은 첫 단계가 게이트
    // 진입이라 stop 후에는 스스로 즉시 반환한다(무해).
    for (auto & job : rest) {
      try {
        job();
      } catch (...) {
      }
    }
  }

  ~ProxyReaper() {stopAndDrain();}

private:
  void ensureThreadNoLock()
  {
    if (thread_.joinable()) {
      return;
    }
    thread_ = std::thread(
      [this] {
        for (;;) {
          std::vector<std::function<void()>> batch;
          {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] {return stopped_ || !queue_.empty();});
            if (stopped_ && queue_.empty()) {
              return;
            }
            batch.swap(queue_);
          }
          for (auto & job : batch) {  // 락 밖 실행 — vsomeip stop/join·재배선이 여기서만
            try {
              job();
            } catch (...) {
            }
          }
        }
      });
  }

  std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<std::function<void()>> queue_;
  std::thread thread_;
  bool stopped_{false};
};

// stop() 의 핸들러 대기 로그 주기 — 상한이 아니라 간격이다(완료까지 기다린다).
// 정상 경로는 첫 대기 안에 끝난다(전체 종료 0.2~0.4s).
constexpr std::chrono::milliseconds kStopBudget{400};

class ParaApChannel : public ApChannel
{
public:
  // 예외 등으로 stop() 없이 파괴되는 경로에서도 AP 콜백 등록이 객체보다
  // 오래 남지 않도록 한다. stop() 은 멱등이라 이중 호출 무해.
  ~ParaApChannel() override
  {
    // ctor-throw 경로에서는 노드 소멸자의 catch 가 적용되지 않는다 — 여기서
    // 던지면 noexcept 소멸자라 사유 없이 terminate 다.
    try {
      stop();
    } catch (...) {
    }
  }

  const char * name() const override {return "para (ara::com)";}

  bool start() override
  {
    config_ = BridgeConfig::fromEnv();

    const auto result = ara::core::Initialize();
    if (!result.HasValue()) {
      RCLCPP_ERROR(
        logger(), "ara::core::Initialize() 실패: %s", toString(result.Error()).c_str());
      return false;
    }
    initialized_ = true;

    reportRunning();
    offerAll();
    startTrajectoryFind();
    startScenarioFind();
    startTurnFind();
    startHazardFind();
    startMetricsFind();
    startRemainingFind();
    startRouteStateFind();
    startMethodFind();
    return true;
  }

  void stop() override
  {
    if (!initialized_) {
      return;
    }
    // 종료 순서 — StopFindService → Unsubscribe → 핸들러 완료 대기 →
    // 락 해제 → Skeleton/Proxy 파괴 → Deinitialize().
    //  - Proxy/Skeleton 파괴가 Deinitialize() 뒤로 가면 SWS_CORE_10002 위반.
    //  - 핸들러 대기를 건너뛰면 진행 중 핸들러가 파괴된 Proxy 를 만진다.
    auto proxy = currentTrajectoryProxy();
    auto scenario_proxy = currentScenarioProxy();
    auto turn_proxy = currentTurnProxy();
    auto hazard_proxy = currentHazardProxy();
    auto metrics_proxy = currentMetricsProxy();
    auto remaining_proxy = currentRemainingProxy();
    auto state_proxy = currentRouteStateProxy();
    auto method_proxy = currentMethodProxy();
    // stop() 이 중도에 던져 재진입되는 경우(소멸자 안전망 경로)
    // 같은 handle 로 StopFindService 를 두 번 부르지
    // 않도록 1회 플래그로 막는다(재정지는 R20-11 미규정 동작).
    if (!find_stop_done_) {
      if (proxy) {
        // 인자는 생성물 구현이 무시한다 — 이 인스턴스의 find 를 멈춘다.
        // unwire 는 여기서 하지 않는다 — 게이트가 닫히기 전이라 진행 중 find
        // 핸들러의 unwire(churn 경로)와 같은 인스턴스·같은 함수 동시 호출
        // (SWS_CM_00023/00029 UB)이 될 수 있다. 게이트
        // 대기 후 일괄 해제한다 — 대기 수렴은 게이트의 진입 차단이 보장한다.
        proxy->StopFindService(trajectory_find_handle_);
      }
      if (scenario_proxy) scenario_proxy->StopFindService(scenario_find_handle_);
      if (turn_proxy) turn_proxy->StopFindService(turn_find_handle_);
      if (hazard_proxy) hazard_proxy->StopFindService(hazard_find_handle_);
      if (metrics_proxy) metrics_proxy->StopFindService(metrics_find_handle_);
      if (remaining_proxy) remaining_proxy->StopFindService(remaining_find_handle_);
      if (method_proxy) {
        method_proxy->StopFindService(method_find_handle_);
        // Method 는 구독이 없어 해제할 배선이 없다. 파괴만 순서대로 한다.
      }
      if (state_proxy) {
        state_proxy->StopFindService(state_find_handle_);
      }
      // 미발견 종료 — 인스턴스가 없어 StopFindService 를 못 부른다. 레지스트리는 spec 키
      // 단일 슬롯이고 재등록이 구 functor 를 파괴하므로(kPara 백엔드), 캡처 없는 no-op 을
      // 재등록해 this-캡처 콜백을 제거한다.
      if (!proxy && trajectory_find_started_) {
        static_cast<void>(TrajectoryProxy::StartFindService(
            [](ara::com::ServiceHandleContainer<TrajectoryProxy::HandleType>,
            ara::com::FindServiceHandle) {},
            ara::core::InstanceSpecifier{"Exe_ServiceCreator/AA/RPort_PV2SCr"}));
      }
      if (!scenario_proxy && scenario_find_started_) {
        static_cast<void>(ScenarioProxy::StartFindService(
          [](ara::com::ServiceHandleContainer<ScenarioProxy::HandleType>,
             ara::com::FindServiceHandle) {},
          ara::core::InstanceSpecifier{"Exe_ServiceCreator/AA/RPort_SS2VS"}));
      }
      if (!turn_proxy && turn_find_started_) {
        static_cast<void>(TurnProxy::StartFindService(
          [](ara::com::ServiceHandleContainer<TurnProxy::HandleType>,
             ara::com::FindServiceHandle) {},
          ara::core::InstanceSpecifier{"Exe_ServiceCreator/AA/RPort_AA2SCrTurn"}));
      }
      if (!hazard_proxy && hazard_find_started_) {
        static_cast<void>(HazardProxy::StartFindService(
          [](ara::com::ServiceHandleContainer<HazardProxy::HandleType>,
             ara::com::FindServiceHandle) {},
          ara::core::InstanceSpecifier{"Exe_ServiceCreator/AA/RPort_AA2SCrHazard"}));
      }
      if (!metrics_proxy && metrics_find_started_) {
        static_cast<void>(MetricsProxy::StartFindService(
          [](ara::com::ServiceHandleContainer<MetricsProxy::HandleType>,
             ara::com::FindServiceHandle) {},
          ara::core::InstanceSpecifier{"Exe_ServiceCreator/AA/RPort_AA2SCrMetrics"}));
      }
      if (!remaining_proxy && remaining_find_started_) {
        static_cast<void>(RemainingProxy::StartFindService(
          [](ara::com::ServiceHandleContainer<RemainingProxy::HandleType>,
             ara::com::FindServiceHandle) {},
          ara::core::InstanceSpecifier{"Exe_ServiceCreator/AA/RPort_AA2SCrRemaining"}));
      }
      if (!method_proxy && method_find_started_) {
        static_cast<void>(MethodProxy::StartFindService(
            [](ara::com::ServiceHandleContainer<MethodProxy::HandleType>,
            ara::com::FindServiceHandle) {},
            ara::core::InstanceSpecifier{"Exe_ServiceCreator/AA/RPort_MP2SCr"}));
      }
      if (!state_proxy && state_find_started_) {
        static_cast<void>(RouteStateProxy::StartFindService(
            [](ara::com::ServiceHandleContainer<RouteStateProxy::HandleType>,
            ara::com::FindServiceHandle) {},
            ara::core::InstanceSpecifier{"Exe_ServiceCreator/AA/RPort_MP2RS"}));
      }
      find_stop_done_ = true;
    }

    // R20-11 에 진행 중 핸들러 완료 보장이 없어 직접 센다. 강제 진행은 UB 이므로
    // 끝날 때까지 반복 대기한다(경과는 주기 ERROR 로) — 종료 정책은 handler_gate.hpp.
    int waited_rounds = 0;
    while (!gate_->stopAndWait(kStopBudget)) {
      ++waited_rounds;
      RCLCPP_ERROR(
        logger(), "AP 핸들러 완료 대기 %lld ms 경과 — 계속 대기",
        static_cast<long long>(kStopBudget.count() * waited_rounds));
    }

    // stopping 표시 전에 진입해 있던 find 핸들러가 새 proxy 를 스왑해 넣었을 수
    // 있다. 이제 AP 콜백이 전부 끝났으므로 경합 없이 마저 내린다.
    auto installed = takeTrajectoryProxy();
    auto scenario_installed = takeScenarioProxy();
    auto turn_installed = takeTurnProxy();
    auto hazard_installed = takeHazardProxy();
    auto metrics_installed = takeMetricsProxy();
    auto remaining_installed = takeRemainingProxy();
    auto state_installed = takeRouteStateProxy();
    if (proxy) {
      unwireTrajectory(*proxy);        // 게이트 닫힘 — 경합 상대 없음
    }
    if (installed && installed != proxy) {
      installed->StopFindService(trajectory_find_handle_);
      unwireTrajectory(*installed);    // 스왑-중-stop 창의 신 proxy 도 해제
    }
    if (scenario_proxy) unwireScenario(*scenario_proxy);
    if (scenario_installed && scenario_installed != scenario_proxy) {
      scenario_installed->StopFindService(scenario_find_handle_);
      unwireScenario(*scenario_installed);
    }
    if (turn_proxy) unwireTurn(*turn_proxy);
    if (turn_installed && turn_installed != turn_proxy) {
      turn_installed->StopFindService(turn_find_handle_);
      unwireTurn(*turn_installed);
    }
    if (hazard_proxy) unwireHazard(*hazard_proxy);
    if (hazard_installed && hazard_installed != hazard_proxy) {
      hazard_installed->StopFindService(hazard_find_handle_);
      unwireHazard(*hazard_installed);
    }
    if (metrics_proxy) unwireMetrics(*metrics_proxy);
    if (metrics_installed && metrics_installed != metrics_proxy) {
      metrics_installed->StopFindService(metrics_find_handle_);
      unwireMetrics(*metrics_installed);
    }
    if (remaining_proxy) unwireRemaining(*remaining_proxy);
    if (remaining_installed && remaining_installed != remaining_proxy) {
      remaining_installed->StopFindService(remaining_find_handle_);
      unwireRemaining(*remaining_installed);
    }
    if (state_proxy) unwireRouteState(*state_proxy);
    if (state_installed && state_installed != state_proxy) {
      state_installed->StopFindService(state_find_handle_);
      unwireRouteState(*state_installed);
    }
    auto method_installed = takeMethodProxy();
    if (method_installed && method_installed != method_proxy) {
      method_installed->StopFindService(method_find_handle_);  // method 쪽 find 도 멈춘다
    }

    // 한계 — 제공자가 한 번도 발견되지 않은 채 종료하면 StopFindService 를 부를
    // 인스턴스가 없어 find 등록이 남고, Deinitialize 뒤에도 통지가 올 수 있다.
    // find 람다의 게이트 진입이 그것을 막는다(gate_ 선언부 참조).

    logStats();

    // 은퇴 proxy 를 전부 파괴하고 소각 스레드를 세운다(Deinitialize() 전).
    reaper_.stopAndDrain();

    // Skeleton/Proxy 를 먼저 파괴한 다음 Deinitialize().
    // sender 람다가 스켈레톤을 참조하므로 벡터를 먼저 비운다.
#define SERVICECREATOR_CLEAR_SENDERS(KEY, EVENT, SAMPLE) senders_##KEY##_.clear();
    SERVICECREATOR_FOR_EACH_INPUT_EVENT(SERVICECREATOR_CLEAR_SENDERS)
#undef SERVICECREATOR_CLEAR_SENDERS
    installed.reset();
    proxy.reset();
    scenario_installed.reset();
    scenario_proxy.reset();
    turn_installed.reset();
    turn_proxy.reset();
    hazard_installed.reset();
    hazard_proxy.reset();
    metrics_installed.reset();
    metrics_proxy.reset();
    remaining_installed.reset();
    remaining_proxy.reset();
    method_installed.reset();
    method_proxy.reset();
#define SERVICECREATOR_RESET(TAG) sk_##TAG##_.reset();
    SERVICECREATOR_FOR_EACH_PPORT(SERVICECREATOR_RESET)
#undef SERVICECREATOR_RESET

    static_cast<void>(ara::core::Deinitialize());
    initialized_ = false;
  }

  // ── 2장. 입력 서비스 · Events (11) ───────────────────────────────
  // 변환은 ap_input_convert.hpp, 송신 대상은 SCr2MP 하나. traceValues 는
  // 건수만으로는 변환이 맞았는지 알 수 없어 대표 값을 남겨 대조한다.
  SendResult send(const rosgraph_msgs::msg::Clock & msg) override
  {
    const auto sample = toAp(msg);
    if (config_.traceValues()) {
      RCLCPP_INFO(
        logger(), "[CLOCK] 송신 값 sec=%d nanosec=%u",
        static_cast<int>(sample.clock.sec),
        static_cast<unsigned>(sample.clock.nanosec));
    }
    return fanOut(ChannelKey::Clock, senders_Clock_, sample);
  }

  SendResult send(const geometry_msgs::msg::AccelWithCovarianceStamped & msg) override
  {
    const auto sample = toAp(msg);
    if (config_.traceValues()) {
      RCLCPP_INFO(
        logger(), "[ACCEL] 송신 값 linear.x=%.3f sec=%d",
        sample.accel.accel.linear.x, static_cast<int>(sample.header.stamp.sec));
    }
    return fanOut(ChannelKey::Accel, senders_Accel_, sample);
  }

  SendResult send(const nav_msgs::msg::Odometry & msg) override
  {
    const auto sample = toAp(msg);
    if (config_.traceValues()) {
      RCLCPP_INFO(
        logger(), "[KINEMATIC_STATE] 송신 값 pos=(%.3f, %.3f) sec=%d",
        sample.pose.pose.position.x, sample.pose.pose.position.y,
        static_cast<int>(sample.header.stamp.sec));
    }
    return fanOut(ChannelKey::KinematicState, senders_KinematicState_, sample);
  }

  SendResult send(const autoware_map_msgs::msg::LaneletMapBin & msg) override
  {
    if (exceedsPayloadCap(msg.data.size(), ChannelKey::VectorMap)) {
      return SendResult::Error;
    }
    const auto sample = toAp(msg);
    if (config_.traceValues()) {
      RCLCPP_INFO(
        logger(), "[VECTOR_MAP] 송신 값 bytes=%zu name='%s'",
        sample.data.size(), safeText(sample.name_map.data(), sample.name_map.size()).c_str());
    }
    return fanOut(ChannelKey::VectorMap, senders_VectorMap_, sample);
  }

  SendResult send(const autoware_perception_msgs::msg::PredictedObjects & msg) override
  {
    const auto sample = toAp(msg);
    if (config_.traceValues()) {
      RCLCPP_INFO(
        logger(), "[OBJECTS] 송신 값 objects=%zu cls0=%zu first.x=%.3f",
        sample.objects.size(),
        sample.objects.empty() ? 0U : sample.objects.front().classification.size(),
        sample.objects.empty() ? 0.0 :
        sample.objects.front().kinematics.initial_pose_with_covariance.pose.position.x);
    }
    return fanOut(ChannelKey::Objects, senders_Objects_, sample);
  }

  SendResult send(const sensor_msgs::msg::PointCloud2 & msg) override
  {
    if (exceedsPayloadCap(msg.data.size(), ChannelKey::PointCloud)) {
      return SendResult::Error;
    }
    const auto sample = toAp(msg);
    if (config_.traceValues()) {
      RCLCPP_INFO(
        logger(), "[POINTCLOUD] 송신 값 %ux%u bytes=%zu",
        static_cast<unsigned>(sample.width), static_cast<unsigned>(sample.height),
        sample.data.size());
    }
    return fanOut(ChannelKey::PointCloud, senders_PointCloud_, sample);
  }

  SendResult send(const nav_msgs::msg::OccupancyGrid & msg) override
  {
    if (exceedsPayloadCap(msg.data.size(), ChannelKey::OccupancyGrid)) {
      return SendResult::Error;
    }
    const auto sample = toAp(msg);
    if (config_.traceValues()) {
      RCLCPP_INFO(
        logger(), "[OCCUPANCY_GRID] 송신 값 %ux%u res=%.3f",
        static_cast<unsigned>(sample.info.width), static_cast<unsigned>(sample.info.height),
        static_cast<double>(sample.info.resolution));
    }
    return fanOut(ChannelKey::OccupancyGrid, senders_OccupancyGrid_, sample);
  }

  SendResult send(const autoware_perception_msgs::msg::TrafficLightGroupArray & msg) override
  {
    const auto sample = toAp(msg);
    if (config_.traceValues()) {
      RCLCPP_INFO(
        logger(), "[TRAFFIC_SIGNALS] 송신 값 groups=%zu first_id=%lld",
        sample.traffic_light_groups.size(),
        sample.traffic_light_groups.empty() ? 0LL :
        static_cast<long long>(sample.traffic_light_groups.front().traffic_light_group_id));
    }
    return fanOut(ChannelKey::TrafficSignals, senders_TrafficSignals_, sample);
  }

  SendResult send(const tier4_planning_msgs::msg::LateralOffset & msg) override
  {
    const auto sample = toAp(msg);
    if (config_.traceValues()) {
      RCLCPP_INFO(
        logger(), "[LATERAL_OFFSET] 송신 값 offset=%.3f",
        static_cast<double>(sample.lateral_offset));
    }
    return fanOut(ChannelKey::LateralOffset, senders_LateralOffset_, sample);
  }

  SendResult send(const tier4_planning_msgs::msg::VelocityLimit & msg) override
  {
    const auto sample = toAp(msg);
    if (config_.traceValues()) {
      RCLCPP_INFO(
        logger(), "[MAX_VELOCITY] 송신 값 max=%.3f use_constraints=%d",
        static_cast<double>(sample.max_velocity),
        static_cast<int>(sample.use_constraints));
    }
    return fanOut(ChannelKey::MaxVelocity, senders_MaxVelocity_, sample);
  }

  SendResult send(const autoware_adapi_v1_msgs::msg::OperationModeState & msg) override
  {
    const auto sample = toAp(msg);
    if (config_.traceValues()) {
      RCLCPP_INFO(
        logger(), "[OPERATION_MODE] 송신 값 mode=%u in_transition=%d",
        static_cast<unsigned>(sample.mode), static_cast<int>(sample.is_in_transition));
    }
    return fanOut(ChannelKey::OperationMode, senders_OperationMode_, sample);
  }

  // ── 3장. 입력 서비스 · Methods (3) ────────────────────────
  bool call(
    const tier4_planning_msgs::srv::ClearRoute::Request &,
    tier4_planning_msgs::srv::ClearRoute::Response & response) override
  {
    // AP clearroute 는 인자를 받지 않고 ROS 요청부도 비어 있다 — 변환 없음.
    return callMethod(
      "clear_route", response,
      [](MethodProxy & proxy) {
        return proxy.planning_missionplanning_missionplanner_clearroute();
      });
  }

  bool call(
    const tier4_planning_msgs::srv::SetLaneletRoute::Request & request,
    tier4_planning_msgs::srv::SetLaneletRoute::Response & response) override
  {
    return callMethod(
      "set_lanelet_route", response,
      [ap_request = toAp(request)](MethodProxy & proxy) {
        return proxy.planning_missionplanning_missionplanner_setlaneletroute(ap_request);
      });
  }

  bool call(
    const tier4_planning_msgs::srv::SetWaypointRoute::Request & request,
    tier4_planning_msgs::srv::SetWaypointRoute::Response & response) override
  {
    return callMethod(
      "set_waypoint_route", response,
      [ap_request = toAp(request)](MethodProxy & proxy) {
        return proxy.planning_missionplanning_missionplanner_setwaypointroute(ap_request);
      });
  }

  // ── 4장. 출력 서비스 · Events ────────────────────────────────────
  // 노드 생성자(setupOutputEvents)가 start() 이전에 부른다 — AP 스레드가 생기기
  // 전이므로 락 없이 쓰고, 이후에는 AP 스레드가 읽기만 한다.
  void setTrajectoryHandler(TrajectoryHandler handler) override
  {
    trajectory_handler_ = std::move(handler);
  }
  void setScenarioTrajectoryHandler(TrajectoryHandler handler) override
  {
    scenario_handler_ = std::move(handler);
  }
  void setTurnHandler(TurnHandler handler) override {turn_handler_ = std::move(handler);}
  void setHazardHandler(HazardHandler handler) override {hazard_handler_ = std::move(handler);}
  void setMetricsHandler(MetricsHandler handler) override {metrics_handler_ = std::move(handler);}
  void setRemainingHandler(RemainingHandler handler) override {remaining_handler_ = std::move(handler);}
  void setRouteStateHandler(RouteStateHandler handler) override
  {
    route_state_handler_ = std::move(handler);
  }
  void setRouteHandler(RouteHandler handler) override
  {
    route_handler_ = std::move(handler);
  }


private:
  void startRouteStateFind()
  {
    const auto find = RouteStateProxy::StartFindService(
      [this, gate = gate_](
        ara::com::ServiceHandleContainer<RouteStateProxy::HandleType> handles,
        ara::com::FindServiceHandle) {
        HandlerGate::Entry entry{*gate};
        if (!entry.entered() || handles.empty()) return;
        try {
          wireRouteState(handles.front());
        } catch (const std::exception & e) {
          RCLCPP_ERROR(logger(), "[ROUTE_STATE] 제공자 연결 실패: %s", e.what());
        } catch (...) {
          RCLCPP_ERROR(logger(), "[ROUTE_STATE] 제공자 연결 실패");
        }
      }, ara::core::InstanceSpecifier{"Exe_ServiceCreator/AA/RPort_MP2RS"});
    if (find.HasValue()) {
      state_find_handle_ = find.Value();
      state_find_started_ = true;
      RCLCPP_INFO(logger(), "[ROUTE_STATE] Service 1814 대기");
    } else {
      RCLCPP_ERROR(logger(), "[ROUTE_STATE] StartFindService 실패: %s",
        toString(find.Error()).c_str());
    }
  }

  void wireRouteState(RouteStateProxy::HandleType & handle)
  {
    auto proxy = std::make_shared<RouteStateProxy>(handle);
    std::weak_ptr<RouteStateProxy> weak = proxy;
    const auto reg = proxy->planning_missionplanning_state.SetReceiveHandler(
      [this, weak] {onRouteStateEvent(weak);});
    if (!reg.HasValue()) {
      RCLCPP_ERROR(logger(), "[ROUTE_STATE] SetReceiveHandler 실패: %s",
        toString(reg.Error()).c_str());
      scheduleRouteStateRewire(handle);
      reaper_.enqueue(std::move(proxy));
      return;
    }
    const auto route_reg = proxy->planning_missionplanning_route.SetReceiveHandler(
      [this, weak] {onRouteEvent(weak);});
    if (!route_reg.HasValue()) {
      RCLCPP_ERROR(logger(), "[ROUTE] SetReceiveHandler 실패: %s",
        toString(route_reg.Error()).c_str());
      proxy->planning_missionplanning_state.UnsetReceiveHandler();
      scheduleRouteStateRewire(handle);
      reaper_.enqueue(std::move(proxy));
      return;
    }
    std::shared_ptr<RouteStateProxy> replaced;
    {
      std::lock_guard<std::mutex> lock(proxy_mutex_);
      replaced = std::exchange(state_proxy_, proxy);
    }
    if (replaced) {
      unwireRouteState(*replaced);
      reaper_.enqueue(std::move(replaced));
    }
    const auto sub = proxy->planning_missionplanning_state.Subscribe(1);
    if (!sub.HasValue()) {
      RCLCPP_ERROR(logger(), "[ROUTE_STATE] Subscribe 실패: %s",
        toString(sub.Error()).c_str());
      scheduleRouteStateRewire(handle);
    }
    const auto route_sub = proxy->planning_missionplanning_route.Subscribe(1);
    if (!route_sub.HasValue()) {
      RCLCPP_ERROR(logger(), "[ROUTE] Subscribe 실패: %s",
        toString(route_sub.Error()).c_str());
      scheduleRouteStateRewire(handle);
    }
    if (sub.HasValue() && route_sub.HasValue()) {
      state_rewire_needed_ = false;
      RCLCPP_INFO(logger(), "[ROUTE] Event 10602 및 상태 Event 10604 구독");
    }
  }

  void onRouteEvent(const std::weak_ptr<RouteStateProxy> & weak)
  {
    HandlerGate::Entry entry{*gate_};
    if (!entry.entered()) return;
    auto proxy = weak.lock();
    if (!proxy) return;
    try {
      const auto taken = proxy->planning_missionplanning_route.GetNewSamples(
        [this](auto sample) {
          if (!route_handler_) return;
          const auto ros = toRosRoute(*sample.Get());
          RCLCPP_INFO(logger(), "[ROUTE] 경로 본문 수신: segments=%zu",
            ros.segments.size());
          route_handler_(ros);
        });
      if (!taken.HasValue())
        RCLCPP_ERROR(logger(), "[ROUTE] GetNewSamples 실패: %s",
          toString(taken.Error()).c_str());
    } catch (const std::exception & e) {
      RCLCPP_ERROR(logger(), "[ROUTE] 수신 실패: %s", e.what());
    } catch (...) {
      RCLCPP_ERROR(logger(), "[ROUTE] 수신 실패");
    }
    try {
      reaper_.enqueue(std::move(proxy));
    } catch (...) {
      RCLCPP_ERROR(logger(), "[ROUTE] proxy 소각 큐잉 실패");
    }
  }

  void onRouteStateEvent(const std::weak_ptr<RouteStateProxy> & weak)
  {
    HandlerGate::Entry entry{*gate_};
    if (!entry.entered()) return;
    auto proxy = weak.lock();
    if (!proxy) return;
    try {
      const auto taken = proxy->planning_missionplanning_state.GetNewSamples(
        [this](auto sample) {
          if (!route_state_handler_) return;
          const auto & ap = *sample.Get();
          tier4_planning_msgs::msg::RouteState ros{};
          ros.stamp.sec = ap.stamp.sec;
          ros.stamp.nanosec = ap.stamp.nanosec;
          ros.state = ap.state;
          route_state_handler_(ros);
        });
      if (!taken.HasValue())
        RCLCPP_ERROR(logger(), "[ROUTE_STATE] GetNewSamples 실패: %s",
          toString(taken.Error()).c_str());
    } catch (const std::exception & e) {
      RCLCPP_ERROR(logger(), "[ROUTE_STATE] 수신 실패: %s", e.what());
    } catch (...) {
      RCLCPP_ERROR(logger(), "[ROUTE_STATE] 수신 실패");
    }
    try {
      reaper_.enqueue(std::move(proxy));
    } catch (...) {
      RCLCPP_ERROR(logger(), "[ROUTE_STATE] proxy 소각 큐잉 실패");
    }
  }

  std::shared_ptr<RouteStateProxy> currentRouteStateProxy()
  {
    std::lock_guard<std::mutex> lock(proxy_mutex_);
    return state_proxy_;
  }

  std::shared_ptr<RouteStateProxy> takeRouteStateProxy()
  {
    std::lock_guard<std::mutex> lock(proxy_mutex_);
    return std::move(state_proxy_);
  }

  void unwireRouteState(RouteStateProxy & proxy)
  {
    proxy.planning_missionplanning_route.Unsubscribe();
    const auto route_unset = proxy.planning_missionplanning_route.UnsetReceiveHandler();
    if (!route_unset.HasValue())
      RCLCPP_WARN(logger(), "[ROUTE] UnsetReceiveHandler 실패: %s",
        toString(route_unset.Error()).c_str());
    proxy.planning_missionplanning_state.Unsubscribe();
    const auto unset = proxy.planning_missionplanning_state.UnsetReceiveHandler();
    if (!unset.HasValue())
      RCLCPP_WARN(logger(), "[ROUTE_STATE] UnsetReceiveHandler 실패: %s",
        toString(unset.Error()).c_str());
  }

  void scheduleRouteStateRewire(RouteStateProxy::HandleType & handle)
  {
    {
      std::lock_guard<std::mutex> lock(rewire_mutex_);
      state_rewire_handle_.emplace(handle);
    }
    state_rewire_needed_ = true;
  }

  // Service Discovery 완료를 기다리지 않는다.
  // ExecutionClient 는 지역 변수다. 멤버로 잡으면 Deinitialize() 이후까지
  // 살아남아 SWS_CORE_10002 를 위반한다.
  void reportRunning() const
  {
    ara::exec::ExecutionClient client;
    const auto reported =
      client.ReportExecutionState(ara::exec::ExecutionState::kRunning);
    if (reported.HasValue()) {
      RCLCPP_INFO(logger(), "EM 에 kRunning 보고");
      return;
    }
    RCLCPP_ERROR(
      logger(), "ReportExecutionState(kRunning) 실패: %s%s",
      toString(reported.Error()).c_str(),
      reported.Error() == ara::exec::ExecErrc::kInvalidTransition
      ? " — Process State 가 Initializing 이 아니다. 보고가 늦었다." : "");
  }

  void offerAll()
  {
    int offered = 0;
#define SERVICECREATOR_OFFER(TAG)                                                      \
    {                                                                              \
      const ara::core::InstanceSpecifier spec{                                     \
        "Exe_ServiceCreator/AA/PPort_SCr2" #TAG};                                  \
      sk_##TAG##_ = std::make_unique<SERVICECREATOR_SKELETON_TYPE(TAG)>(spec);         \
      const auto offer = sk_##TAG##_->OfferService();                              \
      if (offer.HasValue()) {                                                      \
        ++offered;                                                                 \
        registerSenders(*sk_##TAG##_);                                             \
      } else {                                                                     \
        RCLCPP_ERROR(                                                              \
          logger(), "OfferService(PPort_SCr2" #TAG ") 실패: %s",                   \
          toString(offer.Error()).c_str());                                        \
      }                                                                            \
    }
    SERVICECREATOR_FOR_EACH_PPORT(SERVICECREATOR_OFFER)
#undef SERVICECREATOR_OFFER
    RCLCPP_INFO(logger(), "Skeleton offer %d/1", offered);
  }

  void startMetricsFind()
  {
    const auto find = MetricsProxy::StartFindService(
      [this, gate = gate_](
        ara::com::ServiceHandleContainer<MetricsProxy::HandleType> handles,
        ara::com::FindServiceHandle) {
        HandlerGate::Entry entry{*gate};
        if (!entry.entered() || handles.empty()) return;
        try {
          auto next = std::make_shared<MetricsProxy>(handles[0]);
          std::weak_ptr<MetricsProxy> weak = next;
          const auto registered = next->planning_evaluator_metrics.SetReceiveHandler(
            [this, weak, gate] {
              HandlerGate::Entry receive_entry{*gate};
              if (!receive_entry.entered()) return;
              if (auto current = weak.lock()) {
                const auto samples = current->planning_evaluator_metrics.GetNewSamples(
                  [this](auto sample) {
                    if (!metrics_handler_) return;
                    const auto &ap = *sample.Get();
                    tier4_metric_msgs::msg::MetricArray ros;
                    ros.stamp.sec = ap.stamp.sec;
                    ros.stamp.nanosec = ap.stamp.nanosec;
                    ros.metric_array.reserve(ap.metric_array.size());
                    for (const auto &item : ap.metric_array) {
                      tier4_metric_msgs::msg::Metric metric;
                      metric.name = item.name.c_str();
                      metric.unit = item.unit.c_str();
                      metric.value = item.value.c_str();
                      ros.metric_array.push_back(std::move(metric));
                    }
                    metrics_handler_(ros);
                  });
                if (!samples.HasValue()) RCLCPP_ERROR(logger(), "[METRICS] GetNewSamples failed");
              }
            });
          if (!registered.HasValue()) return;
          auto previous = takeMetricsProxy();
          if (previous) {unwireMetrics(*previous); reaper_.enqueue(std::move(previous));}
          {std::lock_guard<std::mutex> lock(proxy_mutex_); metrics_proxy_ = next;}
          if (!next->planning_evaluator_metrics.Subscribe(1).HasValue())
            RCLCPP_ERROR(logger(), "[METRICS] Subscribe failed");
        } catch (const std::exception &e) {
          RCLCPP_ERROR(logger(), "[METRICS] wiring failed: %s", e.what());
        }
      },
      ara::core::InstanceSpecifier{"Exe_ServiceCreator/AA/RPort_AA2SCrMetrics"});
    if (find.HasValue()) {metrics_find_handle_ = find.Value(); metrics_find_started_ = true;}
    else RCLCPP_ERROR(logger(), "[METRICS] StartFindService failed");
  }

  std::shared_ptr<MetricsProxy> currentMetricsProxy()
  {std::lock_guard<std::mutex> lock(proxy_mutex_); return metrics_proxy_;}
  std::shared_ptr<MetricsProxy> takeMetricsProxy()
  {std::lock_guard<std::mutex> lock(proxy_mutex_); return std::exchange(metrics_proxy_, {});}
  void unwireMetrics(MetricsProxy &proxy)
  {
    proxy.planning_evaluator_metrics.Unsubscribe();
    static_cast<void>(proxy.planning_evaluator_metrics.UnsetReceiveHandler());
  }

  void startRemainingFind()
  {
    const auto find = RemainingProxy::StartFindService(
      [this, gate = gate_](
        ara::com::ServiceHandleContainer<RemainingProxy::HandleType> handles,
        ara::com::FindServiceHandle) {
        HandlerGate::Entry entry{*gate};
        if (!entry.entered() || handles.empty()) return;
        try {
          auto next = std::make_shared<RemainingProxy>(handles[0]);
          std::weak_ptr<RemainingProxy> weak = next;
          const auto registered = next->planning_mission_remaining_distance_time.SetReceiveHandler(
            [this, weak, gate] {
              HandlerGate::Entry receive_entry{*gate};
              if (!receive_entry.entered()) return;
              if (auto current = weak.lock()) {
                const auto samples = current->planning_mission_remaining_distance_time.GetNewSamples(
                  [this](auto sample) {
                    if (!remaining_handler_) return;
                    const auto &ap = *sample.Get();
                    autoware_internal_msgs::msg::MissionRemainingDistanceTime ros;
                    ros.remaining_distance = ap.remaining_distance;
                    ros.remaining_time = ap.remaining_time;
                    remaining_handler_(ros);
                  });
                if (!samples.HasValue()) RCLCPP_ERROR(logger(), "[REMAINING] GetNewSamples failed");
              }
            });
          if (!registered.HasValue()) return;
          auto previous = takeRemainingProxy();
          if (previous) {unwireRemaining(*previous); reaper_.enqueue(std::move(previous));}
          {std::lock_guard<std::mutex> lock(proxy_mutex_); remaining_proxy_ = next;}
          if (!next->planning_mission_remaining_distance_time.Subscribe(1).HasValue())
            RCLCPP_ERROR(logger(), "[REMAINING] Subscribe failed");
        } catch (const std::exception &e) {
          RCLCPP_ERROR(logger(), "[REMAINING] wiring failed: %s", e.what());
        }
      },
      ara::core::InstanceSpecifier{"Exe_ServiceCreator/AA/RPort_AA2SCrRemaining"});
    if (find.HasValue()) {remaining_find_handle_ = find.Value(); remaining_find_started_ = true;}
    else RCLCPP_ERROR(logger(), "[REMAINING] StartFindService failed");
  }

  std::shared_ptr<RemainingProxy> currentRemainingProxy()
  {std::lock_guard<std::mutex> lock(proxy_mutex_); return remaining_proxy_;}
  std::shared_ptr<RemainingProxy> takeRemainingProxy()
  {std::lock_guard<std::mutex> lock(proxy_mutex_); return std::exchange(remaining_proxy_, {});}
  void unwireRemaining(RemainingProxy &proxy)
  {
    proxy.planning_mission_remaining_distance_time.Unsubscribe();
    static_cast<void>(proxy.planning_mission_remaining_distance_time.UnsetReceiveHandler());
  }

  void startTurnFind()
  {
    const auto find = TurnProxy::StartFindService(
      [this, gate = gate_](
        ara::com::ServiceHandleContainer<TurnProxy::HandleType> handles,
        ara::com::FindServiceHandle) {
        HandlerGate::Entry entry{*gate};
        if (!entry.entered() || handles.empty()) return;
        try {
          auto next = std::make_shared<TurnProxy>(handles[0]);
          std::weak_ptr<TurnProxy> weak = next;
          const auto registered = next->planning_turn_indicators_cmd.SetReceiveHandler(
            [this, weak, gate] {
              HandlerGate::Entry receive_entry{*gate};
              if (!receive_entry.entered()) return;
              if (auto current = weak.lock()) {
                const auto samples = current->planning_turn_indicators_cmd.GetNewSamples(
                  [this](auto sample) {
                    if (!turn_handler_) return;
                    const auto &ap = *sample.Get();
                    autoware_vehicle_msgs::msg::TurnIndicatorsCommand ros;
                    ros.stamp.sec = ap.stamp.sec;
                    ros.stamp.nanosec = ap.stamp.nanosec;
                    ros.command = ap.command;
                    turn_handler_(ros);
                  });
                if (!samples.HasValue()) RCLCPP_ERROR(logger(), "[TURN] GetNewSamples failed");
              }
            });
          if (!registered.HasValue()) return;
          auto previous = takeTurnProxy();
          if (previous) {unwireTurn(*previous); reaper_.enqueue(std::move(previous));}
          {std::lock_guard<std::mutex> lock(proxy_mutex_); turn_proxy_ = next;}
          if (!next->planning_turn_indicators_cmd.Subscribe(1).HasValue())
            RCLCPP_ERROR(logger(), "[TURN] Subscribe failed");
        } catch (const std::exception &e) {
          RCLCPP_ERROR(logger(), "[TURN] wiring failed: %s", e.what());
        }
      },
      ara::core::InstanceSpecifier{"Exe_ServiceCreator/AA/RPort_AA2SCrTurn"});
    if (find.HasValue()) {turn_find_handle_ = find.Value(); turn_find_started_ = true;}
    else RCLCPP_ERROR(logger(), "[TURN] StartFindService failed");
  }

  std::shared_ptr<TurnProxy> currentTurnProxy()
  {std::lock_guard<std::mutex> lock(proxy_mutex_); return turn_proxy_;}
  std::shared_ptr<TurnProxy> takeTurnProxy()
  {std::lock_guard<std::mutex> lock(proxy_mutex_); return std::exchange(turn_proxy_, {});}
  void unwireTurn(TurnProxy &proxy)
  {
    proxy.planning_turn_indicators_cmd.Unsubscribe();
    static_cast<void>(proxy.planning_turn_indicators_cmd.UnsetReceiveHandler());
  }

  void startHazardFind()
  {
    const auto find = HazardProxy::StartFindService(
      [this, gate = gate_](
        ara::com::ServiceHandleContainer<HazardProxy::HandleType> handles,
        ara::com::FindServiceHandle) {
        HandlerGate::Entry entry{*gate};
        if (!entry.entered() || handles.empty()) return;
        try {
          auto next = std::make_shared<HazardProxy>(handles[0]);
          std::weak_ptr<HazardProxy> weak = next;
          const auto registered = next->planning_hazard_lights_cmd.SetReceiveHandler(
            [this, weak, gate] {
              HandlerGate::Entry receive_entry{*gate};
              if (!receive_entry.entered()) return;
              if (auto current = weak.lock()) {
                const auto samples = current->planning_hazard_lights_cmd.GetNewSamples(
                  [this](auto sample) {
                    if (!hazard_handler_) return;
                    const auto &ap = *sample.Get();
                    autoware_vehicle_msgs::msg::HazardLightsCommand ros;
                    ros.stamp.sec = ap.stamp.sec;
                    ros.stamp.nanosec = ap.stamp.nanosec;
                    ros.command = ap.command;
                    hazard_handler_(ros);
                  });
                if (!samples.HasValue()) RCLCPP_ERROR(logger(), "[HAZARD] GetNewSamples failed");
              }
            });
          if (!registered.HasValue()) return;
          auto previous = takeHazardProxy();
          if (previous) {unwireHazard(*previous); reaper_.enqueue(std::move(previous));}
          {std::lock_guard<std::mutex> lock(proxy_mutex_); hazard_proxy_ = next;}
          if (!next->planning_hazard_lights_cmd.Subscribe(1).HasValue())
            RCLCPP_ERROR(logger(), "[HAZARD] Subscribe failed");
        } catch (const std::exception &e) {
          RCLCPP_ERROR(logger(), "[HAZARD] wiring failed: %s", e.what());
        }
      },
      ara::core::InstanceSpecifier{"Exe_ServiceCreator/AA/RPort_AA2SCrHazard"});
    if (find.HasValue()) {hazard_find_handle_ = find.Value(); hazard_find_started_ = true;}
    else RCLCPP_ERROR(logger(), "[HAZARD] StartFindService failed");
  }

  std::shared_ptr<HazardProxy> currentHazardProxy()
  {std::lock_guard<std::mutex> lock(proxy_mutex_); return hazard_proxy_;}
  std::shared_ptr<HazardProxy> takeHazardProxy()
  {std::lock_guard<std::mutex> lock(proxy_mutex_); return std::exchange(hazard_proxy_, {});}
  void unwireHazard(HazardProxy &proxy)
  {
    proxy.planning_hazard_lights_cmd.Unsubscribe();
    static_cast<void>(proxy.planning_hazard_lights_cmd.UnsetReceiveHandler());
  }

  // Scenario selector's selected trajectory travels on its existing AP service.
  void startScenarioFind()
  {
    const auto find = ScenarioProxy::StartFindService(
      [this, gate = gate_](
        ara::com::ServiceHandleContainer<ScenarioProxy::HandleType> handles,
        ara::com::FindServiceHandle) {
        HandlerGate::Entry entry{*gate};
        if (!entry.entered() || handles.empty()) return;
        try {
          auto next = std::make_shared<ScenarioProxy>(handles[0]);
          std::weak_ptr<ScenarioProxy> weak = next;
          const auto registered = next->planning_scenarioplanning_scenarioselector_trajectory.SetReceiveHandler(
            [this, weak, gate] {
              HandlerGate::Entry receive_entry{*gate};
              if (!receive_entry.entered()) return;
              if (auto current = weak.lock()) {
                const auto samples = current->planning_scenarioplanning_scenarioselector_trajectory.GetNewSamples(
                  [this](auto sample) {
                    if (scenario_handler_) scenario_handler_(toRosTrajectory(*sample.Get()));
                  });
                if (!samples.HasValue()) RCLCPP_ERROR(logger(), "[SCENARIO] GetNewSamples failed");
              }
            });
          if (!registered.HasValue()) {
            RCLCPP_ERROR(logger(), "[SCENARIO] SetReceiveHandler failed");
            return;
          }
          auto previous = takeScenarioProxy();
          if (previous) {
            unwireScenario(*previous);
            reaper_.enqueue(std::move(previous));
          }
          {
            std::lock_guard<std::mutex> lock(proxy_mutex_);
            scenario_proxy_ = next;
          }
          const auto subscribed = next->planning_scenarioplanning_scenarioselector_trajectory.Subscribe(1);
          if (!subscribed.HasValue()) RCLCPP_ERROR(logger(), "[SCENARIO] Subscribe failed");
        } catch (const std::exception &e) {
          RCLCPP_ERROR(logger(), "[SCENARIO] wiring failed: %s", e.what());
        }
      },
      ara::core::InstanceSpecifier{"Exe_ServiceCreator/AA/RPort_SS2VS"});
    if (find.HasValue()) {
      scenario_find_handle_ = find.Value();
      scenario_find_started_ = true;
    } else RCLCPP_ERROR(logger(), "[SCENARIO] StartFindService failed");
  }

  std::shared_ptr<ScenarioProxy> currentScenarioProxy()
  {
    std::lock_guard<std::mutex> lock(proxy_mutex_);
    return scenario_proxy_;
  }

  std::shared_ptr<ScenarioProxy> takeScenarioProxy()
  {
    std::lock_guard<std::mutex> lock(proxy_mutex_);
    return std::exchange(scenario_proxy_, {});
  }

  void unwireScenario(ScenarioProxy &proxy)
  {
    proxy.planning_scenarioplanning_scenarioselector_trajectory.Unsubscribe();
    static_cast<void>(proxy.planning_scenarioplanning_scenarioselector_trajectory.UnsetReceiveHandler());
  }

  // ── 출력 채널 (Event 12506 → /planning/scenario_planning/trajectory) ──

  // 1회성 FindService() 는 제공자가 늦게 뜨는 정상 상황에서 출력 채널을
  // 영구히 죽인다. StartFindService() 로 발견·소실·재등장을 계속 통지받는다.
  void startTrajectoryFind()
  {
    const ara::core::InstanceSpecifier spec{"Exe_ServiceCreator/AA/RPort_PV2SCr"};
    const auto find = TrajectoryProxy::StartFindService(
      [this, gate = gate_](
        ara::com::ServiceHandleContainer<TrajectoryProxy::HandleType> handles,
        ara::com::FindServiceHandle) {
        // 게이트(공유 소유) 진입에 성공했을 때만 this 를 만진다.
        HandlerGate::Entry entry{*gate};
        if (!entry.entered()) {
          return;
        }
        onTrajectoryFound(std::move(handles));
      },
      spec);
    if (find.HasValue()) {
      trajectory_find_handle_ = find.Value();
      trajectory_find_started_ = true;
      RCLCPP_INFO(logger(), "[TRAJECTORY] StartFindService — Service 2317 을 기다린다");
    } else {
      RCLCPP_ERROR(
        logger(), "[TRAJECTORY] StartFindService 실패: %s — 출력 채널 없이 동작",
        toString(find.Error()).c_str());
    }
  }

  // find 핸들러 — 세 번째 종류의 AP 스레드. 게이트를 지나며,
  // proxy 는 배선을 끝낸 뒤 원자적으로 스왑한다. 수신 핸들러는 지역 사본을 쓰므로
  // 이전 proxy 는 마지막 사본이 빠질 때 파괴된다.
  void onTrajectoryFound(ara::com::ServiceHandleContainer<TrajectoryProxy::HandleType> handles)
  {
    // 게이트 진입은 find 람다가 이미 마쳤다.
    // 예외가 SDK 스레드로 새면 terminate — 여기서 전부 막는다. 실패한 통지는 다음 통지/maintain 몫.
    try {
      if (handles.empty()) {
        // 아직 발견 전이거나 소실 통지다. 기존 proxy 는 유지한다 — (재)등장하면
        // 새 proxy 로 교체된다. 첫 콜백은 대부분 빈 목록으로 온다.
        RCLCPP_INFO(logger(), "[TRAJECTORY] 제공자 없음 — (재)등장을 기다린다");
        return;
      }
      RCLCPP_INFO(
        logger(), "[TRAJECTORY] 제공자 발견 — service %u instance %u",
        static_cast<unsigned>(handles[0].GetServiceHandle().serviceId),
        static_cast<unsigned>(handles[0].GetServiceHandle().instanceId));
      wireTrajectory(handles[0]);
    } catch (const std::exception & e) {
      RCLCPP_ERROR(logger(), "[TRAJECTORY] find 핸들러 예외 — 계속: %s", e.what());
    } catch (...) {
      RCLCPP_ERROR(logger(), "[TRAJECTORY] find 핸들러 미상 예외 — 계속");
    }
  }

  // 배선 본체 — find 핸들러(SDK 스레드)와 maintain(executor 스레드) 양쪽에서 부른다.
  // 각자 새 proxy 를 만들므로 같은 인스턴스 동시 호출이 아니고, 승자는 proxy_mutex_ 의
  // exchange 가 정한다(진 쪽은 churn-unwire 경로로 내려간다).
  // 생성물 HandleType 은 복사/조회가 비-const — 비-const 참조로 받는다.
  void wireTrajectory(TrajectoryProxy::HandleType & handle)
  {
    auto proxy = std::make_shared<TrajectoryProxy>(handle);

    // SWS_CM_00181 — SetReceiveHandler 는 Result 를 돌려주며 실패는
    // kSetHandlerNotSet 이다. 등록 실패는 출력 채널이 통째로 죽는다는 뜻이므로
    // ERROR 로 남기고 재시도한다.
    bool registered = false;
    for (int attempt = 1; attempt <= 3; ++attempt) {
      // 통지가 온 "그 proxy" 에만 GetNewSamples 한다(weak 캡처). 현재-proxy 를
      // 읽는 방식은 스왑 직후 구 핸들러가 새 인스턴스를 만져 같은 인스턴스
      // GetNewSamples 동시 호출(SWS_CM_00714 UB)이 될 수 있다.
      // 같은 proxy 안의 핸들러 중복은 SWS_CM_00182 의 핸들러 단위 직렬화가 막는다.
      std::weak_ptr<TrajectoryProxy> weak_proxy = proxy;
      const auto reg = proxy->planning_scenarioplanning_trajectory.SetReceiveHandler(
        [this, weak_proxy] {onTrajectoryEvent(weak_proxy);});
      if (reg.HasValue()) {
        registered = true;
        break;
      }
      RCLCPP_ERROR(
        logger(), "[TRAJECTORY] SetReceiveHandler 실패 (%d/3): %s",
        attempt, toString(reg.Error()).c_str());
    }
    if (!registered) {
      // find 통지는 서비스 "상태 변화" 때만 다시 온다 — 제공자가 계속 떠 있으면
      // 여기서 끝나는 순간 출력 채널이 영구 고착이다.
      // maintain 틱이 같은 handle 로 재시도하도록 예약한다.
      scheduleTrajectoryRewire(handle);
      // 미설치 신규 proxy 의 파괴도 소각 스레드로 — maintain 경유면 이 함수가
      // executor 에서 돌고 있다.
      reaper_.enqueue(std::move(proxy));
      return;
    }

    // Subscribe 직후 도착하는 첫 이벤트부터 새 proxy 를 보도록 먼저 스왑한다.
    // 임계구역에서는 교환만 — 구 proxy 의 파괴/해제(ara 호출)를 락 안에서 하면
    // 락 밖 송신 원칙 위반이자 SDK 내부 락과의 ABBA 소지다.
    std::shared_ptr<TrajectoryProxy> replaced;
    {
      std::lock_guard<std::mutex> lock(proxy_mutex_);
      replaced = std::exchange(trajectory_proxy_, proxy);
    }
    if (replaced) {
      // 재발견 churn — 구 proxy 구독을 내려 이중 구독을 차단한다. 남겨두면
      // 두 수신 핸들러가 같은(새) 인스턴스에 GetNewSamples 를 동시 호출할 수
      // 있다(SWS_CM_00714 UB). 서로 다른 멤버 함수의 동시 호출은
      // SWS_CM_00009 가 허용하므로 진행 중 핸들러와의 겹침은 안전 범위다.
      unwireTrajectory(*replaced);
      reaper_.enqueue(std::move(replaced));  // 파괴는 소각 스레드에서
    }

    const auto subscribe = proxy->planning_scenarioplanning_trajectory.Subscribe(1);
    if (subscribe.HasValue()) {
      RCLCPP_INFO(logger(), "[TRAJECTORY] Subscribe(1) — 수신 대기");
      trajectory_rewire_needed_ = false;  // 완전 배선 — 남은 재시도 예약 소거
    } else {
      // 미구독 proxy 가 current 로 남는다 — 통지 없이는 영구 침묵이므로
      // maintain 틱으로 재배선을 예약한다.
      RCLCPP_ERROR(
        logger(), "[TRAJECTORY] Subscribe 실패: %s — 재배선을 예약한다",
        toString(subscribe.Error()).c_str());
      scheduleTrajectoryRewire(handle);
    }
  }

  void scheduleTrajectoryRewire(TrajectoryProxy::HandleType & handle)
  {
    {
      std::lock_guard<std::mutex> lock(rewire_mutex_);
      trajectory_rewire_handle_.emplace(handle);  // HandleType(HandleType&) — 비-const 원본 필요
    }
    trajectory_rewire_needed_ = true;
  }

  // 자가 회복 틱 — 노드의 유지보수 타이머(executor 스레드, 5s)가 부른다.
  // 게이트를 지나므로 stop() 이후에는 진입하지 않는다.
  void maintain() override
  {
    const bool retry_trajectory = trajectory_rewire_needed_.exchange(false);
    // 실제 배선은 소각 스레드에서 한다 — proxy
    // 생성자에 시한부 대기(최대 ~20s)가 내장돼 있어 executor 에서 돌리면
    // 그 시간 ROS 콜백 전체가 선다. 작업 첫 단계의 게이트 진입(공유 게이트)이
    // 종료 국면 실행을 차단한다.
    if (retry_trajectory) {
      RCLCPP_WARN(logger(), "[TRAJECTORY] 배선 실패분 재시도 예약(배경 스레드)");
      reaper_.enqueueJob(
      [this, gate = gate_]() {
        HandlerGate::Entry entry{*gate};
        if (!entry.entered()) {
          return;  // 종료 중 — 재배선은 의미 없다
        }
        std::optional<TrajectoryProxy::HandleType> handle;
        {
          std::lock_guard<std::mutex> lock(rewire_mutex_);
          if (trajectory_rewire_handle_) {
            handle.emplace(*trajectory_rewire_handle_);  // 비-const 원본에서 복사
          }
        }
        if (!handle) {
          return;
        }
        // 실패하면 wireTrajectory 가 다시 예약한다 — 5s 틱이 자연 백오프다.
        wireTrajectory(*handle);
      });
    }
    if (state_rewire_needed_.exchange(false)) {
      RCLCPP_WARN(logger(), "[ROUTE_STATE] 배선 실패분 재시도 예약(배경 스레드)");
      reaper_.enqueueJob([this, gate = gate_]() {
        HandlerGate::Entry entry{*gate};
        if (!entry.entered()) return;
        std::optional<RouteStateProxy::HandleType> handle;
        {
          std::lock_guard<std::mutex> lock(rewire_mutex_);
          if (state_rewire_handle_) handle.emplace(*state_rewire_handle_);
        }
        if (handle) wireRouteState(*handle);
      });
    }
  }

  // 수신 핸들러 — AP 런타임 스레드. 하는 일은 타입 변환 1회 + publish 1회로
  // 제한한다. SamplePtr 은 콜백 스코프 안에서만 쓴다 — R20-11 에는 반납
  // 강제 조항이 없고(R25-11 의 SWS_CM_00087 이 신설) 방어적 유지.
  void onTrajectoryEvent(const std::weak_ptr<TrajectoryProxy> & weak_proxy)
  {
    HandlerGate::Entry entry{*gate_};
    if (!entry.entered()) {
      return;
    }
    auto proxy = weak_proxy.lock();  // 통지가 온 그 인스턴스 — 스왑돼 파괴 중이면 중단
    if (!proxy) {
      return;
    }
    // 변환·publish 예외가 SDK 스레드로 새면 terminate — 이 1건만
    // 버리고 계속한다. 게이트 leave 는 Entry 소멸자(RAII)가 보장한다.
    try {
      onTrajectoryEventBody(*proxy);
    } catch (const std::exception & e) {
      RCLCPP_ERROR(logger(), "[TRAJECTORY] 수신 핸들러 예외 — 해당 건 폐기 후 계속: %s", e.what());
    } catch (...) {
      RCLCPP_ERROR(logger(), "[TRAJECTORY] 수신 핸들러 미상 예외 — 해당 건 폐기 후 계속");
    }
    // 스왑 직후라면 이 스냅샷이 마지막 참조일 수 있다 — 파괴(vsomeip stop+join)
    // 를 SDK 수신 스레드에서 하지 않도록 소각 큐로 넘긴다. enqueue 의
    // push_back 이 던지는 극단(bad_alloc)도 SDK 스레드로 새면 안 된다.
    try {
      reaper_.enqueue(std::move(proxy));
    } catch (...) {
      RCLCPP_ERROR(logger(), "[TRAJECTORY] 소각 큐잉 실패 — 이 스레드에서 해제");
    }
  }

  void onTrajectoryEventBody(TrajectoryProxy & proxy)
  {
    ++trajectory_events_;
    const auto taken = proxy.planning_scenarioplanning_trajectory.GetNewSamples(
      [this](auto samplePtr) {
        publishTrajectory(*samplePtr.Get());
      });
    if (taken.HasValue()) {
      if (trajectory_rx_error_.exchange(false)) {
        RCLCPP_INFO(logger(), "[TRAJECTORY] GetNewSamples 회복");
      }
    } else if (!trajectory_rx_error_.exchange(true)) {
      // 송신 쪽과 같은 원칙 — 상태 전이만 기록한다.
      RCLCPP_ERROR(
        logger(), "[TRAJECTORY] GetNewSamples 실패: %s",
        toString(taken.Error()).c_str());
    }
  }

  void publishTrajectory(const oss::type::autoware_planning_msgs::msg::Trajectory & sample)
  {
    if (!trajectory_handler_) {
      return;
    }
    const auto msg = toRosTrajectory(sample);
    // Reject malformed samples at the last boundary before ROS publication.
    // The sender log alone does not identify whether serialization, transport,
    // deserialization, or the receiving buffer caused a damaged sample.
    bool valid = msg.points.size() >= 2;
    const autoware_planning_msgs::msg::TrajectoryPoint * previous = nullptr;
    for (const auto & point : msg.points) {
      valid = valid && std::isfinite(point.pose.position.x) &&
        std::isfinite(point.pose.position.y) &&
        std::isfinite(point.pose.position.z) &&
        std::isfinite(point.pose.orientation.x) &&
        std::isfinite(point.pose.orientation.y) &&
        std::isfinite(point.pose.orientation.z) &&
        std::isfinite(point.pose.orientation.w) &&
        std::isfinite(point.longitudinal_velocity_mps) &&
        std::isfinite(point.lateral_velocity_mps) &&
        std::isfinite(point.acceleration_mps2) &&
        std::isfinite(point.heading_rate_rps) &&
        std::isfinite(point.front_wheel_angle_rad) &&
        std::isfinite(point.rear_wheel_angle_rad);
      const auto & q = point.pose.orientation;
      const double norm = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
      valid = valid && norm > 0.25 && norm < 2.25;
      if (valid && previous) {
        const auto dx = point.pose.position.x - previous->pose.position.x;
        const auto dy = point.pose.position.y - previous->pose.position.y;
        valid = dx * dx + dy * dy < 25.0;
      }
      if (!valid) break;
      previous = &point;
    }
    if (!valid) {
      RCLCPP_ERROR(logger(),
                   "[TRAJECTORY] malformed sample dropped: ap_points=%zu ros_points=%zu",
                   sample.points.size(), msg.points.size());
      return;
    }
    if (config_.traceValues()) {
      // 건수만으로는 변환이 맞았는지 알 수 없다. 배열이므로 크기와 첫 점
      // 좌표를 남겨 송신측(Exe_planningvalidator) 로그와 대조한다.
      if (msg.points.empty()) {
        RCLCPP_INFO(
          logger(), "[TRAJECTORY] 수신 값 points=0 sec=%d nanosec=%u",
          static_cast<int>(msg.header.stamp.sec),
          static_cast<unsigned>(msg.header.stamp.nanosec));
      } else {
        const auto & p0 = msg.points.front();
        RCLCPP_INFO(
          logger(),
          "[TRAJECTORY] 수신 값 points=%zu first=(%.3f, %.3f, %.3f) v=%.3f "
          "sec=%d nanosec=%u",
          msg.points.size(), p0.pose.position.x, p0.pose.position.y,
          p0.pose.position.z, static_cast<double>(p0.longitudinal_velocity_mps),
          static_cast<int>(msg.header.stamp.sec),
          static_cast<unsigned>(msg.header.stamp.nanosec));
      }
    }
    trajectory_handler_(msg);
    ++trajectory_samples_;
  }

  // proxy 핸들 접근. 수신·find·stop 이 서로 다른 스레드에서 만지므로
  // 스왑은 뮤텍스로, 사용은 지역 shared_ptr 사본으로 한다.
  std::shared_ptr<TrajectoryProxy> currentTrajectoryProxy()
  {
    std::lock_guard<std::mutex> lock(proxy_mutex_);
    return trajectory_proxy_;
  }

  std::shared_ptr<TrajectoryProxy> takeTrajectoryProxy()
  {
    std::lock_guard<std::mutex> lock(proxy_mutex_);
    return std::move(trajectory_proxy_);
  }

  // 수신 배선 해제. 진행 중 핸들러와 겹칠 수 있으나 안전 근거는 SWS_CM_00009 —
  // 같은 인스턴스라도 "서로 다른" 멤버 함수의 동시 호출은 허용된다. 금지되는
  // 것은 같은 함수의 같은 인스턴스 동시 호출(00023/00029)이며 그 경로는 없다.
  void unwireTrajectory(TrajectoryProxy & proxy)
  {
    proxy.planning_scenarioplanning_trajectory.Unsubscribe();
    const auto unset = proxy.planning_scenarioplanning_trajectory.UnsetReceiveHandler();
    if (!unset.HasValue()) {
      RCLCPP_WARN(
        logger(), "[TRAJECTORY] UnsetReceiveHandler 실패: %s",
        toString(unset.Error()).c_str());
    }
  }

  // ── Method 채널 (Service 1817 → ROS 서비스 3종) ────────────────────

  // 출력 채널과 같은 이유 — 제공자(Exe_missionplanner)가 늦게 떠도 연결되도록
  // StartFindService 를 쓴다. Method 는 구독이 없어 proxy 스왑만 하면 된다.
  void startMethodFind()
  {
    const ara::core::InstanceSpecifier spec{"Exe_ServiceCreator/AA/RPort_MP2SCr"};
    const auto find = MethodProxy::StartFindService(
      [this, gate = gate_](
        ara::com::ServiceHandleContainer<MethodProxy::HandleType> handles,
        ara::com::FindServiceHandle) {
        HandlerGate::Entry entry{*gate};  // 위 trajectory find 와 동일
        if (!entry.entered()) {
          return;
        }
        onMethodFound(std::move(handles));
      },
      spec);
    if (find.HasValue()) {
      method_find_handle_ = find.Value();
      method_find_started_ = true;
      RCLCPP_INFO(logger(), "[METHOD] StartFindService — Service 1817 을 기다린다");
    } else {
      RCLCPP_ERROR(
        logger(), "[METHOD] StartFindService 실패: %s — Method 채널 없이 동작",
        toString(find.Error()).c_str());
    }
  }

  void onMethodFound(ara::com::ServiceHandleContainer<MethodProxy::HandleType> handles)
  {
    // 게이트 진입은 find 람다가 이미 마쳤다.
    // 예외가 벤더 SDK 스레드로 새면 terminate — 전부 막는다.
    // method 는 배선 단계가 스왑뿐이라 재시도 예약은 필요 없다(다음 통지면 충분).
    try {
      onMethodFoundBody(handles);
    } catch (const std::exception & e) {
      RCLCPP_ERROR(logger(), "[METHOD] find 핸들러 예외 — 계속: %s", e.what());
    } catch (...) {
      RCLCPP_ERROR(logger(), "[METHOD] find 핸들러 미상 예외 — 계속");
    }
  }

  // 생성물 HandleType 은 복사/조회가 비-const 라 컨테이너를 비-const 참조로 받는다.
  void onMethodFoundBody(ara::com::ServiceHandleContainer<MethodProxy::HandleType> & handles)
  {
    if (handles.empty()) {
      RCLCPP_INFO(logger(), "[METHOD] 제공자 없음 — (재)등장을 기다린다");
      return;
    }
    RCLCPP_INFO(
      logger(), "[METHOD] 제공자 발견 — service %u instance %u",
      static_cast<unsigned>(handles[0].GetServiceHandle().serviceId),
      static_cast<unsigned>(handles[0].GetServiceHandle().instanceId));
    auto proxy = std::make_shared<MethodProxy>(handles[0]);
    std::shared_ptr<MethodProxy> replaced;
    {
      std::lock_guard<std::mutex> lock(proxy_mutex_);
      replaced = std::exchange(method_proxy_, proxy);
    }
    reaper_.enqueue(std::move(replaced));  // 락 밖 + 소각 스레드에서 파괴
  }

  std::shared_ptr<MethodProxy> currentMethodProxy()
  {
    std::lock_guard<std::mutex> lock(proxy_mutex_);
    return method_proxy_;
  }

  std::shared_ptr<MethodProxy> takeMethodProxy()
  {
    std::lock_guard<std::mutex> lock(proxy_mutex_);
    return std::move(method_proxy_);
  }

  // 공통 호출 경로. ROS executor 스레드에서 돈다.
  //  - wait_for 타임아웃 필수. 초과하면 Future 를 버린다 — 파괴가 규격이
  //    정한 취소다(SWS_CM_00194). 응답 객체를 Future 에 캡처로 넘기지 않으므로
  //    늦은 결과가 이미 답한 response 를 건드릴 경로가 없다.
  //  - 취소는 서버로 전파되지 않아 연속 타임아웃이 서버에 쌓인다 — 백오프.
  template<typename ResponseT, typename InvokeF>
  bool callMethod(const char * name, ResponseT & response, InvokeF && invoke)
  {
    auto proxy = currentMethodProxy();
    if (!proxy) {
      RCLCPP_WARN_THROTTLE(
        logger(), steadyClock(), 5000,
        "[METHOD %s] 제공자 미발견(Service 1817) — 실패로 응답", name);
      return false;
    }
    // 어느 return 경로로 나가든 스냅샷 해제가 executor 의 마지막 참조가 되지
    // 않게 한다. future 는 이 가드보다 늦게 선언되어 먼저 파괴된다 —
    // proxy 가 살아 있는 동안 future 정리가 끝나는 순서다.
    struct SnapshotToReaper
    {
      ProxyReaper & reaper;
      std::shared_ptr<MethodProxy> & snapshot;
      ~SnapshotToReaper() {reaper.enqueue(std::move(snapshot));}
    } snapshot_to_reaper{reaper_, proxy};

    // 백오프는 제공자(Service 1817) 단위 공유 — 세 메소드가 같은 서버를
    // 본다. 메소드별 슬롯이면 무응답 시 탐침 500ms 가 ×3 직렬화되어 executor
    // 를 최대 1.5s 막고, 그동안 서비스 수신 큐가 넘쳐 요청이 드랍됐다
    // (3종 동시 10Hz 에서 40% 미응답).
    auto & backoff = method_backoff_;
    const auto now = std::chrono::steady_clock::now();
    const int consecutive_seen = backoff.consecutive.load();
    if (consecutive_seen > 0) {
      const auto hold = methodBackoffHold(consecutive_seen, config_.methodTimeout());
      const auto last = std::chrono::steady_clock::time_point{
        std::chrono::steady_clock::duration{backoff.last_ns.load()}};
      if (now - last < hold) {
        RCLCPP_WARN(
          logger(), "[METHOD %s] 백오프 중(연속 실패 %d) — 호출 생략", name,
          consecutive_seen);
        return false;
      }
    }

    auto future = invoke(*proxy);
    if (future.wait_for(config_.methodTimeout()) != ara::core::future_status::ready) {
      backoff.consecutive.fetch_add(1);
      backoff.last_ns = std::chrono::steady_clock::now().time_since_epoch().count();
      RCLCPP_ERROR(
        logger(), "[METHOD %s] %lld ms 안에 응답 없음 — Future 파괴로 취소(SWS_CM_00194)",
        name, static_cast<long long>(config_.methodTimeout().count()));
      return false;
    }

    auto result = future.GetResult();
    if (!result.HasValue()) {
      backoff.consecutive.fetch_add(1);
      backoff.last_ns = std::chrono::steady_clock::now().time_since_epoch().count();
      RCLCPP_ERROR(
        logger(), "[METHOD %s] 호출 실패: %s", name, toString(result.Error()).c_str());
      return false;
    }

    backoff.consecutive.store(0);
    toRosStatus(result.Value().res.status, response.status);
    if (config_.traceValues()) {
      // Method 도 값을 대조한다. 응답 status 가 그 값이다.
      RCLCPP_INFO(
        logger(), "[METHOD %s] 응답 success=%d code=%u message='%s'", name,
        static_cast<int>(response.status.success),
        static_cast<unsigned>(response.status.code),
        safeText(response.status.message.data(), response.status.message.size()).c_str());
    }
    return true;
  }

  // 페이로드 상한 — 무상한 blob 이 단일 송신 워커를 수십 초 점유해
  // 전 채널을 정지시키는 것을 막는다. 상한은 SERVICECREATOR_MAX_PAYLOAD_BYTES.
  bool exceedsPayloadCap(std::size_t bytes, ChannelKey key)
  {
    const auto cap = config_.maxPayloadBytes();
    if (!payloadExceedsCap(bytes, cap)) {
      return false;
    }
    RCLCPP_ERROR_THROTTLE(
      logger(), steadyClock(), 5000,
      "[%s] 페이로드 %zu B > 상한 %zu B — 폐기 (SERVICECREATOR_MAX_PAYLOAD_BYTES)",
      toEnvSuffix(key), bytes, cap);
    return true;
  }

  // 사용자 유래 문자열의 로그 살균 — 길이 상한 + 비인쇄문자 치환.
  static std::string safeText(const char * data, std::size_t size)
  {
    std::string out(data, std::min<std::size_t>(size, 48));
    for (auto & c : out) {
      if (static_cast<unsigned char>(c) < 0x20U) {c = '?';}
    }
    if (size > 48) {out += "...";}
    return out;
  }

  // RCLCPP_*_THROTTLE 용 steady clock — 정적 1개면 된다.
  static rclcpp::Clock & steadyClock()
  {
    static rclcpp::Clock clock{RCL_STEADY_TIME};
    return clock;
  }

  // 스켈레톤 하나의 이벤트 11개를 채널별 sender 목록에 등록한다.
  // offerAll() 이 MP 하나에 대해 부르므로 목록마다 sender 하나가 쌓인다.
  // 스켈레톤은 unique_ptr 멤버라 주소가 안정적이고, stop() 이 목록을
  // 스켈레톤 파괴보다 먼저 비우므로 참조가 남지 않는다.
  template<typename SkeletonT>
  void registerSenders(SkeletonT & sk)
  {
#define SERVICECREATOR_REGISTER(KEY, EVENT, SAMPLE) \
    senders_##KEY##_.emplace_back( \
      [&sk](const SAMPLE & s) {return sk.EVENT.Send(s);});
    SERVICECREATOR_FOR_EACH_INPUT_EVENT(SERVICECREATOR_REGISTER)
#undef SERVICECREATOR_REGISTER
  }

  // SCr2MP로 전송한다. 반환값은 sticky 재전송을 결정한다.
  template<typename SampleT>
  SendResult fanOut(
    ChannelKey key,
    const std::vector<std::function<ara::core::Result<void>(const SampleT &)>> & senders,
    const SampleT & sample)
  {
    // start() 실패(또는 offer 전멸) 시의 조용한 성공 금지: 최초 1회
    // ERROR + 이후 채널별 throttled WARN. Error 를 돌려줘 sticky 도 무장하지
    // 않는다(재전송으로 해결될 상태가 아니다).
    if (!initialized_ || senders.empty()) {
      if (!uninit_send_logged_.exchange(true)) {
        RCLCPP_ERROR(
          logger(), "AP 채널 미가동 상태의 send — 이후 반복은 채널별 WARN(5초 스로틀)");
      }
      RCLCPP_WARN_THROTTLE(
        logger(), steadyClock(), 5000, "[%s] AP 미가동 — 송신 무시", toEnvSuffix(key));
      return SendResult::Error;
    }

    auto & stats = stats_[static_cast<std::size_t>(key)];
    ++stats.attempted;

    auto worst = SendState::Ok;
    ara::core::Optional<ara::core::ErrorCode> first_error;
    for (const auto & sender : senders) {
      const auto sent = sender(sample);
      if (sent.HasValue()) {
        continue;
      }
      const auto state = isNoSubscriber(sent.Error())
        ? SendState::NoSubscriber : SendState::Error;
      if (worstOf(worst, state) != worst) {
        worst = state;
        first_error = sent.Error();  // 최악 상태의 에러를 대표로
      } else if (!first_error) {
        first_error = sent.Error();
      }
    }

    switch (worst) {
      case SendState::Ok:           ++stats.sent; break;
      case SendState::NoSubscriber: ++stats.no_subscriber; break;
      case SendState::Error:        ++stats.failed; break;
    }
    reportTransition(key, stats, worst, first_error);

    return toSendResult(worst);
  }

  // 상태 전이만 기록한다. 매 호출을 찍지 않는다.
  void reportTransition(
    ChannelKey key, ChannelStats & stats, SendState now,
    const ara::core::Optional<ara::core::ErrorCode> & error) const
  {
    if (stats.state_known && stats.last_state == now) {
      return;
    }
    const bool first = !stats.state_known;
    stats.state_known = true;
    const auto previous = stats.last_state;
    stats.last_state = now;

    switch (now) {
      case SendState::NoSubscriber:
        RCLCPP_WARN(
          logger(), "[%s] 구독자 없음 — 송신 보류 (%s)", toEnvSuffix(key),
          error ? toString(*error).c_str() : "?");
        break;
      case SendState::Error:
        RCLCPP_ERROR(
          logger(), "[%s] 송신 실패 (%s)", toEnvSuffix(key),
          error ? toString(*error).c_str() : "?");
        break;
      case SendState::Ok:
        if (first) {
          RCLCPP_INFO(logger(), "[%s] 송신 시작", toEnvSuffix(key));
        } else {
          RCLCPP_INFO(
            logger(), "[%s] 송신 회복 — 보류 중 버린 %lu 건", toEnvSuffix(key),
            static_cast<unsigned long>(
              previous == SendState::NoSubscriber ? stats.no_subscriber : stats.failed));
        }
        break;
    }
  }

  // AP 측 [2] 통계 — 누락과 임의 전송을 판정하는 근거.
  void logStats() const
  {
    for (std::size_t i = 0; i < kChannelCount; ++i) {
      const auto & s = stats_[i];
      if (s.attempted == 0) {
        continue;
      }
      RCLCPP_INFO(
        logger(), "[%s] 시도 %lu / 성공 %lu / 구독자없음 %lu / 실패 %lu",
        toEnvSuffix(static_cast<ChannelKey>(i)),
        static_cast<unsigned long>(s.attempted),
        static_cast<unsigned long>(s.sent),
        static_cast<unsigned long>(s.no_subscriber),
        static_cast<unsigned long>(s.failed));
    }
    const auto events = trajectory_events_.load();
    const auto samples = trajectory_samples_.load();
    if (events > 0 || samples > 0) {
      RCLCPP_INFO(
        logger(), "[TRAJECTORY] 수신 통지 %lu / 샘플(발행) %lu",
        static_cast<unsigned long>(events), static_cast<unsigned long>(samples));
    }
  }

#define SERVICECREATOR_DECLARE(TAG) \
  std::unique_ptr<SERVICECREATOR_SKELETON_TYPE(TAG)> sk_##TAG##_;
  SERVICECREATOR_FOR_EACH_PPORT(SERVICECREATOR_DECLARE)
#undef SERVICECREATOR_DECLARE

#define SERVICECREATOR_DECLARE_SENDERS(KEY, EVENT, SAMPLE) \
  std::vector<std::function<ara::core::Result<void>(const SAMPLE &)>> senders_##KEY##_;
  SERVICECREATOR_FOR_EACH_INPUT_EVENT(SERVICECREATOR_DECLARE_SENDERS)
#undef SERVICECREATOR_DECLARE_SENDERS

  // 출력 채널(12506). proxy 는 find 핸들러(AP 스레드)가 만들어 스왑한다.
  TrajectoryHandler trajectory_handler_;
  TrajectoryHandler scenario_handler_;
  TurnHandler turn_handler_;
  HazardHandler hazard_handler_;
  MetricsHandler metrics_handler_;
  RemainingHandler remaining_handler_;
  RouteStateHandler route_state_handler_;
  RouteHandler route_handler_;
  bool find_stop_done_{false};  // stop() 재진입 시 이중 StopFindService 차단
  bool trajectory_find_started_{false};  // 미발견-종료 시 no-op 재등록 대상 판별
  bool scenario_find_started_{false};
  bool turn_find_started_{false};
  bool hazard_find_started_{false};
  bool metrics_find_started_{false};
  bool remaining_find_started_{false};
  bool method_find_started_{false};
  bool state_find_started_{false};
  ProxyReaper reaper_;  // proxy 들보다 먼저 선언 — 멤버 파괴 순서상 마지막에 소멸
  std::shared_ptr<TrajectoryProxy> trajectory_proxy_;
  std::shared_ptr<ScenarioProxy> scenario_proxy_;
  std::shared_ptr<TurnProxy> turn_proxy_;
  std::shared_ptr<HazardProxy> hazard_proxy_;
  std::shared_ptr<MetricsProxy> metrics_proxy_;
  std::shared_ptr<RemainingProxy> remaining_proxy_;
  std::shared_ptr<RouteStateProxy> state_proxy_;
  std::mutex proxy_mutex_;
  // 배선 실패 자가 회복 — handle 은 rewire_mutex_ 아래,
  // 플래그는 원자. maintain 틱과 find 핸들러 어느 쪽이 이겨도 무방하다.
  std::atomic<bool> trajectory_rewire_needed_{false};
  std::mutex rewire_mutex_;
  std::optional<TrajectoryProxy::HandleType> trajectory_rewire_handle_;
  std::atomic<bool> state_rewire_needed_{false};
  std::optional<RouteStateProxy::HandleType> state_rewire_handle_;
  ara::com::FindServiceHandle trajectory_find_handle_;
  ara::com::FindServiceHandle scenario_find_handle_;
  ara::com::FindServiceHandle turn_find_handle_;
  ara::com::FindServiceHandle hazard_find_handle_;
  ara::com::FindServiceHandle metrics_find_handle_;
  ara::com::FindServiceHandle remaining_find_handle_;
  ara::com::FindServiceHandle state_find_handle_;

  // Method 채널(1817). proxy_mutex_ 를 공유한다 — 스왑 빈도가 낮아 분리할
  // 이유가 없고, 두 뮤텍스의 중첩 획득이 애초에 없다.
  std::shared_ptr<MethodProxy> method_proxy_;
  ara::com::FindServiceHandle method_find_handle_;
  // 원자 멤버(위 MethodBackoff 주석) — executor 전제와 무관하게 레이스 없음.
  // 제공자 단위 1개 공유(위 callMethod 주석).
  MethodBackoff method_backoff_{};
  // shared_ptr 소유 — find 람다가 사본을 쥔다. Deinitialize 는 SD 스레드를 세우지 않고
  // find 등록의 정적 해제 API 도 없어, 채널 파괴 뒤에도 SD 통지가 람다를 부를 수 있다.
  // 람다는 게이트에 먼저 진입하고 성공했을 때만 this 를 만진다 — 게이트는 람다 사본이
  // 붙들어 살아 있고 stopping 이라 진입이 거부되므로 this 접촉이 없다.
  std::shared_ptr<HandlerGate> gate_ = std::make_shared<HandlerGate>();
  std::atomic<std::uint64_t> trajectory_events_{0};
  std::atomic<std::uint64_t> trajectory_samples_{0};
  std::atomic<bool> trajectory_rx_error_{false};
  std::atomic<bool> uninit_send_logged_{false};

  BridgeConfig config_{};
  mutable std::array<ChannelStats, kChannelCount> stats_{};
  bool initialized_{false};
};

}  // namespace

std::unique_ptr<ApChannel> makeApChannel()
{
  return std::make_unique<ParaApChannel>();
}

}  // namespace servicecreator
