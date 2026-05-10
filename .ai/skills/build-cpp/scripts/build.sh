#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
target="${1:-workflow}"
cli="$repo_root/build/dev/Tools/NGIN.CLI/ngin"

case "$target" in
  cli)
    cmake --build "$repo_root/build/dev" --target ngin_cli
    ;;
  cli-tests)
    cmake --build "$repo_root/build/dev" --target NGINCliTests
    ;;
  workflow)
    cmake --build "$repo_root/build/dev" --target ngin.workflow
    ;;
  ngin-core-tests)
    cmake --build "$repo_root/build/ngin-core-ci" --config Release --target NGINCoreTests
    ;;
  hello-native)
    "$cli" build \
      --project "$repo_root/Examples/Hello.Native/Hello.Native.nginproj" \
      --profile Debug \
      --output "$repo_root/build/manual/Hello.Native"
    ;;
  hello-hosted)
    "$cli" build \
      --project "$repo_root/Examples/Hello.Hosted/Hello.Hosted.nginproj" \
      --profile Debug \
      --output "$repo_root/build/manual/Hello.Hosted"
    ;;
  hello-reflection)
    cmake --build "$repo_root/build/dev" --target ngin_reflection_metagen
    "$cli" build \
      --project "$repo_root/Examples/Hello.Reflection/Hello.Reflection.nginproj" \
      --profile Debug \
      --output "$repo_root/build/manual/Hello.Reflection"
    ;;
  *)
    echo "unknown build target: $target" >&2
    echo "expected one of: cli, cli-tests, workflow, ngin-core-tests, hello-native, hello-hosted, hello-reflection" >&2
    exit 2
    ;;
esac
