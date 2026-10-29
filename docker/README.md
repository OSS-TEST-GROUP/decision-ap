# Docker 이미지 구성

사용자의 아키텍처별 빌드·실행 명령, RViz 설정, 환경변수, 로그 확인 방법은 [루트 README](../README.md)에 있다. 이 문서는 이미지가 어떻게 구성되는지 설명한다.

빌드는 `decision-ap-bridge`, `decision-ap-planning`, `decision-ap-integrated` 순서다. 브리지 이미지는 고정된 `oss-ap` 소스에 [Decision AP 변경분](../docs/OSS_AP_CHANGES.md)을 적용해 `Exe_ServiceCreator`와 OTAClient를 만든다. 계획 이미지는 `ap-decision`을 만든다. 통합 이미지는 두 이미지의 PARA SDK 버전을 확인한 뒤 ROS 2·PARA 런타임에 계획 실행 파일과 Lanelet 라이브러리를 합친다.

| 디렉터리 | 역할 |
| --- | --- |
| `build/` | 외부 소스 준비와 아키텍처별 이미지 빌드 단계 |
| `images/` | 브리지·계획·통합 이미지 Dockerfile |
| `run/` | 세 컨테이너의 공통 실행 로직 |
| `entrypoint/` | 통합 컨테이너의 EM 시작과 실행 환경 구성 |
| `tools/` | 이미지 병합과 진단 보조 코드 |

통합 컨테이너에는 EM과 CM이 각각 하나씩 있으며, `Exe_ServiceCreator`, OTAClient, `ap-decision`은 별도 프로세스로 실행된다. Perception과 Control은 각각 `katech-perception`, `katech-control` 컨테이너로 실행된다. ROS 도메인과 네트워크 주소는 공통 실행 로직에서 세 컨테이너에 전달된다.
