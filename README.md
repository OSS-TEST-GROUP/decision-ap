# Decision AP

Adaptive AUTOSAR 기반 Autoware Planning을 위한 Decision AP 파티션이다. `ap-decision`이 경로·행동·속도 계획과 최종 궤적을 만들고, `Exe_ServiceCreator`가 ROS 2와 Adaptive SOME/IP 서비스를 연결한다. Perception, Control, Decision AP를 각각 컨테이너로 실행한다.

## 1. 준비

| 항목 | 필요한 것 |
| --- | --- |
| 호스트 | Linux x86_64 PC 또는 ARM64 보드, Docker 데몬과 Docker Buildx |
| PARA SDK | 기본 위치 `../oss-ap/sdk/x86_64/` 또는 `../oss-ap/sdk/aarch64/` |
| 생성 ARA 헤더 | `../oss-ap/src/Ecu_Machine/`의 기본 생성물과 `overrides/generated/decision-ap-generated/`의 Decision AP 전용 생성물 |
| 외부 Adaptive 소스 | 기본 위치 `../oss-ap/` |
| ROS 2 메시지 소스 | `external/msgs/`; 없으면 빌드 스크립트가 `oss-ap.repos`로 가져옴 (`vcs` 필요) |
| 샘플 지도 | 이 저장소의 `map/sample-map-decision/`에 포함 |

샘플 지도는 이 저장소에 포함된다. PARA SDK와 Decision AP 전용 생성 ARA 헤더는 Git에 포함되지 않는다. `../oss-ap`에는 기존 ARA 생성물이 있지만, 새 출력 서비스에 필요한 `AA2SCrMetrics`·`AA2SCrTurn` 등의 전용 생성물은 없다. 따라서 전용 생성 헤더는 별도로 준비해야 한다. `oss-ap`가 없으면 빌드 스크립트가 기본 SSH 주소에서 복제하므로 접근 권한이 필요하다. 빌드 스크립트는 `oss-ap`를 고정 커밋 `2f649badb4a63e47e7df89c5b5b1675a70fa6ad5`로 맞춘다. 기존 작업 트리에 변경이 있으면 자동 갱신이 중단된다. `oss-ap`를 새로 복제해도 Git에서 제외된 PARA SDK는 함께 내려받아지지 않는다.

입력 위치를 바꾸려면 `PARA_SDK`, `DECISION_GENERATED`, `OSS_AP_SOURCE`를 지정한다. `OSS_AP_SOURCE`를 바꾸면 SDK 기본 경로도 해당 저장소의 `sdk/<아키텍처>/`로 바뀐다. ROS 2 메시지는 준비 스크립트가 확인하는 `external/msgs/`에 둔다. 인터넷 없이 이미 준비한 소스로 빌드하려면 명령 앞에 `OSS_AP_UPDATE=0`을 붙인다. 이 경우 `oss-ap`와 `external/msgs/`를 먼저 준비해야 한다.

ROS 2 메시지를 수동으로 가져올 때는 다음 명령을 사용한다.

```sh
vcs import external < oss-ap.repos
```

모든 명령은 저장소 루트에서 실행한다.

## 2. 아키텍처별 빌드와 실행

| 실행 위치 | 이미지 빌드 | 스택 실행 | RViz |
| --- | --- | --- | --- |
| x86_64 PC | `./docker/build-x86.sh` | `./docker/run-x86.sh` | Perception 컨테이너에서 PC 화면에 표시 |
| ARM64 보드 | `./docker/build-arm64.sh` | `KATECH_NETWORK_INTERFACE=eth0 ./docker/run-arm64-board.sh` | 외부 x86_64 PC에서 별도 실행 |
| x86_64 PC에서 ARM64 QEMU 시험 | `./docker/build-arm64.sh` | `./docker/run-arm64-qemu.sh` | ARM64 Perception 컨테이너에서 PC 화면에 표시 |

