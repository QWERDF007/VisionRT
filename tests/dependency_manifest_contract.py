from __future__ import annotations

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "tools"))

from dependency_utils import dependency_default_value, load_dependencies


class DependencyManifestContract(unittest.TestCase):
    def test_json_only_and_invalid_entries_are_not_silently_dropped(self) -> None:
        with tempfile.TemporaryDirectory(prefix="visionrt-manifest-") as temporary:
            manifest = Path(temporary) / "dependencies.json"
            for content in ('dependencies: []', '{"dependencies": [null]}', '{"dependencies": [], "dependencies": []}'):
                with self.subTest(content=content):
                    manifest.write_text(content, encoding="utf-8")
                    with self.assertRaises(ValueError):
                        load_dependencies(manifest)

    def test_cmake_and_python_read_the_same_json_defaults(self) -> None:
        with tempfile.TemporaryDirectory(prefix="visionrt-manifest-") as temporary:
            root = Path(temporary)
            manifest = root / "dependencies.json"
            manifest.write_text(json.dumps({"dependencies": [{
                "name": "probe", "default": "default path/默认", "windows_default": "windows path/工具",
            }]}), encoding="utf-8")
            dependency = load_dependencies(manifest)[0]
            self.assertEqual(dependency_default_value(dependency, "windows"), "windows path/工具")
            self.assertEqual(dependency_default_value(dependency, "linux"), "default path/默认")
            script = root / "verify.cmake"
            script.write_text(f'''
include("{(REPO_ROOT / 'cmake/ConfigDependencyDefaults.cmake').as_posix()}")
set(VISIONRT_DEPENDENCY_MANIFEST "{manifest.as_posix()}")
set(WIN32 TRUE)
visionrt_dependency_default(probe windows)
if(NOT windows STREQUAL "windows path/工具")
    message(FATAL_ERROR "Incorrect Windows default: ${{windows}}")
endif()
set(WIN32 FALSE)
set(APPLE FALSE)
set(UNIX TRUE)
visionrt_dependency_default(probe linux)
if(NOT linux STREQUAL "default path/默认")
    message(FATAL_ERROR "Incorrect default: ${{linux}}")
endif()
''', encoding="utf-8")
            cmake = os.environ.get("CMAKE_COMMAND") or shutil.which("cmake")
            self.assertIsNotNone(cmake, "CMake executable is required")
            result = subprocess.run([cmake, "-P", str(script)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
