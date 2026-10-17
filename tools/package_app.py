"""VisionRT release packaging script.

Packages release runtime into a standalone directory:
Collects executable, project DLLs, QML modules, Python runtime,
third-party dependencies from dependencies.yaml, system runtimes, and Qt plugins.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

if str(Path(__file__).resolve().parent) not in sys.path:
    sys.path.insert(0, str(Path(__file__).resolve().parent))

from dependency_utils import (
    BUILD_CONFIG_DIRECTORIES,
    MARKER_FILE,
    PROJECT_DLL_PREFIX,
    PROJECT_NAME,
    PROJECT_NAME_LONG,
    REPO_ROOT,
    build_dll_variant_sets,
    build_config_directory,
    copy_file,
    dependency_matches_config,
    dependency_patterns,
    dll_matches_config,
    expand_dependency_pattern,
    load_dependencies,
    platform_key,
    read_cmake_cache_value,
    read_cmake_set_expanded,
    resolve_dependency_root,
    resolve_project_path,
    warn,
)

LOCAL_BIN_DLL_NAMES = {"quickui.dll"}


def parse_args() -> argparse.Namespace:
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(description="Package VisionRT release runtime.")
    parser.add_argument("--build-dir", "-BuildDir", default="build", help="CMake build directory.")
    parser.add_argument("--install-dir", "-InstallDir", default=None, help="Installation output directory.")
    parser.add_argument("--config", "-Config", type=str.lower, choices=["release", "debug"], default="release", help="Build configuration.")
    parser.add_argument("--dependencies", default="tools/dependencies.yaml", help="Path to dependencies.yaml.")
    parser.add_argument("--windeployqt", "-WinDeployQt", default="", help="Explicit path to windeployqt.exe.")
    parser.add_argument("--qt-root", "-QtRoot", default="", help="Explicit Qt root directory.")
    parser.add_argument("--skip-windeployqt", "-SkipWinDeployQt", "--skip-qt", action="store_true", help="Skip windeployqt deployment.")
    parser.add_argument("--skip-dependencies", action="store_true", help="Skip dependencies from manifest.")
    parser.add_argument("--allow-missing-dependencies", action="store_true", help="Allow missing manifest dependencies.")
    parser.add_argument("--include-pdb", action="store_true", help="Include PDB files.")
    parser.add_argument("--no-clean", action="store_true", help="Do not clean output directory.")
    parser.add_argument("--force-clean", action="store_true", help="Clean output directory even without marker.")
    parser.add_argument("--skip-system-libs", action="store_true", help="Skip MSVC and Windows SDK runtime DLLs.")
    parser.add_argument("--build", action="store_true", help="Build the configured CMake tree before packaging.")
    parser.add_argument("--parallel", type=int, default=4, help="Build parallelism (default: 4).")
    parser.add_argument("--skip-verify", action="store_true", help="Skip post-package verification.")
    parser.add_argument("--tools-dir", default=None, help="External VisionTools package directory to merge into install_dir.")
    parser.add_argument("--tools-manifest", default=None, help="Manifest list of relative files from VisionTools package.")
    parser.add_argument("--test-verify", action="store_true", help="Run automated negative tests for package verification.")
    return parser.parse_args()


def project_version(repo_root: Path = REPO_ROOT) -> str:
    """Extract project VERSION from root CMakeLists.txt."""
    cmake_lists = repo_root / "CMakeLists.txt"
    text = cmake_lists.read_text(encoding="utf-8", errors="ignore")
    match = re.search(r"project\s*\(\s*VisionRT\s+VERSION\s+(\d+\.\d+\.\d+)", text, re.IGNORECASE)
    if not match:
        match = re.search(r"VERSION\s+(\d+\.\d+\.\d+)", text)
    if not match:
        raise RuntimeError(f"cannot find project VERSION in {cmake_lists}")
    return match.group(1)


def default_install_dir() -> Path:
    """Default output directory: install/VisionRT-<version>."""
    return REPO_ROOT / "install" / f"{PROJECT_NAME_LONG}-{project_version()}"


def build_release(build_dir: Path, config: str, parallel: int) -> None:
    """Build configured CMake tree."""
    if parallel < 1:
        raise ValueError("--parallel must be a positive integer")
    cache_file = build_dir / "CMakeCache.txt"
    if not cache_file.is_file():
        raise RuntimeError(f"CMake build tree is not configured: {build_dir}")
    cmake = shutil.which("cmake")
    if cmake is None:
        raise RuntimeError("cmake was not found on PATH")

    command = [cmake, "--build", str(build_dir), "--config", build_config_directory(config), "--parallel", str(parallel)]
    print("run " + " ".join(command))
    subprocess.run(command, check=True)


def find_executable(build_dir: Path) -> Path:
    """Find VisionRT executable under build directory."""
    exe_name = f"{PROJECT_NAME}.exe" if os.name == "nt" else PROJECT_NAME
    candidates = [
        build_dir / "bin" / exe_name,
        build_dir / "bin" / "Release" / exe_name,
        build_dir / "Release" / "bin" / exe_name,
        build_dir / exe_name,
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve(strict=True)
    matches = sorted(build_dir.rglob(exe_name), key=lambda path: len(str(path)))
    if matches:
        return matches[0].resolve(strict=True)
    raise RuntimeError(f"cannot find {exe_name} under {build_dir}")


def target_architecture(build_dir: Path) -> str:
    """Determine Windows target architecture."""
    platform = read_cmake_cache_value(build_dir / "CMakeCache.txt", "CMAKE_GENERATOR_PLATFORM")
    if platform:
        val = platform.lower()
        if val in {"win32", "x86"}:
            return "x86"
        if val in {"x64", "arm64"}:
            return val
    machine = os.environ.get("PROCESSOR_ARCHITECTURE", "").upper()
    if machine == "ARM64":
        return "arm64"
    if machine == "AMD64":
        return "x64"
    return "x86"


def clear_install_directory(path: Path, clean: bool, force: bool) -> Path | None:
    """Prepare install directory safely according to marker rules, preserving custom tools."""
    if not clean:
        path.mkdir(parents=True, exist_ok=True)
        return None
    marker = path / MARKER_FILE
    if path.is_dir() and any(path.iterdir()) and not marker.exists() and not force:
        raise RuntimeError(
            f"install dir exists and has no {MARKER_FILE}: {path}; "
            "pass --force-clean to remove it or --no-clean to reuse it"
        )
    custom_backup = None
    target_custom = path / "tools" / "custom"
    if target_custom.is_dir() and any(target_custom.iterdir()):
        import tempfile
        custom_backup = Path(tempfile.mkdtemp(prefix="vrt_custom_backup_"))
        shutil.copytree(target_custom, custom_backup / "custom", dirs_exist_ok=True)

    if path.exists():
        for child in path.iterdir():
            if child.is_dir() and not child.is_symlink():
                shutil.rmtree(child)
            else:
                child.unlink()
    else:
        path.mkdir(parents=True)
    return custom_backup


def write_package_marker(
    install_dir: Path,
    build_dir: Path,
    version: str,
    extra_lines: tuple[str, ...] = (),
) -> None:
    """Write package marker file."""
    lines = [
        "generated_by=tools/package_app.py",
        f"build_dir={build_dir}",
        "config=release",
        f"version={version}",
        *extra_lines,
    ]
    (install_dir / MARKER_FILE).write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_windows_marker(
    install_dir: Path,
    build_dir: Path,
    architecture: str,
    version: str,
    skip_dependencies: bool,
) -> None:
    """Write Windows release package marker."""
    write_package_marker(
        install_dir,
        build_dir,
        version,
        (
            f"architecture={architecture}",
            f"skip_dependencies={int(skip_dependencies)}",
        ),
    )


def copy_project_runtime(build_dir: Path, install_dir: Path, include_pdb: bool) -> None:
    """Copy project DLLs, plugins, and QML resources."""
    bin_dir = build_dir / "bin"
    if not bin_dir.is_dir():
        bin_dir = build_dir

    # 1. quickui.dll
    quickui_dll = None
    for candidate in (
        bin_dir / "quickui.dll",
        REPO_ROOT / "3rdparty" / "QuickUI" / "quickui.dll",
        build_dir / "quickui.dll",
    ):
        if candidate.is_file():
            quickui_dll = candidate
            break
    if quickui_dll:
        copy_file(quickui_dll, install_dir / "quickui.dll")

    # 3. QuickUI QML module
    quickui_qml_source = None
    for candidate in (
        build_dir / "qml" / "quickui",
        REPO_ROOT / "3rdparty" / "QuickUI" / "quickui",
    ):
        if candidate.is_dir() and (candidate / "qmldir").is_file():
            quickui_qml_source = candidate
            break

    if quickui_qml_source:
        qml_target = install_dir / "qml" / "quickui"
        shutil.copytree(quickui_qml_source, qml_target, dirs_exist_ok=True)
        print(f"copy {quickui_qml_source} -> {qml_target}")
        if quickui_dll:
            copy_file(quickui_dll, qml_target / "quickui.dll")

    # 4. PDB debug symbols if requested
    if include_pdb and bin_dir.is_dir():
        for pdb in bin_dir.glob("*.pdb"):
            copy_file(pdb, install_dir / pdb.name)


def copy_tools(install_dir: Path, custom_backup: Path | None = None) -> None:
    """Copy assets/tools into install_dir/tools, preserving custom tools."""
    source = REPO_ROOT / "assets" / "tools"
    if not source.is_dir():
        raise RuntimeError(f"tools directory was not found: {source}")
    target = install_dir / "tools"
    shutil.copytree(source, target, dirs_exist_ok=True, ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
    if custom_backup and (custom_backup / "custom").is_dir():
        target_custom = target / "custom"
        shutil.copytree(custom_backup / "custom", target_custom, dirs_exist_ok=True)
        shutil.rmtree(custom_backup, ignore_errors=True)
        print("restored preserved custom tools")
    print(f"copy {source} -> {target}")


def merge_tools_package(install_dir: Path, tools_dir: Path, tools_manifest: Path | None = None) -> None:
    """Merge external tools package tree and verify against manifest if provided."""
    if not tools_dir.is_dir():
        raise RuntimeError(f"tools package directory was not found: {tools_dir}")
    print(f"merging tools package from {tools_dir} into {install_dir}")

    # Copy files while protecting existing user custom directory
    for item in tools_dir.iterdir():
        if item.name == "tools":
            target_tools = install_dir / "tools"
            builtin_src = item / "builtin"
            if builtin_src.is_dir():
                shutil.copytree(builtin_src, target_tools / "builtin", dirs_exist_ok=True, ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
            custom_src = item / "custom"
            if custom_src.is_dir():
                target_custom = target_tools / "custom"
                target_custom.mkdir(parents=True, exist_ok=True)
                for root_dir, _, files in os.walk(custom_src):
                    for file_name in files:
                        if file_name.endswith((".pyc", ".pyo")) or file_name == "__pycache__":
                            continue
                        src_file = Path(root_dir) / file_name
                        rel_path = src_file.relative_to(custom_src)
                        dest_file = target_custom / rel_path
                        if not dest_file.exists():
                            dest_file.parent.mkdir(parents=True, exist_ok=True)
                            copy_file(src_file, dest_file)
            for root_f in item.glob("*"):
                if root_f.is_file():
                    copy_file(root_f, target_tools / root_f.name)
        elif item.name == "assets":
            target_assets = install_dir / "assets"
            shutil.copytree(item, target_assets, dirs_exist_ok=True, ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
            if (item / "tools" / "builtin").is_dir():
                target_builtin = install_dir / "tools" / "builtin"
                shutil.copytree(item / "tools" / "builtin", target_builtin, dirs_exist_ok=True, ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
        elif item.is_dir():
            shutil.copytree(item, install_dir / item.name, dirs_exist_ok=True)
        else:
            copy_file(item, install_dir / item.name)

    if tools_manifest:
        if not tools_manifest.is_file():
            raise RuntimeError(f"tools manifest file was not found: {tools_manifest}")
        manifest_lines = [line.strip() for line in tools_manifest.read_text(encoding="utf-8").splitlines() if line.strip() and not line.startswith("#")]
        missing = [rel for rel in manifest_lines if not (install_dir / rel).exists()]
        if missing:
            raise RuntimeError(f"tools package manifest check failed: missing {len(missing)} files: {', '.join(missing[:5])}")
        print(f"tools package manifest verified: {len(manifest_lines)} entries matched")


def resolve_python_home(build_dir: Path, repo_root: Path = REPO_ROOT) -> Path:
    """Resolve Python home directory."""
    env_home = os.environ.get("VISIONRT_PYTHON_HOME") or os.environ.get("PYTHONHOME")
    if env_home:
        cand = Path(env_home).resolve()
        if cand.is_dir():
            return cand

    cache_val = read_cmake_cache_value(build_dir / "CMakeCache.txt", "VISIONRT_PYTHON_HOME")
    if cache_val:
        cand = Path(cache_val).resolve()
        if cand.is_dir():
            return cand

    cmake_val = read_cmake_set_expanded(repo_root / "cmake" / "ConfigPython.cmake", "VISIONRT_PYTHON_HOME")
    if cmake_val:
        cand = Path(cmake_val).resolve()
        if cand.is_dir():
            return cand

    fallback = Path("D:/Software/anaconda3/envs/py312")
    if fallback.is_dir():
        return fallback

    raise RuntimeError("Python home directory could not be resolved.")


def copy_python_runtime(build_dir: Path, install_dir: Path) -> None:
    """Deploy embedded Python 3.12 environment."""
    python_home = resolve_python_home(build_dir)
    print(f"deploying python runtime from {python_home}")

    # Copy python3*.dll and zlib*.dll to install root
    for dll in python_home.glob("python3*.dll"):
        copy_file(dll, install_dir / dll.name)
    for dll in python_home.glob("zlib*.dll"):
        copy_file(dll, install_dir / dll.name)

    # Copy Lib (excluding site-packages, test, __pycache__)
    python_lib = python_home / "Lib"
    if python_lib.is_dir():
        target_lib = install_dir / "python" / "Lib"
        shutil.copytree(
            python_lib,
            target_lib,
            dirs_exist_ok=True,
            ignore=shutil.ignore_patterns("site-packages", "__pycache__", "test", "tests"),
        )
        print(f"copy {python_lib} -> {target_lib}")

    # Copy DLLs (*.pyd, *.dll)
    python_dlls = python_home / "DLLs"
    if python_dlls.is_dir():
        target_dlls = install_dir / "python" / "DLLs"
        target_dlls.mkdir(parents=True, exist_ok=True)
        for ext in ("*.pyd", "*.dll"):
            for f in python_dlls.glob(ext):
                copy_file(f, target_dlls / f.name)

    # Copy numpy and numpy.libs
    site_packages = python_lib / "site-packages"
    if site_packages.is_dir():
        target_site = install_dir / "python" / "Lib" / "site-packages"
        target_site.mkdir(parents=True, exist_ok=True)
        numpy_dir = site_packages / "numpy"
        if numpy_dir.is_dir():
            shutil.copytree(
                numpy_dir,
                target_site / "numpy",
                dirs_exist_ok=True,
                ignore=shutil.ignore_patterns("__pycache__", "tests", "*.lib"),
            )
            print(f"copy {numpy_dir} -> {target_site / 'numpy'}")
        for dist_info in site_packages.glob("numpy-*.dist-info"):
            shutil.copytree(dist_info, target_site / dist_info.name, dirs_exist_ok=True)
        numpy_libs = site_packages / "numpy.libs"
        if numpy_libs.is_dir():
            shutil.copytree(numpy_libs, target_site / "numpy.libs", dirs_exist_ok=True)


def copy_yaml_dependencies(
    build_dir: Path,
    install_dir: Path,
    dependency_file: Path,
    config: str,
    allow_missing: bool,
) -> None:
    """Copy external dependencies declared in dependencies.yaml."""
    platform = platform_key()
    target_dir = install_dir if platform == "windows" else install_dir / "lib"
    target_dir.mkdir(parents=True, exist_ok=True)
    missing_dependencies: list[str] = []
    copied_runtimes: dict[str, Path] = {}

    for dep in load_dependencies(dependency_file):
        dep_name = str(dep.get("name", "<unnamed>")).lower()
        # qt and python are handled specially by windeployqt and copy_python_runtime
        if dep_name in {"qt", "qt6"}:
            continue
        if not dependency_matches_config(dep, config):
            continue
        patterns = dependency_patterns(dep, platform)
        if not patterns:
            continue
        root = resolve_dependency_root(dep, build_dir)
        if root is None or not root.is_dir():
            missing_dependencies.append(f"{dep_name} (root: {root or dep.get('root')})")
            continue
        matched: list[Path] = []
        for pattern in patterns:
            matches = expand_dependency_pattern(root, pattern, config)
            matched.extend(matches)
        matched = list(dict.fromkeys(matched))
        if not matched:
            missing_dependencies.append(f"{dep_name} (root: {root})")
            continue
        debug_names, release_names = build_dll_variant_sets(matched)
        for runtime in matched:
            if platform == "windows" and not dll_matches_config(runtime, config, debug_names, release_names):
                continue
            source = runtime.resolve(strict=True)
            destination_key = runtime.name.lower()
            prev_source = copied_runtimes.get(destination_key)
            if prev_source is not None:
                if prev_source != source:
                    warn(f"runtime dependency collision for {runtime.name}: {prev_source} and {source}")
                continue
            copy_file(source, target_dir / runtime.name)
            copied_runtimes[destination_key] = source

    if missing_dependencies:
        message = "missing runtime dependencies: " + "; ".join(missing_dependencies)
        if not allow_missing:
            raise RuntimeError(message)
        warn(message)


def visual_studio_roots() -> list[Path]:
    """Locate Visual Studio installation roots."""
    roots: list[Path] = []
    vcinstall = os.environ.get("VCINSTALLDIR")
    if vcinstall:
        roots.append(Path(vcinstall).resolve(strict=False).parent)
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if vswhere.is_file():
        result = subprocess.run(
            [str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )
        if result.stdout.strip():
            roots.append(Path(result.stdout.strip()))
    for year in ("2022", "2019"):
        for edition in ("Community", "Professional", "Enterprise", "BuildTools"):
            candidate = Path(r"C:\Program Files (x86)\Microsoft Visual Studio") / year / edition
            if candidate.is_dir():
                roots.append(candidate)
            candidate = Path(r"C:\Program Files\Microsoft Visual Studio") / year / edition
            if candidate.is_dir():
                roots.append(candidate)
    unique: list[Path] = []
    seen: set[str] = set()
    for root in roots:
        key = str(root).lower()
        if key not in seen:
            unique.append(root)
            seen.add(key)
    return unique


def copy_msvc_runtime(install_dir: Path, architecture: str) -> None:
    """Copy MSVC redistributable runtime DLLs."""
    for root in visual_studio_roots():
        redist = root / "VC" / "Redist" / "MSVC"
        if not redist.is_dir():
            continue
        versions = sorted((p for p in redist.iterdir() if p.is_dir()), reverse=True)
        for version in versions:
            runtime_dir = version / architecture
            if not runtime_dir.is_dir():
                continue
            copied = False
            for dll in sorted(runtime_dir.rglob("*.dll")):
                copy_file(dll, install_dir / dll.name)
                copied = True
            if copied:
                return
    warn("MSVC runtime DLLs were not found in standard paths")


def copy_windows_sdk_dependencies(install_dir: Path, architecture: str) -> None:
    """Copy DirectX shader compilation runtime from Windows SDK."""
    kits_root = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Windows Kits" / "10" / "bin"
    if not kits_root.is_dir():
        return
    versions = sorted((p for p in kits_root.iterdir() if p.is_dir()), reverse=True)
    if not versions:
        return
    kit_bin = versions[0]
    for name in ("dxcompiler.dll", "dxil.dll"):
        candidate = kit_bin / architecture / name
        if candidate.is_file():
            copy_file(candidate, install_dir / name)


def qt_roots(build_dir: Path) -> list[Path]:
    """Collect Qt6 root candidates."""
    roots: list[Path] = []
    dep_file = REPO_ROOT / "tools" / "dependencies.yaml"
    if dep_file.is_file():
        try:
            for dep in load_dependencies(dep_file):
                if dep.get("name") in ("qt", "qt6"):
                    val = dep.get("default")
                    if val:
                        roots.append(Path(val))
        except Exception:
            pass

    config_root = read_cmake_set_expanded(REPO_ROOT / "cmake" / "ConfigQT.cmake", "Qt6_ROOT")
    if config_root:
        roots.append(Path(config_root))

    for key in ("Qt6Core_DIR", "Qt6_DIR"):
        value = read_cmake_cache_value(build_dir / "CMakeCache.txt", key)
        if value:
            norm = value.replace("\\", "/")
            if "/lib/" in norm:
                roots.append(Path(norm.split("/lib/")[0]))

    for env_name in ("Qt6_ROOT", "QT6_ROOT", "Qt_ROOT", "QTDIR"):
        val = os.environ.get(env_name)
        if val:
            roots.append(Path(val))

    unique: list[Path] = []
    seen: set[str] = set()
    for root in roots:
        res = resolve_project_path(root)
        key = str(res).lower()
        if key not in seen:
            unique.append(res)
            seen.add(key)
    return unique


def find_windeployqt(build_dir: Path, explicit: str) -> Path | None:
    """Locate windeployqt.exe."""
    if explicit:
        p = resolve_project_path(explicit)
        return p if p.is_file() else None
    for root in qt_roots(build_dir):
        cand = root / "bin" / "windeployqt.exe"
        if cand.is_file():
            return cand
    found = shutil.which("windeployqt.exe")
    return Path(found) if found else None


def invoke_windeployqt(build_dir: Path, install_dir: Path, exe: Path, explicit: str, skip: bool) -> None:
    """Run windeployqt to deploy Qt runtime."""
    if skip:
        print("skip windeployqt")
        return
    tool = find_windeployqt(build_dir, explicit)
    if tool is None:
        raise RuntimeError("cannot find windeployqt; pass --windeployqt <path>")

    command = [
        str(tool),
        "--release",
        "--no-translations",
        "--dir",
        str(install_dir),
        "--qmldir",
        str(REPO_ROOT / "src" / "ui" / "qml"),
        "--qmlimport",
        str(REPO_ROOT / "3rdparty" / "QuickUI"),
        "--qmlimport",
        str(build_dir / "qml"),
        str(exe),
    ]
    print("run " + " ".join(command))
    subprocess.run(command, check=True)


def cleanup_release_debug_artifacts(install_dir: Path) -> None:
    """Remove qmldbg_*.dll and empty directories."""
    for tool_dir in [p for p in install_dir.rglob("qmltooling") if p.is_dir()]:
        for dll in tool_dir.rglob("qmldbg_*.dll"):
            if dll.is_file() or dll.is_symlink():
                dll.unlink()
                print(f"remove {dll}")
        for subdir in sorted((p for p in tool_dir.rglob("*") if p.is_dir()), key=lambda p: len(p.parts), reverse=True):
            try:
                subdir.rmdir()
            except OSError:
                pass
        try:
            tool_dir.rmdir()
            print(f"remove {tool_dir}")
        except OSError:
            pass


def write_qt_conf(install_dir: Path) -> None:
    """Write qt.conf for relative runtime paths."""
    (install_dir / "qt.conf").write_text(
        "[Paths]\nPrefix=.\nLibraries=.\nPlugins=.\nQml2Imports=qml\nQmlImports=qml\n",
        encoding="utf-8",
    )


def is_reparse_point(path: Path) -> bool:
    """Check if path is symlink or reparse point."""
    if path.is_symlink():
        return True
    if os.name != "nt":
        return False
    try:
        attrs = os.stat(path, follow_symlinks=False).st_file_attributes
        return bool(attrs & 0x400)
    except OSError:
        return False


def verify_package(install_dir: Path, require_qt: bool, build_dir: Path | None = None) -> None:
    """Verify package completeness strictly according to RS-06 and RT-04."""
    required_files = [
        install_dir / f"{PROJECT_NAME}.exe",
        install_dir / MARKER_FILE,
        install_dir / "quickui.dll",
    ]

    # Verify Python DLL (python3*.dll must exist)
    python_dlls = list(install_dir.glob("python3*.dll"))
    if not python_dlls:
        python_dlls = list((install_dir / "python").glob("python3*.dll"))
    if not python_dlls:
        raise RuntimeError(f"package verification failed: missing Python DLL (python3*.dll) in {install_dir}")

    if require_qt:
        required_files.extend([
            install_dir / "Qt6Core.dll",
            install_dir / "Qt6Gui.dll",
            install_dir / "Qt6Qml.dll",
            install_dir / "Qt6Quick.dll",
            install_dir / "platforms" / "qwindows.dll",
        ])

    required_dirs = [
        install_dir / "python" / "Lib",
        install_dir / "python" / "DLLs",
        install_dir / "python" / "Lib" / "site-packages" / "numpy",
        install_dir / "tools",
        install_dir / "qml" / "quickui",
    ]
    required_files.extend([
        install_dir / "qml" / "quickui" / "qmldir",
        install_dir / "qml" / "quickui" / "quickui.dll",
    ])

    missing_files = [str(p) for p in required_files if not p.is_file()]
    missing_dirs = [str(p) for p in required_dirs if not p.is_dir()]
    linked_paths = [str(p) for p in install_dir.rglob("*") if is_reparse_point(p)]

    problems = []
    if missing_files:
        problems.append("missing files: " + ", ".join(missing_files))
    if missing_dirs:
        problems.append("missing directories: " + ", ".join(missing_dirs))
    if linked_paths:
        problems.append("package contains links: " + ", ".join(linked_paths))

    if problems:
        raise RuntimeError("package verification failed: " + "; ".join(problems))

    count = sum(1 for p in install_dir.rglob("*") if p.is_file())
    print(f"package verification complete: {count} files")


def test_verify_package_negatives() -> None:
    """Run automated negative tests for RT-04 resource verification."""
    import tempfile
    with tempfile.TemporaryDirectory(prefix="vrt_verify_neg_") as tmp:
        root = Path(tmp)
        # Create baseline valid mock package
        (root / f"{PROJECT_NAME}.exe").touch()
        (root / MARKER_FILE).touch()
        (root / "quickui.dll").touch()
        (root / "python312.dll").touch()
        (root / "tools").mkdir()
        (root / "python" / "Lib" / "site-packages" / "numpy").mkdir(parents=True)
        (root / "python" / "DLLs").mkdir(parents=True)
        (root / "qml" / "quickui").mkdir(parents=True)
        (root / "qml" / "quickui" / "qmldir").touch()
        (root / "qml" / "quickui" / "quickui.dll").touch()

        # Baseline check (without Qt)
        verify_package(root, require_qt=False)

        # Case 0: Missing qml/quickui
        shutil.rmtree(root / "qml" / "quickui")
        try:
            verify_package(root, require_qt=False)
            raise AssertionError("verify_package did not fail when qml/quickui is missing")
        except RuntimeError as e:
            assert "quickui" in str(e), f"Unexpected error: {e}"
        (root / "qml" / "quickui").mkdir(parents=True)
        (root / "qml" / "quickui" / "qmldir").touch()
        (root / "qml" / "quickui" / "quickui.dll").touch()

        # Case 1: Missing quickui.dll
        (root / "quickui.dll").unlink()
        try:
            verify_package(root, require_qt=False)
            raise AssertionError("verify_package did not fail when quickui.dll is missing")
        except RuntimeError as e:
            assert "quickui.dll" in str(e), f"Unexpected error: {e}"
        (root / "quickui.dll").touch()

        # Case 2: Missing Python DLL
        (root / "python312.dll").unlink()
        try:
            verify_package(root, require_qt=False)
            raise AssertionError("verify_package did not fail when python3*.dll is missing")
        except RuntimeError as e:
            assert "missing Python DLL" in str(e), f"Unexpected error: {e}"
        (root / "python312.dll").touch()

        # Case 3: Missing NumPy
        shutil.rmtree(root / "python" / "Lib" / "site-packages" / "numpy")
        try:
            verify_package(root, require_qt=False)
            raise AssertionError("verify_package did not fail when numpy directory is missing")
        except RuntimeError as e:
            assert "numpy" in str(e), f"Unexpected error: {e}"
        (root / "python" / "Lib" / "site-packages" / "numpy").mkdir(parents=True)

        print("all RT-04 negative verification tests passed successfully!")


def main() -> int:
    """Main package entry point."""
    args = parse_args()
    if args.test_verify:
        test_verify_package_negatives()
        return 0

    build_dir = resolve_project_path(args.build_dir)
    raw_install_dir = resolve_project_path(args.install_dir) if args.install_dir else default_install_dir()

    # Safety protection: If user passes F:\tmp or directory named tmp/temp, place under VisionRT subfolder
    if raw_install_dir.name.lower() in {"tmp", "temp"}:
        install_dir = raw_install_dir / PROJECT_NAME_LONG
        print(f"redirecting installation from {raw_install_dir} to protected subdirectory: {install_dir}")
    else:
        install_dir = raw_install_dir

    dependency_file = resolve_project_path(args.dependencies)

    if not build_dir.is_dir():
        raise RuntimeError(f"build dir does not exist: {build_dir}")
    if args.build:
        build_release(build_dir, args.config, args.parallel)

    source_exe = find_executable(build_dir)
    version = project_version()
    custom_backup = clear_install_directory(install_dir, not args.no_clean, args.force_clean)
    write_package_marker(install_dir, build_dir, version, ("state=incomplete",))

    packaged_exe = install_dir / source_exe.name
    copy_file(source_exe, packaged_exe)
    copy_project_runtime(build_dir, install_dir, args.include_pdb)
    copy_tools(install_dir, custom_backup)
    copy_python_runtime(build_dir, install_dir)

    if args.tools_dir:
        merge_tools_package(install_dir, resolve_project_path(args.tools_dir),
                            resolve_project_path(args.tools_manifest) if args.tools_manifest else None)

    if args.skip_dependencies:
        print("skip manifest dependencies")
    elif dependency_file.is_file():
        copy_yaml_dependencies(build_dir, install_dir, dependency_file, args.config, args.allow_missing_dependencies)
    else:
        msg = f"dependency manifest was not found: {dependency_file}"
        if args.allow_missing_dependencies:
            warn(msg)
        else:
            raise RuntimeError(msg)

    if platform_key() == "windows":
        arch = target_architecture(build_dir)
        if args.skip_system_libs:
            print("skip system libraries")
        else:
            copy_msvc_runtime(install_dir, arch)
            copy_windows_sdk_dependencies(install_dir, arch)
        invoke_windeployqt(build_dir, install_dir, packaged_exe, args.windeployqt, args.skip_windeployqt)
        cleanup_release_debug_artifacts(install_dir)
        write_qt_conf(install_dir)
        write_windows_marker(install_dir, build_dir, arch, version, args.skip_dependencies)
        if not args.skip_verify:
            verify_package(install_dir, not args.skip_windeployqt, build_dir)
        print(f"\npackage complete: {install_dir}")
        print(f"double-click to run: {packaged_exe}")
    else:
        write_qt_conf(install_dir)
        write_package_marker(install_dir, build_dir, version)
        if not args.skip_verify:
            verify_package(install_dir, not args.skip_windeployqt, build_dir)
        print(f"\npackage complete: {install_dir}")

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
