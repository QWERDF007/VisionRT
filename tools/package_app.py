"""VisionRT release packaging script.

Packages release runtime into a standalone directory:
Collects executable, project DLLs, QML modules, Python runtime,
third-party dependencies from dependencies.json, system runtimes, and Qt plugins.
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
    MARKER_FILE,
    PROJECT_NAME,
    PROJECT_NAME_LONG,
    REPO_ROOT,
    build_dll_variant_sets,
    build_config_directory,
    copy_file,
    dependency_default_value,
    dependency_matches_config,
    dependency_patterns,
    dll_matches_config,
    expand_dependency_pattern,
    load_dependencies,
    load_json_file,
    platform_key,
    read_cmake_cache_value,
    resolve_dependency_root,
    resolve_project_path,
    warn,
)

def parse_args() -> argparse.Namespace:
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(description="Package VisionRT release runtime.")
    parser.add_argument("--build-dir", "-BuildDir", default="build", help="CMake build directory.")
    parser.add_argument("--install-dir", "-InstallDir", default=None, help="Installation output directory.")
    parser.add_argument("--config", "-Config", type=str.lower, choices=["release", "debug"], default="release", help="Build configuration.")
    parser.add_argument("--dependencies", default="tools/dependencies.json", help="Path to dependencies.json.")
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
    parser.add_argument("--tools-package", default=None, help="External VisionTools ZIP package to deploy into install_dir/tools-package.")
    parser.add_argument("--tools-index", default=None, help="Optional external module_index.json to assert byte-by-byte consistency with package.")
    parser.add_argument("--validator", default=None, help="Explicit path to visionrt_validate_tool executable.")
    parser.add_argument("--deploy-tools-only", action="store_true", help="Deploy or replace tools package in existing installation without full repackaging.")
    parser.add_argument("--allow-dirty", action="store_true", help="Allow dirty or unknown build metadata in release package.")
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


def find_validator(build_dir: Path, config: str, explicit: str | None) -> Path | None:
    """Use an explicit validator or the one from the selected build tree."""
    if explicit:
        return resolve_project_path(explicit)
    name = "visionrt_validate_tool.exe" if os.name == "nt" else "visionrt_validate_tool"
    return next((candidate for candidate in (
        build_dir / "bin" / name,
        build_dir / "bin" / build_config_directory(config) / name,
    ) if candidate.is_file()), None)


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


def clear_install_directory(path: Path, clean: bool, force: bool) -> None:
    """Prepare install directory safely according to marker rules."""
    resolved = path.resolve()
    repo_resolved = REPO_ROOT.resolve()
    if resolved == repo_resolved:
        raise RuntimeError(f"refusing to clean repository root: {path}")
    if resolved in (repo_resolved / "src", repo_resolved / "tools", repo_resolved / "cmake", repo_resolved / "assets", repo_resolved / "docs"):
        raise RuntimeError(f"refusing to clean repository source directory: {path}")
    if resolved.parent == resolved:
        raise RuntimeError(f"refusing to clean drive root: {path}")

    if not clean:
        path.mkdir(parents=True, exist_ok=True)
        return

    marker = path / MARKER_FILE
    if path.is_dir() and any(path.iterdir()) and not marker.exists() and not force:
        raise RuntimeError(
            f"install dir exists and has no {MARKER_FILE}: {path}; "
            "pass --force-clean to remove it or --no-clean to reuse it"
        )

    if path.exists():
        for child in path.iterdir():
            if child.is_dir() and not child.is_symlink():
                shutil.rmtree(child)
            else:
                child.unlink()
    else:
        path.mkdir(parents=True)


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


def verify_build_metadata(
    meta_path: Path,
    expected_component: str | None = None,
    allow_dirty: bool = False,
) -> dict:
    """Validate build metadata JSON format, schema, commit validity and dirty status."""
    if not meta_path.is_file():
        raise RuntimeError(f"package verification failed: missing build metadata {meta_path}")
    try:
        data = load_json_file(meta_path)
    except Exception as exc:
        raise RuntimeError(f"package verification failed: corrupted build metadata {meta_path}: {exc}") from exc
    if not isinstance(data, dict):
        raise RuntimeError(f"package verification failed: invalid build metadata root in {meta_path}")

    epoch = data.get("built_at_epoch")
    if not isinstance(epoch, int) or isinstance(epoch, bool) or epoch <= 0:
        raise RuntimeError(f"package verification failed: invalid built_at_epoch in {meta_path}")

    commit = data.get("commit")
    dirty = data.get("dirty")

    if expected_component and data.get("component") != expected_component:
        raise RuntimeError(
            f"package verification failed: component mismatch in {meta_path} "
            f"(expected '{expected_component}', got '{data.get('component')}')"
        )

    if not allow_dirty:
        if not commit or commit == "unknown":
            raise RuntimeError(
                f"package verification failed: {meta_path.name} commit is unknown; release packaging rejected"
            )
        if dirty is not False:
            raise RuntimeError(
                f"package verification failed: {meta_path.name} is dirty ({dirty}); release packaging rejected"
            )
    return data


def copy_project_runtime(
    build_dir: Path,
    install_dir: Path,
    include_pdb: bool,
    allow_dirty: bool = False,
) -> None:
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

    # 2. 构建完成阶段生成的组件元数据：关于弹窗只从 exe 同目录读取它们
    for name, comp in (("build_info.json", "VisionRT"), ("contracts-build-info.json", "VisionRTContracts")):
        build_info = bin_dir / name
        if build_info.is_file():
            verify_build_metadata(build_info, expected_component=comp, allow_dirty=allow_dirty)
            copy_file(build_info, install_dir / name)
        else:
            if not allow_dirty:
                raise RuntimeError(f"package verification failed: missing required build metadata {build_info}")

    # 3. PDB debug symbols if requested
    if include_pdb and bin_dir.is_dir():
        for pdb in bin_dir.glob("*.pdb"):
            copy_file(pdb, install_dir / pdb.name)


def deploy_tools_package(
    install_dir: Path,
    package_zip: Path,
    validator: Path | None = None,
    external_index: Path | None = None,
    allow_dirty: bool = False,
) -> None:
    """Deploy external tools package ZIP into install_dir/tools-package (D-05 / D-06)."""
    import zipfile
    import tempfile

    package_zip = Path(package_zip).resolve()
    if not package_zip.is_file():
        raise RuntimeError(f"tools package ZIP file was not found: {package_zip}")

    target_pkg = install_dir / "tools-package"

    with tempfile.TemporaryDirectory(prefix="vrt_deploy_") as tmp_str:
        tmp_dir = Path(tmp_str)

        try:
            with zipfile.ZipFile(package_zip, "r") as zf:
                # 1. Zip-slip & safety check: reject absolute paths, .., and symlinks (D-06)
                for member in zf.infolist():
                    name = member.filename
                    p = Path(name)
                    if p.is_absolute() or name.startswith(("/", "\\")) or ":" in name:
                        raise RuntimeError(f"tools package ZIP contains illegal absolute path: {name}")
                    if ".." in p.parts:
                        raise RuntimeError(f"tools package ZIP contains illegal path traversal: {name}")
                    if (member.external_attr >> 16) & 0o170000 == 0o120000:
                        raise RuntimeError(f"tools package ZIP contains illegal symlink entry: {name}")

                zf.extractall(tmp_dir)
        except zipfile.BadZipFile as e:
            raise RuntimeError(f"corrupt or invalid ZIP package: {e}")

        # 2. Check external index consistency if provided (D-05, D-06)
        if external_index is not None:
            ext_path = Path(external_index).resolve()
            if not ext_path.is_file():
                raise RuntimeError(f"external tools index file was not found: {ext_path}")
            pkg_index = tmp_dir / "module_index.json"
            if not pkg_index.is_file():
                raise RuntimeError(f"tools package missing module_index.json in {tmp_dir}")
            if ext_path.read_bytes() != pkg_index.read_bytes():
                raise RuntimeError("external tools index does not match package internal module_index.json byte-by-byte")

        # 3. Validate build metadata and tools package using host JSON validator (D-06)
        tools_build_info = tmp_dir / "build_info.json"
        if tools_build_info.is_file():
            verify_build_metadata(tools_build_info, expected_component="VisionTools", allow_dirty=allow_dirty)
        verify_tools_package(tmp_dir, validator)

        # 4. Atomic installation: if validation succeeded, cleanly replace target_pkg
        stage_dir = install_dir / f".tools-package-staging-{os.getpid()}"
        if stage_dir.is_dir():
            shutil.rmtree(stage_dir, ignore_errors=True)
        shutil.copytree(tmp_dir, stage_dir, dirs_exist_ok=True)

        backup_dir = install_dir / f".tools-package-backup-{os.getpid()}"
        if backup_dir.is_dir():
            shutil.rmtree(backup_dir, ignore_errors=True)

        if target_pkg.is_dir():
            try:
                target_pkg.rename(backup_dir)
            except OSError as e:
                shutil.rmtree(stage_dir, ignore_errors=True)
                raise RuntimeError(
                    f"failed to replace tools package (files in target directory may be locked or in use by a running process): {e}"
                )

        try:
            stage_dir.rename(target_pkg)
        except Exception as e:
            if backup_dir.is_dir() and not target_pkg.is_dir():
                try:
                    backup_dir.rename(target_pkg)
                except Exception:
                    pass
            shutil.rmtree(stage_dir, ignore_errors=True)
            raise RuntimeError(f"failed to move new tools package into place: {e}")

        if backup_dir.is_dir():
            shutil.rmtree(backup_dir, ignore_errors=True)

    print(f"deployed tools package from {package_zip} into {target_pkg}")


def resolve_python_home(build_dir: Path) -> Path:
    """Use the same interpreter installation selected by the host build."""
    selected = read_cmake_cache_value(build_dir / "CMakeCache.txt", "VISIONRT_PYTHON_HOME")
    if selected:
        runtime = Path(selected).resolve()
        if (runtime / "python.exe").is_file():
            return runtime
    raise RuntimeError("Built Python runtime not found; configure the VisionRT CMake tree first.")


def copy_python_runtime(build_dir: Path, install_dir: Path) -> None:
    """Deploy the matching embedded Python environment."""
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


def copy_dependencies(
    build_dir: Path,
    install_dir: Path,
    dependency_file: Path,
    config: str,
    allow_missing: bool,
) -> None:
    """Copy external dependencies declared in dependencies.json."""
    platform = platform_key()
    target_dir = install_dir if platform == "windows" else install_dir / "lib"
    target_dir.mkdir(parents=True, exist_ok=True)
    missing_dependencies: list[str] = []
    copied_runtimes: dict[str, Path] = {}

    for dep in load_dependencies(dependency_file):
        dep_name = str(dep.get("name", "<unnamed>")).lower()
        # qt and python are handled specially by windeployqt and copy_python_runtime
        if dep_name in {"qt", "qt6", "python"}:
            continue
        if not dependency_matches_config(dep, config):
            continue
        manifest_name = str(dep.get("runtime_manifest") or "")
        if manifest_name:
            # The build wrote the exact libraries it linked for this configuration; scanning the
            # vendor directory instead would deploy debug or unused DLLs.
            manifest = build_dir / manifest_name.replace("{config}", config)
            if not manifest.is_file():
                for alt_config in (config.capitalize(), config.upper(), config.lower()):
                    candidate = build_dir / manifest_name.replace("{config}", alt_config)
                    if candidate.is_file():
                        manifest = candidate
                        break
            if not manifest.is_file():
                missing_dependencies.append(f"{dep_name} (manifest: {manifest})")
                continue
            exact = [Path(line.strip()) for line in manifest.read_text(encoding="utf-8").splitlines()
                     if line.strip()]
            if not exact:
                missing_dependencies.append(f"{dep_name} (empty manifest: {manifest})")
                continue
            for runtime in exact:
                source = runtime.resolve(strict=True)
                destination_key = runtime.name.lower()
                prev_source = copied_runtimes.get(destination_key)
                if prev_source is not None:
                    if prev_source != source:
                        warn(f"runtime dependency collision for {runtime.name}: {prev_source} and {source}")
                    continue
                copy_file(source, target_dir / runtime.name)
                copied_runtimes[destination_key] = source
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

    dep_file = REPO_ROOT / "tools" / "dependencies.json"
    if dep_file.is_file():
        for dep in load_dependencies(dep_file):
            if dep.get("name") in ("qt", "qt6"):
                default = dependency_default_value(dep)
                if default:
                    roots.append(Path(default))

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
        "--qmldir",
        str(REPO_ROOT / "3rdparty" / "QuickUI" / "src" / "controls"),
        str(exe),
    ]
    print("run " + " ".join(command))
    subprocess.run(command, check=True)


def cleanup_release_debug_artifacts(install_dir: Path) -> None:
    """Remove qmldbg_*.dll, embedded qml/quickui source directories, and empty directories."""
    quickui_qml_dir = install_dir / "qml" / "quickui"
    if quickui_qml_dir.is_dir():
        shutil.rmtree(quickui_qml_dir)
        print(f"remove {quickui_qml_dir}")

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


def verify_tools_package(tools_pkg: Path, validator: Path | None = None) -> None:
    """Verify external tools package using the host JSON validator (--package)."""
    if not tools_pkg.is_dir():
        raise RuntimeError(f"tools package directory was not found: {tools_pkg}")

    for source in tools_pkg.rglob("*.json"):
        try:
            load_json_file(source)
        except (OSError, ValueError) as error:
            raise RuntimeError(f"Tools package JSON lint failed: {source}: {error}") from error

    if validator is None or not Path(validator).is_file():
        raise RuntimeError(
            f"Host validator executable is required for tools package verification, "
            f"but was not found or not specified: {validator}"
        )

    cmd = [str(validator), "--package", str(tools_pkg)]
    env = os.environ.copy()
    bin_dir = tools_pkg / "bin"
    if bin_dir.is_dir():
        env["PATH"] = str(bin_dir) + os.pathsep + env.get("PATH", "")
    res = subprocess.run(cmd, env=env, capture_output=True, text=True)
    if res.returncode != 0:
        err = res.stderr.strip() or res.stdout.strip()
        raise RuntimeError(f"Host tools package validation failed via validator:\n{err}")


def verify_package(
    install_dir: Path,
    require_qt: bool,
    validator: Path | None = None,
    allow_dirty: bool = False,
) -> None:
    """Verify package completeness strictly according to RS-06 and RT-04."""
    required_files = [
        install_dir / f"{PROJECT_NAME}.exe",
        install_dir / MARKER_FILE,
        install_dir / "quickui.dll",
    ]
    if not allow_dirty:
        required_files.extend([
            install_dir / "build_info.json",
            install_dir / "contracts-build-info.json",
        ])

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
    ]

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

    # Verify build metadata
    for name, comp in (("build_info.json", "VisionRT"), ("contracts-build-info.json", "VisionRTContracts")):
        meta_file = install_dir / name
        if meta_file.is_file():
            try:
                verify_build_metadata(meta_file, expected_component=comp, allow_dirty=allow_dirty)
            except RuntimeError as exc:
                problems.append(str(exc))

    if problems:
        raise RuntimeError("package verification failed: " + "; ".join(problems))

    # Verify tools package if merged
    tools_pkg_dir = install_dir / "tools-package"
    if tools_pkg_dir.is_dir():
        tools_build_info = tools_pkg_dir / "build_info.json"
        if tools_build_info.is_file():
            try:
                verify_build_metadata(tools_build_info, expected_component="VisionTools", allow_dirty=allow_dirty)
            except RuntimeError as exc:
                raise RuntimeError(f"package verification failed: {exc}") from exc
        verify_tools_package(tools_pkg_dir, validator)
        print("tools package verification passed successfully")

    count = sum(1 for p in install_dir.rglob("*") if p.is_file())
    print(f"package verification complete: {count} files")


def main() -> int:
    """Main package entry point."""
    args = parse_args()
    build_dir = resolve_project_path(args.build_dir)
    raw_install_dir = resolve_project_path(args.install_dir) if args.install_dir else default_install_dir()

    # Safety protection: If user passes F:\tmp or directory named tmp/temp, place under VisionRT subfolder
    if raw_install_dir.name.lower() in {"tmp", "temp"}:
        install_dir = raw_install_dir / PROJECT_NAME_LONG
        print(f"redirecting installation from {raw_install_dir} to protected subdirectory: {install_dir}")
    else:
        install_dir = raw_install_dir

    if args.deploy_tools_only:
        if not args.tools_package:
            raise RuntimeError("--deploy-tools-only requires --tools-package <path_to_zip>")
        if not install_dir.is_dir():
            raise RuntimeError(f"installation directory does not exist: {install_dir}")
        marker = install_dir / MARKER_FILE
        exe_file = install_dir / f"{PROJECT_NAME}.exe"
        if not marker.is_file() and not exe_file.is_file():
            raise RuntimeError(f"specified directory is not a valid VisionRT installation: {install_dir}")

        validator_path = find_validator(build_dir, args.config, args.validator)

        ext_index = resolve_project_path(args.tools_index) if args.tools_index else None
        deploy_tools_package(
            install_dir,
            resolve_project_path(args.tools_package),
            validator=validator_path,
            external_index=ext_index,
            allow_dirty=args.allow_dirty,
        )
        print(f"tools package successfully deployed into {install_dir / 'tools-package'}")
        return 0

    dependency_file = resolve_project_path(args.dependencies)

    if not build_dir.is_dir():
        raise RuntimeError(f"build dir does not exist: {build_dir}")
    if args.build:
        build_release(build_dir, args.config, args.parallel)

    source_exe = find_executable(build_dir)
    validator_path = find_validator(build_dir, args.config, args.validator)
    version = project_version()
    clear_install_directory(install_dir, not args.no_clean, args.force_clean)
    write_package_marker(install_dir, build_dir, version, ("state=incomplete",))

    packaged_exe = install_dir / source_exe.name
    copy_file(source_exe, packaged_exe)
    copy_project_runtime(build_dir, install_dir, args.include_pdb, allow_dirty=args.allow_dirty)
    copy_python_runtime(build_dir, install_dir)

    if args.tools_package:
        ext_index = resolve_project_path(args.tools_index) if args.tools_index else None
        deploy_tools_package(
            install_dir,
            resolve_project_path(args.tools_package),
            validator=validator_path,
            external_index=ext_index,
            allow_dirty=args.allow_dirty,
        )

    if args.skip_dependencies:
        print("skip manifest dependencies")
    elif dependency_file.is_file():
        copy_dependencies(build_dir, install_dir, dependency_file, args.config, args.allow_missing_dependencies)
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
            verify_package(install_dir, not args.skip_windeployqt, validator=validator_path, allow_dirty=args.allow_dirty)
        print(f"\npackage complete: {install_dir}")
        print(f"double-click to run: {packaged_exe}")
    else:
        write_qt_conf(install_dir)
        write_package_marker(install_dir, build_dir, version)
        if not args.skip_verify:
            verify_package(install_dir, not args.skip_windeployqt, validator=validator_path, allow_dirty=args.allow_dirty)
        print(f"\npackage complete: {install_dir}")

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