`eth0`는 보드의 실제 LAN 인터페이스 이름으로 바꾼다. `ip -br address`로 확인할 수 있다. x86_64 PC에서 ARM64 이미지를 빌드·실행하려면 x86_64와 ARM64 PARA SDK 및 Docker의 ARM64 binfmt/QEMU 지원이 필요하다. 이 빌드는 브리지를 ARM64 sysroot로 크로스 컴파일하고 계획 앱을 QEMU에서 빌드한다. `BRIDGE_BUILD_MODE=native`를 지정하면 브리지도 QEMU에서 빌드한다.

빌드는 브리지, 계획 앱, 통합 이미지를 차례로 만든다. 최종 이미지 태그는 x86_64에서 `decision-ap-integrated:local`, ARM64에서 `decision-ap-integrated:local-aarch64`다. x86_64 예시는 다음과 같다.

```sh
./docker/build-x86.sh
./docker/run-x86.sh
```

실행에는 통합 Decision AP 이미지가 로컬 Docker에 있어야 한다. Perception·Control의 기본 태그가 없으면 실행 스크립트가 `ghcr.io/oss-test-group/autoware-partition`에서 해당 아키텍처의 이미지를 내려받아 로컬 태그를 붙인다. 다른 이미지 이름을 지정한 경우에는 그 이미지를 직접 준비해야 한다. 스택은 기본적으로 `ROS_DOMAIN_ID=42`, 계획 시뮬레이션 모드 `PLANNING_AA_SIMULATION=1`로 실행된다. 실행 터미널에서 `Ctrl+C`를 누르면 스택이 종료되고 로그는 남는다.

Perception·Control 이미지를 KATECH 소스에서 직접 만들 때는 해당 저장소의 빌드 환경을 준비한 뒤 아래 명령을 사용한다. 생성되는 이미지의 아키텍처는 빌드 호스트와 해당 스크립트 설정을 확인한다.

```sh
cd ../katech-autoware/katech-autosare-new
./partition/partition_build.sh --repo katech-autoware-system-control --no-cuda --skip-vcs-update
cd ../../decision-ap
```

### ARM64 보드와 외부 PC RViz

보드와 x86_64 PC를 서로 통신 가능한 같은 LAN에 연결한다. 보드에서는 실제 인터페이스를 지정해 스택을 시작한다.

```sh
KATECH_NETWORK_INTERFACE=eth0 ./docker/run-arm64-board.sh
```

보드는 host 네트워크를 사용하며 RViz 창을 열지 않는다. 외부 PC에는 Docker, X11 데스크톱, x86_64 Perception 이미지가 필요하다. 원격 RViz 실행 스크립트는 이미지를 자동으로 내려받지 않으므로 PC의 Docker에 `katech-autoware-system-control:adsw-perception` 태그가 없다면 먼저 준비한다.

```sh
docker pull --platform linux/amd64 ghcr.io/oss-test-group/autoware-partition:adsw-perception
docker tag ghcr.io/oss-test-group/autoware-partition:adsw-perception katech-autoware-system-control:adsw-perception
KATECH_NETWORK_INTERFACE=enp3s0 ./docker/run-remote-rviz.sh
```

`enp3s0`는 PC의 LAN 인터페이스 이름으로 바꾼다. 보드와 PC의 `ROS_DOMAIN_ID`를 같게 설정한다. RViz에서 map·TF·route·trajectory가 보이지 않으면 양쪽 인터페이스 설정과 LAN의 UDP multicast·방화벽을 확인한다. 외부 PC에서 ROS 2나 Autoware를 별도로 빌드할 필요는 없다.

### x86_64 PC의 ARM64 QEMU 시험

```sh
./docker/build-arm64.sh
./docker/run-arm64-qemu.sh
```

QEMU에서는 RViz와 계획 계산이 느릴 수 있다. 실행 스크립트는 이 시뮬레이션 조합에서 Control의 궤적 진단 기준을 완화한 사본을 실행 로그 아래 `qemu-config/`에 만든다. 기본 진단을 사용하려면 `KATECH_QEMU_DIAGNOSTICS=0`을 지정한다.

