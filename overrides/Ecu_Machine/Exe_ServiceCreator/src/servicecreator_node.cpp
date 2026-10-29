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
#include "servicecreator/servicecreator_node.hpp"

#include <cstdlib>
#include <utility>

namespace servicecreator
{

ServiceCreator::ServiceCreator(const rclcpp::NodeOptions & options)
: ServiceCreator(nullptr, options)
{
}

ServiceCreator::ServiceCreator(
  std::unique_ptr<ApChannel> channel, const rclcpp::NodeOptions & options)
: rclcpp::Node("servicecreator", options),
  config_(BridgeConfig::fromEnv()),
  ap_(channel ? std::move(channel) : makeApChannel())
{
  setupInputEvents();
  setupInputMethods();
  setupOutputEvents();

  const bool ap_started = ap_->start();
  RCLCPP_INFO(
    get_logger(),
    "Service Creator 기동 — 입력 Events 11 / 입력 Methods 3 / 출력 Events 1, AP 채널: %s (%s)",
    ap_->name(), ap_started ? "연결됨" : "미연결");
  maintenance_timer_ = create_wall_timer(
    std::chrono::seconds(5),
    [this]() {
      try {
        ap_->maintain();
      } catch (const std::exception & e) {
        RCLCPP_ERROR(get_logger(), "유지보수 틱 예외 — 다음 틱에 계속: %s", e.what());
      } catch (...) {
        RCLCPP_ERROR(get_logger(), "유지보수 틱 미상 예외 — 다음 틱에 계속");
      }
    });

  // 스레드 기동은 마지막 — 이후 어떤 문장이 던져도 joinable 스레드를 든 채
  // 소멸(terminate)하는 경로가 없다. 루프 자체가 k번째
  // 생성에서 던지는 경우(자원 고갈)도 기기동분을 join 하고 재던진다 — 그냥
  // 탈출하면 joinable 파괴로 terminate 라 main 의 catch 에 못 간다.
  try {
    for (auto & worker : workers_) {
      auto * w = worker.get();
      w->thread = std::thread([this, w] {sendWorkerLoop(*w);});
    }
  } catch (...) {
    stopSendWorkers();  // 기기동분만 stop+join (미기동은 joinable=false 로 스킵)
    throw;
  }
}

ServiceCreator::~ServiceCreator()
{
  stopSendWorkers();
  logChannelSummary();
  // noexcept 소멸자에서 채널 stop 이 던지면 terminate — 기반 계약(ApChannel)에
  // noexcept 명시가 없으므로 여기서 막는다. 종료 국면이라
  // 실패는 로그만 남기고 진행한다.
  try {
    if (ap_) {
      ap_->stop();
    }
  } catch (const std::exception & e) {
    RCLCPP_ERROR(get_logger(), "AP 채널 stop 예외 — 종료 계속: %s", e.what());
  } catch (...) {
    RCLCPP_ERROR(get_logger(), "AP 채널 stop 미상 예외 — 종료 계속");
  }
}

// ── 입력 채널 배선 ──────────────────────────────────────────────

template<typename MsgT>
void ServiceCreator::setupInputChannel(
  InputChannel<MsgT> & ch, ChannelKey key,
  const std::string & topic, const rclcpp::QoS & qos, std::uint32_t event_id)
{
  const auto & cfg = config_[key];
  const bool poll = cfg.mode == ChannelConfig::Mode::Poll;

  ch.sub = create_subscription<MsgT>(
    topic, qos,
    [this, &ch, key, topic, event_id, poll](const typename MsgT::SharedPtr msg) {
      // 콜백 예외(대형 메시지 복사 bad_alloc 등)가 spin() 을 뚫으면 프로세스가
      // 내려간다 — 그 1건만 버리고 계속한다.
      try {
        ++ch.received;
        RCLCPP_DEBUG(get_logger(), "입력 Event %u <- %s", event_id, topic.c_str());
        if (poll) {
          // 저장만 한다 — 소비는 타이머가 한다. 이전 값은 덮인다(latest).
          std::lock_guard<std::mutex> lock(ch.mutex);
          ch.slot = *msg;
        } else {
          enqueueSend(ch, *msg);
        }
      } catch (const std::exception & e) {
        RCLCPP_ERROR(
          get_logger(), "[%s] 수신 콜백 예외 — 해당 건 폐기 후 계속: %s",
          toEnvSuffix(key), e.what());
      } catch (...) {
        RCLCPP_ERROR(
          get_logger(), "[%s] 수신 콜백 미상 예외 — 해당 건 폐기 후 계속", toEnvSuffix(key));
      }
    });
  {
    auto worker = std::make_unique<SendWorker>();
    worker->name = toEnvSuffix(key);
    worker->drain = [this, &ch, key]() {return drainOne(ch, key);};
    ch.worker = worker.get();
    workers_.push_back(std::move(worker));
  }

  if (poll) {
    // 반드시 wall timer(steady clock). 노드 시계 + use_sim_time 이면
    // "/clock 을 보내야 시간이 가고, 시간이 가야 /clock 을 보내는" 순환이 된다.
    ch.poll_timer = create_wall_timer(
      cfg.period,
      [this, &ch, key]() {
        try {
          std::optional<MsgT> taken;
          {
            std::lock_guard<std::mutex> lock(ch.mutex);
            if (ch.slot) {
              taken = std::move(ch.slot);
              ch.slot.reset();  // dirty 내림 — 새 값이 없으면 다음 주기는 침묵
            }
          }
          if (taken) {
            enqueueSend(ch, *taken);
          }
        } catch (const std::exception & e) {
          RCLCPP_ERROR(
            get_logger(), "[%s] poll 틱 예외 — 다음 주기에 계속: %s",
            toEnvSuffix(key), e.what());
        } catch (...) {
          RCLCPP_ERROR(get_logger(), "[%s] poll 틱 미상 예외 — 다음 주기에 계속", toEnvSuffix(key));
        }
      });
  }

  if (cfg.policy == ChannelConfig::Policy::Sticky) {
    // 상시 구동 틱 — 잠재웠다가 워커 스레드에서 reset 으로 깨우는 방식은
    // Humble 에서 통하지 않는다: SingleThreadedExecutor 는 rcl_wait 진입
    // 시점의 타이머만 계산하고 타 스레드의 reset 은 guard condition 을 당기지
    // 않아, 한산한 국면에서 재시도가 무기한 지연된다.
    // 값이 없으면 틱은 락 한 번 잡고 즉시 반환 — 5s 주기 비용은 무시 가능.
    ch.retry_timer = create_wall_timer(
      config_.stickyRetry(),
      [this, &ch, key]() {
        try {
          onStickyRetry(ch, key);
        } catch (const std::exception & e) {
          RCLCPP_ERROR(
            get_logger(), "[%s] 재시도 틱 예외 — 다음 틱에 계속: %s",
            toEnvSuffix(key), e.what());
        } catch (...) {
          RCLCPP_ERROR(get_logger(), "[%s] 재시도 틱 미상 예외 — 다음 틱에 계속", toEnvSuffix(key));
        }
      });
  }
}

template<typename MsgT>
void ServiceCreator::enqueueSend(InputChannel<MsgT> & ch, const MsgT & msg)
{
  bool was_empty = false;
  {
    std::lock_guard<std::mutex> lock(ch.mutex);
    was_empty = !ch.pending.has_value();
    ch.pending = msg;  // 워커가 밀려 있으면 최신값으로 덮인다(latest)
    ch.sticky_given_up = false;  // 새 ROS 값 — sticky 포기 상태를 푼다(재시도 큐잉은 이 길로 안 온다)
    ch.sticky_delivered = false;
  }
  if (was_empty) {
    // CV 규칙 — 술어가 읽는 상태(pending)의 갱신은 워커가 술어를 평가할 때
    // 쥐는 뮤텍스 아래에서 해야 통지 유실 창이 없다.
    std::lock_guard<std::mutex> lock(ch.worker->mutex);
    ch.worker->pending.fetch_add(1);
  }
  ch.worker->cv.notify_one();
}

template<typename MsgT>
bool ServiceCreator::drainOne(InputChannel<MsgT> & ch, ChannelKey key)
{
  std::optional<MsgT> taken;
  {
    std::lock_guard<std::mutex> lock(ch.mutex);
    if (ch.pending) {
      taken = std::move(ch.pending);
      ch.pending.reset();
      // take 와 같은 임계구역에서 세운다 — 틈이 있으면 재시도 틱이 구식 sticky 값을
      // 재큐해 신값을 역전시킨다.
      ch.sending = true;
    }
  }
  if (!taken) {
    return false;
  }
  // sending 은 어떤 탈출 경로(예외 포함)로도 반드시 내린다 — 영구 true 면
  // 그 채널의 재시도가 무기한 스킵되는 무증상 정지다.
  struct SendingGuard
  {
    InputChannel<MsgT> & ch;
    ~SendingGuard()
    {
      std::lock_guard<std::mutex> lock(ch.mutex);
      ch.sending = false;
    }
  } sending_guard{ch};
  ch.worker->pending.fetch_sub(1);
  ++ch.dispatched;
  // 변환 + SCr2MP 전송 비용. 워커 스레드 점유
  // 시간이며, executor 는 이 동안 자유롭다.
  const auto t0 = std::chrono::steady_clock::now();
  // send 실패·예외는 에러 로그 후 계속 — 그 1건만 Error 로 접는다.
  // catch 폭은 (...) 까지 — 비-std 예외가 sweep 단위로 격상되면 "1건 폐기"
  // 의도가 깨진다.
  auto outcome = SendResult::Error;
  try {
    outcome = ap_->send(*taken);  // 락 밖
  } catch (const std::exception & e) {
    RCLCPP_ERROR(
      get_logger(), "[%s] send 예외 — 해당 건 폐기 후 계속: %s",
      toEnvSuffix(key), e.what());
  } catch (...) {
    RCLCPP_ERROR(
      get_logger(), "[%s] send 비-std 예외 — 해당 건 폐기 후 계속", toEnvSuffix(key));
  }
  const auto ns = static_cast<std::uint64_t>(
    std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - t0).count());
  ch.send_ns_total += ns;
  if (ns > ch.send_ns_max) {ch.send_ns_max = ns;}
  if (config_[key].policy == ChannelConfig::Policy::Sticky) {
    try {
      handleStickyOutcome(ch, key, *taken, outcome);
    } catch (const std::exception & e) {
      RCLCPP_ERROR(
        get_logger(), "[%s] sticky 처리 예외 — 이번 결과만 버리고 계속: %s",
        toEnvSuffix(key), e.what());
    } catch (...) {
      RCLCPP_ERROR(
        get_logger(), "[%s] sticky 처리 미상 예외 — 이번 결과만 버리고 계속", toEnvSuffix(key));
    }
  }
  return true;
}

