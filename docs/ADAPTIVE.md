# Decision AP Adaptive 구성과 검증 기록

릴리즈 이름은 Decision AP다. 계획 실행 파일은 `ap-decision`이고 설치 경로는 `/opt/ap/opt/DecisionAP/bin/ap-decision`이다. AUTOSAR 앱 식별자는 `DecisionAP`다.

이 문서는 PopcornSAR PARA R20-11 기반 Adaptive AUTOSAR 앱의 구성, 인터페이스, 검증 기록을 정리한다. 사용자 빌드·실행·RViz 절차는 [루트 README](../README.md)에 있다. 계획 앱은 `src/Ecu_Machine/`, 외부 `oss-ap`에 덮어쓰는 수정 파일은 `overrides/Ecu_Machine/`, Dockerfile과 실행 스크립트는 `docker/`에 있다. 빌드에 필요한 SDK는 `sdk/`, 기본 생성 파일과 OTAClient 소스는 형제 저장소 `../oss-ap`, 전용 생성 헤더는 `overrides/generated/`, ROS 메시지는 `external/msgs`에 있다. 원본 `oss-ap` 저장소는 수정하지 않는다.

## 현재 인터페이스

| DecisionAP 포트 | 방향 | SOME/IP 서비스 | 내용 |
| --- | --- | --- | --- |
| `RPort_SCr2MP` | `oss-ap` → DecisionAP | 4308 | clock, localization, 지도, perception, 속도 제한, 운영 모드 등 입력 Event 11개 |
| `PPort_MP2SCr` | DecisionAP ↔ `oss-ap` | 1817 | clear route 10801, set lanelet route 10809, set waypoint route 10812 |
| `PPort_MP2RS` | DecisionAP → `oss-ap` | 1814 | 경로 Event 10602와 경로 상태 Event 10604 |
| `PPort_PV2SCr` | DecisionAP → `oss-ap` | 2317 | **최종 궤적 Event 12506 하나** |

`DecisionAP`는 `map_vectormap`의 `LaneletMapBin.data`를 Lanelet 어댑터로 해독한다. 지도, route, odometry와 운영 모드가 준비되면 새 odometry마다 route 기반 주행 경로를 만들고, 인식 객체·교통 신호·가속도·속도 제한을 반영해 `PlanningFinalFrame`을 조립한다. 최종 파이프라인의 검증을 통과한 궤적만 Event 12506으로 변환해 `Send()`한다. AA의 `PlanningValidatorCore` 결과 14개는 `run-katech-stack.sh`가 시작한 ROS 진단 어댑터로 전달되어 `/diagnostics`에 발행된다. 검증에 실패하면 ERROR 진단을 보내고 궤적을 발행하지 않는다. 진단 어댑터는 검증 계산을 하지 않으며 Control 이미지에도 Planning 검증 노드를 추가하지 않는다. `Planning services active`는 통신 준비 상태이며, 실제 송신은 `Published final trajectory Event 12506` 로그로 확인한다. 지도와 odometry의 `frame_id`가 일치해야 한다. Lanelet route 요청의 segment를 사용하고, waypoint route 요청은 해독한 지도에서 경로를 탐색한다. 사용 절차는 [루트 README](../README.md)를 참고한다.

실행 매니페스트는 `CM=Running`을 `DecisionAP`의 시작 조건으로 둔다. CM이 초기화되기 전에 `OfferService()`를 호출하면 서비스 제공이 실패할 수 있다.

## 구현 구성

브리지 이미지는 `Exe_ServiceCreator`와 OTAClient를 포함하고, 계획 이미지는 DecisionAP를 포함한다. 통합 이미지에서는 EM과 CM이 각각 하나씩 실행되며, `Exe_ServiceCreator`·OTAClient·`ap-decision`이 별도 프로세스로 동작한다. 지도 디렉터리는 Perception·Control 컨테이너의 `/autoware_map`에 읽기 전용으로 연결된다. 보드에서는 host 네트워크, PC에서는 Docker 브리지 네트워크를 사용한다.