## 3. RViz 설정과 경로 요청

x86_64 PC 실행과 QEMU 실행은 PC의 `DISPLAY` 및 X11 소켓 `/tmp/.X11-unix`가 필요하다. 외부 PC RViz도 X11 데스크톱 터미널에서 시작한다. RViz 실행 파일과 기본 화면 구성은 Perception 이미지에 포함돼 있다. RViz의 Fixed Frame이 `map`인지 확인한다.

1. 지도와 TF가 표시될 때까지 기다린다.
2. `2D Pose Estimate`로 차량 초기 위치와 방향을 지정한다. 샘플 시뮬레이터의 `waiting initialization...` 상태는 유효한 초기 위치를 입력해야 끝난다.
3. `2D Goal Pose`로 목표 위치와 방향을 지정한다.
4. route와 `/planning/scenario_planning/trajectory`가 발행되는지 확인한다. 제어 궤적은 국소 구간만 표시하므로 목표 지점보다 앞에서 끝나 보일 수 있다.
5. 시뮬레이션 주행이 필요하면 route와 trajectory가 생성된 뒤 RViz의 Autonomous 모드를 켠다.

샘플 지도에서 검증에 사용한 좌표는 초기 위치 `(3648.46, 73505.0)`, 목표 `(3632.5, 73628.0)`이다. 정적 보행자 회피 사례를 재현할 때는 RViz의 `2D Dummy Pedestrian`으로 `(3645.6, 73545.0)` 근처에 보행자를 둔다. 이 좌표는 샘플 지도 전용이며 초기 방향과 보행자 위치에 따라 결과가 달라진다.

route는 `/planning/mission_planning/route`, 제어 궤적은 `/planning/scenario_planning/trajectory`로 발행된다. `ros/goal_pose_visualizer.py`는 route의 목표 자세만 `/planning/mission_planning/echo_back_goal_pose`로 재발행한다. 경로 전체를 `/planning/mission_planning/route_marker`에 그리던 원본 UDP Marker 브리지는 현재 실행 구성에 포함돼 있지 않다. RViz에서 MarkerArray를 추가해도 이 토픽의 발행자는 생기지 않는다.

## 4. 설정 변경

환경변수를 실행 명령 앞에 붙인다. 예: `KATECH_MAP_PATH=/data/map ./docker/run-x86.sh`.

| 변수 | 용도 |
| --- | --- |
| `KATECH_MAP_PATH` | 샘플 지도 대신 사용할 지도 디렉터리 |
| `KATECH_PERCEPTION_IMAGE`, `KATECH_CONTROL_IMAGE` | Perception·Control 이미지 태그 |
| `INTEGRATED_IMAGE` | 통합 Decision AP 이미지 태그 |
| `KATECH_RVIZ_IMAGE` | 외부 PC RViz용 x86_64 Perception 이미지 태그 |
| `ROS_DOMAIN_ID` | ROS 2 도메인; 기본값 `42` |
| `MMF_DDS_DOMAIN_ID` | OTAClient가 사용하는 DDS 도메인; 기본값 `42` |
| `PLANNING_AA_SIMULATION` | `1`: 샘플 시뮬레이션, `0`: 실제 신호 입력 사용 |
| `PLANNING_AA_STATIC_MAP` | `1`: 첫 지도만 해독, `0`: 실행 중 지도 변경 허용 |
| `KATECH_LOG_DIR`, `KATECH_LOG_WINDOWS` | 로그 저장 위치와 데스크톱 로그 창 표시 |
| `PLANNING_AA_SHM_SIZE` | 통합 컨테이너 공유 메모리 크기; 기본 `2g` |
| `PLANNING_AA_AVOIDANCE_DEBUG` | `1`: 회피 대상 선택·거부 단계를 로그에 기록 |

