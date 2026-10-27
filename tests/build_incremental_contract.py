from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name == "nt", "Windows MSVC build entry")
class BuildIncrementalContract(unittest.TestCase):
    def test_header_dependencies_survive_console_code_page_changes(self) -> None:
        cmake = os.environ.get("CMAKE_COMMAND") or shutil.which("cmake")
        ninja = os.environ.get("CMAKE_MAKE_PROGRAM") or shutil.which("ninja")
        self.assertIsNotNone(cmake, "CMake executable is required")
        self.assertIsNotNone(ninja, "Ninja executable is required")
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE

        with tempfile.TemporaryDirectory(prefix="visionrt-build-") as temporary:
            root = Path(temporary)
            source = root / "source"
            build = root / "build"
            source.mkdir()
            (source / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.25)\n"
                "project(Incremental LANGUAGES CXX)\n"
                f'include("{(REPO_ROOT / "cmake/ConfigCompiler.cmake").as_posix()}")\n'
                "add_executable(probe main.cpp other.cpp)\n", encoding="utf-8")
            header = source / "payload.h"
            header.write_text("struct Payload { char bytes[4]; };\n", encoding="utf-8")
            (source / "main.cpp").write_text(
                '#include "payload.h"\n#include <cstdio>\n'
                "int other();\n"
                'int main() { std::printf("%d\\n", int(sizeof(Payload)) + other()); }\n',
                encoding="utf-8")
            (source / "other.cpp").write_text(
                '#include "payload.h"\nint other() { return int(sizeof(Payload)); }\n',
                encoding="utf-8")

            for code_page, arguments in (
                (65001, [cmake, "-S", source, "-B", build, "-G", "Ninja",
                         f"-DCMAKE_MAKE_PROGRAM={ninja}"]),
                (936, [cmake, "--build", build]),
                (65001, [cmake, "--build", build]),
                (936, [cmake, "--build", build]),
            ):
                if code_page == 65001 and arguments[1] == "--build":
                    time.sleep(1.1)
                    header.write_text("struct Payload { char bytes[20]; };\n", encoding="utf-8")
                command = (f"chcp {code_page}>nul && call "
                           + subprocess.list2cmdline([str(REPO_ROOT / "tools/build.cmd"),
                                                     *map(str, arguments)]))
                result = subprocess.run(
                    [os.environ.get("COMSPEC", "cmd.exe"), "/d", "/s", "/c", command],
                    capture_output=True, startupinfo=startup,
                    creationflags=subprocess.CREATE_NEW_CONSOLE, timeout=90)
                self.assertEqual(result.returncode, 0,
                                 result.stdout.decode("utf-8", errors="replace")
                                 + result.stderr.decode("utf-8", errors="replace"))
                if arguments[1] != "--build":
                    continue
                output = subprocess.check_output([str(build / "probe.exe")], timeout=10)
                expected = b"8" if header.read_text(encoding="utf-8").find("[4]") >= 0 else b"40"
                self.assertEqual(output.strip(), expected, "Both translation units must rebuild")
                if code_page == 936 and expected == b"40":
                    self.assertIn(b"no work to do", result.stdout)

            dependencies = subprocess.check_output([ninja, "-C", str(build), "-t", "deps"], timeout=10)
            self.assertEqual(dependencies.count(b"payload.h"), 2,
                             dependencies.decode("utf-8", errors="replace"))


if __name__ == "__main__":
    unittest.main()
