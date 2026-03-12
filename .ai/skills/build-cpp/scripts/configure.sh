#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
target="${1:-workspace}"

case "$target" in
  workspace)
    cmake --preset dev
    ;;
  ngin-core)
    cmake -S "$repo_root/Packages/NGIN.Core" \
      -B "$repo_root/build/ngin-core-ci" \
      -DNGIN_CORE_BUILD_TESTS=ON \
      -DNGIN_CORE_BUILD_EXAMPLES=OFF
    ;;
  *)
    echo "unknown configure target: $target" >&2
    echo "expected one of: workspace, ngin-core" >&2
    exit 2
    ;;
esac
