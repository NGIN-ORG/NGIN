#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
target="${1:-workflow}"
cli="$repo_root/build/dev/Tools/NGIN.CLI/ngin"

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
  app-native-minimal)
    "$cli" build \
      --project "$repo_root/Examples/App.NativeMinimal/App.NativeMinimal.nginproj" \
      --profile Runtime \
      --output "$repo_root/build/manual/App.NativeMinimal"
    ;;
  app-hosted-core)
    "$cli" build \
      --project "$repo_root/Examples/App.HostedCore/App.HostedCore.nginproj" \
      --profile Runtime \
      --output "$repo_root/build/manual/App.HostedCore"
    ;;
  *)
    echo "unknown build target: $target" >&2
    echo "expected one of: cli, workflow, ngin-core-tests, app-native-minimal, app-hosted-core" >&2
    exit 2
    ;;
esac
