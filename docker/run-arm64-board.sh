#!/usr/bin/env bash
set -euo pipefail

[[ "$(uname -m)" == aarch64 ]] || {
  echo "run-arm64-board.sh requires an ARM64 board." >&2
  exit 1
}
if [[ -z "${KATECH_NETWORK_INTERFACE:-}" ]]; then
  echo "Set KATECH_NETWORK_INTERFACE to the board LAN interface (see: ip -br address)." >&2
  exit 1
fi
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
exec env ARCH=aarch64 KATECH_NETWORK_MODE=host KATECH_RVIZ=false \
  "${repo_dir}/docker/run/run-katech-stack.sh"
