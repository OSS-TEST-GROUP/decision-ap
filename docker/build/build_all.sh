#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)"
arch="${ARCH:-$(uname -m)}"
host_arch="$(uname -m)"
case "${arch}" in
  x86_64) platform=linux/amd64; docker_arch=amd64; suffix=""; build_entry=build-x86.sh ;;
  aarch64) platform=linux/arm64; docker_arch=arm64; suffix=-aarch64; build_entry=build-arm64.sh ;;
  *) echo "Unsupported architecture: ${arch}" >&2; exit 1 ;;
esac
fail() { echo "Error: $*" >&2; exit 1; }
absolute_dir() { (cd "$1" && pwd -P); }

sdk="${PARA_SDK:-${repo_dir}/sdk/${arch}}"
generated="${DECISION_GENERATED:-${repo_dir}/overrides/generated/decision-ap-generated}"
oss_ap="${OSS_AP_SOURCE:-${repo_dir}/../oss-ap}"
msgs="${repo_dir}/external/msgs"
mode="${BRIDGE_BUILD_MODE:-auto}"
case "${mode}" in
  auto)
    mode=native
    if [[ "${host_arch}" == x86_64 && "${arch}" == aarch64 ]]; then mode=cross; fi
    ;;
  native|cross) ;;
  *) fail "BRIDGE_BUILD_MODE must be auto, native, or cross" ;;
esac
if [[ "${mode}" == cross && ( "${host_arch}" != x86_64 || "${arch}" != aarch64 ) ]]; then
  fail "Cross bridge builds require an x86_64 host and ARCH=aarch64"
fi

# Prepare local inputs before starting Docker. Existing inputs are preserved.
update="${OSS_AP_UPDATE:-1}"
case "${update}" in 0|1) ;; *) fail "OSS_AP_UPDATE must be 0 or 1" ;; esac
commit=2f649badb4a63e47e7df89c5b5b1675a70fa6ad5
command -v python3 >/dev/null || fail "Python 3 is required"
if [[ "${update}" == 1 ]]; then
  command -v git >/dev/null || fail "Git is required"
  if [[ ! -e "${oss_ap}" ]]; then
    git clone "${OSS_AP_URL:-git@github.com:OSS-TEST-GROUP/oss-ap.git}" "${oss_ap}"
  fi
  git -C "${oss_ap}" rev-parse --is-inside-work-tree >/dev/null || fail "Not a Git checkout: ${oss_ap}"
  [[ -z "$(git -C "${oss_ap}" status --porcelain)" ]] || fail "oss-ap has local changes: ${oss_ap}; use OSS_AP_UPDATE=0 to keep them"
  if ! git -C "${oss_ap}" cat-file -e "${commit}^{commit}" 2>/dev/null; then
    git -C "${oss_ap}" fetch origin "${commit}"
  fi
  git -C "${oss_ap}" checkout --detach "${commit}"
fi
[[ -d "${oss_ap}/src/Ecu_Machine" ]] || fail "External source missing: ${oss_ap}/src/Ecu_Machine"
oss_ap="$(cd "${oss_ap}" && pwd -P)"

prepare_sdk() {
  local sdk_arch="$1" destination="$2" source="${oss_ap}/sdk/$1"
  if [[ ! -e "${destination}" ]]; then
    [[ -f "${source}/share/cmake/Modules/PARA/Para.cmake" ]] || fail "SDK missing: ${source}; set PARA_SDK or PARA_SDK_HOST"
    mkdir -p "$(dirname "${destination}")"
    cp -a "${source}" "${destination}"
    echo "Copied ${sdk_arch} SDK to ${destination}"
  fi
  [[ -f "${destination}/share/cmake/Modules/PARA/Para.cmake" ]] || fail "Invalid SDK: ${destination}"
}
prepare_sdk "${arch}" "${sdk}"
# OTAClient needs the SDK's matching compiler, backend, and runtime libraries.
# A system-wide PARA_SDK may point to an older installation without these tools.
required_sdk_files=(
  share/cyclonedds/lib/libcycloneddsidlcxx.so
  share/cyclonedds/lib/cmake/CycloneDDS/idlc/Generate.cmake
  external/lib/libddsc.so
  external/lib/libddscxx.so
)
for sdk_file in "${required_sdk_files[@]}"; do
  [[ -f "${sdk}/${sdk_file}" ]] || fail "Incomplete PARA SDK: ${sdk}/${sdk_file} missing. To use the SDK bundled with oss-ap, run: env -u PARA_SDK ./docker/${build_entry}"
