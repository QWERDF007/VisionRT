from __future__ import annotations

import copy
import json
import os
from pathlib import Path
import sys
import subprocess
import shutil
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))

import package_app


class ToolsDeploymentContract(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="visionrt-deploy-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.install = self.root / "installed"
        self.install.mkdir()
        (self.install / package_app.MARKER_FILE).touch()
        self.package = self.install / "tools-package"
        self.package.mkdir()
        (self.package / "old.txt").write_text("old installation", encoding="utf-8")
        self.source = self.root / "source"
        (self.source / "bin").mkdir(parents=True)
        (self.source / "tools/builtin").mkdir(parents=True)
        (self.source / "bin/plugin.dll").write_bytes(b"metadata-only module")
        (self.source / "bin/runtime.dll").write_bytes(b"runtime dependency")
        self.definition = {
            "schema_version": 4, "tool_id": "test.plugin", "display_name": "部署测试",
            "version": "1.0.0", "kind": "cpp", "init_params": [], "inputs": [], "outputs": [],
        }
        self.index = {
            "schema_version": 1, "variant": "cpu",
            "modules": [{"id": "test_module", "library": "bin/plugin.dll"}],
            "tools": [{"tool_id": "test.plugin", "module": "test_module", "definition": "tools/builtin/test.json"}],
            "runtime_files": ["bin/runtime.dll"],
        }
        self.validator = Path(os.environ.get(
            "VISIONTOOLS_VALIDATOR", str(package_app.REPO_ROOT / "build/bin" /
                ("visionrt_validate_tool.exe" if os.name == "nt" else "visionrt_validate_tool"))))
        self.assertTrue(self.validator.is_file(), f"Build the host validator first: {self.validator}")

    def write_source(self) -> None:
        (self.source / "module_index.json").write_text(json.dumps(self.index), encoding="utf-8")
        (self.source / "tools/builtin/test.json").write_text(json.dumps(self.definition), encoding="utf-8")

    def archive(self) -> Path:
        archive = self.root / "tools.zip"
        with zipfile.ZipFile(archive, "w") as output:
            for source in self.source.rglob("*"):
                if source.is_file():
                    output.write(source, source.relative_to(self.source).as_posix())
        return archive

    def test_json_zip_replaces_old_package_after_host_validation(self) -> None:
        self.write_source()
        external = self.source / "module_index.json"
        package_app.deploy_tools_package(self.install, self.archive(), self.validator, external)
        self.assertFalse((self.package / "old.txt").exists())
        self.assertEqual((self.package / "module_index.json").read_bytes(), external.read_bytes())
        self.assertEqual((self.package / "bin/plugin.dll").read_bytes(), b"metadata-only module")

    def test_invalid_metadata_keeps_old_installation(self) -> None:
        valid_index = copy.deepcopy(self.index)
        cases = {
            "missing schema version": lambda: self.index.pop("schema_version"),
            "missing variant": lambda: self.index.pop("variant"),
            "unknown field": lambda: self.index.update(unknown=True),
            "dangling module": lambda: self.index["tools"][0].update(module="missing"),
            "unreferenced module": lambda: self.index["modules"].append({"id": "unused", "library": "bin/plugin.dll"}),
            "missing module": lambda: self.index["modules"][0].update(library="bin/missing.dll"),
            "missing definition": lambda: self.index["tools"][0].update(definition="assets/missing.json"),
            "missing runtime": lambda: self.index.update(runtime_files=["bin/missing.dll"]),
            "identity mismatch": lambda: self.definition.update(tool_id="other.id"),
            "missing declared resource": lambda: self.definition.update(resources=["models/missing_weights.bin"]),
        }
        for name, invalidate in cases.items():
            with self.subTest(name=name):
                self.index = copy.deepcopy(valid_index)
                self.definition["tool_id"] = "test.plugin"
                self.definition.pop("resources", None)
                invalidate()
                self.write_source()
                with self.assertRaisesRegex(RuntimeError, "Host tools package validation failed"):
                    package_app.deploy_tools_package(self.install, self.archive(), self.validator)
                self.assertEqual((self.package / "old.txt").read_text(encoding="utf-8"), "old installation")

    def test_missing_declared_resource_keeps_old_package_executable(self) -> None:
        self.write_source()
        package_app.deploy_tools_package(self.install, self.archive(), self.validator)
        self.assertTrue((self.package / "module_index.json").is_file())
        (self.package / "canary.txt").write_text("prior working package canary", encoding="utf-8")

        self.definition["resources"] = ["models/nonexistent_model.onnx"]
        self.write_source()
        with self.assertRaisesRegex(RuntimeError, "Host tools package validation failed"):
            package_app.deploy_tools_package(self.install, self.archive(), self.validator)

        self.assertTrue((self.package / "canary.txt").is_file())
        self.assertEqual((self.package / "canary.txt").read_text(encoding="utf-8"), "prior working package canary")
        self.assertTrue((self.package / "module_index.json").is_file())
        self.assertEqual((self.package / "bin/plugin.dll").read_bytes(), b"metadata-only module")
        validation = subprocess.run([str(self.validator), "--package", str(self.package)], capture_output=True, text=True)
        self.assertEqual(validation.returncode, 0, f"Old package became invalid: {validation.stderr}")

    def test_missing_json_and_yaml_only_packages_are_rejected(self) -> None:
        self.write_source()
        (self.source / "module_index.json").unlink()
        (self.source / "module_index.yaml").write_text("schema_version: 2", encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "module_index.json"):
            package_app.deploy_tools_package(self.install, self.archive(), self.validator)
        self.assertTrue((self.package / "old.txt").is_file())

    def test_external_index_mismatch_keeps_old_installation(self) -> None:
        self.write_source()
        external = self.root / "external.json"
        external.write_text("{}", encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "byte-by-byte"):
            package_app.deploy_tools_package(self.install, self.archive(), self.validator, external)
        self.assertTrue((self.package / "old.txt").is_file())

    def test_missing_validator_cannot_publish(self) -> None:
        self.write_source()
        with self.assertRaisesRegex(RuntimeError, "validator executable is required"):
            package_app.deploy_tools_package(self.install, self.archive())
        self.assertTrue((self.package / "old.txt").is_file())

    def test_duplicate_json_members_are_rejected_before_installation(self) -> None:
        for name in ("module_index.json", "tools/builtin/test.json"):
            with self.subTest(name=name):
                self.write_source()
                source = self.source / name
                source.write_text(source.read_text(encoding="utf-8").replace(
                    '"schema_version":', '"schema_version": 1, "schema_version":'), encoding="utf-8")
                with self.assertRaisesRegex(RuntimeError, "duplicate JSON member"):
                    package_app.deploy_tools_package(self.install, self.archive(), self.validator)
                self.assertTrue((self.package / "old.txt").is_file())

    def test_invalid_archives_keep_old_installation(self) -> None:
        for entry in ("../escaped.txt", "/absolute.txt", "C:/absolute.txt", "linked.txt"):
            with self.subTest(entry=entry):
                archive = self.root / "invalid.zip"
                with zipfile.ZipFile(archive, "w") as output:
                    member = zipfile.ZipInfo(entry)
                    if entry == "linked.txt":
                        member.create_system = 3
                        member.external_attr = 0o120777 << 16
                    output.writestr(member, b"invalid archive entry")
                with self.assertRaises(RuntimeError):
                    package_app.deploy_tools_package(self.install, archive, self.validator)
                self.assertTrue((self.package / "old.txt").is_file())
                self.assertFalse((self.install / "escaped.txt").exists())
        archive.write_bytes(b"PK\x03\x04NOT_A_VALID_ZIP")
        with self.assertRaisesRegex(RuntimeError, "corrupt or invalid ZIP"):
            package_app.deploy_tools_package(self.install, archive, self.validator)
        self.assertTrue((self.package / "old.txt").is_file())

    @unittest.skipUnless(os.name == "nt", "Windows file-lock semantics")
    def test_locked_target_keeps_old_package_then_replaces_after_release(self) -> None:
        self.write_source()
        with (self.package / "old.txt").open("rb") as locked:
            with self.assertRaisesRegex(RuntimeError, "locked or in use"):
                package_app.deploy_tools_package(self.install, self.archive(), self.validator)
            self.assertEqual(locked.read(), b"old installation")
        package_app.deploy_tools_package(self.install, self.archive(), self.validator)
        self.assertFalse((self.package / "old.txt").exists())

    def test_cli_deploys_with_standard_library_only(self) -> None:
        self.write_source()
        result = subprocess.run([
            sys.executable, "-S", str(package_app.REPO_ROOT / "tools/package_app.py"),
            "--deploy-tools-only", "--install-dir", str(self.install),
            "--tools-package", str(self.archive()), "--validator", str(self.validator),
        ], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((self.package / "module_index.json").is_file())

    def test_build_info_is_preserved_when_present(self) -> None:
        self.write_source()
        (self.source / "build_info.json").write_text(
            '{"schema_version": 1, "component": "VisionTools", "version": "0.1.0", "commit": "abc", "dirty": false, "built_at_epoch": 1793082712}\n',
            encoding="utf-8")
        package_app.deploy_tools_package(self.install, self.archive(), self.validator)
        self.assertTrue((self.package / "build_info.json").is_file())
        content = (self.package / "build_info.json").read_text(encoding="utf-8")
        self.assertIn('"commit": "abc"', content)


class InstallationContract(unittest.TestCase):
    def test_qt_deployment_prefers_the_selected_build(self) -> None:
        with tempfile.TemporaryDirectory(prefix="visionrt-qt-") as temporary:
            root = Path(temporary)
            selected = root / "selected-qt"
            (root / "CMakeCache.txt").write_text(
                f"Qt6Core_DIR:PATH={selected.as_posix()}/lib/cmake/Qt6Core\n", encoding="utf-8")
            self.assertEqual(package_app.qt_roots(root)[0], selected)

    def test_required_runtime_files_and_clean_target_protection(self) -> None:
        with tempfile.TemporaryDirectory(prefix="visionrt-install-") as temporary:
            root = Path(temporary)
            for name in ("VisionRT.exe", package_app.MARKER_FILE, "quickui.dll", "python312.dll"):
                (root / name).touch()
            (root / "build_info.json").write_text(
                '{"schema_version": 1, "component": "VisionRT", "version": "0.1.0", "commit": "abc", "dirty": false, "built_at_epoch": 1793082712}\n',
                encoding="utf-8")
            (root / "contracts-build-info.json").write_text(
                '{"schema_version": 1, "component": "VisionRTContracts", "version": "0.1.0", "commit": "def", "dirty": false, "built_at_epoch": 1793082712}\n',
                encoding="utf-8")
            (root / "python/Lib/site-packages/numpy").mkdir(parents=True)
            (root / "python/DLLs").mkdir()
            package_app.verify_package(root, require_qt=False)
            for name in ("quickui.dll", "python312.dll", "build_info.json", "contracts-build-info.json"):
                with self.subTest(name=name):
                    old_text = (root / name).read_text(encoding="utf-8") if name.endswith(".json") else None
                    (root / name).unlink()
                    with self.assertRaises(RuntimeError):
                        package_app.verify_package(root, require_qt=False)
                    if old_text:
                        (root / name).write_text(old_text, encoding="utf-8")
                    else:
                        (root / name).touch()
            (root / "python/Lib/site-packages/numpy").rmdir()
            with self.assertRaisesRegex(RuntimeError, "numpy"):
                package_app.verify_package(root, require_qt=False)
            for protected in (package_app.REPO_ROOT, package_app.REPO_ROOT / "src"):
                with self.subTest(path=protected), self.assertRaisesRegex(RuntimeError, "refusing to clean"):
                    package_app.clear_install_directory(protected, clean=True, force=True)

    def test_python_runtime_uses_build_selected_installation(self) -> None:
        with tempfile.TemporaryDirectory(prefix="visionrt-python-") as temporary:
            root = Path(temporary)
            selected = root / "python"
            selected.mkdir()
            (selected / "python.exe").touch()
            (root / "CMakeCache.txt").write_text(
                f"VISIONRT_PYTHON_HOME:PATH={selected.as_posix()}\n", encoding="utf-8")
            self.assertEqual(package_app.resolve_python_home(root), selected)


class BuildMetadataIntegrationContract(unittest.TestCase):
    """Integration test matrix for WriteBuildInfo.cmake and release gating (T46-01 / T46-02 / S46-02)."""

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="vrt-git-meta-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.repo_dir = self.root / "repo"
        self.repo_dir.mkdir()
        self.output_json = self.root / "build_info.json"
        self.binary_file = self.root / "bin/app.exe"
        self.binary_file.parent.mkdir(parents=True)
        self.binary_file.write_bytes(b"MZ dummy executable")

        # Initialize clean git repository
        self._run_git(["init", "-b", "main"], cwd=self.repo_dir)
        self._run_git(["config", "user.name", "TestUser"], cwd=self.repo_dir)
        self._run_git(["config", "user.email", "test@example.com"], cwd=self.repo_dir)

        # Initial production source
        (self.repo_dir / "src").mkdir()
        (self.repo_dir / "src/main.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
        self._run_git(["add", "src/main.cpp"], cwd=self.repo_dir)
        self._run_git(["commit", "-m", "initial clean commit"], cwd=self.repo_dir)

    @classmethod
    def _find_git(cls) -> str:
        found = shutil.which("git")
        if found:
            return found
        for candidate in (
            r"D:\Software\Git\cmd\git.exe",
            r"C:\Program Files\Git\cmd\git.exe",
            r"C:\Program Files (x86)\Git\cmd\git.exe",
        ):
            if Path(candidate).is_file():
                return candidate
        return "git"

    @classmethod
    def _find_cmake(cls) -> str:
        found = os.environ.get("CMAKE_COMMAND") or shutil.which("cmake")
        if found:
            return found
        for candidate in (
            r"C:\Program Files\CMake\bin\cmake.exe",
            r"D:\Software\CMake\bin\cmake.exe",
        ):
            if Path(candidate).is_file():
                return candidate
        return "cmake"

    def _run_git(self, args: list[str], cwd: Path) -> subprocess.CompletedProcess:
        git_cmd = self._find_git()
        res = subprocess.run([git_cmd] + args, cwd=cwd, capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, f"git command failed: {' '.join(args)}\n{res.stderr}")
        return res

    def _invoke_cmake_script(
        self,
        repo_dir: Path,
        output_file: Path,
        source_file: Path | None = None,
        release_build: bool = False,
        commit: str = "",
        branch: str = "",
        component: str = "TestComponent",
        version: str = "1.0.0",
    ) -> subprocess.CompletedProcess:
        cmake_path = self._find_cmake()
        self.assertIsNotNone(cmake_path, "cmake executable not found")
        cmd = [
            cmake_path,
            f"-DCOMPONENT={component}",
            f"-DVERSION={version}",
            f"-DOUTPUT={output_file.as_posix()}",
            f"-DREPO_DIR={repo_dir.as_posix()}",
            f"-DGIT_EXECUTABLE={self._find_git()}",
        ]
        if source_file:
            cmd.append(f"-DSOURCE_FILE={source_file.as_posix()}")
        if release_build:
            cmd.append("-DRELEASE_BUILD=ON")
        if commit:
            cmd.append(f"-DCOMMIT={commit}")
        if branch:
            cmd.append(f"-DBRANCH={branch}")
        cmd.extend(["-P", str((package_app.REPO_ROOT / "cmake/WriteBuildInfo.cmake").as_posix())])
        return subprocess.run(cmd, capture_output=True, text=True)

    def test_01_matrix_clean_initial_build(self) -> None:
        """Matrix Case 1: Initial clean build extracts exact commit, branch, dirty=false, and artifact mtime."""
        git_cmd = self._find_git()
        head_commit = subprocess.check_output([git_cmd, "rev-parse", "HEAD"], cwd=self.repo_dir, text=True).strip()
        branch_name = subprocess.check_output([git_cmd, "rev-parse", "--abbrev-ref", "HEAD"], cwd=self.repo_dir, text=True).strip()

        res = self._invoke_cmake_script(self.repo_dir, self.output_json, source_file=self.binary_file)
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertTrue(self.output_json.is_file())

        meta = json.loads(self.output_json.read_text(encoding="utf-8"))
        self.assertEqual(meta["commit"], head_commit)
        self.assertEqual(meta["branch"], branch_name)
        self.assertIs(meta["dirty"], False)
        self.assertGreater(meta["built_at_epoch"], 0)

        # Release packaging validation accepts clean metadata
        verified = package_app.verify_build_metadata(self.output_json, expected_component="TestComponent", allow_dirty=False)
        self.assertEqual(verified["commit"], head_commit)

    def test_02_matrix_rebuild_without_changes(self) -> None:
        """Matrix Case 2: Rebuild with unchanged binary preserves real artifact mtime epoch."""
        self._invoke_cmake_script(self.repo_dir, self.output_json, source_file=self.binary_file)
        meta_first = json.loads(self.output_json.read_text(encoding="utf-8"))

        res = self._invoke_cmake_script(self.repo_dir, self.output_json, source_file=self.binary_file)
        self.assertEqual(res.returncode, 0)
        meta_second = json.loads(self.output_json.read_text(encoding="utf-8"))

        self.assertEqual(meta_first["built_at_epoch"], meta_second["built_at_epoch"])
        self.assertEqual(meta_second["dirty"], False)

    def test_03_matrix_review_docs_committed(self) -> None:
        """Matrix Case 3: Review documentation commit advances commit hash and remains clean."""
        docs_dir = self.repo_dir / "docs"
        docs_dir.mkdir()
        (docs_dir / "R46_review.md").write_text("# Review\n", encoding="utf-8")
        self._run_git(["add", "docs/R46_review.md"], cwd=self.repo_dir)
        self._run_git(["commit", "-m", "docs: update review document"], cwd=self.repo_dir)
        new_head = subprocess.check_output([self._find_git(), "rev-parse", "HEAD"], cwd=self.repo_dir, text=True).strip()

        res = self._invoke_cmake_script(self.repo_dir, self.output_json, source_file=self.binary_file)
        self.assertEqual(res.returncode, 0)
        meta = json.loads(self.output_json.read_text(encoding="utf-8"))
        self.assertEqual(meta["commit"], new_head)
        self.assertIs(meta["dirty"], False)

    def test_04_matrix_tracked_code_dirty(self) -> None:
        """Matrix Case 4: Modified tracked source causes dirty=true and fatal error in RELEASE_BUILD."""
        (self.repo_dir / "src/main.cpp").write_text("int main() { return 1; }\n", encoding="utf-8")

        # Normal build: logs dirty=true, does not fail
        res_dev = self._invoke_cmake_script(self.repo_dir, self.output_json, source_file=self.binary_file, release_build=False)
        self.assertEqual(res_dev.returncode, 0)
        meta_dev = json.loads(self.output_json.read_text(encoding="utf-8"))
        self.assertIs(meta_dev["dirty"], True)

        # Release build: must fatal error
        res_rel = self._invoke_cmake_script(self.repo_dir, self.output_json, source_file=self.binary_file, release_build=True)
        self.assertNotEqual(res_rel.returncode, 0)
        self.assertIn("发布模式禁止脏工作区候选", res_rel.stderr)

        # Packager must reject dirty metadata unless allow_dirty is explicitly given
        with self.assertRaisesRegex(RuntimeError, "is dirty \\(True\\)"):
            package_app.verify_build_metadata(self.output_json, allow_dirty=False)
        verified_dirty = package_app.verify_build_metadata(self.output_json, allow_dirty=True)
        self.assertIs(verified_dirty["dirty"], True)

    def test_05_matrix_untracked_header_dirty(self) -> None:
        """Matrix Case 5: Untracked production header marks dirty=true (fixes F46-02)."""
        (self.repo_dir / "src/foo.h").write_text("#pragma once\n", encoding="utf-8")

        res_dev = self._invoke_cmake_script(self.repo_dir, self.output_json, source_file=self.binary_file, release_build=False)
        self.assertEqual(res_dev.returncode, 0)
        meta_dev = json.loads(self.output_json.read_text(encoding="utf-8"))
        self.assertIs(meta_dev["dirty"], True)

        res_rel = self._invoke_cmake_script(self.repo_dir, self.output_json, source_file=self.binary_file, release_build=True)
        self.assertNotEqual(res_rel.returncode, 0)
        self.assertIn("发布模式禁止脏工作区候选", res_rel.stderr)

    def test_06_matrix_untracked_review_docs_allowed(self) -> None:
        """Matrix Case 6: Whitelisted review docs (WORKLOG.md, docs/R*.md, docs/VisionRT_*.zip) do NOT dirty the build."""
        (self.repo_dir / "WORKLOG.md").write_text("# Worklog\n", encoding="utf-8")
        docs_dir = self.repo_dir / "docs"
        docs_dir.mkdir(exist_ok=True)
        (docs_dir / "R47_验收报告.md").write_text("# Report\n", encoding="utf-8")
        (docs_dir / "VisionRT_Specs_20261030.zip").write_bytes(b"PK\x05\x06" + b"\x00" * 18)

        # Both dev and release builds must pass with dirty=false
        res_dev = self._invoke_cmake_script(self.repo_dir, self.output_json, source_file=self.binary_file, release_build=False)
        self.assertEqual(res_dev.returncode, 0, res_dev.stderr)
        meta_dev = json.loads(self.output_json.read_text(encoding="utf-8"))
        self.assertIs(meta_dev["dirty"], False)

        res_rel = self._invoke_cmake_script(self.repo_dir, self.output_json, source_file=self.binary_file, release_build=True)
        self.assertEqual(res_rel.returncode, 0, res_rel.stderr)

        verified = package_app.verify_build_metadata(self.output_json, allow_dirty=False)
        self.assertIs(verified["dirty"], False)

    def test_07_matrix_submodule_dirty(self) -> None:
        """Matrix Case 7: Modified submodule marks the workspace dirty."""
        sub_repo_dir = self.root / "sub_repo"
        sub_repo_dir.mkdir()
        self._run_git(["init", "-b", "main"], cwd=sub_repo_dir)
        self._run_git(["config", "user.name", "SubUser"], cwd=sub_repo_dir)
        self._run_git(["config", "user.email", "sub@example.com"], cwd=sub_repo_dir)
        (sub_repo_dir / "sub_lib.cpp").write_text("// sub\n", encoding="utf-8")
        self._run_git(["add", "sub_lib.cpp"], cwd=sub_repo_dir)
        self._run_git(["commit", "-m", "sub init"], cwd=sub_repo_dir)

        # Add submodule into main repo
        self._run_git(["-c", "protocol.file.allow=always", "submodule", "add", str(sub_repo_dir.resolve().as_posix()), "3rdparty/sub"], cwd=self.repo_dir)
        self._run_git(["commit", "-m", "add submodule"], cwd=self.repo_dir)

        # Verify clean initially
        res_clean = self._invoke_cmake_script(self.repo_dir, self.output_json, source_file=self.binary_file)
        self.assertEqual(res_clean.returncode, 0)
        self.assertIs(json.loads(self.output_json.read_text(encoding="utf-8"))["dirty"], False)

        # Dirty submodule
        (self.repo_dir / "3rdparty/sub/sub_lib.cpp").write_text("// sub modified\n", encoding="utf-8")
        res_dirty = self._invoke_cmake_script(self.repo_dir, self.output_json, source_file=self.binary_file)
        self.assertEqual(res_dirty.returncode, 0)
        meta_dirty = json.loads(self.output_json.read_text(encoding="utf-8"))
        self.assertIs(meta_dirty["dirty"], True)

    def test_08_matrix_git_failure_unknown_status(self) -> None:
        """Matrix Case 8: Git query failure outputs unknown and does NOT retain stale commit (fixes F46-01)."""
        non_git_dir = self.root / "non_git_workspace"
        non_git_dir.mkdir()

        res_dev = self._invoke_cmake_script(
            non_git_dir,
            self.output_json,
            source_file=self.binary_file,
            release_build=False,
            commit="stale-configure-sha-40chars1234567890abcdef",
            branch="stale-branch",
        )
        self.assertEqual(res_dev.returncode, 0, res_dev.stderr)
        meta_dev = json.loads(self.output_json.read_text(encoding="utf-8"))
        self.assertEqual(meta_dev["commit"], "unknown")
        self.assertEqual(meta_dev["branch"], "unknown")
        self.assertEqual(meta_dev["dirty"], "unknown")

        # In release build, Git failure must terminate with fatal error
        res_rel = self._invoke_cmake_script(
            non_git_dir,
            self.output_json,
            source_file=self.binary_file,
            release_build=True,
            commit="stale-configure-sha",
        )
        self.assertNotEqual(res_rel.returncode, 0)
        self.assertIn("发布模式禁止未知候选", res_rel.stderr)

        # Release packaging gate must reject commit=unknown / dirty=unknown
        with self.assertRaisesRegex(RuntimeError, "commit is unknown"):
            package_app.verify_build_metadata(self.output_json, allow_dirty=False)

    def test_09_matrix_package_release_gating(self) -> None:
        """Matrix Case 9: Release packaging gates strictly enforce clean candidates."""
        install_dir = self.root / "fake_install"
        install_dir.mkdir()
        (install_dir / "VisionRT.exe").touch()
        (install_dir / package_app.MARKER_FILE).touch()
        (install_dir / "quickui.dll").touch()
        (install_dir / "python312.dll").touch()
        (install_dir / "python/Lib/site-packages/numpy").mkdir(parents=True)
        (install_dir / "python/DLLs").mkdir()

        # 1. Unknown commit -> rejected
        (install_dir / "build_info.json").write_text(
            '{"schema_version": 1, "component": "VisionRT", "version": "1.0", "commit": "unknown", "dirty": false, "built_at_epoch": 100}\n',
            encoding="utf-8",
        )
        (install_dir / "contracts-build-info.json").write_text(
            '{"schema_version": 1, "component": "VisionRTContracts", "version": "1.0", "commit": "abc", "dirty": false, "built_at_epoch": 100}\n',
            encoding="utf-8",
        )
        with self.assertRaisesRegex(RuntimeError, "commit is unknown"):
            package_app.verify_package(install_dir, require_qt=False, allow_dirty=False)

        # 2. Dirty candidate -> rejected
        (install_dir / "build_info.json").write_text(
            '{"schema_version": 1, "component": "VisionRT", "version": "1.0", "commit": "abc", "dirty": true, "built_at_epoch": 100}\n',
            encoding="utf-8",
        )
        with self.assertRaisesRegex(RuntimeError, "is dirty \\(True\\)"):
            package_app.verify_package(install_dir, require_qt=False, allow_dirty=False)

        # 3. Clean candidate -> passes
        (install_dir / "build_info.json").write_text(
            '{"schema_version": 1, "component": "VisionRT", "version": "1.0", "commit": "abc", "dirty": false, "built_at_epoch": 100}\n',
            encoding="utf-8",
        )
        package_app.verify_package(install_dir, require_qt=False, allow_dirty=False)


if __name__ == "__main__":
    unittest.main()
