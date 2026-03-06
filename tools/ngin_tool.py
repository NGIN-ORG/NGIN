#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from collections import defaultdict, deque
from pathlib import Path
from typing import Any


ROOT_DIR = Path(__file__).resolve().parent.parent
MANIFEST_PATH = Path(os.environ.get("NGIN_MANIFEST_PATH", str(ROOT_DIR / "manifests" / "platform-release.json")))
OVERRIDES_PATH = Path(os.environ.get("NGIN_WORKSPACE_OVERRIDES", str(ROOT_DIR / ".ngin" / "workspace.overrides.json")))
TARGET_DIR = Path(os.environ.get("NGIN_WORKSPACE_EXTERNALS", str(ROOT_DIR / "workspace" / "externals")))
DEFAULT_CACHE_DIR = ROOT_DIR / ".ngin" / "cache"

SCHEMA_PATHS = {
    "module": ROOT_DIR / "manifests" / "module.schema.json",
    "plugin": ROOT_DIR / "manifests" / "plugin-bundle.schema.json",
    "package": ROOT_DIR / "manifests" / "package.schema.json",
    "package_catalog": ROOT_DIR / "manifests" / "package-catalog.schema.json",
    "package_lock": ROOT_DIR / "manifests" / "package-lock.schema.json",
    "project": ROOT_DIR / "manifests" / "project.schema.json",
}

CATALOG_PATHS = {
    "module": ROOT_DIR / "manifests" / "module-catalog.json",
    "package": ROOT_DIR / "manifests" / "package-catalog.json",
    "plugin": ROOT_DIR / "manifests" / "plugin-catalog.json",
}

COMPONENT_NAME_RE = re.compile(r"^NGIN\.[A-Za-z0-9_.-]+$")
MODULE_NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9]*\.[A-Za-z0-9_.-]+$")
PACKAGE_NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_-]*\.[A-Za-z0-9_.-]+$")
PLUGIN_NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_-]*\.[A-Za-z0-9_.-]+$")

FIND_PACKAGE_RE = re.compile(r"find_package\s*\(\s*([A-Za-z0-9_.+-]+)", re.IGNORECASE)
NGIN_TARGET_RE = re.compile(r"NGIN::([A-Za-z0-9_]+)")

MODULE_FAMILIES = {"Base", "Reflection", "Core", "Platform", "Editor", "Domain", "App"}
MODULE_TYPES = {"Runtime", "Editor", "Program", "Developer", "ThirdParty"}
LOAD_PHASES = {"Bootstrap", "Platform", "CoreServices", "Data", "Domain", "Application", "Editor"}

SEMVER_RE = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-[0-9A-Za-z.-]+)?$")
RANGE_TOKEN_RE = re.compile(r"^(>=|<=|>|<|=)?(.+)$")
PACKAGE_OVERRIDE_RE = re.compile(r"^(?P<name>[A-Za-z][A-Za-z0-9_-]*\.[A-Za-z0-9_.-]+)=(?P<path>.+)$")

DEFAULT_STAGES = ["Build", "Cook", "Stage", "Package"]

TARGET_ALLOWED_MODULE_TYPES: dict[str, set[str]] = {
    "Runtime": {"Runtime", "ThirdParty"},
    "Editor": {"Runtime", "Editor", "Developer", "ThirdParty"},
    "Program": {"Runtime", "Program", "ThirdParty"},
    "Developer": {"Runtime", "Editor", "Program", "Developer", "ThirdParty"},
}

DEPENDENCY_MATRIX: dict[str, dict[str, str]] = {
    "Base": {
        "Base": "N",
        "Reflection": "N",
        "Core": "N",
        "Platform": "N",
        "Editor": "N",
        "Domain": "N",
        "App": "N",
    },
    "Reflection": {
        "Base": "Y",
        "Reflection": "N",
        "Core": "N",
        "Platform": "N",
        "Editor": "N",
        "Domain": "N",
        "App": "N",
    },
    "Core": {
        "Base": "Y",
        "Reflection": "O",
        "Core": "Y",
        "Platform": "Y",
        "Editor": "N",
        "Domain": "N",
        "App": "N",
    },
    "Platform": {
        "Base": "Y",
        "Reflection": "O",
        "Core": "N",
        "Platform": "Y",
        "Editor": "N",
        "Domain": "N",
        "App": "N",
    },
    "Editor": {
        "Base": "Y",
        "Reflection": "O",
        "Core": "Y",
        "Platform": "Y",
        "Editor": "Y",
        "Domain": "O",
        "App": "N",
    },
    "Domain": {
        "Base": "Y",
        "Reflection": "O",
        "Core": "Y",
        "Platform": "Y",
        "Editor": "N",
        "Domain": "Y",
        "App": "N",
    },
    "App": {
        "Base": "Y",
        "Reflection": "O",
        "Core": "Y",
        "Platform": "Y",
        "Editor": "N",
        "Domain": "Y",
        "App": "Y",
    },
}


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


def load_workspace_overrides() -> dict[str, Any]:
    data = load_json_file(OVERRIDES_PATH, optional=True)
    if data is None:
        return {"paths": {}, "packages": {}}
    if not isinstance(data, dict):
        raise SystemExit(f"error: overrides JSON must be an object: {OVERRIDES_PATH}")

    paths = data.get("paths", {})
    packages = data.get("packages", {})
    if not isinstance(paths, dict):
        raise SystemExit(f"error: overrides 'paths' must be an object: {OVERRIDES_PATH}")
    if not isinstance(packages, dict):
        raise SystemExit(f"error: overrides 'packages' must be an object: {OVERRIDES_PATH}")

    normalized_paths: dict[str, str] = {}
    for key, value in paths.items():
        if isinstance(key, str) and isinstance(value, str):
            normalized_paths[key] = value

    normalized_packages: dict[str, str] = {}
    for key, value in packages.items():
        if isinstance(key, str) and isinstance(value, str):
            normalized_packages[key] = value

    return {
        "paths": normalized_paths,
        "packages": normalized_packages,
    }


def load_overrides() -> dict[str, str]:
    return load_workspace_overrides()["paths"]


def load_package_overrides() -> dict[str, str]:
    return load_workspace_overrides()["packages"]


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
        ROOT_DIR / "manifests" / "package-catalog.schema.json",
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


def make_report() -> dict[str, Any]:
    return {
        "errors": [],
        "warnings": [],
        "scanSkipped": [],
    }


def add_error(report: dict[str, Any], message: str) -> None:
    report["errors"].append(message)


def add_warning(report: dict[str, Any], message: str) -> None:
    report["warnings"].append(message)


def add_scan_skipped(report: dict[str, Any], message: str) -> None:
    report["scanSkipped"].append(message)


def write_json_report(path_text: str | None, payload: dict[str, Any]) -> None:
    if not path_text:
        return
    out_path = Path(path_text)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _empty_validation_report() -> dict[str, Any]:
    return {
        "ok": True,
        "counts": {},
        "errors": [],
        "warnings": [],
        "scanSkipped": [],
    }


def _file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            chunk = handle.read(65536)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def _relative_path_text(path: Path, base: Path) -> str:
    try:
        return os.path.relpath(path, base)
    except ValueError:
        return str(path)


def _cache_dir_from_args(args: argparse.Namespace) -> Path:
    return Path(getattr(args, "cache_dir", None) or DEFAULT_CACHE_DIR)


def _validation_report_payload(validation: dict[str, Any]) -> dict[str, Any]:
    return {
        "ok": bool(validation.get("ok", False)),
        "counts": dict(validation.get("counts", {})),
        "errors": list(validation.get("errors", [])),
        "warnings": list(validation.get("warnings", [])),
        "scanSkipped": list(validation.get("scanSkipped", [])),
    }


def _resolution_report_payload(
    report: dict[str, Any],
    payload: dict[str, Any] | None,
) -> dict[str, Any]:
    out: dict[str, Any] = {
        "errors": list(report.get("errors", [])),
        "warnings": list(report.get("warnings", [])),
    }
    if payload is None:
        return out

    out.update(
        {
            "packages": list(payload.get("packages", [])),
            "providedByModule": dict(payload.get("providedByModule", {})),
            "providedByPlugin": dict(payload.get("providedByPlugin", {})),
            "requiredModules": list(payload.get("requiredModules", [])),
            "optionalModules": list(payload.get("optionalModules", [])),
            "enabledPlugins": list(payload.get("enabledPlugins", [])),
            "stages": dict(payload.get("stages", {})),
            "dependencyEdges": dict(payload.get("dependencyEdges", {})),
            "packageEdges": dict(payload.get("packageEdges", {})),
        }
    )
    if "targets" in payload:
        out["targets"] = list(payload.get("targets", []))
    if "restoredPackages" in payload:
        out["restoredPackages"] = list(payload.get("restoredPackages", []))
    if "cacheWrites" in payload:
        out["cacheWrites"] = list(payload.get("cacheWrites", []))
    return out


def _command_report_payload(
    command: str,
    validation: dict[str, Any],
    report: dict[str, Any],
    payload: dict[str, Any] | None,
    *,
    lockfile: dict[str, Any] | None = None,
    cache: dict[str, Any] | None = None,
    reason: str | None = None,
) -> dict[str, Any]:
    target = payload.get("target") if isinstance(payload, dict) else None
    source = payload.get("source") if isinstance(payload, dict) else None
    command_ok = reason is None and not report.get("errors")

    result = {
        "ok": command_ok,
        "command": command,
        "target": target,
        "source": source,
        "validation": _validation_report_payload(validation),
        "resolution": _resolution_report_payload(report, payload),
    }
    if lockfile is not None:
        result["lockfile"] = lockfile
    if cache is not None:
        result["cache"] = cache
    if payload is not None:
        result.update(
            {
                "type": payload.get("type"),
                "platform": payload.get("platform"),
                "enableReflection": payload.get("enableReflection"),
                "project": payload.get("project"),
            }
        )
    if reason is not None:
        result["reason"] = reason
    return result


def _is_string_list(value: Any) -> bool:
    return isinstance(value, list) and all(isinstance(v, str) for v in value)


def _json_pointer(path_tokens: list[Any]) -> str:
    pointer = ""
    for token in path_tokens:
        text = str(token).replace("~", "~0").replace("/", "~1")
        pointer += f"/{text}"
    return pointer


def _valid_semver(text: str) -> bool:
    return bool(SEMVER_RE.fullmatch(text.strip()))


def _valid_version_range(text: str) -> bool:
    normalized = text.strip()
    if not normalized:
        return True

    for token in normalized.split():
        match = RANGE_TOKEN_RE.fullmatch(token)
        if not match:
            return False
        version = match.group(2).strip()
        if not version or not _valid_semver(version):
            return False
    return True


def _parse_prerelease(text: str) -> tuple[tuple[int | str, bool], ...]:
    if not text:
        return ()
    out: list[tuple[int | str, bool]] = []
    for token in text.split("."):
        if token.isdigit():
            out.append((int(token), True))
        else:
            out.append((token, False))
    return tuple(out)


def _parse_semver_parts(text: str) -> tuple[int, int, int, tuple[tuple[int | str, bool], ...]] | None:
    match = SEMVER_RE.fullmatch(text.strip())
    if not match:
        return None
    base, _, prerelease = text.partition("-")
    major_text, minor_text, patch_text = base.split(".")
    return (
        int(major_text),
        int(minor_text),
        int(patch_text),
        _parse_prerelease(prerelease),
    )


def _compare_prerelease(
    left: tuple[tuple[int | str, bool], ...],
    right: tuple[tuple[int | str, bool], ...],
) -> int:
    if not left and not right:
        return 0
    if not left:
        return 1
    if not right:
        return -1

    for left_token, right_token in zip(left, right):
        left_value, left_is_numeric = left_token
        right_value, right_is_numeric = right_token
        if left_is_numeric and right_is_numeric:
            if left_value < right_value:
                return -1
            if left_value > right_value:
                return 1
            continue
        if left_is_numeric != right_is_numeric:
            return -1 if left_is_numeric else 1

        assert isinstance(left_value, str)
        assert isinstance(right_value, str)
        if left_value < right_value:
            return -1
        if left_value > right_value:
            return 1

    if len(left) < len(right):
        return -1
    if len(left) > len(right):
        return 1
    return 0