| 브리지 경계 | ROS 2 이름 | 제공/소비 |
| --- | --- | --- |
| 4308 map_vectormap | `/map/vector_map` | perception의 map loader → DecisionAP |
| 4308 localization_kinematicstate, localization_acceleration | `/localization/kinematic_state`, `/localization/acceleration` | perception localization → DecisionAP |
| 4308 objects, pointcloud, occupancy grid, traffic signals | `/perception/object_recognition/objects`, `/perception/obstacle_segmentation/pointcloud`, `/perception/occupancy_grid_map/map`, `/perception/traffic_light_recognition/traffic_signals` | perception → DecisionAP; 점군·격자는 현재 프레임에 미반영 |
| 4308 operation mode | `/system/operation_mode/state` | control의 operation mode transition manager → DecisionAP |
| 4308 clock | `/clock` | simulation clock → DecisionAP |
| 4308 velocity limit, lateral offset | `/planning/scenario_planning/max_velocity_default`, `/planning/scenario_planning/lane_driving/behavior_planning/behavior_path_planner/input/lateral_offset` | 별도 발행자가 있어야 유입; 필수 입력 아님 |
| 1817 route methods | `/planning/mission_planning/route_selector/main/{clear_route,set_lanelet_route,set_waypoint_route}` | 외부 경로 요청 → DecisionAP; 기존 `mission_planner/*` 별칭도 유지 |
| 2317 trajectory event | `/planning/scenario_planning/trajectory` | DecisionAP → control |
| 2516 selected trajectory event | `/planning/scenario_planning/scenario_selector/trajectory` | DecisionAP → bridge ROS |
| 2340 evaluator event | `/planning/planning_evaluator/metrics` | DecisionAP → bridge ROS |
| 2341 remaining distance event | `/planning/mission_remaining_distance_time` | DecisionAP → bridge ROS |
| 2342 turn command event | `/planning/turn_indicators_cmd` | DecisionAP → bridge ROS |
| 2343 hazard command event | `/planning/hazard_lights_cmd` | DecisionAP → bridge ROS |

## 검증 기록

2026-10-27 x86_64 PC에서 ARM64 브리지 크로스 빌드, DecisionAP QEMU 빌드, 통합 이미지 빌드를 확인했다. ARM64 Perception·Control·DecisionAP를 함께 실행해 171점 궤적의 Control 수신과 검증 진단 발행을 확인했다. 보드 실행과 실차 주행은 별도 검증이 필요하다.

2026-10-19 첫 실행에서는 보행자 정지 판정 후 `StaticAvoidance`가 활성화됐고 route UUID가 유지됐으며, 201개 제어 궤적을 수신했다. 정지 차량 앞 약 30 m까지 발행되는 국소 궤적 끝점은 기본 경로보다 최대 0.182 m 옆으로 이동했다. 보행자는 초기 차량 위치에서 약 40 m 앞에 있어 이 시점에는 회피 경로 전체가 국소 궤적 창에 들어오지 않았다. 초기 위치 `(3648.0, 73505.0)`에서는 Control 종방향 명령이 0 m/s, 기어 명령이 PARK로 남아 출발하지 않았다.

초기 위치를 같은 차로 중심에 가까운 `(3648.46, 73505.0)`으로 바꾼 다음 같은 보행자를 계속 발행하고 Autonomous 모드를 켠 실행에서는 차량이 출발해 보행자 위치를 통과했다. 재실행한 검사 스크립트는 route UUID 유지, 제어 궤적 895개 수신, 차량 이동 `64.935 m`, 보행자 중심까지 최소 거리 `2.449 m`, 보행자 위치 통과를 기록했다. `decision-ap.log`에서 회피 활성화와 지속적인 최종 궤적 송신을 확인했다. 보행자 중심 `(3645.6, 73545.0)`을 지나는 시각의 차량 중심은 `(3643.123, 73545.065)`였고 속도는 `4.164 m/s`였다. 이 결과는 샘플 지도와 이 초기 위치·보행자 배치에서의 종단 주행 확인이다.

2026-10-21에는 원본 스택과 AA 스택을 각각 새로 시작해 위 좌표에서 35초간 보행자를 발행하고 Autonomous 모드로 실제 주행시켰다. 두 스택 모두 route UUID를 유지하며 보행자 위치를 통과했다. 원본의 이동 거리·보행자 중심 최소 거리는 `116.483 m`·`2.408 m`, AA는 `116.605 m`·`2.365 m`였다. 보행자의 Y 좌표 `73545.0`을 지나는 순간의 차량 중심 X 좌표·속도는 원본 `3643.174 m`·`4.166 m/s`, AA `3643.207 m`·`4.163 m/s`였다. 보행자 전후 Y 좌표 `73540`–`73550` 구간의 최저 속도는 각각 `4.152`·`4.151 m/s`로 정지하지 않았다. 두 주행 기록을 Y 좌표 기준으로 보간하면 보행자 전후 20 m 구간의 최대 가로 위치 차이는 `0.072 m`, 최대 속도 차이는 `0.006 m/s`였다.

