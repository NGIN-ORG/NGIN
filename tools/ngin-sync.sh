#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MANIFEST_PATH="${NGIN_MANIFEST_PATH:-$ROOT_DIR/manifests/platform-release.json}"
OVERRIDES_PATH="${NGIN_WORKSPACE_OVERRIDES:-$ROOT_DIR/.ngin/workspace.overrides.json}"
TARGET_DIR="${NGIN_WORKSPACE_EXTERNALS:-$ROOT_DIR/workspace/externals}"

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "error: required command not found: $1" >&2
    exit 1
  fi
}

usage() {
  cat <<'EOF'
Usage:
  ngin-sync.sh list
  ngin-sync.sh sync
  ngin-sync.sh sync --target <dir>

Environment overrides:
  NGIN_MANIFEST_PATH
  NGIN_WORKSPACE_OVERRIDES
  NGIN_WORKSPACE_EXTERNALS

Overrides file format (optional):
{
  "paths": {
    "NGIN.Base": "/abs/path/to/NGIN.Base"
  }
}
EOF
}

manifest_exists() {
  [[ -f "$MANIFEST_PATH" ]] || {
    echo "error: manifest not found: $MANIFEST_PATH" >&2
    exit 1
  }
}

json_get_override_path() {
  local component="$1"
  if [[ ! -f "$OVERRIDES_PATH" ]]; then
    return 0
  fi
  jq -r --arg c "$component" '.paths[$c] // empty' "$OVERRIDES_PATH"
}

list_components() {
  manifest_exists
  require_cmd jq
  jq -r '
    .components[]
    | [
        .name,
        (.version // "n/a"),
        (if .ref == null then "unpinned" else .ref end),
        (.repoUrl // "n/a")
      ]
    | @tsv
  ' "$MANIFEST_PATH" | while IFS=$'\t' read -r name version ref repo; do
    override="$(json_get_override_path "$name")"
    if [[ -n "${override:-}" ]]; then
      printf '%-18s %-8s %-12s %s (override: %s)\n' "$name" "$version" "$ref" "$repo" "$override"
    else
      printf '%-18s %-8s %-12s %s\n' "$name" "$version" "$ref" "$repo"
    fi
  done
}

sync_components() {
  manifest_exists
  require_cmd jq
  require_cmd git
  mkdir -p "$TARGET_DIR"

  jq -c '.components[]' "$MANIFEST_PATH" | while IFS= read -r row; do
    name="$(jq -r '.name' <<<"$row")"
    repo="$(jq -r '.repoUrl // empty' <<<"$row")"
    ref="$(jq -r 'if .ref == null then "" else .ref end' <<<"$row")"
    required="$(jq -r '.required // false' <<<"$row")"

    override="$(json_get_override_path "$name")"
    if [[ -n "${override:-}" ]]; then
      echo "[override] $name -> $override"
      continue
    fi

    if [[ -z "$repo" ]]; then
      if [[ "$required" == "true" ]]; then
        echo "[error] $name has no repoUrl but is required" >&2
        exit 1
      fi
      echo "[skip] $name (no repoUrl)"
      continue
    fi

    if [[ -z "$ref" ]]; then
      echo "[skip] $name (no pinned ref yet; bootstrap/unreleased)"
      continue
    fi

    dest="$TARGET_DIR/$name"
    if [[ ! -d "$dest/.git" ]]; then
      echo "[clone] $name"
      git clone "$repo" "$dest"
    else
      echo "[fetch] $name"
      git -C "$dest" fetch --tags origin
    fi

    echo "[checkout] $name -> $ref"
    git -C "$dest" checkout --detach "$ref"
  done
}

main() {
  cmd="${1:-}"
  shift || true

  case "$cmd" in
    list)
      list_components
      ;;
    sync)
      while [[ $# -gt 0 ]]; do
        case "$1" in
          --target)
            TARGET_DIR="$2"
            shift 2
            ;;
          -h|--help)
            usage
            exit 0
            ;;
          *)
            echo "error: unknown argument: $1" >&2
            usage
            exit 1
            ;;
        esac
      done
      sync_components
      ;;
    -h|--help|"")
      usage
      ;;
    *)
      echo "error: unknown command: $cmd" >&2
      usage
      exit 1
      ;;
  esac
}

main "$@"

