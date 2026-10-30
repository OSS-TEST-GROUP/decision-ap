# Docker 이미지 구성

사용자의 아키텍처별 빌드·실행 명령, RViz 설정, 환경변수, 로그 확인 방법은 [루트 README](../README.md)에 있다. 이 문서는 이미지가 어떻게 구성되는지 설명한다.

빌드는 `decision-ap-bridge`, `decision-ap-planning`, `decision-ap-integrated` 순서다. 브리지 이미지는 고정된 `oss-ap` 소스에 [Decision AP 변경분](../docs/OSS_AP_CHANGES.md)을 적용해 `Exe_ServiceCreator`와 OTAClient를 만든다. Decision 앱 이미지는 실행 파일 `ap-decision`을 담는다. 통합 이미지는 두 이미지의 PARA SDK 버전을 확인한 뒤 ROS 2·PARA 런타임에 Decision 앱 실행 파일과 Lanelet 라이브러리를 합친다.

GHCR 릴리스 대상은 통합 실행 이미지 `ghcr.io/oss-test-group/adsw-decision-ap`다. 브리지와 Decision 앱 이미지는 로컬 빌드에 사용한다. 일반 빌드 스크립트는 세 이미지를 로컬에서 만들고, [릴리스 절차](../release/README.md)는 amd64·arm64 통합 이미지의 staging 게시와 검증된 manifest 조립을 담당한다.

입력 준비는 공통 빌드 스크립트 `build/build_all.sh`가 맡는다. 외부 소스·SDK·생성 헤더·ROS 메시지를 준비한 뒤, Dockerfile의 `COPY` 명령으로 `overrides/`를 이미지 안의 외부 소스에 덮어쓴다. `oss-ap` 작업 트리의 소스 파일은 수정하지 않는다.

| 디렉터리 | 역할 |
| --- | --- |
| `build/` | 준비 스크립트 호출과 아키텍처별 이미지 빌드 |
| `images/` | 브리지·계획·통합 이미지 Dockerfile |
| `run/` | 세 컨테이너의 공통 실행 로직 |
| `entrypoint/` | 통합 컨테이너의 EM 시작과 실행 환경 구성 |
| `tools/` | 이미지 병합과 진단 보조 코드 |

Decision AP 컨테이너 `adsw-decision-ap`에는 EM과 CM이 각각 하나씩 있으며, `Exe_ServiceCreator`, OTAClient, `ap-decision`은 별도 프로세스로 실행된다. Perception과 Control은 각각 `adsw-perception`, `adsw-control` 컨테이너로 실행된다. ROS 도메인과 네트워크 주소는 공통 실행 로직에서 세 컨테이너에 전달된다.
