# 외부 `oss-ap` 변경분

이 디렉터리는 고정된 `oss-ap`와 다른 수정 파일 및 새로 추가한 파일만 보관한다. 원본과 동일한 파일은 중복 저장하지 않는다. 원본 `oss-ap` 작업 트리는 직접 수정하지 않으며, Docker 빌드 컨텍스트에서 파일을 덮어쓴다.

| 경로 | 내용 |
| --- | --- |
| `Ecu_Machine/Exe_ServiceCreator/` | ROS 2↔SOME/IP 브리지 소스·인터페이스 매니페스트 |
| `Ecu_Machine/OTAClient/` | OTAClient 실행 매니페스트 |
| `Ecu_Machine/CM/bridge-manifest/` | 브리지 SOME/IP 머신 매니페스트 |
| `generated/decision-ap-generated/` | 원본에 없는 생성 서비스 헤더·타입 18개. 공통 헤더는 oss-ap에서 사용 |

수정 항목별 차이와 이유는 [`oss-ap` 변경 내역](../docs/OSS_AP_CHANGES.md)에 있다. 빌드 입력 준비와 실행 절차는 [루트 README](../README.md)에 있다.
