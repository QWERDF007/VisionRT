from __future__ import annotations

import json
import os
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import package_app

from toolchain_support import find_git


class BuildMetadataIntegrationContract(unittest.TestCase):
    """Real CMake build, executable compilation and build metadata contract (U01 / F02).

    The nested configure and build reuse the toolchain the outer project already resolved: CMake
    hands its own command, compiler and make program to this test, and the MSVC environment comes
    from ``tools/build.cmd`` - the same entry the incremental contract uses. Nothing here probes
    Visual Studio installations or Windows SDK versions on its own.
    """

    @classmethod
    def setUpClass(cls) -> None:
        cls.cmake = os.environ.get("CMAKE_COMMAND") or shutil.which("cmake")
        if not cls.cmake:
            raise unittest.SkipTest("CMAKE_COMMAND is required to configure the nested project")
        cls.ninja = os.environ.get("CMAKE_MAKE_PROGRAM") or shutil.which("ninja")
        if not cls.ninja:
            raise unittest.SkipTest("CMAKE_MAKE_PROGRAM is required to build the nested project")
        cls.compiler = os.environ.get("CMAKE_CXX_COMPILER", "")
        cls.build_entry = Path(os.environ.get("VISIONRT_BUILD_ENTRY", package_app.REPO_ROOT / "tools/build.cmd"))
        if os.name == "nt" and not cls.build_entry.is_file():
            raise unittest.SkipTest(f"build entry not found: {cls.build_entry}")

    @classmethod
    def _native(cls, arguments: list[str], cwd: Path) -> subprocess.CompletedProcess:
        """Run a build command inside the initialized MSVC environment.

        ``tools/build.cmd`` is the single environment entry: in this project's Windows setup it
        initializes VC/SDK variables and then proxies the given command, so the nested project sees
        exactly the toolchain the outer build used instead of a second discovery chain.
        """
        if os.name != "nt":
            return subprocess.run(arguments, cwd=cwd, capture_output=True, encoding="utf-8", errors="replace")
        # A single command line is required here: cmd must receive the quoted batch path verbatim,
        # so the list form (which escapes embedded quotes) is not used.
        comspec = os.environ.get("COMSPEC", "cmd.exe")
        line = f'"{comspec}" /d /s /c call "{cls.build_entry}" ' + subprocess.list2cmdline(arguments)
        return subprocess.run(line, cwd=cwd, capture_output=True, encoding="utf-8", errors="replace")

    @property
    def executable_name(self) -> str:
        return "mini_app.exe" if os.name == "nt" else "mini_app"

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="vrt-real-cmake-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.repo_dir = self.root / "repo"
        self.repo_dir.mkdir()
        self.build_dir = self.repo_dir / "build"

        # Git init
        self._git(["init", "-b", "main"])
        self._git(["config", "user.name", "TestUser"])
        self._git(["config", "user.email", "test@example.com"])

        # Initial source and CMakeLists.txt
        (self.repo_dir / ".gitignore").write_text("/build/\n", encoding="utf-8")
        (self.repo_dir / "src").mkdir()
        (self.repo_dir / "src/main.cpp").write_text(
            '#include <iostream>\n'
            '#ifdef USE_EXTRA_HEADER\n'
            '#include "extra.h"\n'
            '#endif\n'
            'int main() {\n'
            '    std::cout << "real_mini_app_v1" << std::endl;\n'
            '    return 0;\n'
            '}\n',
            encoding="utf-8",
        )

        write_info_script = (package_app.REPO_ROOT / "cmake/WriteBuildInfo.cmake").as_posix()
        git_cmd = Path(find_git()).as_posix()

        (self.repo_dir / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.20)\n'
            'project(MiniApp LANGUAGES CXX)\n'
            'set(CMAKE_CXX_STANDARD 17)\n'
            'option(MINI_RELEASE_BUILD "Release build gating" OFF)\n'
            'add_executable(mini_app src/main.cpp)\n'
            'target_include_directories(mini_app PRIVATE src)\n'
            'add_custom_target(write_build_info ALL\n'
            '    COMMAND "${CMAKE_COMMAND}"\n'
            '        -DCOMPONENT=MiniApp\n'
            '        -DVERSION=1.0.0\n'
            '        -DREPO_DIR=${CMAKE_CURRENT_SOURCE_DIR}\n'
            '        -DSOURCE_FILE=$<TARGET_FILE:mini_app>\n'
            '        -DOUTPUT=$<TARGET_FILE_DIR:mini_app>/build_info.json\n'
            '        -DRELEASE_BUILD=${MINI_RELEASE_BUILD}\n'
            f'        -DGIT_EXECUTABLE={git_cmd}\n'
            f'        -DPYTHON_EXECUTABLE={Path(sys.executable).as_posix()}\n'
            f'        -P {write_info_script}\n'
            '    COMMENT "Writing build info for mini_app"\n'
            '    VERBATIM\n'
            ')\n'
            'add_dependencies(write_build_info mini_app)\n',
            encoding="utf-8",
        )

        self._git(["add", ".gitignore", "src/main.cpp", "CMakeLists.txt"])
        self._git(["commit", "-m", "initial clean project"])

    def _git(self, args: list[str]) -> subprocess.CompletedProcess:
        git_cmd = find_git()
        res = subprocess.run([git_cmd] + args, cwd=self.repo_dir, capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, f"git command failed: {' '.join(args)}\n{res.stderr}")
        return res

    def assertSucceeded(self, result: subprocess.CompletedProcess, phase: str) -> None:
        self.assertEqual(result.returncode, 0, f"{phase} failed\n{result.stdout}\n{result.stderr}")

    def _configure(self, release_build: bool = False) -> subprocess.CompletedProcess:
        cmd = [
            self.cmake,
            "-B", "build",
            "-S", ".",
            "-G", "Ninja",
            f"-DCMAKE_MAKE_PROGRAM={self.ninja}",
            f"-DMINI_RELEASE_BUILD={'ON' if release_build else 'OFF'}",
        ]
        if self.compiler:
            cmd.append(f"-DCMAKE_CXX_COMPILER={self.compiler}")
        return self._native(cmd, self.repo_dir)

    def _build(self) -> subprocess.CompletedProcess:
        return self._native([self.cmake, "--build", "build"], self.repo_dir)

    def test_initial_build_records_head_and_artifact_time(self) -> None:
        """S47-03 Case 1: Initial real compilation produces working binary and matching build metadata."""
        self.assertSucceeded(self._configure(release_build=True), "configure")
        self.assertSucceeded(self._build(), "build")

        exe = self.build_dir / self.executable_name
        self.assertTrue(exe.is_file(), "Compiled executable must exist")

        # Run real executable
        run_res = subprocess.run([str(exe)], capture_output=True, text=True)
        self.assertEqual(run_res.returncode, 0)
        self.assertIn("real_mini_app_v1", run_res.stdout)

        # Check build_info.json
        info_json = self.build_dir / "build_info.json"
        self.assertTrue(info_json.is_file())
        meta = json.loads(info_json.read_text(encoding="utf-8"))

        head = subprocess.check_output([find_git(), "rev-parse", "HEAD"], cwd=self.repo_dir, text=True).strip()
        self.assertEqual(meta["commit"], head)
        self.assertEqual(meta["branch"], "main")
        self.assertIs(meta["dirty"], False)
        # Artifact mtime epoch matches
        exe_epoch = int(exe.stat().st_mtime)
        self.assertEqual(meta["built_at_epoch"], exe_epoch)

    def test_rebuild_reuses_binary_and_timestamp(self) -> None:
        """S47-03 Case 2: Rebuilding without changes does not relink binary and preserves epoch."""
        self.assertSucceeded(self._configure(), "configure")
        self.assertSucceeded(self._build(), "build")

        info_json = self.build_dir / "build_info.json"
        meta1 = json.loads(info_json.read_text(encoding="utf-8"))
        exe1_mtime = (self.build_dir / self.executable_name).stat().st_mtime

        # Second build without changes: binary is not relinked
        res2 = self._build()
        self.assertEqual(res2.returncode, 0)

        exe2_mtime = (self.build_dir / self.executable_name).stat().st_mtime
        self.assertEqual(exe1_mtime, exe2_mtime)

        meta2 = json.loads(info_json.read_text(encoding="utf-8"))
        self.assertEqual(meta1["built_at_epoch"], meta2["built_at_epoch"])
        self.assertIs(meta2["dirty"], False)

    def test_document_commit_updates_hash_without_relink(self) -> None:
        """S47-03 Case 3: Review doc commit updates candidate commit hash while binary is reused."""
        self.assertSucceeded(self._configure(), "configure")
        self.assertSucceeded(self._build(), "build")
        exe1_mtime = (self.build_dir / self.executable_name).stat().st_mtime

        docs_dir = self.repo_dir / "docs"
        docs_dir.mkdir()
        (docs_dir / "R47_report.md").write_text("# Report\n", encoding="utf-8")
        self._git(["add", "docs/R47_report.md"])
        self._git(["commit", "-m", "docs: update review doc"])
        new_head = subprocess.check_output([find_git(), "rev-parse", "HEAD"], cwd=self.repo_dir, text=True).strip()

        # Build again: binary is untouched, build_info reflects new commit
        self.assertSucceeded(self._build(), "build")
        exe2_mtime = (self.build_dir / self.executable_name).stat().st_mtime
        self.assertEqual(exe1_mtime, exe2_mtime)

        meta = json.loads((self.build_dir / "build_info.json").read_text(encoding="utf-8"))
        self.assertEqual(meta["commit"], new_head)
        self.assertIs(meta["dirty"], False)

    def test_tracked_source_change_marks_dirty(self) -> None:
        """S47-03 Case 4: Production source modification causes recompilation, dirty=true, and release failure."""
        self.assertSucceeded(self._configure(release_build=False), "configure")
        self.assertSucceeded(self._build(), "build")

        # Modify source
        (self.repo_dir / "src/main.cpp").write_text(
            '#include <iostream>\n'
            'int main() {\n'
            '    std::cout << "real_mini_app_v2_modified" << std::endl;\n'
            '    return 0;\n'
            '}\n',
            encoding="utf-8",
        )

        # Normal build succeeds with dirty=true
        res_bld = self._build()
        self.assertEqual(res_bld.returncode, 0)
        meta = json.loads((self.build_dir / "build_info.json").read_text(encoding="utf-8"))
        self.assertIs(meta["dirty"], True)

        # Re-run binary to verify actual compilation happened
        run_res = subprocess.run([str(self.build_dir / self.executable_name)], capture_output=True, text=True)
        self.assertIn("real_mini_app_v2_modified", run_res.stdout)

        # Re-configure for release build: build must fail due to dirty workspace
        self.assertSucceeded(self._configure(release_build=True), "configure")
        res_rel = self._build()
        self.assertNotEqual(res_rel.returncode, 0)
        self.assertIn("发布模式禁止脏工作区候选", res_rel.stdout + res_rel.stderr)

    def test_untracked_included_header_marks_dirty(self) -> None:
        """S47-03 Case 5: Untracked header included in compilation causes dirty=true and release failure."""
        self.assertSucceeded(self._configure(release_build=True), "configure")

        # Create untracked header and include it
        (self.repo_dir / "src/extra.h").write_text("#pragma once\n", encoding="utf-8")
        (self.repo_dir / "src/main.cpp").write_text('#include "extra.h"\nint main() { return 0; }\n', encoding="utf-8")

        res_bld = self._build()
        self.assertNotEqual(res_bld.returncode, 0)
        self.assertIn("发布模式禁止脏工作区候选", res_bld.stdout + res_bld.stderr)

    def test_missing_git_marks_candidate_unknown(self) -> None:
        """S47-03 Case 6: Git query failure sets commit/branch/dirty to unknown and fails release build."""
        self.assertSucceeded(self._configure(release_build=False), "configure")

        # Force a rebuild by touching source, and hide .git
        (self.repo_dir / "src/main.cpp").write_text("// no git\nint main() { return 0; }\n", encoding="utf-8")
        (self.repo_dir / ".git").rename(self.repo_dir / ".git_hidden")

        res_bld = self._build()
        self.assertEqual(res_bld.returncode, 0)
        meta = json.loads((self.build_dir / "build_info.json").read_text(encoding="utf-8"))
        self.assertEqual(meta["commit"], "unknown")
        self.assertEqual(meta["branch"], "unknown")
        self.assertEqual(meta["dirty"], "unknown")

        # In release mode, it must fail
        self.assertSucceeded(self._configure(release_build=True), "configure")
        (self.repo_dir / "src/main.cpp").write_text("// release no git\nint main() { return 1; }\n", encoding="utf-8")
        res_rel = self._build()
        self.assertNotEqual(res_rel.returncode, 0)
        self.assertIn("发布模式禁止未知候选", res_rel.stdout + res_rel.stderr)



if __name__ == "__main__":
    unittest.main()
