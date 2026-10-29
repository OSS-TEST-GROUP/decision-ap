# `oss-ap`에 적용한 Decision AP 변경분

## 기준과 적용 방식

기준 소스는 `oss-ap`의 커밋 `2f649badb4a63e47e7df89c5b5b1675a70fa6ad5`다. 이 저장소는 형제 디렉터리의 `oss-ap` 작업 트리를 직접 수정하지 않는다. 브리지 Docker 빌드가 원본 `src/Ecu_Machine`을 빌드 컨텍스트에 복사한 다음, `overrides/Ecu_Machine/`의 파일을 같은 상대 경로에 덮어쓴다. 인터페이스와 SOME/IP 매니페스트 디렉터리는 기존 파일을 제거하고 이 저장소의 집합으로 교체한다. 실제 적용 순서는 [브리지 Dockerfile](../docker/images/Dockerfile.bridge-local)과 [ARM64 크로스 빌드 Dockerfile](../docker/images/Dockerfile.bridge-cross)에 있다.

이 문서는 기준 커밋과 이 저장소의 오버레이를 비교한 결과다. `overrides/generated/decision-ap-generated/`의 PARA 생성 ARA 헤더는 빌드 입력이지만 Git에 포함되지 않는다. 따라서 아래의 `구현`은 소스와 매니페스트의 배선을 뜻하며, 현재 이미지 조합에서 모든 ROS 2↔SOME/IP 종단 동작이 성공했다는 뜻은 아니다.

## 변경한 부분과 이유

| 위치 | 변경 내용 | 이유 |
| --- | --- | --- |
| `Exe_ServiceCreator/include/servicecreator/ap_channel.hpp` | 기존 입력 Event 11개·route Method 3개의 추상 채널에 route/state, 선택 궤적, 방향지시등, 비상등, 평가 지표, 남은 거리·시간의 출력 핸들러와 메시지 타입을 추가 | DecisionAP의 출력 Event를 ROS 2 publisher까지 전달하기 위해 |
| `Exe_ServiceCreator/include/servicecreator/servicecreator_node.hpp` | 출력 publisher, `route_selector/main` 서비스 별칭, 지도 재전송 상태를 보관하는 멤버 추가 | Control과 AD API가 사용하는 현재 route 이름을 제공하고, 늦게 연결된 DecisionAP에도 지도를 전달하기 위해 |
| `Exe_ServiceCreator/src/servicecreator_node.cpp` | 기존 토픽별 ROS 입력 QoS를 유지하면서 `route_selector/main`의 clear/lanelet/waypoint 서비스를 기존 `mission_planner/*`와 함께 제공. route·state·궤적·조명 명령·평가·남은 거리 publisher 추가. `/map/vector_map`은 성공 후에도 보관해 주기적으로 재전송 | 실행 순서 때문에 첫 지도 송신을 놓치는 경우를 줄이며, DecisionAP 결과를 Control과 RViz가 쓰는 ROS 토픽으로 전달하기 위해 |
| `Exe_ServiceCreator/src/ap_channel_para.cpp` | 새 서비스의 proxy 찾기·구독·해제·재연결과 ARA→ROS 메시지 변환을 추가. 기존 입력 Event 송신과 route Method 호출은 유지 | DecisionAP가 제공하는 각 SOME/IP Event를 실제 브리지 프로세스가 수신하도록 연결하기 위해 |
| `Exe_ServiceCreator/src/ap_route_convert.hpp` | ARA `LaneletRoute`를 ROS `LaneletRoute`로 변환. header, 시작·목표 pose, segment, UUID, 수정 허용 값을 복사 | route Event 10602를 ROS route 토픽으로 발행할 때 두 타입의 필드 이름과 자료형 차이를 처리하기 위해 |
| `Exe_ServiceCreator/src/CMakeLists.txt` | route·scenario·motion 관련 생성 헤더 경로와 `autoware_vehicle_msgs`, `autoware_internal_msgs`, `tier4_metric_msgs` 의존성 추가 | 새 proxy와 ROS 메시지 변환 코드를 컴파일하기 위해 |
| `OTAClient/manifest/exec/OTAClient.json` | 고정된 `MMF_DDS_DOMAIN_ID=42` 환경 항목 제거 | 실행 스크립트와 진입점이 전달한 `MMF_DDS_DOMAIN_ID`를 OTAClient가 받도록 하기 위해 |
| `src/Ecu_Machine/cmake/BridgeCMakeLists.txt` | 브리지 이미지의 최상위 CMake 파일을 CM, EM, Service Creator, OTAClient 대상으로 구성 | 별도 DecisionAP 계획 이미지와 결합할 브리지 산출물만 빌드하기 위해 |

기존 `PPort_SCr2MP.json`, `RPort_MP2SCr.json`, `RPort_PV2SCr.json`은 기준 `oss-ap` 파일과 내용이 같다. 매니페스트 디렉터리를 통째로 교체하는 Docker 빌드에서 기존 입력 4308, route Method 1817, 최종 궤적 2317 계약을 유지하려고 복사했다.

## 추가한 포트와 매니페스트

새 R-Port 인터페이스 파일은 `overrides/Ecu_Machine/Exe_ServiceCreator/manifest/interface/Exe_ServiceCreator/AA/`에 있다. 대응 SOME/IP 머신 매니페스트는 `overrides/Ecu_Machine/CM/bridge-manifest/com/someip/`에 있다.

| 서비스 / Event | 브리지의 역할 | ROS 2 출력 |
| --- | --- | --- |
| 1814 / 10602, 10604 | DecisionAP route와 route 상태를 구독 | `/planning/mission_planning/route`, `/planning/mission_planning/route_selector/main/{route,state}` |
| 2516 / 12908 | 선택된 시나리오 궤적을 구독 | `/planning/scenario_planning/scenario_selector/trajectory` |
| 2340 / 12520 | 계획 평가 지표를 구독 | `/planning/planning_evaluator/metrics` |
| 2341 / 12521 | 남은 거리·시간을 구독 | `/planning/mission_remaining_distance_time` |
| 2342 / 12522 | 방향지시등 명령을 구독 | `/planning/turn_indicators_cmd` |
| 2343 / 12523 | 비상등 명령을 구독 | `/planning/hazard_lights_cmd` |

`CM_P_MP_ServiceCreator.json`은 입력 서비스 4308의 11개 Event를 브리지 skeleton으로 제공한다. `CM_R_Scr_missionplanner.json`과 `CM_R_Scr_planningvalidator.json`은 기존 route Method 1817과 최종 궤적 2317을, 나머지 `CM_R_*.json`은 위 출력 proxy를 브리지 머신에 매핑한다. 통합 이미지 빌드는 브리지 매니페스트와 DecisionAP 매니페스트를 합치며, 실행 진입점은 SOME/IP 주소를 실행 네트워크에 맞춘다.

## 현재 확인 범위

과거 샘플 시뮬레이션에서는 route 요청, Event 10602/10604, 최종 궤적 Event 12506의 종단 경로가 확인됐다. 2026-10-29 실행에서는 지도 Event 6001을 수신했어도 DecisionAP의 Lanelet 지도 해독이 실패해 route가 발행되지 않았다. 이 오류는 새 출력 포트의 존재만으로 해결되지 않는다. 현재 구성과 남은 인터페이스 차이는 [인터페이스 구현 상태](INTERFACE_README.md)와 [Adaptive 검증 기록](ADAPTIVE.md)에 정리돼 있다.
