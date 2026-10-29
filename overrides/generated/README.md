# 로컬 생성 ARA 헤더

`decision-ap-generated/Exe_ServiceCreator/include/`는 빌드 스크립트가 자동으로 준비하는 로컬 PARA 생성 헤더 위치다. 최초 실행 시 `DECISION_GENERATED_SOURCE`로 생성본의 `Ecu_Machine` 디렉터리를 지정한다. 스크립트가 ServiceCreator 헤더와 의존 타입을 복사하고 검사한다. 이후에는 준비된 로컬 파일을 재사용한다. `DECISION_GENERATED`로 다른 준비 위치를 지정할 수도 있다. 로컬 생성 파일은 Git에서 제외된다. 입력과 실행 절차는 [루트 README](../../README.md)에 있다.

현재 고정된 `oss-ap` 커밋 `2f649badb4a63e47e7df89c5b5b1675a70fa6ad5`에는 `AA2SCrMetrics`, `AA2SCrRemaining`, `AA2SCrTurn`, `AA2SCrHazard` 서비스가 없다. 해당 서비스의 proxy·skeleton과 의존 common·type 헤더를 포함한 생성 산출물이 별도로 필요하다. 두 저장소에는 이 네 서비스를 재생성할 ARXML이나 생성 스크립트가 없다.
