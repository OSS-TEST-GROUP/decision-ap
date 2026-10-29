#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)"
"${repo_dir}/docker/build/prepare_oss_ap.sh"
arch="${ARCH:-$(uname -m)}"
case "${arch}" in
  x86_64) platform=linux/amd64; suffix="" ;;
  aarch64) platform=linux/arm64; suffix=-aarch64 ;;
  *) echo "Unsupported architecture: ${arch}" >&2; exit 1 ;;
esac
oss_ap_dir="${OSS_AP_SOURCE:-${repo_dir}/../oss-ap}"
sdk_dir="${PARA_SDK:-${oss_ap_dir}/sdk/${arch}}"
test -d "${sdk_dir}" || { echo "PARA SDK directory missing: ${sdk_dir}" >&2; exit 1; }
sdk_dir="$(cd "${sdk_dir}" && pwd -P)"
generated_dir="${DECISION_GENERATED:-${repo_dir}/overrides/generated/decision-ap-generated}"
test -d "${oss_ap_dir}" || { echo "Generated ARA source directory missing: ${oss_ap_dir}" >&2; exit 1; }
oss_ap_dir="$(cd "${oss_ap_dir}" && pwd -P)"
oss_ecu_dir="${oss_ap_dir}/src/Ecu_Machine"
test -f "${oss_ecu_dir}/Exe_missionplanner/include/oss/srv/MP2RS/rs_si_missionplanner_skeleton.h" || {
  echo "Service 1814 artifacts missing in ${oss_ap_dir}" >&2
  exit 1
}
test -f "${sdk_dir}/VERSION" || { echo "PARA SDK VERSION file missing: ${sdk_dir}" >&2; exit 1; }
test -f "${sdk_dir}/share/cmake/Modules/PARA/Para.cmake" || {
  echo "PARA CMake module missing: ${sdk_dir}" >&2; exit 1;
}
test -d "${generated_dir}/Exe_ServiceCreator/include/oss/srv/AA2SCrMetrics" || {
  echo "Decision AP generated ARA headers missing: ${generated_dir}" >&2; exit 1;
}
generated_dir="$(cd "${generated_dir}" && pwd -P)"
sdk_version="$(cat "${sdk_dir}/VERSION")"
case "${sdk_version}" in
  R20-11_*_native-linux-gcc) ;;
  *) echo "Unsupported PARA SDK version: ${sdk_version}" >&2; exit 1 ;;
esac
image_tag="${IMAGE_TAG:-decision-ap-planning:local${suffix}}"
docker buildx build --load --platform "${platform}" \
  --build-context "para_sdk=${sdk_dir}" \
  --build-context "oss_ap=${oss_ecu_dir}" \
  --build-context "decision_generated=${generated_dir}" \
  --build-arg "PARA_SDK_VERSION=${sdk_version}" \
  -f "${repo_dir}/docker/images/Dockerfile.planning" \
  -t "${image_tag}" "${repo_dir}"
echo "Built ${image_tag} with ${sdk_version}"
