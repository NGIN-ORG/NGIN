#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
target="${1:-workflow}"

case "$target" in
  cli)
    cmake --build "$repo_root/build/dev" --target ngin_cli
    ;;
  workflow)
    cmake --build "$repo_root/build/dev" --target ngin.workflow
    ;;
  ngin-core-tests)
    cmake --build "$repo_root/build/ngin-core-ci" --config Release --target NGINCoreTests
    ;;
  *)
    echo "unknown build target: $target" >&2
    echo "expected one of: cli, workflow, ngin-core-tests" >&2
    exit 2
    ;;
esac
