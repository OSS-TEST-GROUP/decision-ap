# `oss-ap`에 적용한 Decision AP 변경 사항

## 목적과 비교 기준

이 문서는 팝콘사에서 Decision AP 연동을 위해 변경한 `oss-ap` 파일, 변경 이유, 기존 구성에 미치는 영향을 검토할 수 있도록 정리한 것이다.

- 기준: `oss-ap` 커밋 `2f649badb4a63e47e7df89c5b5b1675a70fa6ad5`
- Decision 앱: 실행 파일 `ap-decision`, AUTOSAR 앱 식별자 `DecisionAP`
- Service Creator: ROS 2와 Adaptive SOME/IP 사이의 입력·출력 및 서비스 요청을 연결하는 `Exe_ServiceCreator`

원본은 여러 Adaptive planning 앱으로 입력을 분배하는 구성이다. Decision AP는 planning을 하나의 Decision 앱에서 수행하므로, **입력 송신 대상을 하나로 줄이고 Decision 앱의 출력이 ROS 2로 돌아오는 경로를 확장**했다. OTAClient 변경은 실행 manifest의 DDS 도메인 설정에 한정된다.

## 파일 적용 방식

원본 `../oss-ap` 작업 트리의 소스 파일은 직접 수정하지 않는다. 빌드 스크립트가 기준 커밋을 준비하고, Docker 빌드 중 복사된 소스에 변경 파일을 덮어쓴다. 아래의 원본 경로는 별도 표기가 없으면 `oss-ap/src/Ecu_Machine/` 기준이다.

| Decision AP 저장소의 파일 | 이미지 안에서 적용하는 원본 경로 | 적용 방식 |
| --- | --- | --- |
| `overrides/Ecu_Machine/Exe_ServiceCreator/src/` | `Exe_ServiceCreator/src/` | 변경 파일 덮어쓰기 및 새 변환 헤더 추가. 나머지 원본 파일은 유지 |
| `overrides/Ecu_Machine/Exe_ServiceCreator/include/servicecreator/` | `Exe_ServiceCreator/include/servicecreator/` | 변경한 수기 헤더 덮어쓰기 |
| `overrides/Ecu_Machine/Exe_ServiceCreator/manifest/interface/` | `Exe_ServiceCreator/manifest/interface/` | 원본의 기존 계약 3개를 유지하고 나머지를 제외한 뒤, 신규 인터페이스 6개 추가 |
| `overrides/Ecu_Machine/CM/bridge-manifest/com/` | `CM/manifest/com/` | 원본의 기존 계약 3개를 유지하고 나머지를 제외한 뒤, 수정·추가 머신 파일 6개 적용. 상위 CM 설정은 원본 유지 |
| `overrides/Ecu_Machine/OTAClient/manifest/exec/OTAClient.json` | `OTAClient/manifest/exec/OTAClient.json` | 실행 manifest 덮어쓰기 |
| `src/Ecu_Machine/cmake/BridgeCMakeLists.txt` | `CMakeLists.txt` | 브리지 전용 최상위 빌드 구성으로 교체 |
| `overrides/generated/decision-ap-generated/Exe_ServiceCreator/include/` | `Exe_ServiceCreator/include/` | 신규 서비스 헤더·타입 18개만 원본 include에 추가. 이후 수기 헤더 변경분 적용 |

실제 적용 순서는 [네이티브 브리지 Dockerfile](../docker/images/Dockerfile.bridge-local)과 [ARM64 크로스 빌드 Dockerfile](../docker/images/Dockerfile.bridge-cross)에 있다. EM·CM 런타임 구현 및 OTAClient C++ 구현을 이 변경분에서 수정하지는 않는다.

## Service Creator 소스 변경

