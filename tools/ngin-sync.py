#!/usr/bin/env python3
from __future__ import annotations

import argparse
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

SCHEMA_PATHS = {
    "module": ROOT_DIR / "manifests" / "module.schema.json",
    "plugin": ROOT_DIR / "manifests" / "plugin-bundle.schema.json",
    "target": ROOT_DIR / "manifests" / "target.schema.json",
    "graph": ROOT_DIR / "manifests" / "module-graph.schema.json",
}

CATALOG_PATHS = {
    "module": ROOT_DIR / "manifests" / "module-catalog.json",
    "plugin": ROOT_DIR / "manifests" / "plugin-catalog.json",
    "target": ROOT_DIR / "manifests" / "target-catalog.json",
}

COMPONENT_NAME_RE = re.compile(r"^NGIN\.[A-Za-z0-9_.-]+$")
MODULE_NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9]*\.[A-Za-z0-9_.-]+$")
PLUGIN_NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_-]*\.[A-Za-z0-9_.-]+$")

FIND_PACKAGE_RE = re.compile(r"find_package\s*\(\s*([A-Za-z0-9_.+-]+)", re.IGNORECASE)
NGIN_TARGET_RE = re.compile(r"NGIN::([A-Za-z0-9_]+)")

MODULE_FAMILIES = {"Base", "Reflection", "RuntimeSvc", "Platform", "Editor", "Domain", "App"}
MODULE_TYPES = {"Runtime", "Editor", "Program", "Developer", "ThirdParty"}
TARGET_TYPES = {"Runtime", "Editor", "Program", "Developer"}
STAGES = {"Build", "Cook", "Stage", "Package"}

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
        "RuntimeSvc": "N",
        "Platform": "N",
        "Editor": "N",
        "Domain": "N",
        "App": "N",
    },
    "Reflection": {
        "Base": "Y",
        "Reflection": "N",
        "RuntimeSvc": "N",
        "Platform": "N",
        "Editor": "N",
        "Domain": "N",
        "App": "N",
    },
    "RuntimeSvc": {
        "Base": "Y",
        "Reflection": "O",
        "RuntimeSvc": "Y",
        "Platform": "Y",
        "Editor": "N",
        "Domain": "N",
        "App": "N",
    },
    "Platform": {
        "Base": "Y",
        "Reflection": "O",
        "RuntimeSvc": "N",
        "Platform": "Y",
        "Editor": "N",
        "Domain": "N",
        "App": "N",
    },
    "Editor": {
        "Base": "Y",
        "Reflection": "O",
        "RuntimeSvc": "Y",
        "Platform": "Y",
        "Editor": "Y",
        "Domain": "O",
        "App": "N",
    },
    "Domain": {
        "Base": "Y",
        "Reflection": "O",
        "RuntimeSvc": "Y",
        "Platform": "Y",
        "Editor": "N",
        "Domain": "Y",
        "App": "N",
    },
    "App": {
        "Base": "Y",
        "Reflection": "O",
        "RuntimeSvc": "Y",
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


def _is_string_list(value: Any) -> bool:
    return isinstance(value, list) and all(isinstance(v, str) for v in value)


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

        if not isinstance(module.get("version"), str):
            add_error(report, f"{ctx}.version must be a string")
        if not isinstance(module.get("compatiblePlatformRange"), str):
            add_error(report, f"{ctx}.compatiblePlatformRange must be a string")
        if not _is_string_list(module.get("platforms")):
            add_error(report, f"{ctx}.platforms must be an array of strings")
        if not isinstance(module.get("loadPhase"), str):
            add_error(report, f"{ctx}.loadPhase must be a string")

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

        if not isinstance(plugin.get("version"), str):
            add_error(report, f"{ctx}.version must be a string")
        if not isinstance(plugin.get("compatiblePlatformRange"), str):
            add_error(report, f"{ctx}.compatiblePlatformRange must be a string")

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


def _validate_target_entries(
    targets: list[dict[str, Any]],
    report: dict[str, Any],
) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    allowed_keys = {
        "name",
        "type",
        "platform",
        "enableReflection",
        "modules",
        "plugins",
        "stages",
    }

    for i, target in enumerate(targets):
        ctx = f"target-catalog.targets[{i}]"
        _check_exact_keys(target, allowed_keys, allowed_keys, ctx, report)

        name = target.get("name")
        if not isinstance(name, str) or not name:
            add_error(report, f"{ctx}.name must be a non-empty string")
            continue
        if name in result:
            add_error(report, f"{ctx}.name duplicates '{name}'")
            continue

        target_type = target.get("type")
        if target_type not in TARGET_TYPES:
            add_error(report, f"{ctx}.type must be one of {sorted(TARGET_TYPES)}")

        if not isinstance(target.get("platform"), str) or not target.get("platform"):
            add_error(report, f"{ctx}.platform must be a non-empty string")

        if not isinstance(target.get("enableReflection"), bool):
            add_error(report, f"{ctx}.enableReflection must be a boolean")

        if not _is_string_list(target.get("modules")):
            add_error(report, f"{ctx}.modules must be an array of strings")
        if not _is_string_list(target.get("plugins")):
            add_error(report, f"{ctx}.plugins must be an array of strings")

        stages = target.get("stages")
        if not _is_string_list(stages):
            add_error(report, f"{ctx}.stages must be an array of strings")
        else:
            for stage in stages:
                if stage not in STAGES:
                    add_error(report, f"{ctx}.stages contains unknown stage '{stage}'")

        result[name] = target

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


def _perform_spec001_validation(manifest: dict[str, Any], overrides: dict[str, str], target_dir: Path) -> dict[str, Any]:
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

    modules = _load_catalog(CATALOG_PATHS["module"], "modules", report)
    plugins = _load_catalog(CATALOG_PATHS["plugin"], "plugins", report)
    targets = _load_catalog(CATALOG_PATHS["target"], "targets", report)

    combined = {
        "schemaVersion": 1,
        "modules": modules,
        "plugins": plugins,
        "targets": targets,
    }
    if not isinstance(combined, dict) or combined.get("schemaVersion") != 1:
        add_error(report, "module graph envelope is invalid")

    components_by_name = _validate_component_graph(manifest, report)
    component_names = set(components_by_name.keys())

    modules_by_name = _validate_module_entries(modules, component_names, report)
    plugins_by_name = _validate_plugin_entries(plugins, report)
    targets_by_name = _validate_target_entries(targets, report)

    _validate_module_graph(modules_by_name, report)
    _validate_targets_and_plugins(manifest, modules_by_name, plugins_by_name, targets_by_name, report)
    _validate_static_scan(components_by_name, overrides, target_dir, report)

    report["ok"] = len(report["errors"]) == 0
    report["counts"] = {
        "errors": len(report["errors"]),
        "warnings": len(report["warnings"]),
        "scanSkipped": len(report["scanSkipped"]),
        "components": len(components_by_name),
        "modules": len(modules_by_name),
        "plugins": len(plugins_by_name),
        "targets": len(targets_by_name),
    }
    report["models"] = {
        "components": components_by_name,
        "modules": modules_by_name,
        "plugins": plugins_by_name,
        "targets": targets_by_name,
    }
    return report


def _print_validation_summary(report: dict[str, Any]) -> None:
    counts = report.get("counts", {})
    print("Spec 001 validation")
    print(
        f"  components={counts.get('components', 0)} "
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
        print("validate-spec001 result: PASS")
    else:
        print("validate-spec001 result: FAIL")


def cmd_validate_spec001(args: argparse.Namespace) -> int:
    manifest = load_manifest()
    overrides = load_overrides()
    target_dir = Path(args.target) if args.target else TARGET_DIR

    report = _perform_spec001_validation(manifest, overrides, target_dir)
    _print_validation_summary(report)

    json_payload = {
        "ok": report["ok"],
        "counts": report["counts"],
        "errors": report["errors"],
        "warnings": report["warnings"],
        "scanSkipped": report["scanSkipped"],
    }
    write_json_report(args.json_report, json_payload)

    return 0 if report["ok"] else 1


def cmd_resolve_target(args: argparse.Namespace) -> int:
    manifest = load_manifest()
    overrides = load_overrides()
    target_dir = TARGET_DIR

    report = _perform_spec001_validation(manifest, overrides, target_dir)
    if not report.get("ok"):
        _print_validation_summary(report)
        write_json_report(
            args.json_report,
            {
                "ok": False,
                "reason": "Spec 001 validation failed",
                "errors": report["errors"],
                "warnings": report["warnings"],
            },
        )
        return 1

    models = report["models"]
    targets_by_name: dict[str, dict[str, Any]] = models["targets"]
    modules_by_name: dict[str, dict[str, Any]] = models["modules"]
    plugins_by_name: dict[str, dict[str, Any]] = models["plugins"]

    target_name = args.target_name
    target = targets_by_name.get(target_name)
    if target is None:
        eprint(f"error: unknown target '{target_name}'. Available: {', '.join(sorted(targets_by_name.keys()))}")
        return 1

    try:
        ordered_required, ordered_optional, dep_edges = _resolve_target_plan(target, modules_by_name, plugins_by_name)
    except ValueError as exc:
        eprint(f"error: failed to resolve target '{target_name}': {exc}")
        return 1

    print(f"Resolved target: {target_name}")
    print(f"  type: {target.get('type')}")
    print(f"  platform: {target.get('platform')}")
    print(f"  reflection: {target.get('enableReflection')}")
    print(f"  stages: {', '.join(target.get('stages', []))}")
    print("\nRequired modules (dependencies-first order):")
    for name in ordered_required:
        print(f"  - {name}")

    if ordered_optional:
        print("\nOptional modules (dependencies-first order):")
        for name in ordered_optional:
            print(f"  - {name}")

    stage_plan: dict[str, dict[str, list[str]]] = {}
    for stage in target.get("stages", []):
        stage_plan[stage] = {
            "requiredModules": ordered_required,
            "optionalModules": ordered_optional,
        }

    payload = {
        "ok": True,
        "target": target_name,
        "type": target.get("type"),
        "platform": target.get("platform"),
        "enableReflection": target.get("enableReflection"),
        "requiredModules": ordered_required,
        "optionalModules": ordered_optional,
        "stages": stage_plan,
        "dependencyEdges": {key: sorted(value) for key, value in dep_edges.items()},
    }
    write_json_report(args.json_report, payload)
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

    p_validate = sub.add_parser(
        "validate-spec001",
        help="Validate Spec 001 component/module/plugin/target graph rules and static dependency scan.",
    )
    p_validate.add_argument("--target", help="Alternate externals target directory for static scan resolution.")
    p_validate.add_argument("--json-report", help="Write validation report as JSON to this path.")

    p_resolve = sub.add_parser(
        "resolve-target",
        help="Resolve a target's module/plugin closure using Spec 001 metadata.",
    )
    p_resolve.add_argument("--target", dest="target_name", required=True, help="Target name from manifests/target-catalog.json")
    p_resolve.add_argument("--json-report", help="Write resolved target plan as JSON to this path.")

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
    if args.command == "validate-spec001":
        return cmd_validate_spec001(args)
    if args.command == "resolve-target":
        return cmd_resolve_target(args)

    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