보드용 `KATECH_NETWORK_INTERFACE`는 필수다. `PLANNING_AA_SIMULATION=0`에서는 신호 인식 메시지가 없거나 오래되면 신호등 앞에서 정지할 수 있다. 외부 이미지 태그를 지정하면 자동 GHCR 다운로드 대상에서 제외되므로 해당 이미지를 미리 준비한다.

## 5. 로그와 문제 해결

실행 화면의 `Logs:` 줄이 해당 실행의 로그 디렉터리다. 기본 위치는 `output/katech-logs/<실행 시각>/`이다. 실시간으로 보려면 다른 터미널에서 `tail -F <로그 디렉터리>/*.log`를 실행한다.

| 파일 | 확인할 내용 |
| --- | --- |
| `decision-ap.log` | 지도 해독, route 요청·응답, 계획·궤적 송신 |
| `katech-perception.log` | 지도 로딩, RViz 초기 위치·목표 입력 |
| `katech-control.log` | Control 노드와 토픽 상태 |
| `trajectory-diagnostic.log` | Control에서 본 route·trajectory 수신과 궤적 샘플 |
| `validator-diagnostic-bridge.log` | 계획 검증 진단 어댑터 |
| `goal-pose-visualizer.log` | route 목표 자세 재발행 노드 |

- **route가 생성되지 않음:** `decision-ap.log`에서 `PLAA_MAP ready` 또는 `PLAA_MAP decode_failed`를 확인한다. 지도 해독 실패 시 요청에 `60001: The mission planner is not ready`가 반환될 수 있다. 2026-10-29 실행에서 이 오류가 기록됐고 이미지 태그 변경 전후에 모두 나타났다.
- **지도나 RViz가 보이지 않음:** `katech-perception.log`의 지도 로딩·`rviz2` 오류, X11 설정, 보드와 PC의 도메인·인터페이스·multicast를 확인한다.
- **trajectory가 보이지 않음:** `decision-ap.log`의 `Published final trajectory Event 12506`과 `trajectory-diagnostic.log`의 Control 수신 기록을 비교한다. `Planning services active`만으로 궤적 송신을 확인할 수 없다.
- **보드에서 ROS 시각 오류:** 시스템 시계가 2038년 이후로 잘못 설정되면 ROS 2 launch의 `sec` 범위 오류가 날 수 있다. `date`, `timedatectl status`, `sudo hwclock -r`로 시간을 확인한다.
- **컨테이너가 시작 직후 종료됨:** 해당 실행의 `katech-perception.log`, `katech-control.log`, `decision-ap.log`에서 첫 오류를 확인한다. 같은 이름의 컨테이너가 이미 실행 중이면 이전 스택을 종료한 뒤 다시 시작한다.

## Docker 없이 컴파일 확인

이미지 생성 없이 DecisionAP의 컴파일·설치만 확인하려면 PARA SDK, 생성 ARA 헤더와 함께 CMake, Ninja, C++17 컴파일러, Eigen 3, Boost 헤더·serialization 라이브러리가 필요하다.

```sh
cmake -S src/Ecu_Machine -B build-adaptive -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$(pwd)/output/adaptive"
cmake --build build-adaptive -j 4
cmake --install build-adaptive --component Standalone
```

## 범위와 기술 문서

이 저장소는 차로 주행용 Planning을 대상으로 한다. 주차 기능, 원본 Decision 전체와의 동등성, 실차 주행은 완료 범위에 포함되지 않는다. SDK와 생성 파일을 포함한 실행 이미지는 해당 라이선스 조건을 확인한 뒤 배포해야 한다. 과거 검증용 코드와 기준 데이터는 이 저장소에 없으므로 과거 수치 결과는 이 저장소만으로 재현할 수 없다.

- [Adaptive 구성과 검증 기록](docs/ADAPTIVE.md)
- [인터페이스 구현 상태](docs/INTERFACE_README.md)
- [Docker 이미지 구성](docker/README.md)
- [외부 `oss-ap` 수정 내용과 이유](docs/OSS_AP_CHANGES.md)