def _compare_semver(left: str, right: str) -> int:
    left_parts = _parse_semver_parts(left)
    right_parts = _parse_semver_parts(right)
    if left_parts is None or right_parts is None:
        raise ValueError(f"invalid semver compare: '{left}' vs '{right}'")

    for left_value, right_value in zip(left_parts[:3], right_parts[:3]):
        if left_value < right_value:
            return -1
        if left_value > right_value:
            return 1

    return _compare_prerelease(left_parts[3], right_parts[3])


def _version_satisfies_range(version: str, range_text: str) -> bool:
    normalized = range_text.strip()
    if not normalized:
        return True

    for token in normalized.split():
        match = RANGE_TOKEN_RE.fullmatch(token)
        if not match:
            return False
        operator = match.group(1) or "="
        rhs = match.group(2).strip()
        comparison = _compare_semver(version, rhs)
        if operator == "=" and comparison != 0:
            return False
        if operator == ">" and comparison <= 0:
            return False
        if operator == ">=" and comparison < 0:
            return False
        if operator == "<" and comparison >= 0:
            return False
        if operator == "<=" and comparison > 0:
            return False
    return True


def _platform_aliases(platform_name: str) -> set[str]:
    value = platform_name.strip().lower()
    if not value:
        return set()
    aliases = {value}
    primary = value.split("-", 1)[0]
    aliases.add(primary)
    if primary.startswith("win"):
        aliases.add("windows")
    if primary == "darwin":
        aliases.add("macos")
    return aliases


def _platform_supported(target_platform: str, declared_platforms: list[str]) -> bool:
    aliases = _platform_aliases(target_platform)
    if not aliases:
        return False
    declared = {value.strip().lower() for value in declared_platforms if isinstance(value, str)}
    return not declared.isdisjoint(aliases)


def _check_exact_keys(obj: dict[str, Any], required: set[str], allowed: set[str], ctx: str, report: dict[str, Any]) -> None:
    missing = sorted(required - obj.keys())
    extra = sorted(set(obj.keys()) - allowed)
    for key in missing:
        add_error(report, f"{ctx}: missing required key '{key}'")
    for key in extra:
        add_error(report, f"{ctx}: unknown key '{key}'")


def _load_catalog(path: Path, list_key: str, report: dict[str, Any]) -> list[dict[str, Any]]:
    data = load_json_file(path)
    if not isinstance(data, dict):
        add_error(report, f"{path}: top-level must be an object")
        return []
    if data.get("schemaVersion") != 1:
        add_error(report, f"{path}: schemaVersion must be 1")
    values = data.get(list_key)
    if not isinstance(values, list):
        add_error(report, f"{path}: '{list_key}' must be an array")
        return []
    entries: list[dict[str, Any]] = []
    for i, entry in enumerate(values):
        if not isinstance(entry, dict):
            add_error(report, f"{path}:{list_key}[{i}] must be an object")
            continue
        entries.append(entry)
    return entries


def _load_package_manifest(path: Path, report: dict[str, Any], context: str) -> dict[str, Any] | None:
    try:
        data = load_json_file(path)
    except SystemExit as exc:
        add_error(report, f"{context}: {exc}")
        return None
    if not isinstance(data, dict):
        add_error(report, f"{context}: package manifest must be a JSON object")
        return None
    return data


def _validate_package_manifest(
    package: dict[str, Any],
    context: str,
    report: dict[str, Any],
) -> bool:
    allowed_keys = {
        "schemaVersion",
        "name",
        "version",
        "compatiblePlatformRange",
        "platforms",
        "dependencies",
        "bootstrap",
        "provides",
    }
    _check_exact_keys(
        package,
        {"schemaVersion", "name", "version", "compatiblePlatformRange", "platforms", "dependencies", "provides"},
        allowed_keys,
        context,
        report,
    )

    ok = True
    if package.get("schemaVersion") != 1:
        add_error(report, f"{context}.schemaVersion must be 1")
        ok = False

    name = package.get("name")
    if not isinstance(name, str) or not PACKAGE_NAME_RE.match(name):
        add_error(report, f"{context}.name must be a valid package name")
        ok = False

    version = package.get("version")
    if not isinstance(version, str) or not _valid_semver(version):
        add_error(report, f"{context}.version must be valid semver")
        ok = False

    compatible_range = package.get("compatiblePlatformRange")
    if not isinstance(compatible_range, str) or not _valid_version_range(compatible_range):
        add_error(report, f"{context}.compatiblePlatformRange contains invalid semver range token(s)")
        ok = False

    platforms = package.get("platforms")
    if not _is_string_list(platforms) or not platforms:
        add_error(report, f"{context}.platforms must be a non-empty array of strings")
        ok = False

    dependencies = package.get("dependencies")
    if not isinstance(dependencies, list):
        add_error(report, f"{context}.dependencies must be an array")
        ok = False
    else:
        for index, dependency in enumerate(dependencies):
            dep_ctx = f"{context}.dependencies[{index}]"
            if not isinstance(dependency, dict):
                add_error(report, f"{dep_ctx} must be an object")
                ok = False
                continue
            _check_exact_keys(
                dependency,
                {"name", "versionRange", "optional"},
                {"name", "versionRange", "optional"},
                dep_ctx,
                report,
            )
            dep_name = dependency.get("name")
            if not isinstance(dep_name, str) or not PACKAGE_NAME_RE.match(dep_name):
                add_error(report, f"{dep_ctx}.name must be a valid package name")
                ok = False
            dep_range = dependency.get("versionRange")
            if not isinstance(dep_range, str) or not _valid_version_range(dep_range):
                add_error(report, f"{dep_ctx}.versionRange contains invalid semver range token(s)")
                ok = False
            if not isinstance(dependency.get("optional"), bool):
                add_error(report, f"{dep_ctx}.optional must be a boolean")
                ok = False

    provides = package.get("provides")
    if not isinstance(provides, dict):
        add_error(report, f"{context}.provides must be an object")
        ok = False
    else:
        _check_exact_keys(
            provides,
            {"modules", "plugins"},
            {"modules", "plugins"},
            f"{context}.provides",
            report,
        )
        if not _is_string_list(provides.get("modules")):
            add_error(report, f"{context}.provides.modules must be an array of strings")
            ok = False
        if not _is_string_list(provides.get("plugins")):
            add_error(report, f"{context}.provides.plugins must be an array of strings")
            ok = False

    bootstrap = package.get("bootstrap")
    if bootstrap is not None:
        if not isinstance(bootstrap, dict):
            add_error(report, f"{context}.bootstrap must be an object")
            ok = False
        else:
            _check_exact_keys(
                bootstrap,
                {"mode", "entryPoint", "autoApply"},
                {"mode", "entryPoint", "autoApply"},
                f"{context}.bootstrap",
                report,
            )
            if bootstrap.get("mode") != "BuilderHookV1":
                add_error(report, f"{context}.bootstrap.mode must be 'BuilderHookV1'")
                ok = False
            if not isinstance(bootstrap.get("entryPoint"), str):
                add_error(report, f"{context}.bootstrap.entryPoint must be a string")
                ok = False
            if not isinstance(bootstrap.get("autoApply"), bool):
                add_error(report, f"{context}.bootstrap.autoApply must be a boolean")
                ok = False

    return ok


def _validate_package_catalog_entries(
    packages: list[dict[str, Any]],
    component_names: set[str],
    report: dict[str, Any],
) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    allowed_keys = {"name", "manifestPath", "component"}

    for index, package in enumerate(packages):
        ctx = f"package-catalog.packages[{index}]"
        _check_exact_keys(package, {"name", "manifestPath"}, allowed_keys, ctx, report)

        name = package.get("name")
        if not isinstance(name, str) or not PACKAGE_NAME_RE.match(name):
            add_error(report, f"{ctx}.name must be a valid package name")
            continue
        if name in result:
            add_error(report, f"{ctx}.name duplicates '{name}'")
            continue

        manifest_path = package.get("manifestPath")
        if not isinstance(manifest_path, str) or not manifest_path:
            add_error(report, f"{ctx}.manifestPath must be a non-empty string")
            continue

        component = package.get("component")
        if component is not None:
            if not isinstance(component, str) or not COMPONENT_NAME_RE.match(component):
                add_error(report, f"{ctx}.component must be a valid NGIN component name")
            elif component not in component_names:
                add_error(report, f"{ctx}.component references unknown manifest component '{component}'")

        result[name] = package

    return result


def _schema_validate_with_key(path: Path, schema_key: str, report: dict[str, Any]) -> None:
    try:
        import jsonschema
    except ModuleNotFoundError:
        add_error(report, "python dependency missing: 'jsonschema' is required for workspace validation")
        return

    schema = load_json_file(SCHEMA_PATHS[schema_key])
    payload = load_json_file(path)
    if not isinstance(schema, dict):
        add_error(report, f"{SCHEMA_PATHS[schema_key]}: schema must be a JSON object")
        return

    validator = jsonschema.Draft202012Validator(schema)
    errors = sorted(validator.iter_errors(payload), key=lambda err: list(err.absolute_path))
    for err in errors:
        pointer = _json_pointer(list(err.absolute_path))
        add_error(report, f"{path}{pointer}: {err.message}")


def _schema_validate_project_manifest(project_path: Path, report: dict[str, Any]) -> None:
    _schema_validate_with_key(project_path, "project", report)


def _schema_validate_lockfile(lockfile_path: Path, report: dict[str, Any]) -> None:
    _schema_validate_with_key(lockfile_path, "package_lock", report)


def _effective_family(module: dict[str, Any]) -> str:
    mod_type = str(module.get("type", ""))
    if mod_type in {"Developer", "ThirdParty"}:
        return "App"
    return str(module.get("family", ""))


def _dependency_allowed(source_module: dict[str, Any], target_module: dict[str, Any]) -> bool:
    src_family = _effective_family(source_module)
    dst_family = _effective_family(target_module)
    row = DEPENDENCY_MATRIX.get(src_family)
    if row is None:
        return False
    rule = row.get(dst_family)
    if rule in {"Y", "O"}:
        return True
    # Spec note: Base->Base is marked N due family granularity; allow as intra-family foundation dependency.
    if src_family == "Base" and dst_family == "Base":
        return True
    return False


def _cycle_nodes(edges: dict[str, set[str]], nodes: set[str]) -> list[str]:
    indegree: dict[str, int] = {n: 0 for n in nodes}
    for src in nodes:
        for dst in edges.get(src, set()):
            if dst in indegree:
                indegree[dst] += 1

    queue: deque[str] = deque(sorted(n for n, deg in indegree.items() if deg == 0))
    visited = 0
    while queue:
        cur = queue.popleft()
        visited += 1
        for nxt in sorted(edges.get(cur, set())):
            if nxt not in indegree:
                continue
            indegree[nxt] -= 1
            if indegree[nxt] == 0:
                queue.append(nxt)
    if visited == len(nodes):
        return []
    return sorted(n for n, deg in indegree.items() if deg > 0)


def _topological_dependencies_first(nodes: set[str], dependency_edges: dict[str, set[str]]) -> list[str] | None:
    indegree: dict[str, int] = {n: 0 for n in nodes}
    dependents: dict[str, set[str]] = {n: set() for n in nodes}

    for node in nodes:
        deps = dependency_edges.get(node, set())
        indegree[node] = sum(1 for dep in deps if dep in nodes)
        for dep in deps:
            if dep in nodes:
                dependents.setdefault(dep, set()).add(node)

    queue: list[str] = sorted(n for n, deg in indegree.items() if deg == 0)
    ordered: list[str] = []

    while queue:
        cur = queue.pop(0)
        ordered.append(cur)
        for dep in sorted(dependents.get(cur, set())):
            indegree[dep] -= 1
            if indegree[dep] == 0:
                queue.append(dep)
                queue.sort()

    if len(ordered) != len(nodes):
        return None
    return ordered


