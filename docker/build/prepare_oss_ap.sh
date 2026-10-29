#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)"
oss_ap_dir="${OSS_AP_SOURCE:-${repo_dir}/../oss-ap}"
update="${OSS_AP_UPDATE:-1}"
pinned_commit="2f649badb4a63e47e7df89c5b5b1675a70fa6ad5"
case "${update}" in
  0|1) ;;
  *) echo "OSS_AP_UPDATE must be 0 or 1" >&2; exit 1 ;;
esac

if [[ ! -d "${oss_ap_dir}" ]]; then
  if [[ "${update}" == 0 ]]; then
    echo "oss-ap source is missing: ${oss_ap_dir}" >&2
    exit 1
  fi
  git clone "${OSS_AP_URL:-git@github.com:OSS-TEST-GROUP/oss-ap.git}" "${oss_ap_dir}"
fi
oss_ap_dir="$(cd "${oss_ap_dir}" && pwd -P)"
git_root="$(git -C "${oss_ap_dir}" rev-parse --show-toplevel 2>/dev/null || true)"

if [[ "${update}" == 1 ]]; then
  if [[ "${git_root}" != "${oss_ap_dir}" ]]; then
    echo "oss-ap source is not a Git checkout; use OSS_AP_UPDATE=0 for an offline copy: ${oss_ap_dir}" >&2
    exit 1
  fi
  if [[ -n "$(git -C "${oss_ap_dir}" status --porcelain)" ]]; then
    echo "oss-ap has local changes; update it separately or use OSS_AP_UPDATE=0: ${oss_ap_dir}" >&2
    exit 1
  fi
  if ! git -C "${oss_ap_dir}" cat-file -e "${pinned_commit}^{commit}" 2>/dev/null; then
    git -C "${oss_ap_dir}" fetch origin
  fi
  if ! git -C "${oss_ap_dir}" cat-file -e "${pinned_commit}^{commit}" 2>/dev/null; then
    echo "Pinned oss-ap commit is unavailable: ${pinned_commit}" >&2
    exit 1
  fi
  if [[ "$(git -C "${oss_ap_dir}" rev-parse HEAD)" != "${pinned_commit}" ]]; then
    git -C "${oss_ap_dir}" checkout --detach "${pinned_commit}"
  fi
fi

test -f "${oss_ap_dir}/src/Ecu_Machine/package.xml" || {
  echo "oss-ap Ecu_Machine is missing: ${oss_ap_dir}" >&2
  exit 1
}
if [[ "${git_root}" == "${oss_ap_dir}" ]]; then
  if [[ "$(git -C "${oss_ap_dir}" rev-parse HEAD)" != "${pinned_commit}" ]]; then
    echo "oss-ap must be at pinned commit ${pinned_commit}: ${oss_ap_dir}" >&2
    exit 1
  fi
  echo "oss-ap source: $(git -C "${oss_ap_dir}" rev-parse --short HEAD) (${oss_ap_dir})"
else
  echo "oss-ap source: offline copy (${oss_ap_dir})"
fi

if [[ ! -f "${repo_dir}/external/msgs/autoware_msgs/autoware_planning_msgs/package.xml" ]]; then
  if [[ "${update}" == 0 ]]; then
    echo "ROS messages are missing; run vcs import external < oss-ap.repos before offline build" >&2
    exit 1
  fi
  command -v vcs >/dev/null || {
    echo "vcs is required to import ROS messages from oss-ap.repos" >&2
    exit 1
  }
  vcs import "${repo_dir}/external" --input "${repo_dir}/oss-ap.repos" --skip-existing
fi
