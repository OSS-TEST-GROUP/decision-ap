#!/usr/bin/env bash
set -euo pipefail

[[ "$(uname -m)" == x86_64 ]] || {
  echo "run-arm64-qemu.sh requires an x86_64 PC with ARM64 binfmt/QEMU." >&2
  exit 1
}
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
exec env ARCH=aarch64 KATECH_NETWORK_MODE=bridge KATECH_RVIZ=true \
  "${repo_dir}/docker/run/run-katech-stack.sh"
