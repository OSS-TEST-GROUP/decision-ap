# 생성 ARA 헤더

`decision-ap-generated/Exe_ServiceCreator/include/`에는 기준 `oss-ap`에 없는 생성 ARA 헤더·타입 18개만 포함돼 있다. 원본과 동일한 공통 헤더 및 수기 Service Creator 헤더는 포함하지 않는다. 생성 파일은 Git으로 관리하며, 빌드 스크립트는 `oss-ap` 원본과 이 변경분을 임시 디렉터리에 합쳐 의존성을 검사한 뒤 사용한다. 검사는 원본과 변경분 디렉터리에 파일을 추가하거나 수정하지 않는다. 일반 빌드에는 `DECISION_GENERATED_SOURCE` 지정이나 ARXML·코드 생성 도구가 필요하지 않다.

현재 고정된 `oss-ap` 커밋 `2f649badb4a63e47e7df89c5b5b1675a70fa6ad5`에 없는 `AA2SCrMetrics`, `AA2SCrRemaining`, `AA2SCrTurn`, `AA2SCrHazard` 서비스의 proxy·skeleton·common 헤더와 의존 타입도 이 디렉터리에 포함한다.

다른 생성본은 `DECISION_GENERATED`로 `Exe_ServiceCreator/include/`를 포함한 디렉터리를 지정해 사용할 수 있다. ARXML과 재생성 스크립트는 포함돼 있지 않으므로, 통신 인터페이스 변경 시에는 별도의 생성 환경이 필요하다.

빌드와 실행 절차는 [루트 README](../../README.md)에 있다.