def _validate_module_entries(
    modules: list[dict[str, Any]],
    component_names: set[str],
    report: dict[str, Any],
) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    allowed_keys = {
        "name",
        "family",
        "type",
        "component",
        "version",
        "compatiblePlatformRange",
        "platforms",
        "loadPhase",
        "dependencies",
        "flags",
    }

    for i, module in enumerate(modules):
        ctx = f"module-catalog.modules[{i}]"
        _check_exact_keys(module, allowed_keys, allowed_keys, ctx, report)

        name = module.get("name")
        if not isinstance(name, str) or not MODULE_NAME_RE.match(name):
            add_error(report, f"{ctx}.name must match '<Family>.<Name>'")
            continue
        if name in result:
            add_error(report, f"{ctx}.name duplicates '{name}'")
            continue

        family = module.get("family")
        if family not in MODULE_FAMILIES:
            add_error(report, f"{ctx}.family must be one of {sorted(MODULE_FAMILIES)}")

        mod_type = module.get("type")
        if mod_type not in MODULE_TYPES:
            add_error(report, f"{ctx}.type must be one of {sorted(MODULE_TYPES)}")

        component = module.get("component")
        if not isinstance(component, str) or not COMPONENT_NAME_RE.match(component):
            add_error(report, f"{ctx}.component must be a valid NGIN component name")
        elif component not in component_names:
            add_error(report, f"{ctx}.component references unknown manifest component '{component}'")

        version = module.get("version")
        if not isinstance(version, str):
            add_error(report, f"{ctx}.version must be a string")
        elif not _valid_semver(version):
            add_error(report, f"{ctx}.version must be valid semver (major.minor.patch[-prerelease])")

        compatible_range = module.get("compatiblePlatformRange")
        if not isinstance(compatible_range, str):
            add_error(report, f"{ctx}.compatiblePlatformRange must be a string")
        elif not _valid_version_range(compatible_range):
            add_error(report, f"{ctx}.compatiblePlatformRange contains invalid semver range token(s)")

        if not _is_string_list(module.get("platforms")):
            add_error(report, f"{ctx}.platforms must be an array of strings")
        load_phase = module.get("loadPhase")
        if not isinstance(load_phase, str):
            add_error(report, f"{ctx}.loadPhase must be a string")
        elif load_phase not in LOAD_PHASES:
            add_error(report, f"{ctx}.loadPhase must be one of {sorted(LOAD_PHASES)}")

        deps = module.get("dependencies")
        if not isinstance(deps, dict):
            add_error(report, f"{ctx}.dependencies must be an object")
        else:
            _check_exact_keys(deps, {"required", "optional"}, {"required", "optional"}, f"{ctx}.dependencies", report)
            req = deps.get("required")
            opt = deps.get("optional")
            if not _is_string_list(req):
                add_error(report, f"{ctx}.dependencies.required must be an array of strings")
            if not _is_string_list(opt):
                add_error(report, f"{ctx}.dependencies.optional must be an array of strings")

        flags = module.get("flags")
        if not isinstance(flags, dict):
            add_error(report, f"{ctx}.flags must be an object")
        else:
            _check_exact_keys(flags, {"editorOnly", "requiresReflection"}, {"editorOnly", "requiresReflection"}, f"{ctx}.flags", report)
            if not isinstance(flags.get("editorOnly"), bool):
                add_error(report, f"{ctx}.flags.editorOnly must be a boolean")
            if not isinstance(flags.get("requiresReflection"), bool):
                add_error(report, f"{ctx}.flags.requiresReflection must be a boolean")

        result[name] = module

    return result


def _validate_plugin_entries(
    plugins: list[dict[str, Any]],
    report: dict[str, Any],
) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    allowed_keys = {
        "name",
        "version",
        "compatiblePlatformRange",
        "abiVersion",
        "modules",
        "platforms",
    }

    for i, plugin in enumerate(plugins):
        ctx = f"plugin-catalog.plugins[{i}]"
        _check_exact_keys(plugin, allowed_keys, allowed_keys, ctx, report)

        name = plugin.get("name")
        if not isinstance(name, str) or not PLUGIN_NAME_RE.match(name):
            add_error(report, f"{ctx}.name must match '<Vendor>.<PluginName>'")
            continue
        if name in result:
            add_error(report, f"{ctx}.name duplicates '{name}'")
            continue

        version = plugin.get("version")
        if not isinstance(version, str):
            add_error(report, f"{ctx}.version must be a string")
        elif not _valid_semver(version):
            add_error(report, f"{ctx}.version must be valid semver (major.minor.patch[-prerelease])")

        compatible_range = plugin.get("compatiblePlatformRange")
        if not isinstance(compatible_range, str):
            add_error(report, f"{ctx}.compatiblePlatformRange must be a string")
        elif not _valid_version_range(compatible_range):
            add_error(report, f"{ctx}.compatiblePlatformRange contains invalid semver range token(s)")

        abi = plugin.get("abiVersion")
        if abi is not None and not isinstance(abi, int):
            add_error(report, f"{ctx}.abiVersion must be integer or null")

        modules = plugin.get("modules")
        if not isinstance(modules, dict):
            add_error(report, f"{ctx}.modules must be an object")
        else:
            _check_exact_keys(modules, {"required", "optional"}, {"required", "optional"}, f"{ctx}.modules", report)
            if not _is_string_list(modules.get("required")):
                add_error(report, f"{ctx}.modules.required must be an array of strings")
            if not _is_string_list(modules.get("optional")):
                add_error(report, f"{ctx}.modules.optional must be an array of strings")

        if not _is_string_list(plugin.get("platforms")):
            add_error(report, f"{ctx}.platforms must be an array of strings")

        result[name] = plugin

    return result


def _validate_component_graph(manifest: dict[str, Any], report: dict[str, Any]) -> dict[str, dict[str, Any]]:
    components = iter_components(manifest)
    by_name: dict[str, dict[str, Any]] = {}

    for idx, component in enumerate(components):
        ctx = f"platform-release.components[{idx}]"
        name = component.get("name")
        if not isinstance(name, str) or not COMPONENT_NAME_RE.match(name):
            add_error(report, f"{ctx}.name must match 'NGIN.<Component>'")
            continue
        if name in by_name:
            add_error(report, f"{ctx}.name duplicates '{name}'")
            continue
        by_name[name] = component

    edges: dict[str, set[str]] = {name: set() for name in by_name}
    for name, component in by_name.items():
        deps = component.get("dependsOn", [])
        if deps is None:
            deps = []
        if not isinstance(deps, list) or not all(isinstance(dep, str) for dep in deps):
            add_error(report, f"component '{name}' dependsOn must be a string array")
            continue
        for dep in deps:
            if dep not in by_name:
                add_error(report, f"component '{name}' dependsOn unknown component '{dep}'")
                continue
            if dep == name:
                add_error(report, f"component '{name}' cannot depend on itself")
                continue
            edges[name].add(dep)

    if "NGIN.Base" in by_name:
        base_deps = by_name["NGIN.Base"].get("dependsOn", [])
        if isinstance(base_deps, list) and base_deps:
            add_error(report, "component graph violation: NGIN.Base must not depend on higher-level components")

    cycles = _cycle_nodes(edges, set(by_name.keys()))
    if cycles:
        add_error(report, f"component graph contains cycle(s) involving: {', '.join(cycles)}")

    return by_name


def _validate_required_ref_policy(manifest: dict[str, Any], report: dict[str, Any]) -> None:
    channel = manifest.get("channel")
    if not isinstance(channel, str):
        add_error(report, f"{MANIFEST_PATH}: channel must be a string")
        return

    if channel not in {"alpha", "beta", "stable"}:
        return

    for idx, component in enumerate(iter_components(manifest)):
        required = bool(component.get("required", False))
        if not required:
            continue

        ref = component.get("ref")
        ref_text = ref.strip() if isinstance(ref, str) else ""
        if not ref_text:
            name = str(component.get("name", "<unknown>"))
            add_error(
                report,
                f"{MANIFEST_PATH}: components[{idx}] '{name}' has required=true but null/empty ref on channel '{channel}'",
            )


def _validate_manifest_semantics(manifest: dict[str, Any], report: dict[str, Any]) -> None:
    platform_version = manifest.get("platformVersion")
    if not isinstance(platform_version, str):
        add_error(report, f"{MANIFEST_PATH}: platformVersion must be a string")
    elif not _valid_semver(platform_version):
        add_error(report, f"{MANIFEST_PATH}: platformVersion must be valid semver (major.minor.patch[-prerelease])")


def _validate_module_graph(modules_by_name: dict[str, dict[str, Any]], report: dict[str, Any]) -> None:
    required_edges: dict[str, set[str]] = {name: set() for name in modules_by_name}

    for module_name, module in modules_by_name.items():
        deps = module.get("dependencies", {})
        req = deps.get("required", []) if isinstance(deps, dict) else []
        opt = deps.get("optional", []) if isinstance(deps, dict) else []

        for dep in req:
            if dep not in modules_by_name:
                add_error(report, f"module '{module_name}' required dependency '{dep}' does not exist")
            else:
                required_edges[module_name].add(dep)

        for dep in opt:
            if dep not in modules_by_name:
                add_error(report, f"module '{module_name}' optional dependency '{dep}' does not exist")

        for dep in req + opt:
            if dep not in modules_by_name:
                continue
            dep_module = modules_by_name[dep]
            if not _dependency_allowed(module, dep_module):
                src_family = _effective_family(module)
                dst_family = _effective_family(dep_module)
                add_error(
                    report,
                    f"module graph violation: '{module_name}' ({src_family}) cannot depend on '{dep}' ({dst_family})",
                )

    cycle_nodes = _cycle_nodes(required_edges, set(modules_by_name.keys()))
    if cycle_nodes:
        add_error(report, f"module graph contains required-dependency cycle(s) involving: {', '.join(cycle_nodes)}")


def _resolve_component_scan_maps(components_by_name: dict[str, dict[str, Any]]) -> tuple[dict[str, str], dict[str, str]]:
    package_to_component: dict[str, str] = {}
    target_to_component: dict[str, str] = {}

    for component_name, component in components_by_name.items():
        suffix = component_name.split(".")[-1]
        target_to_component[suffix] = component_name

        package_name = component.get("packageName")
        if isinstance(package_name, str) and package_name:
            package_to_component[package_name] = component_name

    return package_to_component, target_to_component


def _iter_cmake_files(repo_path: Path) -> list[Path]:
    files: list[Path] = []
    for path in repo_path.rglob("*"):
        if not path.is_file():
            continue
        if path.name != "CMakeLists.txt" and path.suffix.lower() != ".cmake":
            continue
        lowered_parts = {part.lower() for part in path.parts}
        if ".git" in lowered_parts or "build" in lowered_parts or "build-dev" in lowered_parts or "build-all" in lowered_parts:
            continue
        files.append(path)
    return sorted(files)


def _scan_component_dependencies(
    component_name: str,
    repo_path: Path,
    package_to_component: dict[str, str],
    target_to_component: dict[str, str],
) -> set[str]:
    observed: set[str] = set()
    for cmake_file in _iter_cmake_files(repo_path):
        try:
            text = cmake_file.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue

        for match in FIND_PACKAGE_RE.finditer(text):
            package = match.group(1)
            dep_component = package_to_component.get(package)
            if dep_component and dep_component != component_name:
                observed.add(dep_component)

        for match in NGIN_TARGET_RE.finditer(text):
            target = match.group(1)
            dep_component = target_to_component.get(target)
            if dep_component and dep_component != component_name:
                observed.add(dep_component)

    return observed


