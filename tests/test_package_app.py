from __future__ import annotations

import copy
import json
import os
from pathlib import Path
import sys
import subprocess
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
            "schema_version": 2, "tool_id": "test.plugin", "display_name": "部署测试",
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
        }
        for name, invalidate in cases.items():
            with self.subTest(name=name):
                self.index = copy.deepcopy(valid_index)
                self.definition["tool_id"] = "test.plugin"
                invalidate()
                self.write_source()
                with self.assertRaisesRegex(RuntimeError, "Host tools package validation failed"):
                    package_app.deploy_tools_package(self.install, self.archive(), self.validator)
                self.assertEqual((self.package / "old.txt").read_text(encoding="utf-8"), "old installation")

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
            (root / "python/Lib/site-packages/numpy").mkdir(parents=True)
            (root / "python/DLLs").mkdir()
            package_app.verify_package(root, require_qt=False)
            for name in ("quickui.dll", "python312.dll"):
                with self.subTest(name=name):
                    (root / name).unlink()
                    with self.assertRaises(RuntimeError):
                        package_app.verify_package(root, require_qt=False)
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


if __name__ == "__main__":
    unittest.main()
