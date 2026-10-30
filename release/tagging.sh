#!/usr/bin/env bash

# Tag grammar and validation follow katech-autoware/release/tagging.sh.
release_die() {
  echo "ERROR: $*" >&2
  return 1
}

release_validate_snapshot_tag() {
  local snapshot_tag="$1" default_branch="$2" source_short_sha="$3"
  local expected_pattern="^${default_branch}-[0-9]{8}-${source_short_sha}$"
  if [[ ! "${snapshot_tag}" =~ $expected_pattern ]]; then
    release_die "snapshot tag '${snapshot_tag}' must match ${default_branch}-YYYYMMDD-${source_short_sha}"
    return 1
  fi
  local snapshot_date="${snapshot_tag#${default_branch}-}"
  snapshot_date="${snapshot_date%-${source_short_sha}}"
  if ! date -u -d "${snapshot_date:0:4}-${snapshot_date:4:2}-${snapshot_date:6:2}" +%Y%m%d \
    2>/dev/null | grep -Fxq "${snapshot_date}"; then
    release_die "snapshot tag '${snapshot_tag}' contains an invalid UTC date"
    return 1
  fi
}

release_validate_semver_tag() {
  local release_tag="$1"
  if [[ ! "${release_tag}" =~ ^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-(rc|beta)[1-9][0-9]*)?$ ]]; then
    release_die "release tag '${release_tag}' is not an allowed vMAJOR.MINOR.PATCH tag"
    return 1
  fi
}
