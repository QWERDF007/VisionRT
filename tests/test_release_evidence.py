"""The release record is only worth signing if a contradiction stops it.

These cases exercise the audit rules directly: no build, package or git
repository is needed, which is exactly why the rules are separable.
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))

import release_evidence


def ready_report(**overrides):
    report = {
        "role": "ready",
        "path": "F:/tmp/ready.json",
        "phase": "ready",
        "selected": "D:/python/python.exe",
        "prefix": "D:/python",
        "basePrefix": "D:/python",
        "numpyVersion": "2.4.3",
        "numpyFile": "D:/python/Lib/site-packages/numpy/__init__.py",
        "runtime": "D:/python/python312.dll",
        "bridge": True,
        "modules": ["python312.dll", "visionrt_python_bridge.dll"],
        "reason": "",
    }
    report.update(overrides)
    return report


def unconfigured_report(**overrides):
    report = {
        "role": "unconfigured",
        "path": "F:/tmp/unconfigured.json",
        "phase": "unconfigured",
        "selected": "",
        "runtime": "",
        "bridge": False,
        "modules": [],
        "reason": "未配置 Python 环境",
    }
    report.update(overrides)
    return report


def record(**overrides):
    value = {
        "repositories": {
            "visionrt": {"path": "F:/Projects/VisionRT", "head": "a" * 40, "dirty": False,
                         "contracts": "c" * 40},
            "visiontools": {"path": "F:/Projects/VisionTools", "head": "b" * 40, "dirty": False,
                            "contracts": "c" * 40},
        },
        "build": {"configured": True, "compiler": "cl.exe", "generator": "Ninja"},
        "package": {
            "version": "0.1.0",
            "architecture": "x64",
            "fileCount": 573,
            "bundledPythonRuntime": [],
            "startupPythonImports": [],
            "startupChain": ["VisionRT.exe", "Qt6Core.dll"],
            "bridgePresent": True,
        },
        "pythonReports": [unconfigured_report(), ready_report()],
        "notes": [],
    }
    value.update(overrides)
    return value


class ReleaseEvidenceAudit(unittest.TestCase):
    def test_a_consistent_record_passes(self):
        self.assertEqual(release_evidence.audit(record(), allow_dirty=False, require_clean=True), [])

    def test_the_two_repositories_must_pin_the_same_contracts(self):
        value = record()
        value["repositories"]["visiontools"]["contracts"] = "d" * 40
        problems = release_evidence.audit(value, allow_dirty=False, require_clean=True)
        self.assertTrue(any("Contracts" in problem for problem in problems), problems)

    def test_an_unconfigured_process_must_not_have_loaded_python(self):
        value = record()
        value["pythonReports"] = [
            unconfigured_report(phase="ready", runtime="D:/python/python312.dll",
                                modules=["python312.dll"]),
            ready_report(),
        ]
        problems = release_evidence.audit(value, allow_dirty=False, require_clean=True)
        self.assertTrue(any("unconfigured" in problem for problem in problems), problems)
        self.assertTrue(any("加载了 Python 模块" in problem for problem in problems), problems)

    def test_a_ready_report_must_name_a_runtime_and_numpy_inside_the_environment(self):
        value = record()
        value["pythonReports"] = [
            unconfigured_report(),
            ready_report(runtime="C:/other/python312.dll",
                         numpyFile="C:/other/site-packages/numpy/__init__.py"),
        ]
        problems = release_evidence.audit(value, allow_dirty=False, require_clean=True)
        self.assertTrue(any("运行时不在所选环境内" in problem for problem in problems), problems)
        self.assertTrue(any("NumPy 不在所选环境内" in problem for problem in problems), problems)

    def test_a_venv_runtime_in_the_base_environment_is_accepted(self):
        # A venv shares its interpreter and NumPy with the base installation; the
        # identity check has to accept both, not just the venv directory.
        value = record()
        value["pythonReports"] = [
            unconfigured_report(),
            ready_report(prefix="F:/tmp/venv-sys", basePrefix="D:/python",
                         runtime="D:/python/python312.dll",
                         numpyFile="D:/python/Lib/site-packages/numpy/__init__.py"),
        ]
        self.assertEqual(release_evidence.audit(value, allow_dirty=False, require_clean=True), [])

    def test_a_negative_scenario_must_state_its_reason(self):
        value = record()
        value["pythonReports"] = [
            unconfigured_report(),
            ready_report(),
            {"role": "invalid", "path": "F:/tmp/invalid.json", "phase": "invalid", "reason": ""},
        ]
        problems = release_evidence.audit(value, allow_dirty=False, require_clean=True)
        self.assertTrue(any("没有给出原因" in problem for problem in problems), problems)

    def test_a_negative_role_must_match_the_phase_it_claims(self):
        value = record()
        value["pythonReports"] = [
            unconfigured_report(),
            ready_report(),
            {"role": "failed", "path": "F:/tmp/failed.json", "phase": "unconfigured", "reason": "x"},
        ]
        problems = release_evidence.audit(value, allow_dirty=False, require_clean=True)
        self.assertTrue(any("failed 场景实际为 unconfigured" in problem for problem in problems), problems)

    def test_a_package_that_ships_python_cannot_be_released(self):
        value = record()
        value["package"]["bundledPythonRuntime"] = ["tools-package/vendor/python.exe"]
        problems = release_evidence.audit(value, allow_dirty=False, require_clean=True)
        self.assertTrue(any("携带 Python 运行时" in problem for problem in problems), problems)

    def test_a_startup_chain_that_imports_python_cannot_be_released(self):
        value = record()
        value["package"]["startupPythonImports"] = ["VisionRT.exe -> python312.dll"]
        problems = release_evidence.audit(value, allow_dirty=False, require_clean=True)
        self.assertTrue(any("启动链隐式导入 Python" in problem for problem in problems), problems)

    def test_dirty_sources_need_an_explicit_local_only_flag(self):
        value = record()
        value["repositories"]["visionrt"]["dirty"] = True
        self.assertTrue(release_evidence.audit(value, allow_dirty=False, require_clean=True))
        self.assertEqual(release_evidence.audit(value, allow_dirty=True, require_clean=False), [])
        self.assertTrue(value["notes"], "a local-only record must say so")


class ReleaseEvidenceParsing(unittest.TestCase):
    def test_report_arguments_are_role_and_path(self):
        self.assertEqual(release_evidence.parse_report("ready=F:/tmp/a.json"),
                         ("ready", Path("F:/tmp/a.json")))
        with self.assertRaises(Exception):
            release_evidence.parse_report("F:/tmp/a.json")
        with self.assertRaises(Exception):
            release_evidence.parse_report("other=F:/tmp/a.json")

    def test_report_summary_keeps_what_the_audit_reads(self):
        import json
        import tempfile

        payload = {
            "application": "F:/Projects/VisionRT/build/bin",
            "environment": {
                "phase": "ready", "selected": "D:/python/python.exe", "executable": "D:/python/python.exe",
                "version": "3.12.4", "prefix": "D:/python", "basePrefix": "D:/python",
                "numpyVersion": "2.4.3", "numpyFile": "D:/python/Lib/site-packages/numpy/__init__.py",
                "reason": "",
            },
            "process": {
                "bridge": True, "runtime": "D:/python/python312.dll",
                "modules": [{"name": "python312.dll", "path": "D:/python/python312.dll"}],
            },
        }
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "report.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            summary = release_evidence.summarize_report("ready", path)
        self.assertEqual(summary["phase"], "ready")
        self.assertEqual(summary["numpyFile"], payload["environment"]["numpyFile"])
        self.assertEqual(summary["modules"], ["python312.dll"])


if __name__ == "__main__":
    unittest.main()