def _validate_static_scan(
    components_by_name: dict[str, dict[str, Any]],
    overrides: dict[str, str],
    target_dir: Path,
    report: dict[str, Any],
) -> None:
    package_to_component, target_to_component = _resolve_component_scan_maps(components_by_name)

    for component_name, component in components_by_name.items():
        declared = component.get("dependsOn", [])
        declared_set = set(declared) if isinstance(declared, list) else set()

        source, path = resolve_component_path(component_name, overrides, target_dir)
        if path is None:
            add_scan_skipped(report, f"{component_name}: scan_skipped (component not present locally)")
            continue
        if not path.exists() or not (path / "CMakeLists.txt").exists():
            add_scan_skipped(report, f"{component_name}: scan_skipped (no CMake project at {path})")
            continue

        observed = _scan_component_dependencies(component_name, path, package_to_component, target_to_component)
        undeclared = sorted(dep for dep in observed if dep not in declared_set)
        for dep in undeclared:
            add_error(
                report,
                f"static scan violation: '{component_name}' references '{dep}' in CMake but it is not declared in dependsOn",
            )


def _collect_target_modules(
    target: dict[str, Any],
    plugins_by_name: dict[str, dict[str, Any]],
) -> tuple[list[str], list[str]]:
    required = list(target.get("modules", []))
    optional: list[str] = []
    for plugin_name in target.get("plugins", []):
        plugin = plugins_by_name.get(plugin_name)
        if not plugin:
            continue
        modules = plugin.get("modules", {})
        if isinstance(modules, dict):
            required.extend([m for m in modules.get("required", []) if isinstance(m, str)])
            optional.extend([m for m in modules.get("optional", []) if isinstance(m, str)])
    return required, optional


def _validate_targets_and_plugins(
    manifest: dict[str, Any],
    modules_by_name: dict[str, dict[str, Any]],
    plugins_by_name: dict[str, dict[str, Any]],
    targets_by_name: dict[str, dict[str, Any]],
    report: dict[str, Any],
) -> None:
    platform_abi = None
    compatibility = manifest.get("compatibility")
    if isinstance(compatibility, dict):
        platform_abi = compatibility.get("pluginAbiVersion")

    for plugin_name, plugin in plugins_by_name.items():
        modules = plugin.get("modules", {})
        if isinstance(modules, dict):
            for dep in modules.get("required", []) + modules.get("optional", []):
                if dep not in modules_by_name:
                    add_error(report, f"plugin '{plugin_name}' references unknown module '{dep}'")

        plugin_abi = plugin.get("abiVersion")
        if platform_abi is None:
            pass
        elif plugin_abi is None:
            add_error(report, f"plugin '{plugin_name}' abiVersion is null but platform requires abiVersion={platform_abi}")
        elif plugin_abi != platform_abi:
            add_error(
                report,
                f"plugin '{plugin_name}' abiVersion={plugin_abi} does not match platform abiVersion={platform_abi}",
            )

    for target_name, target in targets_by_name.items():
        target_type = str(target.get("type", ""))
        if target_type == "Editor" and target.get("enableReflection") is not True:
            add_error(report, f"target '{target_name}' must set enableReflection=true for Editor targets")

        required_modules, optional_modules = _collect_target_modules(target, plugins_by_name)

        for plugin_name in target.get("plugins", []):
            if plugin_name not in plugins_by_name:
                add_error(report, f"target '{target_name}' references unknown plugin '{plugin_name}'")

        for module_name in required_modules + optional_modules:
            if module_name not in modules_by_name:
                add_error(report, f"target '{target_name}' references unknown module '{module_name}'")
                continue

            module = modules_by_name[module_name]
            mod_type = str(module.get("type", ""))
            allowed_types = TARGET_ALLOWED_MODULE_TYPES.get(target_type, set())
            if mod_type not in allowed_types:
                add_error(
                    report,
                    f"target '{target_name}' ({target_type}) cannot include module '{module_name}' of type '{mod_type}'",
                )

            flags = module.get("flags", {})
            editor_only = isinstance(flags, dict) and bool(flags.get("editorOnly", False))
            requires_reflection = isinstance(flags, dict) and bool(flags.get("requiresReflection", False))

            if editor_only and target_type != "Editor":
                add_error(report, f"target '{target_name}' includes editor-only module '{module_name}'")
            if requires_reflection and target.get("enableReflection") is not True:
                add_error(report, f"target '{target_name}' includes module '{module_name}' that requires reflection")


def _normalize_package_reference(package: dict[str, Any]) -> dict[str, Any]:
    return {
        "name": str(package.get("name", "")),
        "versionRange": str(package.get("versionRange", "")) if package.get("versionRange") is not None else "",
        "optional": bool(package.get("optional", False)),
    }


def _project_target_to_target_entry(project_path: Path, project: dict[str, Any], target: dict[str, Any]) -> dict[str, Any]:
    modules = target.get("modules", {})
    plugins = target.get("plugins", {})
    enable_modules = modules.get("enable", []) if isinstance(modules, dict) else []
    disable_modules = modules.get("disable", []) if isinstance(modules, dict) else []
    enable_plugins = plugins.get("enable", []) if isinstance(plugins, dict) else []
    disable_plugins = plugins.get("disable", []) if isinstance(plugins, dict) else []

    filtered_modules = [name for name in enable_modules if isinstance(name, str) and name not in set(disable_modules)]
    filtered_plugins = [name for name in enable_plugins if isinstance(name, str) and name not in set(disable_plugins)]

    return {
        "name": target.get("name"),
        "type": target.get("type"),
        "platform": target.get("platform"),
        "enableReflection": bool(target.get("enableReflection", False)),
        "packages": [_normalize_package_reference(package) for package in target.get("packages", []) if isinstance(package, dict)],
        "modules": filtered_modules,
        "plugins": filtered_plugins,
        "stages": list(DEFAULT_STAGES),
        "sourceKind": "project",
        "projectPath": str(project_path),
        "projectName": project.get("name"),
    }


def _discover_project_file(start_dir: Path) -> Path | None:
    current = start_dir.resolve()
    while True:
        candidate = current / "ngin.project.json"
        if candidate.exists():
            return candidate
        if current == current.parent:
            return None
        current = current.parent


def _resolve_project_path(project_path_text: str | None) -> Path:
    if project_path_text:
        return Path(project_path_text).resolve()

    discovered = _discover_project_file(Path.cwd())
    if discovered is not None:
        return discovered

    raise SystemExit("error: no project manifest specified and no ngin.project.json found in the current directory tree")


def _load_project_targets(project_path: Path) -> tuple[dict[str, Any], dict[str, dict[str, Any]]]:
    project = load_json_file(project_path)
    if not isinstance(project, dict):
        raise SystemExit(f"error: project manifest must be a JSON object: {project_path}")
    if project.get("schemaVersion") != 1:
        raise SystemExit(f"error: project manifest schemaVersion must be 1: {project_path}")
    if not isinstance(project.get("targets"), list):
        raise SystemExit(f"error: project manifest targets must be an array: {project_path}")

    targets_by_name: dict[str, dict[str, Any]] = {}
    for target in project["targets"]:
        if not isinstance(target, dict):
            continue
        normalized = _project_target_to_target_entry(project_path, project, target)
        name = normalized.get("name")
        if isinstance(name, str) and name:
            targets_by_name[name] = normalized

    return project, targets_by_name


def _default_lockfile_path(project_path: Path) -> Path:
    return project_path.parent / "ngin.lock.json"


def _resolve_lockfile_path(lockfile_text: str | None, project_path: Path | None) -> Path:
    if lockfile_text:
        return Path(lockfile_text).resolve()
    if project_path is not None:
        return _default_lockfile_path(project_path).resolve()
    raise SystemExit("error: --locked requires --lockfile when no project manifest is specified")


def _cache_package_dir(cache_dir: Path, package_name: str, version: str) -> Path:
    return cache_dir / "packages" / package_name / version


def _iter_cached_entry_paths(cache_dir: Path) -> list[Path]:
    packages_root = cache_dir / "packages"
    if not packages_root.exists():
        return []
    return sorted(path for path in packages_root.rglob("entry.json") if path.is_file())


def _load_cached_entry(entry_path: Path) -> dict[str, Any]:
    data = load_json_file(entry_path)
    if not isinstance(data, dict):
        raise SystemExit(f"error: cache entry must be a JSON object: {entry_path}")
    return data


def _build_cache_entry(
    package: dict[str, Any],
    manifest_hash: str,
    cache_manifest_path: Path,
    cache_entry_path: Path,
) -> dict[str, Any]:
    return {
        "schemaVersion": 1,
        "name": package["name"],
        "version": package["version"],
        "sourceKind": package["source"],
        "sourceManifestPath": package["manifestPath"],
        "manifestHash": manifest_hash,
        "cachedManifestPath": str(cache_manifest_path.name),
        "cacheEntryPath": str(cache_entry_path),
        "dependencies": list(package.get("dependencies", [])),
        "provides": dict(package.get("provides", {})),
    }


def _cache_entry_summary(entry_path: Path, entry: dict[str, Any]) -> dict[str, Any]:
    return {
        "name": entry.get("name"),
        "version": entry.get("version"),
        "sourceKind": entry.get("sourceKind"),
        "sourceManifestPath": entry.get("sourceManifestPath"),
        "manifestHash": entry.get("manifestHash"),
        "cacheEntryPath": str(entry_path),
        "cachedManifestPath": str(entry_path.parent / str(entry.get("cachedManifestPath", ""))),
        "dependencies": list(entry.get("dependencies", [])),
        "provides": dict(entry.get("provides", {})),
    }


def _lockfile_target_index(lockfile: dict[str, Any]) -> dict[str, dict[str, Any]]:
    targets = lockfile.get("targets", [])
    if not isinstance(targets, list):
        raise SystemExit("error: lockfile targets must be an array")
    result: dict[str, dict[str, Any]] = {}
    for target in targets:
        if not isinstance(target, dict):
            continue
        name = target.get("name")
        if isinstance(name, str) and name:
            result[name] = target
    return result


def _load_lockfile(lockfile_path: Path) -> dict[str, Any]:
    if not lockfile_path.exists():
        raise SystemExit(f"error: lockfile not found: {lockfile_path}")
    data = load_json_file(lockfile_path)
    if not isinstance(data, dict):
        raise SystemExit(f"error: lockfile must be a JSON object: {lockfile_path}")
    report = make_report()
    _schema_validate_lockfile(lockfile_path, report)
    if report["errors"]:
        raise SystemExit(f"error: invalid lockfile '{lockfile_path}': {report['errors'][0]}")
    return data


def _parse_package_override(text: str) -> tuple[str, str]:
    match = PACKAGE_OVERRIDE_RE.fullmatch(text)
    if not match:
        raise SystemExit(f"error: invalid package override '{text}', expected Package.Name=/path/to/ngin.package.json")
    return match.group("name"), match.group("path")


def _build_package_override_map(values: list[str] | None) -> dict[str, str]:
    out: dict[str, str] = {}
    for value in values or []:
        name, path = _parse_package_override(value)
        out[name] = path
    return out


def _resolve_package_manifest_path(path_text: str) -> Path:
    path = Path(path_text)
    if not path.is_absolute():
        path = ROOT_DIR / path
    return path


def _package_catalog_index(packages_by_name: dict[str, dict[str, Any]]) -> dict[str, Path]:
    result: dict[str, Path] = {}
    for name, entry in packages_by_name.items():
        manifest_path = entry.get("manifestPath")
        if isinstance(manifest_path, str) and manifest_path:
            result[name] = _resolve_package_manifest_path(manifest_path)
    return result


def _resolve_package_manifest_candidate(path: Path) -> Path:
    if path.is_dir():
        return path / "ngin.package.json"
    return path


