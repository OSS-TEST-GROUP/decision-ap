# Decision AP 태깅 및 릴리즈 규칙

## 1. 적용 대상

- 소스 정본: `https://github.com/OSS-TEST-GROUP/decision-ap`
- 기본 브랜치: `main`
- 컨테이너 정본: GitHub Container Registry(GHCR)의 **Private** 패키지
- 대상 이미지: `ghcr.io/oss-test-group/adsw-decision-ap` (브리지와 Decision 앱을 합친 통합 실행 이미지)
- 대상 플랫폼: `linux/amd64`, `linux/arm64`

브리지 이미지 `decision-ap-bridge`와 Decision 앱 이미지 `decision-ap-planning`은 로컬 빌드 입력이다. Git 태그는 소스 릴리스를, GHCR 태그는 통합 이미지 빌드 산출물을 식별한다.

## 2. Git 릴리즈 태그

정식 릴리즈는 `vMAJOR.MINOR.PATCH` 형식의 annotated tag를 사용한다.

- 첫 공식 릴리즈 예시: `v0.1.0`
- 호환성이 깨지는 변경: MAJOR 증가
- 하위 호환 기능 추가: MINOR 증가
- 하위 호환 버그 수정: PATCH 증가
- 사전 릴리즈: `v1.2.0-rc1` 또는 `v1.2.0-beta1`

한번 원격에 push한 Git 태그는 이동하거나 덮어쓰지 않는다. 잘못 발행한 경우 다음 PATCH 버전을 발행한다. GitHub Release에는 변경 내용, 검증 결과, 스냅샷 태그와 이미지 digest를 기록한다.

## 3. 컨테이너 태그

| 종류 | 형식 | 불변성 | 용도 |
| --- | --- | --- | --- |
| 스냅샷 | `main-YYYYMMDD-<shortsha>` | 불변 | 배포, 재현, 롤백 |
| 릴리즈 | `vMAJOR.MINOR.PATCH` | 불변 | Git 릴리즈와 대응 |
| 최신 포인터 | `latest` | 이동 | 개발 PC의 최신 이미지 pull |
| 네이티브 staging | `<snapshot>-amd64`, `<snapshot>-arm64` | 불변 | 두 플랫폼 manifest 조립 입력 |

예: `ghcr.io/oss-test-group/adsw-decision-ap:main-20261030-33a32bd`. 날짜는 UTC, short SHA는 `decision-ap`의 릴리즈 커밋에서 계산한다. 운영·보드 배포에는 스냅샷, 릴리즈 태그 또는 digest를 사용하며 `latest`를 배포 기준으로 사용하지 않는다.

## 4. 릴리즈 입력 고정

릴리즈 빌드는 다음 입력을 검사한다.

- `decision-ap`의 깨끗한 `main` 커밋이 `origin/main`과 일치
- 외부 `oss-ap`가 고정 커밋 `2f649badb4a63e47e7df89c5b5b1675a70fa6ad5`에 있음
- 로컬 PARA SDK가 해당 `oss-ap`의 아키텍처별 SDK와 동일
- 네 ROS 메시지 checkout이 [`release/ros-messages.lock`](ros-messages.lock)의 전체 commit SHA와 일치하고 변경 사항이 없음
- amd64와 arm64의 스냅샷 값이 동일하고, 최종 이미지의 PARA SDK 버전 라벨이 일치

릴리즈 빌드는 ARM64 장비에서도 네이티브로 진행한다. `oss-ap.repos`의 이동 브랜치를 그대로 빌드 기준으로 쓰지 않고 실제 메시지 checkout을 lock과 대조한다. Docker base image와 apt 저장소는 digest로 고정하지 않았으므로 이 절차가 bit-for-bit 재빌드를 보장하지는 않는다. 배포된 이미지 자체는 digest로 다시 선택할 수 있다.

## 5. 릴리즈 발행 절차

아래 `v0.1.0`은 예시다. 다음 릴리즈에서는 새 버전을 Git 태그, workflow 입력, 이미지 검사 명령에 동일하게 사용한다.

### 5.1 공통 스냅샷 확정

릴리즈 변경을 `main`에 반영한 뒤 값을 한 번 계산해 두 장비에 전달한다.

```bash
git checkout main
git pull --ff-only origin main
git status --short

SOURCE_SHA=$(git rev-parse HEAD)
SNAPSHOT_TAG="main-$(date -u +%Y%m%d)-$(git rev-parse --short=7 HEAD)"
printf 'SOURCE_SHA=%s\nSNAPSHOT_TAG=%s\n' "$SOURCE_SHA" "$SNAPSHOT_TAG"
```