| 원본 기준 위치 | 변경 내용 | 변경 이유와 영향 |
| --- | --- | --- |
| `Exe_ServiceCreator/src/ap_channel_para.cpp` — 입력 | 기존 `SERVICECREATOR_FOR_EACH_PPORT`의 16개 대상(BPP~VS)을 MP 하나로 축소. 서비스 4301~4316 중 `SCr2MP` 서비스 4308만 제공 | planning 모듈이 Decision 앱 한 프로세스에 있으므로 입력을 16개 서비스로 복제할 필요가 없음. 기존의 다른 15개 입력 서비스를 사용하는 앱과는 그대로 호환되지 않음 |
| `Exe_ServiceCreator/include/servicecreator/ap_channel.hpp` | 기존 최종 궤적 핸들러에 route·state, 선택 궤적, 방향지시등, 비상등, 평가 지표, 남은 거리·시간 핸들러 및 ROS 메시지 타입 추가 | ARA proxy에서 받은 출력을 ROS publisher로 전달하기 위한 추상 채널 확장 |
| `Exe_ServiceCreator/include/servicecreator/servicecreator_node.hpp` | 출력 publisher, route 서비스 별칭, 지도 송신 성공 상태를 저장하는 `sticky_delivered` 추가 | 새 출력 경로와 지도 재전송 정책을 지원 |
| `Exe_ServiceCreator/src/servicecreator_node.cpp` — route 서비스 | 기존 `mission_planner/*` 이름을 유지하고 `route_selector/main/{clear_route,set_lanelet_route,set_waypoint_route}` 별칭 추가. 두 경로 모두 기존 서비스 1817의 같은 Method 호출로 연결 | 현재 Autoware AD API가 사용하는 서비스 경로로 요청을 받기 위함. 기존 클라이언트 경로도 유지 |
| 같은 파일 — 출력 | 기존 최종 궤적 publisher를 유지하고 아래 표의 route·state·선택 궤적·조명 명령·평가·남은 거리 publisher 추가 | Decision 앱의 결과를 Control·System·RViz 등 ROS 2 소비자에게 전달 |
| 같은 파일 — 지도 | `/map/vector_map` 송신 성공 후에도 최신 지도를 보관해 재전송. VectorMap은 `stickyGiveUp()` 시간 상한에서 제외 | 늦게 시작하거나 다시 연결된 Decision 앱에도 지도 입력을 제공. 지도는 성공 후에도 반복 송신되며, 구독자가 없어도 시간 상한만으로 재시도를 중단하지 않음 |
| `Exe_ServiceCreator/src/ap_channel_para.cpp` — 출력 | 새 서비스 proxy 탐색·수신 핸들러·구독·정리 경로와 ARA→ROS 변환 추가 | manifest만 추가한 상태에서 끝나지 않고 실제 Event 수신과 ROS 발행을 연결. 기존 route Method와 최종 궤적 수신 경로는 유지 |
| `Exe_ServiceCreator/src/ap_route_convert.hpp` (신규) | ARA `LaneletRoute`를 ROS `LaneletRoute`로 변환. header, 시작·목표 pose, segments, UUID, 수정 허용 값을 복사. ARA의 `segment.primitive_type`을 ROS의 `preferred_primitive`에 매핑 | 생성 타입과 ROS 메시지의 필드 이름 차이를 처리 |
| `Exe_ServiceCreator/src/CMakeLists.txt` | `Exe_routeselector`, `Exe_scenarioselector`, `Exe_motionvelocityplanner`의 include 경로 및 `autoware_vehicle_msgs`, `autoware_internal_msgs`, `tier4_metric_msgs` 의존성 추가 | 추가 proxy의 의존 타입과 ROS 출력 메시지를 컴파일하기 위함 |

### QoS와 지도 재전송 영향

기존 ROS 입력 토픽별 QoS는 유지했다. 추가한 route·state 출력은 reliable/transient-local/depth 1이며, 선택 궤적·방향지시등·비상등·평가 지표는 `rclcpp::QoS{1}`, 남은 거리·시간은 `rclcpp::QoS{10}`을 사용한다.

지도 재전송 주기는 기존 `SERVICECREATOR_STICKY_RETRY_MS` 설정을 사용한다. 이 변경은 ARA 송신 성공이 수신 측의 지도 해독 완료를 보장하지 않는다는 전제에서 적용했다. 큰 지도를 반복 송신하는 비용이 있으므로 일반 `oss-ap`에 반영할 경우 적용 대상과 재전송 정책을 검토할 필요가 있다. Decision 앱의 `PLANNING_AA_STATIC_MAP` 처리는 별도의 수신 측 설정이며, Service Creator의 재전송을 끄는 설정은 아니다.

## 통신 포트와 manifest 변경

인터페이스 파일은 `overrides/Ecu_Machine/Exe_ServiceCreator/manifest/interface/Exe_ServiceCreator/AA/`, 머신 파일은 `overrides/Ecu_Machine/CM/bridge-manifest/com/someip/`에 있다. 모두 Service Creator 관점의 역할이다.

| 구분 | 인터페이스 / 머신 파일 | 서비스 / Event 또는 Method | ROS 2 연결 |
| --- | --- | --- | --- |
| 유지 | `PPort_SCr2MP.json` / `CM_P_MP_ServiceCreator.json` | 서비스 4308, 기존 입력 Event 11개 | 지도·차량 상태 등 ROS 입력 → Decision 앱 |
| 유지 | `RPort_MP2SCr.json` / `CM_R_Scr_missionplanner.json` | 서비스 1817, Method 10801·10809·10812 | route clear/lanelet/waypoint 요청 |
| 유지 | `RPort_PV2SCr.json` / `CM_R_Scr_planningvalidator.json` | 서비스 2317, Event 12506 | `/planning/scenario_planning/trajectory` |
| 추가 | `RPort_MP2RS.json` / `CM_R_MP_missionplanner.json` | 서비스 1814, Event 10602·10604 | `/planning/mission_planning/route`, `/planning/mission_planning/route_selector/main/{route,state}` |
| 인터페이스 추가·기존 머신 설정 변경 | `RPort_SS2VS.json` / `CM_R_VS_scenarioselector.json` | 서비스 2516, Event 12908 | `/planning/scenario_planning/scenario_selector/trajectory` |
| 추가 | `RPort_AA2SCrMetrics.json` / `CM_R_AA2SCrMetrics.json` | 서비스 2340, Event 12520 | `/planning/planning_evaluator/metrics` |
| 추가 | `RPort_AA2SCrRemaining.json` / `CM_R_AA2SCrRemaining.json` | 서비스 2341, Event 12521 | `/planning/mission_remaining_distance_time` |
| 추가 | `RPort_AA2SCrTurn.json` / `CM_R_AA2SCrTurn.json` | 서비스 2342, Event 12522 | `/planning/turn_indicators_cmd` |
| 추가 | `RPort_AA2SCrHazard.json` / `CM_R_AA2SCrHazard.json` | 서비스 2343, Event 12523 | `/planning/hazard_lights_cmd` |