void ServiceCreator::sendWorkerLoop(SendWorker & worker)
{
  // send 실패·예외는 에러 로그 후 계속 — 그 1건만 버리고 루프를 잇는다
  // (예외 후에는 다음 wait 로 수렴하므로 스핀 없음).
  for (;;) {
    {
      std::unique_lock<std::mutex> lock(worker.mutex);
      worker.cv.wait(
        lock, [&worker] {return worker.stop.load() || worker.pending.load() > 0;});
    }
    if (worker.stop.load()) {
      break;  // 종료 시 잔여분은 버린다 — 프로세스가 내려가는 길이다
    }
    try {
      // 자기 채널만 소진한다 — 다른 채널의 대형 send 와 서로 격리.
      while (!worker.stop.load() && worker.drain()) {
      }
    } catch (const std::exception & e) {
      RCLCPP_ERROR(
        get_logger(), "[%s] 송신 워커 예외 — 해당 건 폐기 후 계속: %s",
        worker.name, e.what());
    } catch (...) {
      RCLCPP_ERROR(
        get_logger(), "[%s] 송신 워커 미상 예외 — 해당 건 폐기 후 계속", worker.name);
    }
  }
  worker.done = true;
}

void ServiceCreator::stopSendWorkers()
{
  // 전 워커에 stop 을 먼저 뿌린다 — 대기 총량은 "가장 느린 send 1건" 수준으로
  // 수렴한다(순차 join 이지만 나머지는 그 사이 이미 끝난다).
  for (auto & worker : workers_) {
    if (!worker->thread.joinable()) {
      continue;
    }
    {
      std::lock_guard<std::mutex> lock(worker->mutex);  // 통지 유실 방지 — 위와 동일
      worker->stop = true;
    }
    worker->cv.notify_all();
  }
  // 진행 중인 ap_->send() 는 중단 수단이 없으므로 완료까지 기다린다(강제 진행은 UB).
  // 최종 상한은 EM 의 termination-timeout(SIGKILL)이며 이 프로세스의 자기 종료가 아니다.
  // 경과는 400ms 마다 반복해 남긴다.
  for (auto & worker : workers_) {
    if (!worker->thread.joinable()) {
      continue;
    }
    auto next_mark = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
    int waited_ms = 0;
    while (!worker->done.load()) {
      if (std::chrono::steady_clock::now() > next_mark) {
        waited_ms += 400;
        RCLCPP_ERROR(
          get_logger(),
          "[%s] 송신 워커 대기 %d ms 경과 — send 완료까지 계속 대기",
          worker->name, waited_ms);
        next_mark += std::chrono::milliseconds(400);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    worker->thread.join();
  }
}

template<typename MsgT>
void ServiceCreator::handleStickyOutcome(
  InputChannel<MsgT> & ch, ChannelKey key, const MsgT & msg, SendResult outcome)
{
  // 재시도 타이머는 상시 구동이다 — setup 의 주석 참조.
  // 여기서는 값·에피소드 상태만 조작한다 — 워커 스레드가 rcl 타이머를 만질
  // 일이 없으므로, 락 아래 rcl 호출과 EventsExecutor ABBA 우려도
  // 없다.
  if (outcome == SendResult::NoSubscriber) {
    // 값을 보관한다. 이미 에피소드가 열려 있으면 값만 갱신한다
    // (상한은 에피소드 시작 기준). 포기가 확정된 에피소드의 잔여 재큐가
    // 뒤늦게 NoSub 로 돌아와도 재개하지 않는다 — 포기는 새 ROS 값이 올
    // 때까지 최종이다.
    bool armed = false;
    bool dropped = false;
    {
      std::lock_guard<std::mutex> lock(ch.mutex);
      if (ch.sticky_given_up) {
        dropped = true;
      } else {
        armed = !ch.sticky_value.has_value() || ch.sticky_delivered;
        ch.sticky_delivered = false;
        ch.sticky_value = msg;
        if (armed) {
          ch.episode_start = std::chrono::steady_clock::now();
        }
      }
    }
    if (dropped) {
      RCLCPP_WARN(
        get_logger(), "[%s] 포기 확정 후의 잔여 재시도 — 보관하지 않는다", toEnvSuffix(key));
    } else if (armed) {
      RCLCPP_WARN(
        get_logger(), "[%s] 구독자 없음 — 값을 보관하고 재시도한다", toEnvSuffix(key));
    }
  } else if (outcome == SendResult::Ok) {
    // 지도는 일회성 ROS 토픽이므로 AP 의 수신 완료를 확인할 수 없을 때도
    // 새 구독자와 일시적인 전달 누락에서 회복하도록 주기적으로 재전송한다.
    bool disarmed = false;
    {
      std::lock_guard<std::mutex> lock(ch.mutex);
      disarmed = ch.sticky_value.has_value() && !ch.sticky_delivered;
      if (key == ChannelKey::VectorMap) {
        ch.sticky_value = msg;
        ch.sticky_delivered = true;
      } else {
        ch.sticky_value.reset();
      }
    }
    if (disarmed) {
      RCLCPP_INFO(
        get_logger(), "[%s] 전송 회복%s", toEnvSuffix(key),
        key == ChannelKey::VectorMap ? " — 지도 주기 전송 유지" : " — 재시도 중단");
    }
  }
  // Error 는 sticky 트리거가 아니다 — 진짜 장애는 AP 측이 상태 전이로
  // 기록한다. 재전송으로 해결되는 상태가 아니다.
}

template<typename MsgT>
void ServiceCreator::onStickyRetry(InputChannel<MsgT> & ch, ChannelKey key)
{
  std::optional<MsgT> value;
  std::chrono::steady_clock::time_point episode_start;
  {
    std::lock_guard<std::mutex> lock(ch.mutex);
    if (ch.sending) {
      // 진행 중 send 가 있다 — 그 결과(disarm/재무장)를 기다린다. 여기서
      // 스냅샷을 큐잉하면 in-flight 신값을 구식값이 역전할 수 있다.
      return;
    }
    if (ch.pending) {
      // 신값이 대기 중 — 이번 틱은 양보한다. pending 이 비어 있을 때만 재큐한다 —
      // 아니면 워커가 신값을 보낸 뒤의 재큐가 구식값으로 역전시킨다.
      return;
    }
    if (!ch.sticky_value) {
      return;  // 무장 안 됨 — 상시 틱은 여기서 끝난다(비용: 락 1회/5s)
    }
    value = ch.sticky_value;
    episode_start = ch.episode_start;
  }

  if (key != ChannelKey::VectorMap &&
      std::chrono::steady_clock::now() - episode_start > config_.stickyGiveUp()) {
    // 재시도 상한 — 그 뒤에도 구독자가 없으면 배포 구성 문제다.
    RCLCPP_ERROR(
      get_logger(),
      "[%s] %lld ms 동안 구독자가 없다 — 재시도를 포기한다. 배포 구성을 확인할 것",
      toEnvSuffix(key), static_cast<long long>(config_.stickyGiveUp().count()));
    {
      std::lock_guard<std::mutex> lock(ch.mutex);
      ch.sticky_value.reset();
      ch.sticky_given_up = true;  // 잔여 재큐의 뒤늦은 NoSub 가 재개시키지 못하게
    }
    return;
  }

  // 워커 경유로 재전송한다 — 결과 처리는 drainOne → handleStickyOutcome.
  // 아래 !pending 재확인은 이중 방어다(실제 보장은 위 검사).
  bool queued = false;
  {
    std::lock_guard<std::mutex> lock(ch.mutex);
    if (!ch.pending) {
      ch.pending = *value;
      queued = true;
    }
  }
  if (queued) {
    std::lock_guard<std::mutex> lock(ch.worker->mutex);  // 통지 유실 방지
    ch.worker->pending.fetch_add(1);
  }
  if (queued) {
    ch.worker->cv.notify_one();
  }
}

template<typename SrvT>
typename rclcpp::Service<SrvT>::SharedPtr ServiceCreator::serveInputMethod(
  const std::string & service, std::uint32_t method_id)
{
  // 수신 큐 깊이 100 — 기본(10)은 탐침(≤500ms) 중의 유입조차 못 담아 DDS 가
  // 요청을 드랍하고, 드랍된 호출자는 응답을 영영 못 받는다(3종 동시
  // 10Hz × 무응답 제공자에서 40% 미응답). 100 이면 서비스당
  // 초당 200발 유입까지 탐침 1회(500ms)를 흡수한다.
  rmw_qos_profile_t service_qos = rmw_qos_profile_services_default;
  service_qos.depth = 100;
  return create_service<SrvT>(
    service,
    [this, service, method_id](
      const std::shared_ptr<typename SrvT::Request> request,
      std::shared_ptr<typename SrvT::Response> response) {
      // 콜백 예외가 spin() 을 뚫으면 프로세스가 내려간다 — 실패 응답으로 접고 계속한다.
      try {
        if (!ap_->call(*request, *response)) {
          // 세 응답의 구조는 모두 status : ResponseStatus 하나로 동일하다.
          response->status.success = false;
          RCLCPP_WARN(
            get_logger(), "Method %u (%s) — AP 호출 실패", method_id, service.c_str());
        }
      } catch (const std::exception & e) {
        response->status.success = false;
        RCLCPP_ERROR(
          get_logger(), "Method %u (%s) — 호출 예외, 실패로 응답: %s",
          method_id, service.c_str(), e.what());
      } catch (...) {
        response->status.success = false;
        RCLCPP_ERROR(
          get_logger(), "Method %u (%s) — 호출 미상 예외, 실패로 응답",
          method_id, service.c_str());
      }
    },
    service_qos);
}

void ServiceCreator::setupInputEvents()
{
  // 이 프로세스는 Autoware 의 판단(Planning) 모듈을 대체한다. 따라서 QoS 는 원래
  // 그 자리에서 이 토픽들을 구독하던 planning 노드들의 값과 동일해야 한다.
  //
  //   기본값 rclcpp::QoS{1} (reliable / volatile / depth 1)
  //     autoware_universe_utils/ros/polling_subscriber.hpp:165
  //     — InterProcessPollingSubscriber 의 기본 인자. behavior_path_planner 가
  //       odometry / accel / perception / occupancy_grid / traffic_signals /
  //       lateral_offset 를 이 값으로 구독한다.
  //
  //   transient_local (늦게 뜬 구독자도 마지막 값을 받는다 — 틀리면 영영 못 받는다)
  //     behavior_path_planner_node.hpp:96   /map/vector_map
  //     behavior_path_planner_node.hpp:115  /system/operation_mode/state
  //     autoware_state_panel.cpp:153        /planning/scenario_planning/max_velocity_default
  //                                         (발행측이 transient_local 이다)
  //
  //   SensorDataQoS (best_effort)
  //     autoware_obstacle_stop_planner/src/node.cpp:218  ~/input/pointcloud
  //     — best_effort 구독자는 reliable 발행자로부터도 받을 수 있지만 그 역은
  //       불가능하다. 포인트클라우드는 best_effort 로 두는 편이 안전하다.
  const auto planning_default = rclcpp::QoS{1};
  const auto latched = rclcpp::QoS{1}.transient_local();
  const auto sensor = rclcpp::SensorDataQoS().keep_last(1);

  setupInputChannel(clock_ch_, ChannelKey::Clock, "/clock", rclcpp::ClockQoS(), 1);
  setupInputChannel(
    accel_ch_, ChannelKey::Accel, "/localization/acceleration", planning_default, 4001);
  setupInputChannel(
    kinematic_state_ch_, ChannelKey::KinematicState,
    "/localization/kinematic_state", planning_default, 4002);
  setupInputChannel(
    vector_map_ch_, ChannelKey::VectorMap, "/map/vector_map", latched, 6001);
  setupInputChannel(
    objects_ch_, ChannelKey::Objects,
    "/perception/object_recognition/objects", planning_default, 30101);
  setupInputChannel(
    obstacle_pointcloud_ch_, ChannelKey::PointCloud,
    "/perception/obstacle_segmentation/pointcloud", sensor, 30201);
  setupInputChannel(
    occupancy_grid_ch_, ChannelKey::OccupancyGrid,
    "/perception/occupancy_grid_map/map", planning_default, 30301);
  setupInputChannel(
    traffic_signals_ch_, ChannelKey::TrafficSignals,
    "/perception/traffic_light_recognition/traffic_signals", planning_default, 30401);
  setupInputChannel(
    lateral_offset_ch_, ChannelKey::LateralOffset,
    "/planning/scenario_planning/lane_driving/behavior_planning"
    "/behavior_path_planner/input/lateral_offset", planning_default, 11629);
  setupInputChannel(
    max_velocity_ch_, ChannelKey::MaxVelocity,
    "/planning/scenario_planning/max_velocity_default", latched, 12503);
  setupInputChannel(
    operation_mode_ch_, ChannelKey::OperationMode,
    "/system/operation_mode/state", latched, 6101);
}

void ServiceCreator::setupInputMethods()
{
  clear_route_srv_ = serveInputMethod<tier4_planning_msgs::srv::ClearRoute>(
    "/planning/mission_planning/mission_planner/clear_route", 10801);
  set_lanelet_route_srv_ = serveInputMethod<tier4_planning_msgs::srv::SetLaneletRoute>(
    "/planning/mission_planning/mission_planner/set_lanelet_route", 10809);
  set_waypoint_route_srv_ = serveInputMethod<tier4_planning_msgs::srv::SetWaypointRoute>(
    "/planning/mission_planning/mission_planner/set_waypoint_route", 10812);
  // Autoware AD API calls route_selector/main; keep the older mission_planner
  // names above for existing clients while both paths use the same 1817 proxy.
  main_clear_route_srv_ = serveInputMethod<tier4_planning_msgs::srv::ClearRoute>(
    "/planning/mission_planning/route_selector/main/clear_route", 10801);
  main_set_lanelet_route_srv_ = serveInputMethod<tier4_planning_msgs::srv::SetLaneletRoute>(
    "/planning/mission_planning/route_selector/main/set_lanelet_route", 10809);
  main_set_waypoint_route_srv_ = serveInputMethod<tier4_planning_msgs::srv::SetWaypointRoute>(
    "/planning/mission_planning/route_selector/main/set_waypoint_route", 10812);
}

void ServiceCreator::setupOutputEvents()
{
  // 출력 서비스 · Events (3).
  //   Event 12506 / Service 2317 (SCr_SI_planningvalidator) / R-Port RPort_PV2SCr
  //   -> /planning/scenario_planning/trajectory
  //   Event 10604 / Service 1814 (RS_SI_missionplanner) / R-Port RPort_MP2RS
  //   -> /planning/mission_planning/route_selector/main/state
  //   Event 10602 / Service 1814 (RS_SI_missionplanner) / R-Port RPort_MP2RS
  //   -> /planning/mission_planning/route 및 route_selector/main/route
  // 발행 QoS 는 대체 대상인 planning_validator 와 동일하게 맞춘다.
  //   autoware_planning_validator/src/planning_validator.cpp:38
  //     pub_traj_ = create_publisher<Trajectory>("~/output/trajectory", 1);
  //   -> depth 1, reliable, volatile. 하류(scenario_selector / trajectory_follower)가
  //      기본 QoS{1} 로 구독하므로 그대로 호환된다.
  trajectory_pub_ = create_publisher<autoware_planning_msgs::msg::Trajectory>(
    "/planning/scenario_planning/trajectory", rclcpp::QoS{1});

  // [this] 가 아니라 값 캡처. 핸들러는 AP 런타임 스레드에서 불리므로
  // this 캡처는 종료 국면에서 댕글링이 된다. rclcpp::PublisherBase 가 rcl_node_t
  // 의 shared_ptr 을 보유하므로 pub 사본은 노드보다 오래 살아도 안전하다.
  ap_->setTrajectoryHandler(
    [pub = trajectory_pub_](const autoware_planning_msgs::msg::Trajectory & trajectory) {
      pub->publish(trajectory);
    });
  scenario_trajectory_pub_ = create_publisher<autoware_planning_msgs::msg::Trajectory>(
    "/planning/scenario_planning/scenario_selector/trajectory", rclcpp::QoS{1});
  ap_->setScenarioTrajectoryHandler(
    [pub = scenario_trajectory_pub_](const autoware_planning_msgs::msg::Trajectory & trajectory) {
      pub->publish(trajectory);
    });
  turn_pub_ = create_publisher<autoware_vehicle_msgs::msg::TurnIndicatorsCommand>(
    "/planning/turn_indicators_cmd", rclcpp::QoS{1});
  ap_->setTurnHandler([pub = turn_pub_](const auto & command) {pub->publish(command);});
  hazard_pub_ = create_publisher<autoware_vehicle_msgs::msg::HazardLightsCommand>(
    "/planning/hazard_lights_cmd", rclcpp::QoS{1});
  ap_->setHazardHandler([pub = hazard_pub_](const auto & command) {pub->publish(command);});
  remaining_pub_ = create_publisher<autoware_internal_msgs::msg::MissionRemainingDistanceTime>(
    "/planning/mission_remaining_distance_time", rclcpp::QoS{10});
  ap_->setRemainingHandler([pub = remaining_pub_](const auto & message) {pub->publish(message);});
  metrics_pub_ = create_publisher<tier4_metric_msgs::msg::MetricArray>(
    "/planning/planning_evaluator/metrics", rclcpp::QoS{1});
  ap_->setMetricsHandler([pub = metrics_pub_](const auto & message) {pub->publish(message);});
  route_state_pub_ = create_publisher<tier4_planning_msgs::msg::RouteState>(
    "/planning/mission_planning/route_selector/main/state",
    rclcpp::QoS{1}.reliable().transient_local());
  ap_->setRouteStateHandler(
    [pub = route_state_pub_](const tier4_planning_msgs::msg::RouteState & state) {
      pub->publish(state);
    });
  const auto route_qos = rclcpp::QoS{1}.reliable().transient_local();
  route_pub_ = create_publisher<autoware_planning_msgs::msg::LaneletRoute>(
    "/planning/mission_planning/route", route_qos);
  main_route_pub_ = create_publisher<autoware_planning_msgs::msg::LaneletRoute>(
    "/planning/mission_planning/route_selector/main/route", route_qos);
  ap_->setRouteHandler(
    [route = route_pub_, main = main_route_pub_](
      const autoware_planning_msgs::msg::LaneletRoute & message) {
      route->publish(message);
      main->publish(message);
    });
}

void ServiceCreator::logChannelSummary()
{
  // ROS 측 수신/전달 건수 [1]. AP 측 [2]는 ApChannel 이 남긴다.
  const auto one = [this](const auto & ch, ChannelKey key) {
      if (ch.received == 0 && ch.dispatched == 0) {
        return;
      }
      const auto avg_us = ch.dispatched == 0 ? 0.0 :
        static_cast<double>(ch.send_ns_total) / static_cast<double>(ch.dispatched) / 1000.0;
      RCLCPP_INFO(
        get_logger(), "[%s] ROS 수신 %lu / AP 전달 %lu / 송신비용 avg %.1fus max %.1fus",
        toEnvSuffix(key),
        static_cast<unsigned long>(ch.received),
        static_cast<unsigned long>(ch.dispatched),
        avg_us, static_cast<double>(ch.send_ns_max) / 1000.0);
    };
  one(clock_ch_, ChannelKey::Clock);
  one(accel_ch_, ChannelKey::Accel);
  one(kinematic_state_ch_, ChannelKey::KinematicState);
  one(vector_map_ch_, ChannelKey::VectorMap);
  one(objects_ch_, ChannelKey::Objects);
  one(obstacle_pointcloud_ch_, ChannelKey::PointCloud);
  one(occupancy_grid_ch_, ChannelKey::OccupancyGrid);
  one(traffic_signals_ch_, ChannelKey::TrafficSignals);
  one(lateral_offset_ch_, ChannelKey::LateralOffset);
  one(max_velocity_ch_, ChannelKey::MaxVelocity);
  one(operation_mode_ch_, ChannelKey::OperationMode);
}

}  // namespace servicecreator
