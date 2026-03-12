#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
target="${1:-workspace}"
cli="$repo_root/build/dev/Tools/NGIN.CLI/ngin"
project="$repo_root/Examples/App.Basic/App.Basic.nginproj"

case "$target" in
  workspace)
    ctest --test-dir "$repo_root/build/dev" --output-on-failure
    ;;
  workflow)
    cmake --build "$repo_root/build/dev" --target ngin.workflow
    ;;
  ngin-core)
    ctest --test-dir "$repo_root/build/ngin-core-ci" --output-on-failure -C Release
    ;;
  app-basic)
    "$cli" project validate --project "$project" --variant Runtime
    "$cli" project build --project "$project" --variant Runtime --output "$repo_root/build/manual/App.Basic"
    ;;
  *)
    echo "unknown test target: $target" >&2
    echo "expected one of: workspace, workflow, ngin-core, app-basic" >&2
    exit 2
    ;;
esac