### 5.2 네이티브 staging 빌드

amd64 PC와 arm64 장비의 깨끗한 checkout에서 같은 `SOURCE_SHA`를 사용한다. 각 장비에 Docker Buildx, Git, vcstool, `jq`와 `oss-ap` 접근 권한이 필요하다. 토큰은 표준 입력으로 GHCR에 전달한다.

```bash
read -r -s -p 'GHCR token: ' GHCR_TOKEN; echo
printf '%s' "$GHCR_TOKEN" | docker login ghcr.io -u YOUR_GITHUB_USER --password-stdin
unset GHCR_TOKEN
./release/build-native.sh "$SNAPSHOT_TAG"
```

스크립트는 호스트 아키텍처에 따라 `<snapshot>-amd64` 또는 `<snapshot>-arm64`를 push한다. 이미지에는 `revision`, `ref.name`, `created`, `version`, `source` OCI 라벨과 `para.sdk.version` 라벨이 포함된다. 기존 staging 태그는 이미지 config digest가 같을 때만 재사용한다.

최초 staging 게시 후 조직 패키지 설정에서 `adsw-decision-ap`가 **Private**인지 확인한다. 같은 설정에서 `decision-ap` 저장소에 **Manage Actions access: Write**를 부여해야 아래 workflow의 `GITHUB_TOKEN`이 패키지를 갱신할 수 있다. GitHub의 `production` environment에는 필요한 승인 규칙을 설정한다.

### 5.3 Git 태그와 최종 manifest 발행

두 staging 이미지가 준비되면 같은 `SOURCE_SHA`에 annotated tag를 생성한다.

```bash
git fetch --tags origin
git tag -a v0.1.0 "$SOURCE_SHA" -m 'v0.1.0 Decision AP integrated image'
git push origin v0.1.0
```

GitHub Actions의 **Publish verified Decision AP integrated image** workflow를 `main`에서 수동 실행한다. 앞서 확정한 `SNAPSHOT_TAG`와 `v0.1.0`을 각각 `snapshot_tag`, `release_tag`에 입력한다.

워크플로는 다음을 확인하고 발행한다.

1. annotated Git 태그, workflow 커밋, 스냅샷 short SHA가 같은 소스를 가리킨다.
2. amd64·arm64 staging 이미지의 플랫폼, 소스·버전 OCI 라벨과 PARA SDK 버전이 맞는다.
3. 기존 스냅샷 또는 릴리즈 태그가 다른 digest를 가리키면 중단한다.
4. 두 플랫폼의 스냅샷 manifest를 만들고 릴리즈 태그와 `latest`를 게시한다.
5. 최종 manifest에 `linux/amd64`와 `linux/arm64`가 각각 하나씩 포함되는지 재검증한다.

워크플로 요약의 digest와 두 아키텍처의 실행 검증 결과를 GitHub Release에 기록한다.

## 6. 배포 확인

Private 패키지를 pull하는 장비마다 읽기 권한과 GHCR 인증이 필요하다.

```bash
docker buildx imagetools inspect \
  ghcr.io/oss-test-group/adsw-decision-ap:v0.1.0
docker pull --platform linux/amd64 \
  ghcr.io/oss-test-group/adsw-decision-ap:v0.1.0
INTEGRATED_IMAGE=ghcr.io/oss-test-group/adsw-decision-ap:v0.1.0 \
  ./docker/run-x86.sh
```

ARM64 장비에서는 같은 버전 태그를 `linux/arm64`로 pull한 뒤 `INTEGRATED_IMAGE=ghcr.io/oss-test-group/adsw-decision-ap:v0.1.0 ./docker/run-arm64-board.sh`로 확인한다. 검증한 이미지 digest, BSP·PC 아키텍처, Lane Driving 결과를 배포 기록에 남긴다.

## 7. 금지 사항

- 이미 push된 Git 태그, 스냅샷 또는 릴리즈 이미지 태그 이동
- 기능 브랜치 또는 dirty checkout에서 staging push
- 두 아키텍처에 서로 다른 스냅샷 값 사용
- lock과 다른 ROS 메시지 checkout 또는 PARA SDK로 릴리즈 빌드
- `latest`를 보드·운영 배포 기준으로 사용
- 검증 workflow를 거치지 않은 수동 manifest·릴리즈 alias 생성
- 토큰이나 registry credential을 저장소·스크립트·로그에 평문 저장
