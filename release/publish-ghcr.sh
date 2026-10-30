#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
source "${repo_dir}/release/tagging.sh"
cd "${repo_dir}"

snapshot_tag="${SNAPSHOT_TAG:?SNAPSHOT_TAG is required}"
release_tag="${RELEASE_TAG:?RELEASE_TAG is required}"
repository=ghcr.io/oss-test-group/adsw-decision-ap
[[ "${GITHUB_REF_NAME:-$(git symbolic-ref --quiet --short HEAD || true)}" == main ]] || {
  echo "Publish must run from main" >&2; exit 1;
}
source_sha="$(git rev-parse HEAD)"
release_validate_snapshot_tag "${snapshot_tag}" main "$(git rev-parse --short=7 HEAD)"
release_validate_semver_tag "${release_tag}"

git fetch --quiet --tags origin
[[ "$(git cat-file -t "refs/tags/${release_tag}")" == tag ]] || {
  echo "${release_tag} must be an annotated Git tag" >&2; exit 1;
}
[[ "$(git rev-parse "${release_tag}^{commit}")" == "${source_sha}" ]] || {
  echo "Git release tag does not point to the workflow commit" >&2; exit 1;
}

image_json() { docker buildx imagetools inspect "$1" --format '{{json .Image}}'; }
manifest_json() { docker buildx imagetools inspect "$1" --format '{{json .Manifest}}'; }

declare -A staging_digest
sdk_version=""
for arch in amd64 arm64; do
  ref="${repository}:${snapshot_tag}-${arch}"
  image="$(image_json "${ref}")"
  manifest="$(manifest_json "${ref}")"
  jq -e --arg arch "${arch}" --arg sha "${source_sha}" \
    --arg snapshot "${snapshot_tag}" \
    '.architecture == $arch and .os == "linux" and
     .config.Labels["org.opencontainers.image.revision"] == $sha and
     .config.Labels["org.opencontainers.image.ref.name"] == "main" and
     .config.Labels["org.opencontainers.image.version"] == $snapshot and
     .config.Labels["org.opencontainers.image.source"] == "https://github.com/OSS-TEST-GROUP/decision-ap" and
     (.config.Labels["org.opencontainers.image.created"] | type == "string" and length > 0) and
     (.config.Labels["para.sdk.version"] | type == "string" and length > 0 and . != "unknown")' \
    <<<"${image}" >/dev/null || { echo "Invalid staging image: ${ref}" >&2; exit 1; }
  actual_sdk="$(jq -er '.config.Labels["para.sdk.version"]' <<<"${image}")"
  if [[ -n "${sdk_version}" && "${actual_sdk}" != "${sdk_version}" ]]; then
    echo "PARA SDK versions differ between architectures" >&2; exit 1
  fi
  sdk_version="${actual_sdk}"
  staging_digest["${arch}"]="$(jq -er '.digest' <<<"${manifest}")"
done

snapshot_ref="${repository}:${snapshot_tag}"
if existing="$(manifest_json "${snapshot_ref}" 2>/dev/null)"; then
  for arch in amd64 arm64; do
    count="$(jq --arg arch "${arch}" --arg digest "${staging_digest[$arch]}" \
      '[.manifests[] | select(.platform.os == "linux" and .platform.architecture == $arch and .digest == $digest)] | length' \
      <<<"${existing}")"
    [[ "${count}" == 1 ]] || { echo "Refusing to overwrite immutable snapshot ${snapshot_ref}" >&2; exit 1; }
  done
  [[ "$(jq '.manifests | length' <<<"${existing}")" == 2 ]] || {
    echo "Snapshot contains unexpected manifests" >&2; exit 1;
  }
else
  docker buildx imagetools create --tag "${snapshot_ref}" \
    "${repository}@${staging_digest[amd64]}" \
    "${repository}@${staging_digest[arm64]}"
fi

snapshot_manifest="$(manifest_json "${snapshot_ref}")"
for arch in amd64 arm64; do
  count="$(jq --arg arch "${arch}" --arg digest "${staging_digest[$arch]}" \
    '[.manifests[] | select(.platform.os == "linux" and .platform.architecture == $arch and .digest == $digest)] | length' \
    <<<"${snapshot_manifest}")"
  [[ "${count}" == 1 ]] || { echo "Published snapshot platform check failed: ${arch}" >&2; exit 1; }
done
[[ "$(jq '.manifests | length' <<<"${snapshot_manifest}")" == 2 ]] || {
  echo "Published snapshot must contain exactly two platforms" >&2; exit 1;
}
snapshot_digest="$(jq -er '.digest' <<<"${snapshot_manifest}")"

release_ref="${repository}:${release_tag}"
if existing="$(manifest_json "${release_ref}" 2>/dev/null)"; then
  [[ "$(jq -er '.digest' <<<"${existing}")" == "${snapshot_digest}" ]] || {
    echo "Refusing to overwrite immutable release ${release_ref}" >&2; exit 1;
  }
else
  docker buildx imagetools create --tag "${release_ref}" "${repository}@${snapshot_digest}"
fi
[[ "$(jq -er '.digest' <<<"$(manifest_json "${release_ref}")")" == "${snapshot_digest}" ]] || {
  echo "Release digest check failed" >&2; exit 1;
}

docker buildx imagetools create --tag "${repository}:latest" "${repository}@${snapshot_digest}"
echo "Published ${release_ref} and ${snapshot_ref} at ${snapshot_digest} (PARA SDK ${sdk_version})"
if [[ -n "${GITHUB_STEP_SUMMARY:-}" ]]; then
  {
    echo "## Decision AP integrated image"
    echo
    echo "- Source: \`${source_sha}\`"
    echo "- Release: \`${release_ref}\`"
    echo "- Snapshot: \`${snapshot_ref}\`"
    echo "- Digest: \`${snapshot_digest}\`"
    echo "- PARA SDK: \`${sdk_version}\`"
  } >>"${GITHUB_STEP_SUMMARY}"
fi
