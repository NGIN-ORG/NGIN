#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
target="${1:-workspace}"
cli="$repo_root/build/dev/Tools/NGIN.CLI/ngin"

smoke_project() {
  local project="$1"
  local output="$2"
  "$cli" validate --project "$project" --profile Runtime
  "$cli" build --project "$project" --profile Runtime --output "$output"
}

smoke_run_project() {
  local project="$1"
  local output="$2"
  smoke_project "$project" "$output"
  "$cli" run --project "$project" --profile Runtime --output "$output"
}

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
  app-native-minimal)
    smoke_run_project \
      "$repo_root/Examples/App.NativeMinimal/App.NativeMinimal.nginproj" \
      "$repo_root/build/manual/App.NativeMinimal"
    ;;
  app-hosted-core)
    smoke_run_project \
      "$repo_root/Examples/App.HostedCore/App.HostedCore.nginproj" \
      "$repo_root/build/manual/App.HostedCore"
    ;;
  app-basic)
    smoke_project \
      "$repo_root/Examples/App.Basic/App.Basic.nginproj" \
      "$repo_root/build/manual/App.Basic"
    ;;
  *)
    echo "unknown test target: $target" >&2
    echo "expected one of: workspace, workflow, ngin-core, app-native-minimal, app-hosted-core, app-basic" >&2
    exit 2
    ;;
esac