def _resolve_package_manifest_from_roots(package_name: str, roots: list[Path]) -> Path | None:
    package_leaf = package_name.replace(".", "/")
    for root in roots:
        direct = _resolve_package_manifest_candidate(root / package_leaf)
        if direct.exists():
            return direct
        sibling = _resolve_package_manifest_candidate(root / package_name)
        if sibling.exists():
            return sibling
    return None


def _load_package_catalog(
    manifest: dict[str, Any],
    report: dict[str, Any],
) -> dict[str, dict[str, Any]]:
    packages = _load_catalog(CATALOG_PATHS["package"], "packages", report)
    components_by_name = _validate_component_graph(manifest, make_report())
    return _validate_package_catalog_entries(packages, set(components_by_name.keys()), report)


def _resolve_package_graph(
    direct_references: list[dict[str, Any]],
    target_platform: str,
    manifest: dict[str, Any],
    catalog_packages_by_name: dict[str, dict[str, Any]],
    report: dict[str, Any],
    *,
    package_roots: list[Path],
    package_override_map: dict[str, str],
    workspace_package_overrides: dict[str, str],
    allow_package_probe: bool,
) -> tuple[list[dict[str, Any]], dict[str, dict[str, Any]], dict[str, set[str]]]:
    platform_version = str(manifest.get("platformVersion", ""))
    catalog_index = _package_catalog_index(catalog_packages_by_name)
    resolved: dict[str, dict[str, Any]] = {}
    dependency_edges: dict[str, set[str]] = defaultdict(set)
    queue: deque[tuple[dict[str, Any], str | None]] = deque()

    for reference in direct_references:
        queue.append((reference, None))

    while queue:
        reference, required_by = queue.popleft()
        package_name = str(reference.get("name", ""))
        version_range = str(reference.get("versionRange", "")) if reference.get("versionRange") is not None else ""
        optional = bool(reference.get("optional", False))
        if not package_name:
            continue

        manifest_path: Path | None = None
        source = "catalog"
        if package_name in package_override_map:
            manifest_path = _resolve_package_manifest_candidate(Path(package_override_map[package_name]))
            source = "cli-override"
        elif package_name in workspace_package_overrides:
            manifest_path = _resolve_package_manifest_candidate(Path(workspace_package_overrides[package_name]))
            source = "workspace-override"
        elif package_name in catalog_index:
            manifest_path = catalog_index[package_name]
            source = "catalog"
        elif allow_package_probe:
            manifest_path = _resolve_package_manifest_from_roots(package_name, package_roots)
            source = "probe"

        if manifest_path is None or not manifest_path.exists():
            message = f"package '{package_name}' could not be resolved"
            if required_by:
                message += f" (required by '{required_by}')"
            if optional:
                add_warning(report, message)
                continue
            add_error(report, message)
            continue

        if package_name in resolved:
            if version_range and not _version_satisfies_range(resolved[package_name]["version"], version_range):
                message = (
                    f"package '{package_name}' version {resolved[package_name]['version']} does not satisfy "
                    f"requested range '{version_range}'"
                )
                if optional:
                    add_warning(report, message)
                else:
                    add_error(report, message)
            if required_by:
                dependency_edges[required_by].add(package_name)
            continue

        package = _load_package_manifest(manifest_path, report, f"package '{package_name}'")
        if package is None or not _validate_package_manifest(package, f"package '{package_name}'", report):
            if optional:
                add_warning(report, f"package '{package_name}' manifest is invalid and will be skipped")
                continue
            add_error(report, f"package '{package_name}' manifest is invalid")
            continue

        actual_name = str(package.get("name", ""))
        if actual_name != package_name:
            message = f"package '{package_name}' resolved to manifest for '{actual_name}'"
            if optional:
                add_warning(report, message)
                continue
            add_error(report, message)
            continue

        package_version = str(package.get("version", ""))
        if version_range and not _version_satisfies_range(package_version, version_range):
            message = f"package '{package_name}' version {package_version} does not satisfy '{version_range}'"
            if optional:
                add_warning(report, message)
                continue
            add_error(report, message)
            continue

        package_platforms = package.get("platforms", [])
        if not isinstance(package_platforms, list) or not _platform_supported(target_platform, package_platforms):
            message = f"package '{package_name}' is not supported on platform '{target_platform}'"
            if optional:
                add_warning(report, message)
                continue
            add_error(report, message)
            continue

        compatible_range = str(package.get("compatiblePlatformRange", ""))
        if platform_version and compatible_range and not _version_satisfies_range(platform_version, compatible_range):
            message = (
                f"package '{package_name}' compatiblePlatformRange '{compatible_range}' does not include "
                f"platform version '{platform_version}'"
            )
            if optional:
                add_warning(report, message)
                continue
            add_error(report, message)
            continue

        package_record = {
            "name": package_name,
            "version": package_version,
            "manifestPath": str(manifest_path),
            "manifestHash": _file_sha256(manifest_path),
            "source": source,
            "component": catalog_packages_by_name.get(package_name, {}).get("component"),
            "dependencies": package.get("dependencies", []),
            "provides": package.get("provides", {}),
        }
        resolved[package_name] = package_record

        if required_by:
            dependency_edges[required_by].add(package_name)
        dependency_edges.setdefault(package_name, set())

        for dependency in package_record["dependencies"]:
            if not isinstance(dependency, dict):
                continue
            dependency_name = dependency.get("name")
            if isinstance(dependency_name, str):
                dependency_edges[package_name].add(dependency_name)
                queue.append((_normalize_package_reference(dependency), package_name))

    cycles = _cycle_nodes({name: set(edges) for name, edges in dependency_edges.items()}, set(resolved.keys()))
    if cycles:
        add_error(report, f"package graph contains dependency cycle(s) involving: {', '.join(cycles)}")
        return [], resolved, dependency_edges

    ordered = _topological_dependencies_first(set(resolved.keys()), {name: set(edges) for name, edges in dependency_edges.items()})
    if ordered is None:
        add_error(report, "package graph could not be ordered")
        return [], resolved, dependency_edges

    return [resolved[name] for name in ordered], resolved, dependency_edges


def _validate_package_provides(
    ordered_packages: list[dict[str, Any]],
    modules_by_name: dict[str, dict[str, Any]],
    plugins_by_name: dict[str, dict[str, Any]],
    report: dict[str, Any],
) -> tuple[dict[str, set[str]], dict[str, set[str]]]:
    providers_by_module: dict[str, set[str]] = defaultdict(set)
    providers_by_plugin: dict[str, set[str]] = defaultdict(set)

    for package in ordered_packages:
        provides = package.get("provides", {})
        modules = provides.get("modules", []) if isinstance(provides, dict) else []
        plugins = provides.get("plugins", []) if isinstance(provides, dict) else []
        for module_name in modules:
            if module_name not in modules_by_name:
                add_error(report, f"package '{package['name']}' provides unknown module '{module_name}'")
                continue
            providers_by_module[module_name].add(package["name"])
        for plugin_name in plugins:
            if plugin_name not in plugins_by_name:
                add_error(report, f"package '{package['name']}' provides unknown plugin '{plugin_name}'")
                continue
            providers_by_plugin[plugin_name].add(package["name"])

    return providers_by_module, providers_by_plugin


def _load_target_source(
    target_name: str | None,
    project_path_text: str | None,
) -> tuple[str, dict[str, Any], dict[str, dict[str, Any]], str]:
    project_path = _resolve_project_path(project_path_text)
    project, targets_by_name = _load_project_targets(project_path)
    selected_target = target_name or str(project.get("defaultTarget", ""))
    return (
        selected_target,
        project,
        targets_by_name,
        f"project:{project_path}",
    )


def _render_target_resolution(
    target: dict[str, Any],
    *,
    ordered_packages: list[dict[str, Any]],
    required_modules: list[str],
    optional_modules: list[str],
    dependency_edges: dict[str, set[str]],
    package_edges: dict[str, set[str]],
    providers_by_module: dict[str, set[str]],
    providers_by_plugin: dict[str, set[str]],
    target_source: str,
) -> dict[str, Any]:
    stage_plan: dict[str, dict[str, list[str]]] = {}
    for stage in target.get("stages", []):
        stage_plan[stage] = {
            "requiredModules": required_modules,
            "optionalModules": optional_modules,
        }

    return {
        "ok": True,
        "target": target.get("name"),
        "source": target_source,
        "project": target.get("projectName"),
        "type": target.get("type"),
        "platform": target.get("platform"),
        "enableReflection": target.get("enableReflection"),
        "packages": [
            {
                "name": package["name"],
                "version": package["version"],
                "source": package["source"],
                "manifestPath": package["manifestPath"],
                "manifestHash": package.get("manifestHash"),
                "dependencies": list(package.get("dependencies", [])),
                "provides": dict(package.get("provides", {})),
            }
            for package in ordered_packages
        ],
        "providedByModule": {name: sorted(values) for name, values in sorted(providers_by_module.items())},
        "providedByPlugin": {name: sorted(values) for name, values in sorted(providers_by_plugin.items())},
        "requiredModules": required_modules,
        "optionalModules": optional_modules,
        "enabledPlugins": list(target.get("plugins", [])),
        "stages": stage_plan,
        "dependencyEdges": {key: sorted(value) for key, value in dependency_edges.items()},
        "packageEdges": {key: sorted(value) for key, value in package_edges.items()},
    }


def _resolve_target_with_packages(
    target: dict[str, Any],
    manifest: dict[str, Any],
    workspace_overrides: dict[str, str],
    package_overrides: dict[str, str],
    target_dir: Path,
    report: dict[str, Any],
    *,
    package_roots: list[Path],
    allow_package_probe: bool,
    catalog_packages_by_name: dict[str, dict[str, Any]],
    modules_by_name: dict[str, dict[str, Any]],
    plugins_by_name: dict[str, dict[str, Any]],
) -> dict[str, Any] | None:
    target_report = make_report()
    _validate_targets_and_plugins(
        manifest,
        modules_by_name,
        plugins_by_name,
        {str(target.get("name", "")): target},
        target_report,
    )
    report["errors"].extend(target_report["errors"])
    report["warnings"].extend(target_report["warnings"])
    if report["errors"]:
        return None

    ordered_packages, _, package_edges = _resolve_package_graph(
        target.get("packages", []),
        str(target.get("platform", "")),
        manifest,
        catalog_packages_by_name,
        report,
        package_roots=package_roots,
        package_override_map=package_overrides,
        workspace_package_overrides=workspace_overrides,
        allow_package_probe=allow_package_probe,
    )
    if report["errors"]:
        return None

    providers_by_module, providers_by_plugin = _validate_package_provides(
        ordered_packages,
        modules_by_name,
        plugins_by_name,
        report,
    )
    if report["errors"]:
        return None

    direct_modules = [name for name in target.get("modules", []) if isinstance(name, str)]
    direct_plugins = [name for name in target.get("plugins", []) if isinstance(name, str)]

    for plugin_name in direct_plugins:
        if plugin_name not in plugins_by_name:
            add_error(report, f"target '{target['name']}' references unknown plugin '{plugin_name}'")
            continue
        if providers_by_plugin and plugin_name not in providers_by_plugin:
            add_warning(report, f"target '{target['name']}' enables plugin '{plugin_name}' without a resolved package provider")

    for module_name in direct_modules:
        if module_name not in modules_by_name:
            add_error(report, f"target '{target['name']}' references unknown module '{module_name}'")
            continue
        if providers_by_module and module_name not in providers_by_module:
            add_warning(report, f"target '{target['name']}' enables module '{module_name}' without a resolved package provider")

    if report["errors"]:
        return None

    try:
        ordered_required, ordered_optional, dep_edges = _resolve_target_plan(target, modules_by_name, plugins_by_name)
    except ValueError as exc:
        add_error(report, f"failed to resolve target '{target['name']}': {exc}")
        return None

    return _render_target_resolution(
        target,
        ordered_packages=ordered_packages,
        required_modules=ordered_required,
        optional_modules=ordered_optional,
        dependency_edges=dep_edges,
        package_edges=package_edges,
        providers_by_module=providers_by_module,
        providers_by_plugin=providers_by_plugin,
        target_source=str(target.get("sourceKind", "catalog")),
    )