done
if [[ "${mode}" == native ]]; then
  [[ -x "${sdk}/share/cyclonedds/bin/idlc" ]] || fail "SDK idlc missing or not executable: ${sdk}/share/cyclonedds/bin/idlc. Run: env -u PARA_SDK ./docker/${build_entry}"
fi
if [[ "${mode}" == cross ]]; then
  host_sdk="${PARA_SDK_HOST:-${HOST_PARA_SDK:-${repo_dir}/sdk/x86_64}}"
  prepare_sdk x86_64 "${host_sdk}"
  [[ -x "${host_sdk}/share/cyclonedds/bin/idlc" ]] || fail "Host SDK idlc missing: ${host_sdk}"
  [[ -f "${host_sdk}/share/cyclonedds/lib/libcycloneddsidlcxx.so" ]] || fail "Host SDK CycloneDDS backend missing: ${host_sdk}"
  if [[ -f "${host_sdk}/VERSION" && -f "${sdk}/VERSION" ]]; then
    [[ "$(cat "${host_sdk}/VERSION")" == "$(cat "${sdk}/VERSION")" ]] || fail "Host and target PARA SDK versions must match"
  fi
fi

python3 "${repo_dir}/docker/tools/prepare-generated.py" \
  "${DECISION_GENERATED_SOURCE:-${oss_ap}/src/Ecu_Machine}" \
  "${generated}"

packages=(autoware_msgs autoware_adapi_msgs autoware_internal_msgs tier4_autoware_msgs)
messages_ready() {
  local package
  for package in "${packages[@]}"; do
    [[ -d "${msgs}/${package}" ]] || return 1
    [[ -n "$(find "${msgs}/${package}" -name package.xml -print -quit)" ]] || return 1
  done
}
if [[ -n "${ROS_MSGS_SOURCE:-}" ]]; then
  for package in "${packages[@]}"; do
    if [[ ! -e "${msgs}/${package}" ]]; then
      [[ -d "${ROS_MSGS_SOURCE}/${package}" ]] || fail "ROS message source missing: ${ROS_MSGS_SOURCE}/${package}"
      mkdir -p "${msgs}"
      cp -a "${ROS_MSGS_SOURCE}/${package}" "${msgs}/${package}"
    fi
  done
fi
if ! messages_ready; then
  [[ "${update}" == 1 ]] || fail "ROS message sources missing: ${msgs}; set ROS_MSGS_SOURCE or import ${oss_ap}/oss-ap.repos online"
  command -v vcs >/dev/null || fail "vcstool is required to import ${oss_ap}/oss-ap.repos; alternatively set ROS_MSGS_SOURCE"
  [[ -f "${oss_ap}/oss-ap.repos" ]] || fail "ROS message manifest missing: ${oss_ap}/oss-ap.repos"
  mkdir -p "${repo_dir}/external"
  vcs import "${repo_dir}/external" < "${oss_ap}/oss-ap.repos"
  messages_ready || fail "ROS message import did not create all required packages"