2026-10-22에는 원본 KATECH README 7.1.3의 RViz `2D Dummy Pedestrian` 입력 경로를 재현했다. 차량 `(3648.46, 73505.0)`, 목표 `(3632.5, 73628.0)`, 보행자 `(3645.6, 73545.0)`, 차량 초기 yaw `1.697865 rad`에서 더미 보행자를 생성했다. 보행자가 인식되고 정적 회피가 활성화됐으며, Autonomous 주행 중 route UUID가 유지됐다. 차량은 보행자 Y 좌표를 `4.167 m/s`로 지나갔고 보행자 중심 최소 거리는 `2.407 m`, 인근 최저 속도는 `4.154 m/s`였다. 원본 README의 스크린샷에는 지도 좌표 메타데이터가 없어 촬영 당시 클릭 좌표 자체를 복원한 결과는 아니다. 이 좌표는 같은 정상 판정 조건을 실측으로 재현한다.

2026-10-19 13:40 원본 `katech-autoware-new`의 Decision·Perception·Control 스택에서도 **같은 초기 위치 `(3648.46, 73505.0)`, 목표 `(3632.5, 73628.0)`, 정지 보행자 `(3645.6, 73545.0)`**를 시험했다. 6초 동안 route UUID가 유지됐고, 보행자 부근 행동 경로는 기본 경로보다 최대 `0.203 m`, 최종 궤적 끝점은 `0.188 m` 옆으로 이동했다. 이어 동일한 입력을 AA에 주입한 실행에서 `selected=1 candidates=1`, `PLAA_AVOIDANCE active=1`이 기록됐고 최종 궤적 끝점 이동량은 `0.181 m`였다. 이 수치는 정지 차량에서 국소 궤적 창에 들어온 회피 시작 부분만 비교한다.

13:35의 별도 RViz 배치에서는 인식 객체가 AA에 들어와 `selected=1`까지 갔지만 `candidates=0`이므로 회피가 활성화되지 않았다. 당시 로그에는 객체의 x좌표 약 `3761.5`만 남았고 y좌표와 후보 거부 단계가 없어, 이 배치의 정확한 실패 조건은 판정할 수 없다.

14:07 RViz 수동 배치에서는 차량 초기 위치 `(3730.405, 73677.742)`, 목표 `(3795.017, 73711.516)`, 인식된 보행자 `(3759.44, 73694.09)`였다. AA는 보행자를 정적 회피 대상으로 선택했지만 `candidates=0`이었다. 같은 좌표에 정지 보행자를 주입해 재현한 결과 `geometry_margin shoulder=1.508 required=2.696`으로, 보행자에서 회피 반대쪽 주행 가능 경계까지의 폭이 차량 폭·보행자 안전 여유·경계 여유의 합보다 작았다. AA의 최종 궤적은 보행자 앞에서 멈췄고 route UUID는 유지됐다. 원본 KATECH Decision·Perception·Control에도 같은 초기 위치·목표·보행자를 주입했으며, 원본도 행동 경로와 최종 궤적의 측방 이동 없이 보행자 앞에서 멈췄다. 이 배치는 원본에서도 회피 가능한 위치가 아니다. 이 조건을 통과시키려고 안전 여유를 줄이지 않는다.

2026-10-19에 원본 `partition/run_partitions.sh --headless`와 AA `run-katech-stack.sh`를 각각 실행하고 같은 차량 초기 위치 `(3648.0, 73505.0, 19.0)`와 목표 `(3632.5, 73628.0, 19.0)`를 `/initialpose3d` 및 main waypoint route 서비스에 보냈다. 두 실행 모두 route 요청이 성공했고, 출력 route의 lanelet 구간 `102 → 87`, 시작·목표 자세, 최종 trajectory의 점 수(171점)와 `time_from_start`가 일치했다. 수집한 trajectory 3건의 대응 점에서 최대 위치 차이는 `9.5e-6 m`, quaternion 성분 차이는 `7.6e-7`, 종방향 속도 차이는 `2.4e-7 m/s`였다. 매 실행에서 생성되는 route UUID와 header 시각은 비교 대상에서 제외했다.

