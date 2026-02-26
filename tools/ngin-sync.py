#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


ROOT_DIR = Path(__file__).resolve().parent.parent
MANIFEST_PATH = Path(os.environ.get("NGIN_MANIFEST_PATH", str(ROOT_DIR / "manifests" / "platform-release.json")))
OVERRIDES_PATH = Path(os.environ.get("NGIN_WORKSPACE_OVERRIDES", str(ROOT_DIR / ".ngin" / "workspace.overrides.json")))
TARGET_DIR = Path(os.environ.get("NGIN_WORKSPACE_EXTERNALS", str(ROOT_DIR / "workspace" / "externals")))


def eprint(*args: object) -> None:
    print(*args, file=sys.stderr)


def run_git(args: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", *args],
        cwd=str(cwd),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def require_tool(name: str) -> None:
    if shutil.which(name) is None:
        raise SystemExit(f"error: required command not found: {name}")


def load_json_file(path: Path, *, optional: bool = False) -> Any:
    if not path.exists():
        if optional:
            return None
        raise SystemExit(f"error: manifest not found: {path}")
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        kind = "overrides" if optional else "manifest"
        raise SystemExit(f"error: invalid {kind} JSON at {path}: {exc}") from exc


def load_manifest() -> dict[str, Any]:
    data = load_json_file(MANIFEST_PATH)
    if not isinstance(data, dict) or not isinstance(data.get("components"), list):
        raise SystemExit(f"error: invalid manifest structure: {MANIFEST_PATH}")
    return data


def load_overrides() -> dict[str, str]:
    data = load_json_file(OVERRIDES_PATH, optional=True)
    if data is None:
        return {}
    if not isinstance(data, dict):
        raise SystemExit(f"error: overrides JSON must be an object: {OVERRIDES_PATH}")
    paths = data.get("paths", {})
    if not isinstance(paths, dict):
        raise SystemExit(f"error: overrides 'paths' must be an object: {OVERRIDES_PATH}")
    result: dict[str, str] = {}
    for key, value in paths.items():
        if isinstance(key, str) and isinstance(value, str):
            result[key] = value
    return result


def component_root_path(component: str) -> Path:
    return ROOT_DIR / component


def component_external_path(component: str, target_dir: Path) -> Path:
    return target_dir / component


def is_git_repo(path: Path) -> bool:
    cp = run_git(["rev-parse", "--git-dir"], cwd=path)
    return cp.returncode == 0


def git_head_or_unborn(path: Path) -> str:
    verify = run_git(["rev-parse", "--verify", "HEAD"], cwd=path)
    if verify.returncode != 0:
        return "UNBORN"
    head = run_git(["rev-parse", "HEAD"], cwd=path)
    if head.returncode != 0:
        return "UNBORN"
    return head.stdout.strip() or "UNBORN"


def git_has_commit(path: Path, ref: str) -> bool:
    cp = run_git(["rev-parse", "--verify", f"{ref}^{{commit}}"], cwd=path)
    return cp.returncode == 0


def short_ref(value: str) -> str:
    if value in ("", "unpinned", "UNBORN", "-"):
        return value
    return value[:12]


def resolve_component_path(component: str, overrides: dict[str, str], target_dir: Path) -> tuple[str, Path | None]:
    override = overrides.get(component)
    if override:
        return ("override", Path(override))

    root_path = component_root_path(component)
    if root_path.exists():
        return ("root", root_path)

    external_path = component_external_path(component, target_dir)
    if external_path.exists():
        return ("externals", external_path)

    return ("none", None)


def iter_components(manifest: dict[str, Any]) -> list[dict[str, Any]]:
    components = manifest.get("components", [])
    if not isinstance(components, list):
        raise SystemExit(f"error: manifest 'components' must be an array: {MANIFEST_PATH}")
    return [c for c in components if isinstance(c, dict)]


def cmd_list(_: argparse.Namespace) -> int:
    manifest = load_manifest()
    overrides = load_overrides()
    for c in iter_components(manifest):
        name = str(c.get("name", ""))
        version = str(c.get("version", "n/a"))
        ref = "unpinned" if c.get("ref") is None else str(c.get("ref"))
        repo = str(c.get("repoUrl", "n/a"))
        override = overrides.get(name)
        if override:
            print(f"{name:<18} {version:<8} {ref:<12} {repo} (override: {override})")
        else:
            print(f"{name:<18} {version:<8} {ref:<12} {repo}")
    return 0


def cmd_status(args: argparse.Namespace) -> int:
    manifest = load_manifest()
    overrides = load_overrides()
    target_dir = Path(args.target) if args.target else TARGET_DIR

    print(f"{'COMPONENT':<18} {'SOURCE':<10} {'REQ':<8} {'PIN':<12} {'HEAD':<12} PATH/DETAIL")
    for c in iter_components(manifest):
        name = str(c.get("name", ""))
        required = "yes" if bool(c.get("required", False)) else "no"
        ref_full = "unpinned" if c.get("ref") is None else str(c.get("ref"))
        ref_disp = short_ref(ref_full)
        source, path = resolve_component_path(name, overrides, target_dir)

        if source == "none" or path is None:
            print(f"{name:<18} {source:<10} {required:<8} {ref_disp:<12} {'-':<12} not present locally")
            continue

        if not path.exists():
            print(f"{name:<18} {source:<10} {required:<8} {ref_disp:<12} {'-':<12} missing path: {path}")
            continue
        if not is_git_repo(path):
            print(f"{name:<18} {source:<10} {required:<8} {ref_disp:<12} {'-':<12} exists but not a git repo: {path}")
            continue

        head = git_head_or_unborn(path)
        head_disp = short_ref(head)
        detail = str(path)
        if ref_full != "unpinned":
            if head == "UNBORN":
                detail += " (head=unborn)"
            elif head == ref_full:
                detail += " (matches pin)"
            elif git_has_commit(path, ref_full):
                detail += " (pin present, checked out different HEAD)"
            else:
                detail += " (pin missing locally; fetch needed)"
        print(f"{name:<18} {source:<10} {required:<8} {ref_disp:<12} {head_disp:<12} {detail}")

    return 0


def _doctor_component_checks(manifest: dict[str, Any], overrides: dict[str, str], target_dir: Path) -> int:
    fail = 0
    print()
    print("Component checks:")
    for c in iter_components(manifest):
        name = str(c.get("name", ""))
        required = bool(c.get("required", False))
        repo = str(c.get("repoUrl", ""))
        ref = "" if c.get("ref") is None else str(c.get("ref"))
        source, path = resolve_component_path(name, overrides, target_dir)

        if source == "none" or path is None:
            if required:
                print(f"[warn] {name}: not present locally (allowed if consuming installed packages)")
            else:
                print(f"[ok]   {name}: not present locally (optional)")
            continue

        if not path.exists():
            print(f"[error] {name}: selected {source} path does not exist: {path}")
            fail = 1
            continue
        if not is_git_repo(path):
            print(f"[error] {name}: selected {source} path is not a git repo: {path}")
            fail = 1
            continue

        head = git_head_or_unborn(path)
        head_short = short_ref(head)

        if not repo:
            if required:
                print(f"[error] {name}: required component missing repoUrl in manifest")
                fail = 1
            else:
                print(f"[warn] {name}: no repoUrl in manifest")
            continue

        if not ref:
            print(f"[warn] {name}: unpinned in manifest (bootstrap/unreleased), local={source}, head={head_short}")
            continue

        if head == ref:
            print(f"[ok]   {name}: {source}, head={head_short} (matches pinned ref)")
            continue

        if head == "UNBORN":
            print(f"[warn] {name}: repo has no HEAD commit yet; manifest pin={short_ref(ref)}")
            continue

        if git_has_commit(path, ref):
            print(f"[warn] {name}: pinned ref present but HEAD differs (head={head_short}, pin={short_ref(ref)})")
        else:
            print(f"[warn] {name}: pinned ref not found locally (fetch needed), head={head_short}, pin={short_ref(ref)})")
    return fail


def cmd_doctor(args: argparse.Namespace) -> int:
    target_dir = Path(args.target) if args.target else TARGET_DIR
    fail = 0

    print("NGIN workspace doctor")
    print(f"  root:      {ROOT_DIR}")
    print(f"  manifest:  {MANIFEST_PATH}")
    print(f"  overrides: {OVERRIDES_PATH}")
    print(f"  externals: {target_dir}")
    print()

    for tool in ("git", "cmake"):
        if shutil.which(tool):
            print(f"[ok] tool: {tool}")
        else:
            print(f"[error] missing tool: {tool}")
            fail = 1

    pyver = f"{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}"
    print(f"[ok] tool: python ({pyver})")

    if not MANIFEST_PATH.exists():
        print(f"[error] manifest not found: {MANIFEST_PATH}")
        fail = 1
        manifest = None
    else:
        try:
            manifest = load_manifest()
            print(
                "[ok] manifest JSON parses "
                f"(platformVersion={manifest.get('platformVersion', 'unknown')}, "
                f"channel={manifest.get('channel', 'unknown')})"
            )
        except SystemExit as exc:
            print(str(exc))
            fail = 1
            manifest = None

    if OVERRIDES_PATH.exists():
        try:
            overrides = load_overrides()
            print("[ok] overrides JSON parses")
        except SystemExit as exc:
            print(str(exc))
            fail = 1
            overrides = {}
    else:
        print("[ok] overrides file absent (optional)")
        overrides = {}

    for schema_path in (
        ROOT_DIR / "manifests" / "components.schema.json",
        ROOT_DIR / "manifests" / "platform-release.schema.json",
    ):
        if schema_path.exists():
            print(f"[ok] schema present: {schema_path.relative_to(ROOT_DIR)}")
        else:
            print(f"[warn] schema missing: {schema_path.relative_to(ROOT_DIR)}")

    if manifest is None:
        print()
        print("doctor failed before component checks")
        print()
        print("doctor result: FAIL")
        return 1

    fail |= _doctor_component_checks(manifest, overrides, target_dir)
    print()
    if fail:
        print("doctor result: FAIL")
        return 1
    print("doctor result: PASS")
    return 0


def cmd_sync(args: argparse.Namespace) -> int:
    require_tool("git")
    manifest = load_manifest()
    overrides = load_overrides()
    target_dir = Path(args.target) if args.target else TARGET_DIR
    target_dir.mkdir(parents=True, exist_ok=True)

    for c in iter_components(manifest):
        name = str(c.get("name", ""))
        repo = str(c.get("repoUrl", ""))
        ref = "" if c.get("ref") is None else str(c.get("ref"))
        required = bool(c.get("required", False))

        override = overrides.get(name)
        if override:
            print(f"[override] {name} -> {override}")
            continue

        if not repo:
            if required:
                eprint(f"[error] {name} has no repoUrl but is required")
                return 1
            print(f"[skip] {name} (no repoUrl)")
            continue

        if not ref:
            print(f"[skip] {name} (no pinned ref yet; bootstrap/unreleased)")
            continue

        dest = target_dir / name
        if not (dest / ".git").exists():
            print(f"[clone] {name}")
            cp = subprocess.run(["git", "clone", repo, str(dest)], text=True)
            if cp.returncode != 0:
                return cp.returncode
        else:
            print(f"[fetch] {name}")
            cp = subprocess.run(["git", "-C", str(dest), "fetch", "--tags", "origin"], text=True)
            if cp.returncode != 0:
                return cp.returncode

        print(f"[checkout] {name} -> {ref}")
        cp = subprocess.run(["git", "-C", str(dest), "checkout", "--detach", ref], text=True)
        if cp.returncode != 0:
            return cp.returncode

    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="ngin-sync",
        description="Manifest-driven workspace helper for the NGIN umbrella repo.",
    )
    sub = parser.add_subparsers(dest="command")

    sub.add_parser("list", help="List manifest components, versions, pins, and overrides.")

    p_status = sub.add_parser("status", help="Show local workspace component resolution and pin match status.")
    p_status.add_argument("--target", help="Alternate externals target directory.")

    p_doctor = sub.add_parser("doctor", help="Validate tools, manifest/overrides, and local component repos.")
    p_doctor.add_argument("--target", help="Alternate externals target directory.")

    p_sync = sub.add_parser("sync", help="Clone/fetch pinned components into the externals directory.")
    p_sync.add_argument("--target", help="Override externals target directory.")

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    if args.command == "list":
        return cmd_list(args)
    if args.command == "status":
        return cmd_status(args)
    if args.command == "doctor":
        return cmd_doctor(args)
    if args.command == "sync":
        return cmd_sync(args)

    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

