# Exe_ServiceCreator 소스

이 디렉터리에는 Decision AP 전용 Service Creator의 직접 수정하는 C++ 소스와 헤더를 둔다. 빌드 시 `docker/images/Dockerfile.bridge-local` 또는 `docker/images/Dockerfile.bridge-cross`가 이를 외부 `oss-ap`에서 만든 빌드 트리에 덮어 넣는다.

기본 ARA 헤더·IDL은 외부 `oss-ap`에서 읽고, 전용 생성 헤더는 로컬 `overrides/generated/decision-ap-generated`에서 읽는다. 서비스 매니페스트와 수정 소스는 이 저장소에서 관리한다. ROS 2 메시지 패키지는 `external/msgs`에 별도로 가져온다. 브리지 실행 파일은 ROS 2와 `DecisionAP`의 SOME/IP 서비스 사이를 연결한다.