def _resolve_target_plan(
    target: dict[str, Any],
    modules_by_name: dict[str, dict[str, Any]],
    plugins_by_name: dict[str, dict[str, Any]],
) -> tuple[list[str], list[str], dict[str, set[str]]]:
    required_modules, optional_modules = _collect_target_modules(target, plugins_by_name)

    required_set: set[str] = set()
    optional_set: set[str] = set()

    req_queue: deque[str] = deque(sorted(required_modules))
    opt_queue: deque[str] = deque(sorted(optional_modules))

    while req_queue:
        current = req_queue.popleft()
        if current in required_set:
            continue
        required_set.add(current)
        optional_set.discard(current)

        module = modules_by_name.get(current)
        if not module:
            continue
        deps = module.get("dependencies", {})
        req_deps = deps.get("required", []) if isinstance(deps, dict) else []
        opt_deps = deps.get("optional", []) if isinstance(deps, dict) else []

        for dep in sorted(d for d in req_deps if isinstance(d, str)):
            if dep not in required_set:
                req_queue.append(dep)
        for dep in sorted(d for d in opt_deps if isinstance(d, str)):
            if dep not in required_set:
                opt_queue.append(dep)

    while opt_queue:
        current = opt_queue.popleft()
        if current in required_set or current in optional_set:
            continue
        optional_set.add(current)

        module = modules_by_name.get(current)
        if not module:
            continue
        deps = module.get("dependencies", {})
        req_deps = deps.get("required", []) if isinstance(deps, dict) else []
        opt_deps = deps.get("optional", []) if isinstance(deps, dict) else []

        for dep in sorted(d for d in req_deps if isinstance(d, str)):
            if dep not in required_set and dep not in optional_set:
                opt_queue.append(dep)
        for dep in sorted(d for d in opt_deps if isinstance(d, str)):
            if dep not in required_set and dep not in optional_set:
                opt_queue.append(dep)

    all_nodes = required_set | optional_set
    dependency_edges: dict[str, set[str]] = {name: set() for name in all_nodes}
    for module_name in all_nodes:
        module = modules_by_name.get(module_name)
        if not module:
            continue
        deps = module.get("dependencies", {})
        req_deps = deps.get("required", []) if isinstance(deps, dict) else []
        opt_deps = deps.get("optional", []) if isinstance(deps, dict) else []
        for dep in req_deps + opt_deps:
            if isinstance(dep, str) and dep in all_nodes:
                dependency_edges[module_name].add(dep)

    ordered = _topological_dependencies_first(all_nodes, dependency_edges)
    if ordered is None:
        raise ValueError("target closure contains cyclic dependencies")

    ordered_required = [name for name in ordered if name in required_set]
    ordered_optional = [name for name in ordered if name in optional_set]
    return ordered_required, ordered_optional, dependency_edges


def _perform_workspace_validation(
    manifest: dict[str, Any],
    overrides: dict[str, str],
    target_dir: Path,
) -> dict[str, Any]:
    report = make_report()

    for schema_name, schema_path in SCHEMA_PATHS.items():
        if not schema_path.exists():
            add_error(report, f"missing schema file: {schema_path}")
            continue
        schema_data = load_json_file(schema_path)
        if not isinstance(schema_data, dict):
            add_error(report, f"schema '{schema_name}' must be a JSON object")
            continue
        if schema_data.get("$schema") != "https://json-schema.org/draft/2020-12/schema":
            add_error(report, f"schema '{schema_name}' must declare draft/2020-12")

    _validate_manifest_semantics(manifest, report)
    _validate_required_ref_policy(manifest, report)

    components_by_name = _validate_component_graph(manifest, report)
    component_names = set(components_by_name.keys())

    modules = _load_catalog(CATALOG_PATHS["module"], "modules", report)
    plugins = _load_catalog(CATALOG_PATHS["plugin"], "plugins", report)
    package_entries = _load_catalog(CATALOG_PATHS["package"], "packages", report)

    modules_by_name = _validate_module_entries(modules, component_names, report)
    plugins_by_name = _validate_plugin_entries(plugins, report)
    packages_by_name = _validate_package_catalog_entries(package_entries, component_names, report)

    _validate_module_graph(modules_by_name, report)
    _validate_static_scan(components_by_name, overrides, target_dir, report)

    report["ok"] = len(report["errors"]) == 0
    report["counts"] = {
        "errors": len(report["errors"]),
        "warnings": len(report["warnings"]),
        "scanSkipped": len(report["scanSkipped"]),
        "components": len(components_by_name),
        "packages": len(packages_by_name),
        "modules": len(modules_by_name),
        "plugins": len(plugins_by_name),
        "targets": 0,
    }
    report["models"] = {
        "components": components_by_name,
        "packages": packages_by_name,
        "modules": modules_by_name,
        "plugins": plugins_by_name,
        "targets": {},
    }
    return report


def _print_validation_summary(report: dict[str, Any]) -> None:
    counts = report.get("counts", {})
    print("Workspace validation")
    print(
        f"  components={counts.get('components', 0)} "
        f"packages={counts.get('packages', 0)} "
        f"modules={counts.get('modules', 0)} "
        f"plugins={counts.get('plugins', 0)} "
        f"targets={counts.get('targets', 0)}"
    )

    if report.get("errors"):
        print("\nErrors:")
        for issue in report["errors"]:
            print(f"  - {issue}")

    if report.get("warnings"):
        print("\nWarnings:")
        for issue in report["warnings"]:
            print(f"  - {issue}")

    if report.get("scanSkipped"):
        print("\nStatic scan skipped:")
        for issue in report["scanSkipped"]:
            print(f"  - {issue}")

    print()
    if report.get("ok"):
        print("workspace validation result: PASS")
    else:
        print("workspace validation result: FAIL")


def _package_roots_from_args(args: argparse.Namespace) -> list[Path]:
    return [Path(path) for path in getattr(args, "package_roots", []) or []]


def _load_live_project_context(
    project_path: Path,
    target_dir: Path,
) -> dict[str, Any] | None:
    manifest = load_manifest()
    overrides = load_overrides()
    workspace_package_overrides = load_package_overrides()

    validation = _perform_workspace_validation(manifest, overrides, target_dir)
    _schema_validate_project_manifest(project_path, validation)
    validation["ok"] = len(validation["errors"]) == 0
    if not validation.get("ok"):
        _print_validation_summary(validation)
        return None

    project, targets_by_name = _load_project_targets(project_path)
    validation["models"]["targets"] = targets_by_name
    validation["counts"]["targets"] = len(targets_by_name)

    return {
        "manifest": manifest,
        "validation": validation,
        "projectPath": project_path,
        "project": project,
        "targetsByName": targets_by_name,
        "targetDir": target_dir,
        "workspacePackageOverrides": workspace_package_overrides,
    }


def _build_lockfile_summary(lockfile_path: Path | None, *, source: str, exists: bool = True) -> dict[str, Any] | None:
    if lockfile_path is None:
        return None
    return {
        "path": str(lockfile_path),
        "source": source,
        "exists": exists,
    }


def _build_cache_summary(
    cache_dir: Path,
    *,
    mode: str,
    restored_packages: list[dict[str, Any]] | None = None,
    entries: list[dict[str, Any]] | None = None,
) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "path": str(cache_dir),
        "mode": mode,
    }
    if restored_packages is not None:
        payload["restoredPackages"] = list(restored_packages)
    if entries is not None:
        payload["entries"] = list(entries)
    return payload


def _resolve_live_target_payload(
    args: argparse.Namespace,
    *,
    require_target: bool,
) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any]] | None:
    project_path = _resolve_project_path(getattr(args, "project_path", None))
    target_dir = Path(args.target_dir) if getattr(args, "target_dir", None) else TARGET_DIR
    context = _load_live_project_context(project_path, target_dir)
    if context is None:
        return None

    targets_by_name = context["targetsByName"]
    selected_target_name = getattr(args, "target_name", None) or str(context["project"].get("defaultTarget", ""))
    target = targets_by_name.get(selected_target_name)
    if target is None:
        available = ", ".join(sorted(targets_by_name.keys()))
        if require_target:
            raise SystemExit(f"error: unknown target '{selected_target_name}'. Available: {available}")
        return None

    package_overrides = _build_package_override_map(getattr(args, "package_overrides", None))
    report = make_report()
    payload = _resolve_target_with_packages(
        target,
        context["manifest"],
        context["workspacePackageOverrides"],
        package_overrides,
        target_dir,
        report,
        package_roots=_package_roots_from_args(args),
        allow_package_probe=bool(getattr(args, "allow_package_probe", False)),
        catalog_packages_by_name=context["validation"]["models"]["packages"],
        modules_by_name=context["validation"]["models"]["modules"],
        plugins_by_name=context["validation"]["models"]["plugins"],
    )
    context["resolutionReport"] = report
    context["lockfileSummary"] = _build_lockfile_summary(
        _resolve_lockfile_path(getattr(args, "lockfile", None), project_path),
        source="project-default" if getattr(args, "lockfile", None) is None else "explicit",
        exists=_resolve_lockfile_path(getattr(args, "lockfile", None), project_path).exists(),
    )
    context["cacheSummary"] = _build_cache_summary(_cache_dir_from_args(args), mode="live")
    return context, target, payload


def _resolve_locked_package_paths(lockfile_path: Path, package: dict[str, Any]) -> tuple[Path, Path]:
    lock_root = lockfile_path.parent
    entry_path = Path(str(package.get("cacheEntryPath", "")))
    manifest_path = Path(str(package.get("cacheManifestPath", "")))
    if not entry_path.is_absolute():
        entry_path = (lock_root / entry_path).resolve()
    if not manifest_path.is_absolute():
        manifest_path = (lock_root / manifest_path).resolve()
    return entry_path, manifest_path


def _validate_locked_target_cache(
    target_payload: dict[str, Any],
    lockfile_path: Path,
    report: dict[str, Any],
) -> None:
    for package in target_payload.get("packages", []):
        if not isinstance(package, dict):
            continue
        entry_path, manifest_path = _resolve_locked_package_paths(lockfile_path, package)
        if not entry_path.exists():
            add_error(report, f"locked package '{package.get('name')}' is missing cache entry '{entry_path}'")
            continue
        if not manifest_path.exists():
            add_error(report, f"locked package '{package.get('name')}' is missing cached manifest '{manifest_path}'")
            continue

        entry = _load_cached_entry(entry_path)
        if str(entry.get("name", "")) != str(package.get("name", "")):
            add_error(report, f"locked cache entry '{entry_path}' does not match package '{package.get('name')}'")
            continue
        if str(entry.get("version", "")) != str(package.get("version", "")):
            add_error(report, f"locked cache entry '{entry_path}' version does not match package '{package.get('name')}'")
            continue

        manifest_hash = _file_sha256(manifest_path)
        if str(entry.get("manifestHash", "")) != manifest_hash:
            add_error(report, f"locked cached manifest hash mismatch for package '{package.get('name')}'")
            continue
        if str(package.get("manifestHash", "")) and str(package.get("manifestHash", "")) != manifest_hash:
            add_error(report, f"lockfile manifest hash mismatch for package '{package.get('name')}'")
            continue

        cached_manifest = _load_package_manifest(manifest_path, report, f"locked package '{package.get('name')}'")
        if cached_manifest is None or not _validate_package_manifest(
            cached_manifest,
            f"locked package '{package.get('name')}'",
            report,
        ):
            add_error(report, f"locked package '{package.get('name')}' cached manifest is invalid")


