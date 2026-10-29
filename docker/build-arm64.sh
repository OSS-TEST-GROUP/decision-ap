#!/usr/bin/env bash
set -euo pipefail

case "$(uname -m)" in
  x86_64|aarch64) ;;
  *) echo "build-arm64.sh requires an x86_64 PC or ARM64 board." >&2; exit 1 ;;
esac
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
exec env ARCH=aarch64 "${repo_dir}/docker/build/build_all.sh"
