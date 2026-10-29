#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)"
arch="${ARCH:-$(uname -m)}"
case "${arch}" in
  x86_64) suffix="" ;;
  aarch64) suffix=-aarch64 ;;
  *) echo "Unsupported architecture: ${arch}" >&2; exit 1 ;;
esac
bridge_image="${BRIDGE_IMAGE:-decision-ap-bridge:local${suffix}}"
planning_image="${PLANNING_IMAGE:-${IMAGE_TAG:-decision-ap-planning:local${suffix}}}"
integrated_image="${INTEGRATED_IMAGE:-decision-ap-integrated:local${suffix}}"

"${repo_dir}/docker/build/prepare_oss_ap.sh"
OSS_AP_UPDATE=0 BRIDGE_IMAGE="${bridge_image}" "${repo_dir}/docker/build/build_bridge.sh"
OSS_AP_UPDATE=0 IMAGE_TAG="${planning_image}" "${repo_dir}/docker/build/build_planning.sh"
BRIDGE_IMAGE="${bridge_image}" PLANNING_IMAGE="${planning_image}" \
  INTEGRATED_IMAGE="${integrated_image}" "${repo_dir}/docker/build/build_integrated.sh"