def _resolve_locked_target_payload(
    args: argparse.Namespace,
    *,
    require_target: bool,
) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any]] | None:
    manifest = load_manifest()
    overrides = load_overrides()
    target_dir = Path(args.target_dir) if getattr(args, "target_dir", None) else TARGET_DIR
    validation = _perform_workspace_validation(manifest, overrides, target_dir)

    project_path: Path | None = None
    if getattr(args, "project_path", None):
        project_path = _resolve_project_path(getattr(args, "project_path", None))
        _schema_validate_project_manifest(project_path, validation)
    elif getattr(args, "lockfile", None) is None:
        project_path = _resolve_project_path(None)
        _schema_validate_project_manifest(project_path, validation)

    validation["ok"] = len(validation["errors"]) == 0
    if not validation.get("ok"):
        _print_validation_summary(validation)
        return None

    lockfile_path = _resolve_lockfile_path(getattr(args, "lockfile", None), project_path)
    lockfile = _load_lockfile(lockfile_path)
    project_info = lockfile.get("project", {})
    if project_path is not None and str(project_info.get("projectPath", "")) not in {"", str(project_path)}:
        raise SystemExit(
            f"error: lockfile '{lockfile_path}' was generated for a different project: {project_info.get('projectPath')}"
        )

    selected_target_name = getattr(args, "target_name", None) or str(lockfile.get("defaultTarget", ""))
    targets_by_name = _lockfile_target_index(lockfile)
    target = targets_by_name.get(selected_target_name)
    if target is None:
        available = ", ".join(sorted(targets_by_name.keys()))
        if require_target:
            raise SystemExit(f"error: unknown target '{selected_target_name}' in lockfile. Available: {available}")
        return None

    report = make_report()
    payload = dict(target)
    payload["target"] = target.get("name")
    payload["source"] = f"lockfile:{lockfile_path}"
    payload["project"] = project_info.get("name")
    _validate_locked_target_cache(payload, lockfile_path, report)

    context = {
        "manifest": manifest,
        "validation": validation,
        "resolutionReport": report,
        "lockfileSummary": _build_lockfile_summary(lockfile_path, source="locked", exists=True),
        "cacheSummary": _build_cache_summary(_cache_dir_from_args(args), mode="locked"),
    }
    return context, target, payload


def _resolve_modern_target_payload(
    args: argparse.Namespace,
    *,
    require_target: bool,
) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any]] | None:
    if bool(getattr(args, "locked", False)):
        return _resolve_locked_target_payload(args, require_target=require_target)
    return _resolve_live_target_payload(args, require_target=require_target)


def _resolve_all_project_targets_live(
    args: argparse.Namespace,
) -> tuple[dict[str, Any], dict[str, dict[str, Any]]] | None:
    project_path = _resolve_project_path(getattr(args, "project_path", None))
    target_dir = Path(args.target_dir) if getattr(args, "target_dir", None) else TARGET_DIR
    context = _load_live_project_context(project_path, target_dir)
    if context is None:
        return None

    package_overrides = _build_package_override_map(getattr(args, "package_overrides", None))
    resolved_targets: dict[str, dict[str, Any]] = {}
    combined_report = make_report()

    for target_name in sorted(context["targetsByName"].keys()):
        target = context["targetsByName"][target_name]
        target_report = make_report()
        payload = _resolve_target_with_packages(
            target,
            context["manifest"],
            context["workspacePackageOverrides"],
            package_overrides,
            target_dir,
            target_report,
            package_roots=_package_roots_from_args(args),
            allow_package_probe=bool(getattr(args, "allow_package_probe", False)),
            catalog_packages_by_name=context["validation"]["models"]["packages"],
            modules_by_name=context["validation"]["models"]["modules"],
            plugins_by_name=context["validation"]["models"]["plugins"],
        )
        combined_report["errors"].extend(target_report["errors"])
        combined_report["warnings"].extend(target_report["warnings"])
        if payload is not None and not target_report["errors"]:
            resolved_targets[target_name] = payload

    context["resolutionReport"] = combined_report
    context["lockfilePath"] = _resolve_lockfile_path(getattr(args, "lockfile", None), project_path)
    context["cacheDir"] = _cache_dir_from_args(args)
    return context, resolved_targets


def _write_package_cache(
    resolved_targets: dict[str, dict[str, Any]],
    cache_dir: Path,
) -> tuple[dict[tuple[str, str], dict[str, Any]], list[str]]:
    cache_dir.mkdir(parents=True, exist_ok=True)
    cache_records: dict[tuple[str, str], dict[str, Any]] = {}
    cache_writes: list[str] = []

    for payload in resolved_targets.values():
        for package in payload.get("packages", []):
            if not isinstance(package, dict):
                continue
            package_name = str(package.get("name", ""))
            package_version = str(package.get("version", ""))
            cache_key = (package_name, package_version)
            if cache_key in cache_records:
                continue

            package_dir = _cache_package_dir(cache_dir, package_name, package_version)
            package_dir.mkdir(parents=True, exist_ok=True)
            source_manifest_path = Path(str(package.get("manifestPath", ""))).resolve()
            cache_manifest_path = package_dir / "ngin.package.json"
            cache_entry_path = package_dir / "entry.json"
            shutil.copyfile(source_manifest_path, cache_manifest_path)
            manifest_hash = _file_sha256(cache_manifest_path)

            entry = _build_cache_entry(
                package,
                manifest_hash,
                cache_manifest_path,
                cache_entry_path,
            )
            cache_entry_path.write_text(json.dumps(entry, indent=2, sort_keys=True) + "\n", encoding="utf-8")

            cache_records[cache_key] = {
                "entryPath": cache_entry_path,
                "manifestPath": cache_manifest_path,
                "manifestHash": manifest_hash,
                "sourceKind": package.get("source"),
            }
            cache_writes.extend([str(cache_manifest_path), str(cache_entry_path)])

    return cache_records, sorted(cache_writes)


def _build_lockfile_payload(
    context: dict[str, Any],
    resolved_targets: dict[str, dict[str, Any]],
    cache_records: dict[tuple[str, str], dict[str, Any]],
) -> dict[str, Any]:
    project_path = Path(context["projectPath"])
    lockfile_path = Path(context["lockfilePath"])
    lock_root = lockfile_path.parent
    targets: list[dict[str, Any]] = []

    for target_name in sorted(resolved_targets.keys()):
        payload = resolved_targets[target_name]
        packages: list[dict[str, Any]] = []
        for package in payload.get("packages", []):
            cache_record = cache_records[(str(package["name"]), str(package["version"]))]
            packages.append(
                {
                    "name": package["name"],
                    "version": package["version"],
                    "source": package["source"],
                    "manifestPath": package["manifestPath"],
                    "manifestHash": cache_record["manifestHash"],
                    "cacheEntryPath": _relative_path_text(Path(cache_record["entryPath"]), lock_root),
                    "cacheManifestPath": _relative_path_text(Path(cache_record["manifestPath"]), lock_root),
                    "dependencies": list(package.get("dependencies", [])),
                    "provides": dict(package.get("provides", {})),
                }
            )

        targets.append(
            {
                "name": payload.get("target"),
                "source": payload.get("source"),
                "type": payload.get("type"),
                "platform": payload.get("platform"),
                "enableReflection": payload.get("enableReflection"),
                "packages": packages,
                "providedByModule": dict(payload.get("providedByModule", {})),
                "providedByPlugin": dict(payload.get("providedByPlugin", {})),
                "requiredModules": list(payload.get("requiredModules", [])),
                "optionalModules": list(payload.get("optionalModules", [])),
                "enabledPlugins": list(payload.get("enabledPlugins", [])),
                "stages": dict(payload.get("stages", {})),
                "dependencyEdges": dict(payload.get("dependencyEdges", {})),
                "packageEdges": dict(payload.get("packageEdges", {})),
            }
        )

    return {
        "schemaVersion": 1,
        "project": {
            "name": context["project"].get("name"),
            "projectPath": str(project_path),
            "manifestHash": _file_sha256(project_path),
        },
        "defaultTarget": context["project"].get("defaultTarget"),
        "targets": targets,
    }