fi
if [[ -n "${RELEASE_SNAPSHOT_TAG:-}" ]]; then
  # Normal builds may reuse local inputs; a release must match its locked sources.
  diff -qr "${oss_ap}/sdk/${arch}" "${sdk}" >/dev/null || fail "Local PARA SDK differs from pinned oss-ap SDK"
  lock_count=0
  while read -r package expected_sha; do
    [[ -z "${package}" || "${package}" == \#* ]] && continue
    [[ "${expected_sha}" =~ ^[0-9a-f]{40}$ ]] || fail "Invalid ROS lock entry: ${package}"
    package_dir="${msgs}/${package}"
    [[ -d "${package_dir}/.git" ]] || fail "Locked ROS checkout missing: ${package_dir}"
    [[ "$(git -C "${package_dir}" rev-parse HEAD)" == "${expected_sha}" ]] || fail "ROS checkout commit differs from lock: ${package}"
    [[ -z "$(git -C "${package_dir}" status --porcelain)" ]] || fail "ROS checkout is dirty: ${package}"
    lock_count=$((lock_count + 1))
  done < "${repo_dir}/release/ros-messages.lock"
  [[ "${lock_count}" -eq "${#packages[@]}" ]] || fail "ROS lock must contain ${#packages[@]} repositories"
fi
echo "Prepared ${arch} inputs. Starting Docker image builds."

[[ -f "${sdk}/share/cmake/Modules/PARA/Para.cmake" ]] || fail "PARA SDK missing: ${sdk}; set PARA_SDK"
sdk="$(absolute_dir "${sdk}")"
if [[ "${mode}" == cross ]]; then
  host_sdk="${PARA_SDK_HOST:-${HOST_PARA_SDK:-${repo_dir}/sdk/x86_64}}"
  [[ -x "${host_sdk}/share/cyclonedds/bin/idlc" ]] || fail "Host SDK idlc missing: ${host_sdk}; set PARA_SDK_HOST"
  [[ -f "${host_sdk}/share/cyclonedds/lib/libcycloneddsidlcxx.so" ]] || fail "Host SDK CycloneDDS backend missing: ${host_sdk}"
  host_sdk="$(absolute_dir "${host_sdk}")"
fi
command -v docker >/dev/null || fail "Docker is required"
docker buildx version >/dev/null || fail "Docker Buildx is required"
docker info >/dev/null || fail "Docker daemon is unavailable"

oss_ap="$(absolute_dir "${oss_ap}")"
generated="$(absolute_dir "${generated}")"

# Both component images use the same target SDK and carry its identity label.
if [[ -n "${PARA_SDK_VERSION:-}" ]]; then
  sdk_version="${PARA_SDK_VERSION}"
elif [[ -f "${sdk}/VERSION" ]]; then
  sdk_version="$(cat "${sdk}/VERSION")"
else
  sdk_version="$(sha256sum "${sdk}/share/cmake/Modules/PARA/Para.cmake" | cut -d ' ' -f 1)"
fi
bridge_image="${BRIDGE_IMAGE:-decision-ap-bridge:local${suffix}}"
planning_image="${PLANNING_IMAGE:-${IMAGE_TAG:-decision-ap-planning:local${suffix}}}"
integrated_image="${INTEGRATED_IMAGE:-decision-ap-integrated:local${suffix}}"
common=(--builder default --platform "${platform}" --load --build-arg "PARA_SDK_VERSION=${sdk_version}"
  --build-context "para_sdk=${sdk}" --build-context "oss_ap=${oss_ap}/src/Ecu_Machine"
  --build-context "decision_generated=${generated}")
bridge_args=(--build-context "bridge_msgs=${msgs}")
bridge_file=bridge-local
if [[ "${mode}" == cross ]]; then
  bridge_file=bridge-cross
  bridge_args+=(--build-context "host_sdk=${host_sdk}")
fi
echo "Building ${platform} images (bridge: ${mode})"
docker buildx build "${common[@]}" "${bridge_args[@]}" \
  -f "${repo_dir}/docker/images/Dockerfile.${bridge_file}" -t "${bridge_image}" "${repo_dir}"
docker buildx build "${common[@]}" \
  -f "${repo_dir}/docker/images/Dockerfile.planning" -t "${planning_image}" "${repo_dir}"
for image in "${bridge_image}" "${planning_image}"; do
  image_arch="$(docker image inspect --format '{{.Architecture}}' "${image}")"
  [[ "${image_arch}" == "${docker_arch}" ]] || fail "Image architecture mismatch: ${image} is ${image_arch}, expected ${docker_arch}"
  version="$(docker image inspect --format '{{index .Config.Labels "para.sdk.version"}}' "${image}")"
  [[ "${version}" == "${sdk_version}" ]] || fail "PARA SDK label mismatch in ${image}"
done
# The Docker driver can resolve the component images loaded above locally.
release_labels=()
release_build_options=()
if [[ -n "${RELEASE_SNAPSHOT_TAG:-}" ]]; then
  [[ -n "${RELEASE_SOURCE_SHA:-}" && -n "${RELEASE_CREATED:-}" ]] || fail "Release labels require RELEASE_SOURCE_SHA and RELEASE_CREATED"
  release_labels=(
    --label "org.opencontainers.image.source=https://github.com/OSS-TEST-GROUP/decision-ap"
    --label "org.opencontainers.image.revision=${RELEASE_SOURCE_SHA}"
    --label "org.opencontainers.image.ref.name=main"
    --label "org.opencontainers.image.version=${RELEASE_SNAPSHOT_TAG}"
    --label "org.opencontainers.image.created=${RELEASE_CREATED}"
  )
  release_build_options=(--provenance=false)
fi
docker buildx build --builder default --platform "${platform}" --load \
  "${release_build_options[@]}" "${release_labels[@]}" \
  --build-arg "BRIDGE_IMAGE=${bridge_image}" --build-arg "PLANNING_IMAGE=${planning_image}" \
  -f "${repo_dir}/docker/images/Dockerfile.integrated" -t "${integrated_image}" "${repo_dir}"
echo "Built ${integrated_image}"
