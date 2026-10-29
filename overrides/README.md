# 외부 `oss-ap` 변경분

이 디렉터리는 고정된 `oss-ap` 소스에 적용하는 Decision AP 전용 파일을 보관한다. 원본 `oss-ap` 작업 트리는 직접 수정하지 않으며, Docker 빌드 컨텍스트에서 파일을 덮어쓴다.

| 경로 | 내용 |
| --- | --- |
| `Ecu_Machine/Exe_ServiceCreator/` | ROS 2↔SOME/IP 브리지 소스·인터페이스 매니페스트 |
| `Ecu_Machine/OTAClient/` | OTAClient 실행 매니페스트 |
| `Ecu_Machine/CM/bridge-manifest/` | 브리지 SOME/IP 머신 매니페스트 |
| `generated/decision-ap-generated/` | 로컬 PARA 생성 ARA 헤더; Git 제외 |

수정 항목별 차이와 이유는 [`oss-ap` 변경 내역](../docs/OSS_AP_CHANGES.md)에 있다. 빌드 입력 준비와 실행 절차는 [루트 README](../README.md)에 있다.