def cmd_package_restore(args: argparse.Namespace) -> int:
    resolved = _resolve_all_project_targets_live(args)
    if resolved is None:
        return 1

    context, resolved_targets = resolved
    report = context["resolutionReport"]
    validation = context["validation"]
    lockfile_path = Path(context["lockfilePath"])
    cache_dir = Path(context["cacheDir"])

    if report["errors"]:
        _print_validation_summary(validation)
        print("\nPackage restore errors:")
        for issue in report["errors"]:
            print(f"  - {issue}")
        if report["warnings"]:
            print("\nPackage restore warnings:")
            for issue in report["warnings"]:
                print(f"  - {issue}")
        write_json_report(
            args.json_report,
            _command_report_payload(
                "package-restore",
                validation,
                report,
                {
                    "source": f"project:{context['projectPath']}",
                    "project": context["project"].get("name"),
                },
                lockfile=_build_lockfile_summary(lockfile_path, source="project-default", exists=lockfile_path.exists()),
                cache=_build_cache_summary(cache_dir, mode="restore"),
                reason="package restore failed",
            ),
        )
        return 1

    cache_records, cache_writes = _write_package_cache(resolved_targets, cache_dir)
    lockfile_payload = _build_lockfile_payload(context, resolved_targets, cache_records)
    lockfile_path.parent.mkdir(parents=True, exist_ok=True)
    lockfile_path.write_text(json.dumps(lockfile_payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    restored_packages = [
        {
            "name": name,
            "version": version,
            "cacheEntryPath": str(cache_records[(name, version)]["entryPath"]),
            "cacheManifestPath": str(cache_records[(name, version)]["manifestPath"]),
            "sourceKind": str(cache_records[(name, version)]["sourceKind"]),
        }
        for name, version in sorted(cache_records.keys())
    ]
    payload = {
        "source": f"project:{context['projectPath']}",
        "project": context["project"].get("name"),
        "targets": [
            {
                "name": target_name,
                "packages": list(resolved_targets[target_name].get("packages", [])),
                "requiredModules": list(resolved_targets[target_name].get("requiredModules", [])),
                "optionalModules": list(resolved_targets[target_name].get("optionalModules", [])),
                "enabledPlugins": list(resolved_targets[target_name].get("enabledPlugins", [])),
                "packageEdges": dict(resolved_targets[target_name].get("packageEdges", {})),
                "dependencyEdges": dict(resolved_targets[target_name].get("dependencyEdges", {})),
            }
            for target_name in sorted(resolved_targets.keys())
        ],
        "restoredPackages": restored_packages,
        "cacheWrites": cache_writes,
    }

    print(f"Restored project: {context['project'].get('name')}")
    print(f"  source: project:{context['projectPath']}")
    print(f"  targets: {len(resolved_targets)}")
    print(f"  restored packages: {len(restored_packages)}")
    print(f"  lockfile: {lockfile_path}")
    print(f"  cache: {cache_dir}")
    if report["warnings"]:
        print("\nWarnings:")
        for issue in report["warnings"]:
            print(f"  - {issue}")

    write_json_report(
        args.json_report,
        _command_report_payload(
            "package-restore",
            validation,
            report,
            payload,
            lockfile=_build_lockfile_summary(lockfile_path, source="written", exists=True),
            cache=_build_cache_summary(cache_dir, mode="restore", restored_packages=restored_packages),
        ),
    )
    return 0


def cmd_package_list(args: argparse.Namespace) -> int:
    cache_dir = _cache_dir_from_args(args)
    entries = [
        _cache_entry_summary(entry_path, _load_cached_entry(entry_path))
        for entry_path in _iter_cached_entry_paths(cache_dir)
    ]

    if entries:
        print(f"Cached packages: {len(entries)}")
        for entry in entries:
            print(
                f"  - {entry['name']} {entry['version']} [{entry['sourceKind']}] "
                f"{entry['cacheEntryPath']}"
            )
    else:
        print(f"No cached packages under {cache_dir}")

    write_json_report(
        args.json_report,
        _command_report_payload(
            "package-list",
            _empty_validation_report(),
            make_report(),
            {"source": f"cache:{cache_dir}"},
            cache=_build_cache_summary(cache_dir, mode="list", entries=entries),
        ),
    )
    return 0


def cmd_package_show(args: argparse.Namespace) -> int:
    cache_dir = _cache_dir_from_args(args)
    package_name = args.package_name
    entries = []
    for entry_path in _iter_cached_entry_paths(cache_dir):
        entry = _load_cached_entry(entry_path)
        if str(entry.get("name", "")) == package_name:
            entries.append(_cache_entry_summary(entry_path, entry))

    report = make_report()
    if not entries:
        add_error(report, f"cached package '{package_name}' was not found in {cache_dir}")
        write_json_report(
            args.json_report,
            _command_report_payload(
                "package-show",
                _empty_validation_report(),
                report,
                {"source": f"cache:{cache_dir}"},
                cache=_build_cache_summary(cache_dir, mode="show", entries=[]),
                reason="cached package not found",
            ),
        )
        print(f"Package not found in cache: {package_name}")
        return 1

    print(f"Cached package: {package_name}")
    for entry in sorted(entries, key=lambda item: str(item.get("version", ""))):
        print(f"  version: {entry['version']}")
        print(f"  source: {entry['sourceKind']}")
        print(f"  source manifest: {entry['sourceManifestPath']}")
        print(f"  cached manifest: {entry['cachedManifestPath']}")
        print(f"  cache entry: {entry['cacheEntryPath']}")
        print(f"  dependencies: {len(entry['dependencies'])}")
        print(f"  modules: {len(entry['provides'].get('modules', []))}")
        print(f"  plugins: {len(entry['provides'].get('plugins', []))}")

    write_json_report(
        args.json_report,
        _command_report_payload(
            "package-show",
            _empty_validation_report(),
            report,
            {"source": f"cache:{cache_dir}", "restoredPackages": entries},
            cache=_build_cache_summary(cache_dir, mode="show", entries=entries),
        ),
    )
    return 0


def cmd_validate(args: argparse.Namespace) -> int:
    resolved = _resolve_modern_target_payload(args, require_target=True)
    if resolved is None:
        return 1

    context, _, payload = resolved
    report = context["resolutionReport"]
    validation = context["validation"]
    if payload is None or report["errors"]:
        _print_validation_summary(validation)
        if report["errors"]:
            print("\nPackage resolution errors:")
            for issue in report["errors"]:
                print(f"  - {issue}")
        if report["warnings"]:
            print("\nPackage resolution warnings:")
            for issue in report["warnings"]:
                print(f"  - {issue}")
        write_json_report(
            args.json_report,
            _command_report_payload(
                "validate",
                validation,
                report,
                payload,
                lockfile=context.get("lockfileSummary"),
                cache=context.get("cacheSummary"),
                reason="target validation failed",
            ),
        )
        return 1

    print(f"Validated target: {payload['target']}")
    print(f"  source: {payload['source']}")
    print(f"  packages: {len(payload['packages'])}")
    print(f"  required modules: {len(payload['requiredModules'])}")
    print(f"  optional modules: {len(payload['optionalModules'])}")
    if report["warnings"]:
        print("\nWarnings:")
        for issue in report["warnings"]:
            print(f"  - {issue}")

    write_json_report(
        args.json_report,
        _command_report_payload(
            "validate",
            validation,
            report,
            payload,
            lockfile=context.get("lockfileSummary"),
            cache=context.get("cacheSummary"),
        ),
    )
    return 0


def cmd_resolve_target(args: argparse.Namespace) -> int:
    resolved = _resolve_modern_target_payload(args, require_target=True)
    if resolved is None:
        return 1

    context, _, payload = resolved
    report = context["resolutionReport"]
    validation = context["validation"]

    if payload is None or report["errors"]:
        _print_validation_summary(validation)
        if report["errors"]:
            print("\nResolution errors:")
            for issue in report["errors"]:
                print(f"  - {issue}")
        if report["warnings"]:
            print("\nResolution warnings:")
            for issue in report["warnings"]:
                print(f"  - {issue}")
        write_json_report(
            args.json_report,
            _command_report_payload(
                "resolve",
                validation,
                report,
                payload,
                lockfile=context.get("lockfileSummary"),
                cache=context.get("cacheSummary"),
                reason="target resolution failed",
            ),
        )
        return 1

    print(f"Resolved target: {payload['target']}")
    print(f"  source: {payload['source']}")
    print(f"  type: {payload.get('type')}")
    print(f"  platform: {payload.get('platform')}")
    print(f"  reflection: {payload.get('enableReflection')}")
    print(f"  stages: {', '.join(payload.get('stages', {}).keys())}")
    print("\nPackages (dependency order):")
    for package in payload["packages"]:
        print(f"  - {package['name']} {package['version']} [{package['source']}]")
    print("\nRequired modules (dependencies-first order):")
    for name in payload["requiredModules"]:
        print(f"  - {name}")

    if payload["optionalModules"]:
        print("\nOptional modules (dependencies-first order):")
        for name in payload["optionalModules"]:
            print(f"  - {name}")

    if report["warnings"]:
        print("\nResolution warnings:")
        for issue in report["warnings"]:
            print(f"  - {issue}")

    write_json_report(
        args.json_report,
        _command_report_payload(
            "resolve",
            validation,
            report,
            payload,
            lockfile=context.get("lockfileSummary"),
            cache=context.get("cacheSummary"),
        ),
    )
    return 0


def cmd_graph(args: argparse.Namespace) -> int:
    resolved = _resolve_modern_target_payload(args, require_target=True)
    if resolved is None:
        return 1

    context, _, payload = resolved
    report = context["resolutionReport"]
    validation = context["validation"]
    if payload is None or report["errors"]:
        _print_validation_summary(validation)
        if report["errors"]:
            print("\nResolution errors:")
            for issue in report["errors"]:
                print(f"  - {issue}")
        if report["warnings"]:
            print("\nResolution warnings:")
            for issue in report["warnings"]:
                print(f"  - {issue}")
        write_json_report(
            args.json_report,
            _command_report_payload(
                "graph",
                validation,
                report,
                payload,
                lockfile=context.get("lockfileSummary"),
                cache=context.get("cacheSummary"),
                reason="target graph generation failed",
            ),
        )
        return 1

    print(f"Graph for target: {payload['target']}")
    print(f"  source: {payload['source']}")
    print("\nPackages:")
    for package in payload["packages"]:
        edges = payload["packageEdges"].get(package["name"], [])
        edge_text = ", ".join(edges) if edges else "(none)"
        print(f"  - {package['name']} -> {edge_text}")

    print("\nModules:")
    for module_name in payload["requiredModules"] + payload["optionalModules"]:
        edges = payload["dependencyEdges"].get(module_name, [])
        edge_text = ", ".join(edges) if edges else "(none)"
        print(f"  - {module_name} -> {edge_text}")

    if report["warnings"]:
        print("\nResolution warnings:")
        for issue in report["warnings"]:
            print(f"  - {issue}")

    write_json_report(
        args.json_report,
        _command_report_payload(
            "graph",
            validation,
            report,
            payload,
            lockfile=context.get("lockfileSummary"),
            cache=context.get("cacheSummary"),
        ),
    )
    return 0


def _add_shared_resolution_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--project", dest="project_path", help="Path to a specific ngin.project.json file.")
    parser.add_argument("--target", dest="target_name", help="Target name from the selected project manifest.")
    parser.add_argument("--lockfile", help="Path to a specific ngin.lock.json file.")
    parser.add_argument("--cache-dir", help="Path to the local package cache directory.")
    parser.add_argument("--locked", action="store_true", help="Resolve the target from a lockfile and cached manifests.")
    parser.add_argument(
        "--package-root",
        dest="package_roots",
        action="append",
        default=[],
        help="Additional local roots to probe for package manifests when --allow-package-probe is set.",
    )
    parser.add_argument(
        "--package-override",
        dest="package_overrides",
        action="append",
        default=[],
        help="Override one package manifest path in the form Package.Name=/path/to/ngin.package.json.",
    )
    parser.add_argument(
        "--allow-package-probe",
        action="store_true",
        help="Allow local fallback probing through package roots when a package is not in the package catalog.",
    )
    parser.add_argument("--target-dir", help="Alternate externals target directory for validation parity.")
    parser.add_argument("--json-report", help="Write command output as JSON to this path.")


def _add_package_restore_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--project", dest="project_path", help="Path to a specific ngin.project.json file.")
    parser.add_argument("--lockfile", help="Path to write the ngin.lock.json output.")
    parser.add_argument("--cache-dir", help="Path to the local package cache directory.")
    parser.add_argument(
        "--package-root",
        dest="package_roots",
        action="append",
        default=[],
        help="Additional local roots to probe for package manifests when --allow-package-probe is set.",
    )
    parser.add_argument(
        "--package-override",
        dest="package_overrides",
        action="append",
        default=[],
        help="Override one package manifest path in the form Package.Name=/path/to/ngin.package.json.",
    )
    parser.add_argument(
        "--allow-package-probe",
        action="store_true",
        help="Allow local fallback probing through package roots when a package is not in the package catalog.",
    )
    parser.add_argument("--target-dir", help="Alternate externals target directory for validation parity.")
    parser.add_argument("--json-report", help="Write command output as JSON to this path.")

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="ngin",
        description="NGIN platform CLI for project/package composition and workspace tooling.",
    )
    sub = parser.add_subparsers(dest="command")

    sub.add_parser("list", help="List manifest components, versions, pins, and overrides.")

    p_status = sub.add_parser("status", help="Show local workspace component resolution and pin match status.")
    p_status.add_argument("--target", help="Alternate externals target directory.")

    p_doctor = sub.add_parser("doctor", help="Validate tools, manifests, overrides, and local component repos.")
    p_doctor.add_argument("--target", help="Alternate externals target directory.")

    p_sync = sub.add_parser("sync", help="Clone or fetch pinned components into the externals directory.")
    p_sync.add_argument("--target", help="Override externals target directory.")

    p_package = sub.add_parser("package", help="Manage local package restore, cache, and inspection.")
    package_sub = p_package.add_subparsers(dest="package_command")
    p_package_restore = package_sub.add_parser("restore", help="Restore all targets from a project into a lockfile and cache.")
    _add_package_restore_args(p_package_restore)
    p_package_list = package_sub.add_parser("list", help="List cached package metadata entries.")
    p_package_list.add_argument("--cache-dir", help="Path to the local package cache directory.")
    p_package_list.add_argument("--json-report", help="Write command output as JSON to this path.")
    p_package_show = package_sub.add_parser("show", help="Show cached metadata for one package.")
    p_package_show.add_argument("package_name", help="Package name to inspect in the local cache.")
    p_package_show.add_argument("--cache-dir", help="Path to the local package cache directory.")
    p_package_show.add_argument("--json-report", help="Write command output as JSON to this path.")

    p_validate = sub.add_parser("validate", help="Validate a target's package/module/plugin composition.")
    _add_shared_resolution_args(p_validate)

    p_graph = sub.add_parser("graph", help="Print the resolved package/module dependency graph for one target.")
    _add_shared_resolution_args(p_graph)

    p_resolve = sub.add_parser("resolve", help="Resolve one target into a deterministic package/module plan.")
    _add_shared_resolution_args(p_resolve)

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
    if args.command == "package":
        if args.package_command == "restore":
            return cmd_package_restore(args)
        if args.package_command == "list":
            return cmd_package_list(args)
        if args.package_command == "show":
            return cmd_package_show(args)
        parser.print_help()
        return 1
    if args.command == "validate":
        return cmd_validate(args)
    if args.command == "graph":
        return cmd_graph(args)
    if args.command == "resolve":
        return cmd_resolve_target(args)

    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