2026-10-19에 같은 초기 위치와 목표로 추가 출력을 재검증했다. 원본과 AA 모두 선택 궤적 259점, 방향지시등 0, 비상등 1을 발행했다. 남은 거리는 원본 `123.97266581346074 m`, AA `123.97266581345065 m`였고 남은 시간은 각각 `29.72965552644317 s`, `29.72965607037186 s`였다. 최종 궤적 171점의 대응 위치 최대 차이는 `1.63e-9 m`, 종방향 속도 차이는 0이었다. 평가 지표의 차로 ID, 차로 내 거리·횡방향 거리, 차량 속도·가속도·저크도 일치했고 궤적 통계는 수치상 근접했다. 원본의 `modified_goal`은 이 경로에서 발행되지 않았다.

이 한 경로만으로 decision 전체 대체를 증명할 수는 없다. 초기 평가 지표 표본에서는 원본이 42개, AA가 54개 항목을 냈지만 충분히 기다린 뒤에는 양쪽 모두 54개 항목을 발행했다. AA의 방향지시등 판단은 원본의 교차로 차로 속성, 경로 변경 및 후진 출발 규칙을 기반으로 하지만 원본의 모든 behavior 모듈 간 우선순위와 시점 조건을 아직 재현하지 않는다. 차로 변경, 회피, 주차, 실제 주행 신호에서 별도 검증이 필요하다.

## 파일 구성

| 파일 | 역할 |
| --- | --- |
| `src/Ecu_Machine/CMakeLists.txt` | 생성 ARA 포트와 CM 설정을 빌드 디렉터리에 준비하고 DecisionAP를 빌드 |
| `sdk/`, `../oss-ap`, `overrides/generated/`, `external/msgs` | PARA SDK, 외부 기본 ARA 파일, 전용 생성 헤더, ROS 메시지 |
| `src/Ecu_Machine/DecisionAP/src/` | `ara::com` 입력 구독, route Method 제공, 검증된 궤적 송신, EM 상태 보고 |
| `src/Ecu_Machine/{DecisionAP,EM,CM}/manifest/` | 앱 실행·EM·머신·기능 그룹·SOME/IP 매니페스트 |
| `docker/images/Dockerfile.planning`, `docker/build/build_planning.sh` | 통합 이미지에 복사할 계획 앱 산출물 빌드 |
| `docker/images/Dockerfile.bridge-local`, `docker/build/build_bridge.sh` | 외부 `oss-ap`와 이 저장소의 전용 변경으로 브리지·OTAClient 이미지 빌드 |
| `docker/run-x86.sh`, `docker/run-arm64-board.sh`, `docker/run-arm64-qemu.sh` | 환경별 세 컨테이너 실행 명령 |
| `docker/run/run-katech-stack.sh` | 세 컨테이너의 공통 실행 로직 |
| `docker/entrypoint/ap_entrypoint.sh` | 통합 DecisionAP 컨테이너의 EM 시작 |

## 현재 제한

점군·점유 격자·lateral offset은 여전히 최신 샘플만 보관하며 이 주행 프레임에 반영하지 않는다. Parking 경로는 연결되지 않았다. 지도·점군·점유 격자처럼 큰 UDP Event는 기존 `oss-ap` CM의 기본 `max-data-bytes=0` 설정에서 전송 실패할 수 있다. 과거의 일부 종단 성공 사례만으로 모든 이미지 조합과 대형 입력의 전송을 보장할 수 없다.

2026-10-29 실행에서는 지도 Event 6001의 바이트를 수신했지만 DecisionAP가 Lanelet 지도를 해독하지 못해 route 요청에 `60001: The mission planner is not ready`를 반환했다. 해독 오류는 이미지 태그 변경 전후에 모두 기록됐다. 위의 과거 주행 결과는 해당 실행의 재현을 보장하지 않는다. 원본의 UDP route Marker 브리지는 현재 실행 스크립트에 포함돼 있지 않다. 현재 `ros/goal_pose_visualizer.py`는 발행된 route의 목표 자세만 재발행한다.

Event 변환은 validator가 이번 프레임을 유효하다고 판정한 궤적에만 적용한다. 내부 `TrajectoryPoint`에는 `time_from_start`가 없으며, 원본 planning_validator의 출력과 동일하게 이 필드를 0으로 발행한다.
