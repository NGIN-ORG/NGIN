from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[2]
TOOLS_DIR = ROOT_DIR / "tools"
WORKSPACE_PROJECT = ROOT_DIR / "manifests" / "workspace.project.json"


def run_cli(*args: str, env: dict[str, str] | None = None, cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    run_env = os.environ.copy()
    if env:
        run_env.update(env)
    return subprocess.run(
        [sys.executable, *args],
        cwd=cwd or ROOT_DIR,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=run_env,
        check=False,
    )


def write_json(path: Path, payload: object) -> None:
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def read_json(path: Path) -> object:
    return json.loads(path.read_text(encoding="utf-8"))


def make_project(target_name: str, packages: list[dict[str, object]]) -> dict[str, object]:
    return {
        "schemaVersion": 1,
        "name": "Tests.PackageResolution",
        "defaultTarget": target_name,
        "targets": [
            {
                "name": target_name,
                "type": "Program",
                "profile": "ConsoleApp",
                "platform": "linux-x64",
                "enableReflection": False,
                "packages": packages,
                "modules": {
                    "enable": [],
                    "disable": [],
                },
                "plugins": {
                    "enable": [],
                    "disable": [],
                },
                "environmentName": "Test",
                "configSources": [],
                "workingDirectory": ".",
            }
        ],
    }


def make_package(
    name: str,
    *,
    version: str = "0.1.0",
    compatible_platform_range: str = ">=0.1.0-alpha.1 <0.2.0",
    platforms: list[str] | None = None,
    dependencies: list[dict[str, object]] | None = None,
    contents: list[dict[str, object]] | None = None,
) -> dict[str, object]:
    payload = {
        "schemaVersion": 1,
        "name": name,
        "version": version,
        "compatiblePlatformRange": compatible_platform_range,
        "platforms": platforms or ["linux", "windows", "macos"],
        "dependencies": dependencies or [],
        "provides": {
            "modules": [],
            "plugins": [],
        },
    }
    if contents is not None:
        payload["contents"] = {"files": contents}
    return payload


class NginCliTests(unittest.TestCase):
    def test_package_restore_writes_lockfile_and_cache(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_text:
            temp_dir = Path(temp_dir_text)
            lockfile_path = temp_dir / "workspace.lock.json"
            cache_dir = temp_dir / "cache"
            report_path = temp_dir / "restore-report.json"

            cp = run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "package",
                "restore",
                "--project",
                str(WORKSPACE_PROJECT),
                "--lockfile",
                str(lockfile_path),
                "--cache-dir",
                str(cache_dir),
                "--json-report",
                str(report_path),
            )
            self.assertEqual(cp.returncode, 0, cp.stdout + cp.stderr)
            self.assertTrue(lockfile_path.exists())
            self.assertTrue((cache_dir / "packages" / "NGIN.Core" / "0.1.0" / "entry.json").exists())
            self.assertTrue((cache_dir / "packages" / "NGIN.Core" / "0.1.0" / "ngin.package.json").exists())

            lockfile = read_json(lockfile_path)
            self.assertIsInstance(lockfile, dict)
            self.assertEqual(lockfile["schemaVersion"], 1)
            self.assertEqual(lockfile["defaultTarget"], "NGIN.CoreSample")
            self.assertEqual(len(lockfile["targets"]), 4)

            report = read_json(report_path)
            self.assertTrue(report["ok"])
            self.assertEqual(report["command"], "package-restore")
            self.assertEqual(report["lockfile"]["path"], str(lockfile_path))
            self.assertEqual(report["cache"]["path"], str(cache_dir))
            self.assertTrue(
                (cache_dir / "packages" / "NGIN.Editor" / "0.1.0" / "content" / "assets" / "default-layout.json").exists()
            )

    def test_package_restore_is_stable_for_unchanged_inputs(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_text:
            temp_dir = Path(temp_dir_text)
            lockfile_path = temp_dir / "workspace.lock.json"
            cache_dir = temp_dir / "cache"

            cp_first = run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "package",
                "restore",
                "--project",
                str(WORKSPACE_PROJECT),
                "--lockfile",
                str(lockfile_path),
                "--cache-dir",
                str(cache_dir),
            )
            self.assertEqual(cp_first.returncode, 0, cp_first.stdout + cp_first.stderr)
            first_lock = lockfile_path.read_text(encoding="utf-8")
            first_entry = (cache_dir / "packages" / "NGIN.Core" / "0.1.0" / "entry.json").read_text(encoding="utf-8")

            cp_second = run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "package",
                "restore",
                "--project",
                str(WORKSPACE_PROJECT),
                "--lockfile",
                str(lockfile_path),
                "--cache-dir",
                str(cache_dir),
            )
            self.assertEqual(cp_second.returncode, 0, cp_second.stdout + cp_second.stderr)
            self.assertEqual(first_lock, lockfile_path.read_text(encoding="utf-8"))
            self.assertEqual(first_entry, (cache_dir / "packages" / "NGIN.Core" / "0.1.0" / "entry.json").read_text(encoding="utf-8"))

    def test_package_list_and_show_read_cache(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_text:
            temp_dir = Path(temp_dir_text)
            lockfile_path = temp_dir / "workspace.lock.json"
            cache_dir = temp_dir / "cache"
            run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "package",
                "restore",
                "--project",
                str(WORKSPACE_PROJECT),
                "--lockfile",
                str(lockfile_path),
                "--cache-dir",
                str(cache_dir),
            )

            cp_list = run_cli(str(TOOLS_DIR / "ngin.py"), "package", "list", "--cache-dir", str(cache_dir))
            self.assertEqual(cp_list.returncode, 0, cp_list.stdout + cp_list.stderr)
            self.assertIn("NGIN.Core 0.1.0", cp_list.stdout)

            cp_show = run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "package",
                "show",
                "NGIN.Core",
                "--cache-dir",
                str(cache_dir),
            )
            self.assertEqual(cp_show.returncode, 0, cp_show.stdout + cp_show.stderr)
            self.assertIn("Cached package: NGIN.Core", cp_show.stdout)
            self.assertIn("source manifest:", cp_show.stdout)

    def test_package_restore_materializes_declared_contents(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_text:
            temp_dir = Path(temp_dir_text)
            package_root = temp_dir / "pkg"
            package_root.mkdir(parents=True, exist_ok=True)
            (package_root / "assets").mkdir()
            (package_root / "assets" / "hello.txt").write_text("hello\n", encoding="utf-8")
            package_path = package_root / "ngin.package.json"
            project_path = temp_dir / "ngin.project.json"
            lockfile_path = temp_dir / "ngin.lock.json"
            cache_dir = temp_dir / "cache"

            write_json(
                package_path,
                make_package(
                    "Acme.Content",
                    contents=[{"path": "assets/hello.txt", "kind": "asset"}],
                ),
            )
            write_json(
                project_path,
                make_project(
                    "Content.Target",
                    [{"name": "Acme.Content", "versionRange": ">=0.1.0 <1.0.0", "optional": False}],
                ),
            )

            cp = run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "package",
                "restore",
                "--project",
                str(project_path),
                "--lockfile",
                str(lockfile_path),
                "--cache-dir",
                str(cache_dir),
                "--package-override",
                f"Acme.Content={package_path}",
            )
            self.assertEqual(cp.returncode, 0, cp.stdout + cp.stderr)
            materialized_path = cache_dir / "packages" / "Acme.Content" / "0.1.0" / "content" / "assets" / "hello.txt"
            self.assertTrue(materialized_path.exists())
            self.assertEqual("hello\n", materialized_path.read_text(encoding="utf-8"))

            lockfile = read_json(lockfile_path)
            package = lockfile["targets"][0]["packages"][0]
            self.assertEqual(package["contents"][0]["path"], "assets/hello.txt")
            self.assertTrue(package["contents"][0]["cachePath"].endswith("content/assets/hello.txt"))

    def test_build_stages_workspace_target_contents(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_text:
            temp_dir = Path(temp_dir_text)
            lockfile_path = temp_dir / "workspace.lock.json"
            cache_dir = temp_dir / "cache"
            output_dir = temp_dir / "stage"
            report_path = temp_dir / "build.json"

            restore = run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "package",
                "restore",
                "--project",
                str(WORKSPACE_PROJECT),
                "--lockfile",
                str(lockfile_path),
                "--cache-dir",
                str(cache_dir),
            )
            self.assertEqual(restore.returncode, 0, restore.stdout + restore.stderr)

            cp = run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "build",
                "--lockfile",
                str(lockfile_path),
                "--cache-dir",
                str(cache_dir),
                "--target",
                "NGIN.EditorSample",
                "--output",
                str(output_dir),
                "--json-report",
                str(report_path),
            )
            self.assertEqual(cp.returncode, 0, cp.stdout + cp.stderr)
            self.assertTrue((output_dir / "assets" / "default-layout.json").exists())
            self.assertTrue((output_dir / "config" / "diagnostics.defaults.json").exists())
            self.assertTrue((output_dir / "ngin.target.json").exists())
            report = read_json(report_path)
            self.assertEqual(report["command"], "build")
            self.assertEqual(report["build"]["path"], str(output_dir))
            self.assertTrue(report["ok"])

    def test_build_copies_config_sources_and_target_paths(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_text:
            temp_dir = Path(temp_dir_text)
            package_root = temp_dir / "pkg"
            package_root.mkdir(parents=True, exist_ok=True)
            (package_root / "assets").mkdir()
            (package_root / "assets" / "hello.txt").write_text("hello\n", encoding="utf-8")
            package_path = package_root / "ngin.package.json"
            project_path = temp_dir / "ngin.project.json"
            lockfile_path = temp_dir / "ngin.lock.json"
            cache_dir = temp_dir / "cache"
            output_dir = temp_dir / "stage"
            (temp_dir / "config").mkdir()
            (temp_dir / "config" / "app.json").write_text("{\"mode\":\"dev\"}\n", encoding="utf-8")

            write_json(
                package_path,
                make_package(
                    "Acme.Content",
                    contents=[{"path": "assets/hello.txt", "kind": "asset", "targetPath": "share/hello.txt"}],
                ),
            )
            project = make_project(
                "Content.Target",
                [{"name": "Acme.Content", "versionRange": ">=0.1.0 <1.0.0", "optional": False}],
            )
            project["targets"][0]["configSources"] = ["config/app.json"]
            write_json(project_path, project)

            restore = run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "package",
                "restore",
                "--project",
                str(project_path),
                "--lockfile",
                str(lockfile_path),
                "--cache-dir",
                str(cache_dir),
                "--package-override",
                f"Acme.Content={package_path}",
            )
            self.assertEqual(restore.returncode, 0, restore.stdout + restore.stderr)

            cp = run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "build",
                "--lockfile",
                str(lockfile_path),
                "--cache-dir",
                str(cache_dir),
                "--target",
                "Content.Target",
                "--output",
                str(output_dir),
            )
            self.assertEqual(cp.returncode, 0, cp.stdout + cp.stderr)
            self.assertEqual("hello\n", (output_dir / "share" / "hello.txt").read_text(encoding="utf-8"))
            self.assertEqual("{\"mode\":\"dev\"}\n", (output_dir / "config" / "app.json").read_text(encoding="utf-8"))
            build_manifest = read_json(output_dir / "ngin.target.json")
            self.assertEqual(build_manifest["target"], "Content.Target")
            self.assertIn("config/app.json", build_manifest["configSources"])

    def test_missing_declared_content_fails_restore(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_text:
            temp_dir = Path(temp_dir_text)
            package_root = temp_dir / "pkg"
            package_root.mkdir(parents=True, exist_ok=True)
            package_path = package_root / "ngin.package.json"
            project_path = temp_dir / "ngin.project.json"

            write_json(
                package_path,
                make_package(
                    "Acme.BrokenContent",
                    contents=[{"path": "assets/missing.txt", "kind": "asset"}],
                ),
            )
            write_json(
                project_path,
                make_project(
                    "Broken.Content",
                    [{"name": "Acme.BrokenContent", "versionRange": ">=0.1.0 <1.0.0", "optional": False}],
                ),
            )

            cp = run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "package",
                "restore",
                "--project",
                str(project_path),
                "--package-override",
                f"Acme.BrokenContent={package_path}",
            )
            self.assertNotEqual(cp.returncode, 0, cp.stdout + cp.stderr)
            self.assertIn("content file 'assets/missing.txt' does not exist", cp.stdout)

    def test_validate_workspace_project_target(self) -> None:
        cp = run_cli(
            str(TOOLS_DIR / "ngin.py"),
            "validate",
            "--project",
            str(WORKSPACE_PROJECT),
            "--target",
            "NGIN.CoreSample",
        )
        self.assertEqual(cp.returncode, 0, cp.stdout + cp.stderr)
        self.assertIn("Validated target: NGIN.CoreSample", cp.stdout)
        self.assertIn("packages: 5", cp.stdout)

    def test_graph_workspace_project_target(self) -> None:
        cp = run_cli(
            str(TOOLS_DIR / "ngin.py"),
            "graph",
            "--project",
            str(WORKSPACE_PROJECT),
            "--target",
            "NGIN.EditorSample",
        )
        self.assertEqual(cp.returncode, 0, cp.stdout + cp.stderr)
        self.assertIn("Graph for target: NGIN.EditorSample", cp.stdout)
        self.assertIn("NGIN.Diagnostics", cp.stdout)

    def test_resolve_project_default_target(self) -> None:
        project_path = ROOT_DIR / "docs" / "examples" / "project-model" / "ngin.project.json"
        cp = run_cli(str(TOOLS_DIR / "ngin.py"), "resolve", "--project", str(project_path))
        self.assertEqual(cp.returncode, 0, cp.stdout + cp.stderr)
        self.assertIn("Resolved target: Sandbox.Game", cp.stdout)
        self.assertIn("NGIN.ECS 0.1.0 [catalog]", cp.stdout)

    def test_resolve_project_editor_target(self) -> None:
        project_path = ROOT_DIR / "docs" / "examples" / "project-model" / "ngin.project.json"
        cp = run_cli(str(TOOLS_DIR / "ngin.py"), "resolve", "--project", str(project_path), "--target", "Tools.Editor")
        self.assertEqual(cp.returncode, 0, cp.stdout + cp.stderr)
        self.assertIn("Resolved target: Tools.Editor", cp.stdout)
        self.assertIn("NGIN.Editor 0.1.0 [catalog]", cp.stdout)
        self.assertIn("Editor.Workspace", cp.stdout)

    def test_project_auto_discovery(self) -> None:
        project_dir = ROOT_DIR / "docs" / "examples" / "project-model"
        cp = run_cli(str(TOOLS_DIR / "ngin.py"), "resolve", "--target", "Tools.Cli", cwd=project_dir)
        self.assertEqual(cp.returncode, 0, cp.stdout + cp.stderr)
        self.assertIn("Resolved target: Tools.Cli", cp.stdout)

    def test_missing_project_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_text:
            cp = run_cli(str(TOOLS_DIR / "ngin.py"), "validate", cwd=Path(temp_dir_text))
            self.assertNotEqual(cp.returncode, 0, cp.stdout + cp.stderr)
            self.assertIn("no project manifest specified", cp.stderr or cp.stdout)

    def test_validate_locked_matches_live_resolution(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_text:
            temp_dir = Path(temp_dir_text)
            lockfile_path = temp_dir / "workspace.lock.json"
            cache_dir = temp_dir / "cache"
            live_report = temp_dir / "live.json"
            locked_report = temp_dir / "locked.json"

            restore = run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "package",
                "restore",
                "--project",
                str(WORKSPACE_PROJECT),
                "--lockfile",
                str(lockfile_path),
                "--cache-dir",
                str(cache_dir),
            )
            self.assertEqual(restore.returncode, 0, restore.stdout + restore.stderr)

            live = run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "resolve",
                "--project",
                str(WORKSPACE_PROJECT),
                "--target",
                "NGIN.EditorSample",
                "--json-report",
                str(live_report),
            )
            self.assertEqual(live.returncode, 0, live.stdout + live.stderr)

            locked = run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "resolve",
                "--locked",
                "--lockfile",
                str(lockfile_path),
                "--cache-dir",
                str(cache_dir),
                "--target",
                "NGIN.EditorSample",
                "--json-report",
                str(locked_report),
            )
            self.assertEqual(locked.returncode, 0, locked.stdout + locked.stderr)

            live_payload = read_json(live_report)
            locked_payload = read_json(locked_report)
            self.assertEqual(live_payload["resolution"]["requiredModules"], locked_payload["resolution"]["requiredModules"])
            self.assertEqual(live_payload["resolution"]["optionalModules"], locked_payload["resolution"]["optionalModules"])
            self.assertEqual(
                [pkg["name"] for pkg in live_payload["resolution"]["packages"]],
                [pkg["name"] for pkg in locked_payload["resolution"]["packages"]],
            )
            self.assertEqual(locked_payload["source"], f"lockfile:{lockfile_path}")

    def test_malformed_lockfile_fails_locked_validation(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_text:
            temp_dir = Path(temp_dir_text)
            lockfile_path = temp_dir / "broken.lock.json"
            lockfile_path.write_text("{\n  \"schemaVersion\": 1,\n", encoding="utf-8")

            cp = run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "validate",
                "--locked",
                "--lockfile",
                str(lockfile_path),
                "--target",
                "NGIN.CoreSample",
            )
            self.assertNotEqual(cp.returncode, 0, cp.stdout + cp.stderr)
            self.assertIn("invalid manifest JSON", cp.stderr or cp.stdout)

    def test_missing_cache_entry_fails_locked_resolve(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_text:
            temp_dir = Path(temp_dir_text)
            lockfile_path = temp_dir / "workspace.lock.json"
            cache_dir = temp_dir / "cache"

            restore = run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "package",
                "restore",
                "--project",
                str(WORKSPACE_PROJECT),
                "--lockfile",
                str(lockfile_path),
                "--cache-dir",
                str(cache_dir),
            )
            self.assertEqual(restore.returncode, 0, restore.stdout + restore.stderr)
            (cache_dir / "packages" / "NGIN.Core" / "0.1.0" / "entry.json").unlink()

            cp = run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "resolve",
                "--locked",
                "--lockfile",
                str(lockfile_path),
                "--cache-dir",
                str(cache_dir),
                "--target",
                "NGIN.CoreSample",
            )
            self.assertNotEqual(cp.returncode, 0, cp.stdout + cp.stderr)
            self.assertIn("missing cache entry", cp.stdout)

    def test_missing_cached_content_fails_locked_resolve(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_text:
            temp_dir = Path(temp_dir_text)
            lockfile_path = temp_dir / "workspace.lock.json"
            cache_dir = temp_dir / "cache"

            restore = run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "package",
                "restore",
                "--project",
                str(WORKSPACE_PROJECT),
                "--lockfile",
                str(lockfile_path),
                "--cache-dir",
                str(cache_dir),
            )
            self.assertEqual(restore.returncode, 0, restore.stdout + restore.stderr)
            (cache_dir / "packages" / "NGIN.Editor" / "0.1.0" / "content" / "assets" / "default-layout.json").unlink()

            cp = run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "resolve",
                "--locked",
                "--lockfile",
                str(lockfile_path),
                "--cache-dir",
                str(cache_dir),
                "--target",
                "NGIN.EditorSample",
            )
            self.assertNotEqual(cp.returncode, 0, cp.stdout + cp.stderr)
            self.assertIn("missing content file", cp.stdout)

    def test_required_missing_package_fails_validation(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_text:
            temp_dir = Path(temp_dir_text)
            project_path = temp_dir / "ngin.project.json"
            write_json(
                project_path,
                make_project(
                    "Missing.Required",
                    [{"name": "Acme.Missing", "versionRange": ">=0.1.0 <1.0.0", "optional": False}],
                ),
            )

            cp = run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "validate",
                "--project",
                str(project_path),
                "--target",
                "Missing.Required",
            )
            self.assertNotEqual(cp.returncode, 0, cp.stdout + cp.stderr)
            self.assertIn("Package resolution errors:", cp.stdout)
            self.assertIn("package 'Acme.Missing' could not be resolved", cp.stdout)

    def test_optional_missing_package_warns_and_succeeds(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_text:
            temp_dir = Path(temp_dir_text)
            project_path = temp_dir / "ngin.project.json"
            write_json(
                project_path,
                make_project(
                    "Missing.Optional",
                    [{"name": "Acme.Optional", "versionRange": ">=0.1.0 <1.0.0", "optional": True}],
                ),
            )

            cp = run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "resolve",
                "--project",
                str(project_path),
                "--target",
                "Missing.Optional",
            )
            self.assertEqual(cp.returncode, 0, cp.stdout + cp.stderr)
            self.assertIn("Resolved target: Missing.Optional", cp.stdout)
            self.assertIn("Resolution warnings:", cp.stdout)
            self.assertIn("package 'Acme.Optional' could not be resolved", cp.stdout)

    def test_package_version_mismatch_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_text:
            temp_dir = Path(temp_dir_text)
            project_path = temp_dir / "ngin.project.json"
            package_path = temp_dir / "Acme.Versioned.ngin.package.json"
            write_json(
                project_path,
                make_project(
                    "Version.Mismatch",
                    [{"name": "Acme.Versioned", "versionRange": ">=1.0.0 <2.0.0", "optional": False}],
                ),
            )
            write_json(package_path, make_package("Acme.Versioned", version="0.1.0"))

            cp = run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "validate",
                "--project",
                str(project_path),
                "--target",
                "Version.Mismatch",
                "--package-override",
                f"Acme.Versioned={package_path}",
            )
            self.assertNotEqual(cp.returncode, 0, cp.stdout + cp.stderr)
            self.assertIn("does not satisfy", cp.stdout)

    def test_package_platform_mismatch_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_text:
            temp_dir = Path(temp_dir_text)
            project_path = temp_dir / "ngin.project.json"
            package_path = temp_dir / "Acme.Platform.ngin.package.json"
            write_json(
                project_path,
                make_project(
                    "Platform.Mismatch",
                    [{"name": "Acme.Platform", "versionRange": ">=0.1.0 <1.0.0", "optional": False}],
                ),
            )
            write_json(package_path, make_package("Acme.Platform", platforms=["windows"]))

            cp = run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "validate",
                "--project",
                str(project_path),
                "--target",
                "Platform.Mismatch",
                "--package-override",
                f"Acme.Platform={package_path}",
            )
            self.assertNotEqual(cp.returncode, 0, cp.stdout + cp.stderr)
            self.assertIn("is not supported on platform 'linux-x64'", cp.stdout)

    def test_package_cycle_fails(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_text:
            temp_dir = Path(temp_dir_text)
            project_path = temp_dir / "ngin.project.json"
            alpha_path = temp_dir / "Acme.Alpha.ngin.package.json"
            beta_path = temp_dir / "Acme.Beta.ngin.package.json"
            write_json(
                project_path,
                make_project(
                    "Cycle.Target",
                    [{"name": "Acme.Alpha", "versionRange": ">=0.1.0 <1.0.0", "optional": False}],
                ),
            )
            write_json(
                alpha_path,
                make_package(
                    "Acme.Alpha",
                    dependencies=[{"name": "Acme.Beta", "versionRange": ">=0.1.0 <1.0.0", "optional": False}],
                ),
            )
            write_json(
                beta_path,
                make_package(
                    "Acme.Beta",
                    dependencies=[{"name": "Acme.Alpha", "versionRange": ">=0.1.0 <1.0.0", "optional": False}],
                ),
            )

            cp = run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "validate",
                "--project",
                str(project_path),
                "--target",
                "Cycle.Target",
                "--package-override",
                f"Acme.Alpha={alpha_path}",
                "--package-override",
                f"Acme.Beta={beta_path}",
            )
            self.assertNotEqual(cp.returncode, 0, cp.stdout + cp.stderr)
            self.assertIn("package graph contains dependency cycle", cp.stdout)

    def test_validate_json_report_success_shape(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_text:
            temp_dir = Path(temp_dir_text)
            report_path = temp_dir / "report.json"
            cp = run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "validate",
                "--project",
                str(WORKSPACE_PROJECT),
                "--target",
                "NGIN.CoreSample",
                "--json-report",
                str(report_path),
            )
            self.assertEqual(cp.returncode, 0, cp.stdout + cp.stderr)
            report = read_json(report_path)
            self.assertIsInstance(report, dict)
            self.assertTrue(report["ok"])
            self.assertEqual(report["command"], "validate")
            self.assertEqual(report["target"], "NGIN.CoreSample")
            self.assertIn("validation", report)
            self.assertIn("resolution", report)
            self.assertTrue(report["validation"]["ok"])
            self.assertEqual(len(report["resolution"]["packages"]), 5)
            self.assertEqual(report["resolution"]["errors"], [])

    def test_graph_json_report_success_shape(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_text:
            temp_dir = Path(temp_dir_text)
            report_path = temp_dir / "graph-report.json"
            cp = run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "graph",
                "--project",
                str(WORKSPACE_PROJECT),
                "--target",
                "NGIN.EditorSample",
                "--json-report",
                str(report_path),
            )
            self.assertEqual(cp.returncode, 0, cp.stdout + cp.stderr)
            report = read_json(report_path)
            self.assertEqual(report["command"], "graph")
            self.assertTrue(report["ok"])
            self.assertIn("packageEdges", report["resolution"])
            self.assertIn("dependencyEdges", report["resolution"])
            self.assertIn("NGIN.Editor", report["resolution"]["packageEdges"])

    def test_validate_json_report_failure_shape(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_text:
            temp_dir = Path(temp_dir_text)
            project_path = temp_dir / "ngin.project.json"
            report_path = temp_dir / "failure-report.json"
            write_json(
                project_path,
                make_project(
                    "Missing.Required",
                    [{"name": "Acme.Missing", "versionRange": ">=0.1.0 <1.0.0", "optional": False}],
                ),
            )

            cp = run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "validate",
                "--project",
                str(project_path),
                "--target",
                "Missing.Required",
                "--json-report",
                str(report_path),
            )
            self.assertNotEqual(cp.returncode, 0, cp.stdout + cp.stderr)
            report = read_json(report_path)
            self.assertFalse(report["ok"])
            self.assertEqual(report["command"], "validate")
            self.assertEqual(report["reason"], "target validation failed")
            self.assertTrue(report["validation"]["ok"])
            self.assertIn("package 'Acme.Missing' could not be resolved", report["resolution"]["errors"])

    def test_resolve_json_report_includes_warnings(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir_text:
            temp_dir = Path(temp_dir_text)
            project_path = temp_dir / "ngin.project.json"
            report_path = temp_dir / "warning-report.json"
            write_json(
                project_path,
                make_project(
                    "Missing.Optional",
                    [{"name": "Acme.Optional", "versionRange": ">=0.1.0 <1.0.0", "optional": True}],
                ),
            )

            cp = run_cli(
                str(TOOLS_DIR / "ngin.py"),
                "resolve",
                "--project",
                str(project_path),
                "--target",
                "Missing.Optional",
                "--json-report",
                str(report_path),
            )
            self.assertEqual(cp.returncode, 0, cp.stdout + cp.stderr)
            report = read_json(report_path)
            self.assertTrue(report["ok"])
            self.assertEqual(report["command"], "resolve")
            self.assertEqual(report["resolution"]["errors"], [])
            self.assertIn("package 'Acme.Optional' could not be resolved", report["resolution"]["warnings"])


if __name__ == "__main__":
    unittest.main()
