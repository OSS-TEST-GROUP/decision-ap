#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
source "${repo_dir}/release/tagging.sh"
snapshot_tag="${1:?Usage: release/build-native.sh main-YYYYMMDD-shortsha}"
[[ $# -eq 1 ]] || { echo "Expected one snapshot tag" >&2; exit 1; }

cd "${repo_dir}"
[[ "$(git symbolic-ref --quiet --short HEAD || true)" == main ]] || {
  echo "Native release builds require the main branch" >&2; exit 1;
}
[[ -z "$(git status --porcelain)" ]] || {
  echo "Native release builds require a clean checkout" >&2; exit 1;
}
source_sha="$(git rev-parse HEAD)"
release_validate_snapshot_tag "${snapshot_tag}" main "$(git rev-parse --short=7 HEAD)"
command -v jq >/dev/null || { echo "jq is required for staging digest verification" >&2; exit 1; }
git fetch --quiet origin main
[[ "$(git rev-parse origin/main)" == "${source_sha}" ]] || {
  echo "Local HEAD must match origin/main" >&2; exit 1;
}

case "$(uname -m)" in
  x86_64) arch=x86_64; platform=amd64 ;;
  aarch64) arch=aarch64; platform=arm64 ;;
  *) echo "Native release requires an x86_64 or aarch64 host" >&2; exit 1 ;;
esac

repository=ghcr.io/oss-test-group/adsw-decision-ap
staging_ref="${repository}:${snapshot_tag}-${platform}"

# Release inputs must come from the pinned oss-ap checkout and its SDK.
for name in PARA_SDK PARA_SDK_HOST HOST_PARA_SDK PARA_SDK_VERSION ROS_MSGS_SOURCE DECISION_GENERATED DECISION_GENERATED_SOURCE OSS_AP_SOURCE OSS_AP_URL; do
  if [[ -n "${!name:-}" ]]; then
    echo "Unset ${name} for a release build" >&2
    exit 1
  fi
done

local_ref="decision-ap-integrated:release-${snapshot_tag}-${platform}"
ARCH="${arch}" OSS_AP_UPDATE=1 BRIDGE_BUILD_MODE=native \
  INTEGRATED_IMAGE="${local_ref}" RELEASE_SOURCE_SHA="${source_sha}" \
  RELEASE_SNAPSHOT_TAG="${snapshot_tag}" RELEASE_CREATED="$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
  "${repo_dir}/docker/build/build_all.sh"

[[ "$(docker image inspect --format '{{.Architecture}}' "${local_ref}")" == "${platform}" ]] || {
  echo "Built image architecture mismatch" >&2; exit 1;
}
[[ "$(docker image inspect --format '{{index .Config.Labels "org.opencontainers.image.revision"}}' "${local_ref}")" == "${source_sha}" ]] || {
  echo "Built image revision label mismatch" >&2; exit 1;
}
local_config="$(docker image inspect --format '{{.Id}}' "${local_ref}")"
if remote_manifest="$(docker buildx imagetools inspect "${staging_ref}" --raw 2>/dev/null)"; then
  remote_config="$(jq -er '.config.digest' <<<"${remote_manifest}")"
  [[ "${local_config}" == "${remote_config}" ]] || {
    echo "Refusing to overwrite staging image with a different config: ${staging_ref}" >&2
    exit 1
  }
  echo "Verified existing staging image ${staging_ref} (${local_config})"
else
  docker tag "${local_ref}" "${staging_ref}"
  docker push "${staging_ref}"
fi
docker buildx imagetools inspect "${staging_ref}"
