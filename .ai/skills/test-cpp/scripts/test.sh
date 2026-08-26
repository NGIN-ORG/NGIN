#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
target="${1:-workspace}"
cli="$repo_root/build/dev/Tools/NGIN.CLI/ngin"

smoke_project() {
  local project="$1"
  local output="$2"
  local configuration="${3:-Debug}"
  "$cli" validate --project "$project" --configuration "$configuration"
  "$cli" build --project "$project" --configuration "$configuration" --output "$output"
}

smoke_run_project() {
  local project="$1"
  local output="$2"
  local configuration="${3:-Debug}"
  smoke_project "$project" "$output" "$configuration"
  "$cli" run --project "$project" --configuration "$configuration" --output "$output"
}

case "$target" in
  workspace)
    ctest --test-dir "$repo_root/build/dev" --output-on-failure
    ;;
  cli)
    "$repo_root/build/dev/Tools/NGIN.CLI/tests/NGINCliTests"
    ;;
  workflow)
    cmake --build "$repo_root/build/dev" --target ngin.workflow
    ;;
  ngin-core)
    ctest --test-dir "$repo_root/build/ngin-core-ci" --output-on-failure -C Release
    ;;
  hello-native)
    smoke_run_project \
      "$repo_root/Examples/Hello.Native/Hello.Native.nginproj" \
      "$repo_root/build/manual/Hello.Native"
    ;;
  hello-hosted)
    smoke_run_project \
      "$repo_root/Examples/Hello.Hosted/Hello.Hosted.nginproj" \
      "$repo_root/build/manual/Hello.Hosted"
    ;;
  hello-reflection)
    cmake --build "$repo_root/build/dev" --target ngin_reflection_metagen
    "$cli" validate \
      --project "$repo_root/Examples/Hello.Reflection/Hello.Reflection.nginproj" \
      --configuration Debug
    "$cli" package lock \
      --project "$repo_root/Examples/Hello.Reflection/Hello.Reflection.nginproj" \
      --configuration Debug \
      --output "$repo_root/build/manual/Hello.Reflection/ngin.lock"
    "$cli" build \
      --project "$repo_root/Examples/Hello.Reflection/Hello.Reflection.nginproj" \
      --configuration Debug \
      --lock "$repo_root/build/manual/Hello.Reflection/ngin.lock" \
      --output "$repo_root/build/manual/Hello.Reflection"
    "$cli" run \
      --project "$repo_root/Examples/Hello.Reflection/Hello.Reflection.nginproj" \
      --configuration Debug \
      --lock "$repo_root/build/manual/Hello.Reflection/ngin.lock" \
      --output "$repo_root/build/manual/Hello.Reflection"
    ;;
  *)
    echo "unknown test target: $target" >&2
    echo "expected one of: workspace, cli, workflow, ngin-core, hello-native, hello-hosted, hello-reflection" >&2
    exit 2
    ;;
esac