“유지”로 표시한 인터페이스 3개와 머신 파일 3개는 `overrides/`에 중복 보관하지 않고 빌드 시 원본에서 유지한다. `overrides/`에는 수정·추가 파일만 저장한다. 원본의 다른 planning 앱용 입력 P-Port와 CM SOME/IP 설정은 이 전용 이미지에서 제외된다. 전체 디렉터리를 원본 `oss-ap`에 그대로 덮어쓰면 기존 다중 앱 구성이 달라지므로, 원본 반영 시에는 전용 빌드 프로파일로 구분하거나 필요한 변경만 선택해야 한다.

`CM_R_VS_scenarioselector.json`은 신규 파일이 아니다. 기존 서비스 2516의 TCP·UDP 포트를 `4215 → 4415`, client ID를 `16917 → 17428`로 변경했다. 서비스 ID·major version 41·Event 12908은 유지하며, 새 `RPort_SS2VS.json`과 client ID를 맞췄다. 이는 Decision AP용 포트 배치 변경이며 원본 모든 구성에 필수인 수정이라는 의미는 아니다.

브리지 전용 설정은 P-Port 1개와 R-Port 8개, SOME/IP 머신 파일 9개다. 통합 이미지에서는 Decision 앱 측 머신 파일 9개를 추가하고, 앱 측 주소 `.93`을 브리지 측 `.92`로 통일한다. 병합 도구는 총 18개 파일의 TCP·UDP 포트 중복을 검사한다. 이 병합 및 실행 시 주소 치환은 Decision AP 배포 로직이며 `oss-ap` 런타임 구현 수정과는 구분한다.

## OTAClient와 빌드 구성 변경

| 대상 | 변경 내용 | 이유와 적용 범위 |
| --- | --- | --- |
| `OTAClient/manifest/exec/OTAClient.json` | `MMF_DDS_DOMAIN_ID=42` 환경 항목 제거. 나머지 설정 유지 | 실행 스크립트와 entrypoint가 전달하는 도메인 값을 사용하기 위함. 현재 Decision AP entrypoint는 환경변수가 없으면 42를 기본값으로 export함. 이 manifest만 다른 환경에 적용할 때는 환경변수 전달 경로도 확인해야 함 |
| 최상위 `CMakeLists.txt` | 이 저장소의 `src/Ecu_Machine/cmake/BridgeCMakeLists.txt`로 교체해 CM·EM·Service Creator·OTAClient만 빌드 대상으로 구성 | 원본의 개별 planning 앱 대신 별도로 빌드한 Decision 앱을 결합하기 위한 전용 구성. 기존 최상위 빌드 구성을 일반적으로 대체하려는 변경은 아님 |

## 생성 ARA 헤더와 팝콘사 확인 사항

원본에 없는 생성 서비스 헤더 12개와 타입 헤더 6개만 `overrides/generated/decision-ap-generated/Exe_ServiceCreator/include/`에 포함한다. 공통 생성 헤더는 `oss-ap` 원본에서 가져온다. 빌드 전 검사에서는 원본과 변경분을 임시 디렉터리에 합쳐 의존성을 확인한다. 기준 `oss-ap`에 없는 네 서비스 `AA2SCrMetrics`, `AA2SCrRemaining`, `AA2SCrTurn`, `AA2SCrHazard`는 이 생성본을 사용한다. 일반 빌드에는 별도 ARXML 생성 과정이 필요하지 않지만, 이 저장소에는 해당 서비스를 재생성할 ARXML이나 생성 스크립트가 없다.

팝콘사 검토 시에는 다음을 확인하면 된다.

- 서비스 2340~2343의 타입·서비스 ID·Event ID 및 생성 헤더가 기준 PARA SDK와 맞는지, 향후 재생성 원본을 어떻게 관리할지.
- 서비스 1814·2516을 Service Creator에서 구독하는 방식과 추가 포트·client ID 배치가 기존 인터페이스 설계에 맞는지.
- 입력 16개 서비스를 하나로 줄이는 구성과 지도 반복 송신 정책을 Decision AP 전용 설정으로 분리할지.
- OTAClient의 DDS 도메인을 실행 환경에서 전달하는 정책을 원본에도 적용할지.
