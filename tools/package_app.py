"""VisionRT release packaging script.

Packages release runtime into a standalone directory:
Collects executable, project DLLs (including the Python bridge), QML modules,
third-party dependencies from dependencies.json, system runtimes, and Qt plugins.
The CPython interpreter and its packages stay in the user's chosen environment.
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import re
import shutil
import struct
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath
from typing import Iterator

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

if str(REPO_ROOT / "cmake") not in sys.path:
    sys.path.insert(0, str(REPO_ROOT / "cmake"))
try:
    from check_git_workspace import check_git_workspace, is_whitelisted
except ImportError:
    check_git_workspace = None
    is_whitelisted = None

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
    parser.add_argument("--evidence", default=None, help="Path of the package evidence JSON (default: <build-dir>/evidence/package-evidence.json).")
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


# Everything the application creates inside its own directory and keeps across an upgrade. A clean
# install replaces program files only: user tools, logs and interaction preferences are data, not
# build output. The marker itself is preserved too, because it is what authorises the next clean.
PRESERVED_INSTALL_ENTRIES = frozenset(
    {"custom-tools", "logs", "ui-preferences.json", ".visionrt_package", "install-manifest.json"}
)

# The previous package's own file list. It is the only authority for "this file is mine": anything
# absent from it belongs to the user and survives an upgrade untouched. The manifest is produced
# from the set of files this run actually wrote, never from a scan of the directory.
MANIFEST_FILE = "install-manifest.json"
MANIFEST_SCHEMA_VERSION = 1
PROTECTED_INSTALL_ROOTS = frozenset({"custom-tools", "logs"})
PROTECTED_INSTALL_FILES = frozenset({"ui-preferences.json"})
# The example projects are product assets next to the executable, not test data.
EXAMPLES_DIRECTORY = "examples"

# The first batch is the product promise: these six projects ship with every installation and run
# without prerequisites. Packaging compares source and installation byte for byte, which cannot
# notice a project that is missing from both sides, so the shipped set is stated here once.
REQUIRED_EXAMPLE_PROJECTS = (
    "01_基础入门/01_图像读取/图像读取.vrt.json",
    "02_工具示例/01_ROI定位与裁剪/ROI定位与裁剪.vrt.json",
    "02_工具示例/02_图像预处理/图像预处理.vrt.json",
    "02_工具示例/03_圆测量/圆测量.vrt.json",
    "02_工具示例/04_行间距测量/行间距测量.vrt.json",
    "03_综合示例/01_多分支视觉检测/多分支视觉检测.vrt.json",
)

# A developer machine path in a shipped project is a broken example on every other machine.
ABSOLUTE_PATH_LITERAL = re.compile(r"^(?:[A-Za-z]:[\\/]|\\\\|/)")


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


def manifest_entry_error(relative: str) -> str | None:
    """Why a manifest path cannot name a product file, or None when it can."""
    if not relative or relative.startswith(("/", "\\")) or ":" in relative or "\\" in relative:
        return f"not a relative forward-slash path: {relative!r}"
    parts = PurePosixPath(relative).parts
    if not parts or any(part in {"", ".", ".."} for part in parts):
        return f"path leaves the install directory: {relative!r}"
    if parts[0] in PROTECTED_INSTALL_ROOTS or relative in PROTECTED_INSTALL_FILES:
        return f"path belongs to the user and is never product-owned: {relative!r}"
    if relative in {MANIFEST_FILE, MARKER_FILE}:
        return f"path is packaging metadata, not product content: {relative!r}"
    return None


def _validate_manifest_entry(relative: str, manifest: Path) -> str:
    problem = manifest_entry_error(relative)
    if problem:
        raise RuntimeError(f"install manifest is unsafe ({manifest}): {problem}")
    return relative


def read_install_manifest(path: Path) -> set[str]:
    """Files the previous package installed. A missing manifest means no claim on anything."""
    manifest = path / MANIFEST_FILE
    if not manifest.is_file():
        return set()
    try:
        document = json.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise RuntimeError(f"install manifest is unreadable: {manifest}: {error}") from error
    if not isinstance(document, dict):
        raise RuntimeError(f"install manifest is invalid: {manifest}: root is not an object")
    if document.get("schema_version") != MANIFEST_SCHEMA_VERSION:
        raise RuntimeError(
            f"install manifest is invalid: {manifest}: unsupported schema {document.get('schema_version')!r}")
    files = document.get("files")
    if not isinstance(files, list) or not all(isinstance(item, str) for item in files):
        raise RuntimeError(f"install manifest is invalid: {manifest}: files is not a string list")
    if len(files) != len(set(files)):
        raise RuntimeError(f"install manifest is invalid: {manifest}: duplicate entries")
    entries = {_validate_manifest_entry(item, manifest) for item in files}
    # A path cannot be both a file and a directory in one package; such a record would delete half
    # of the tree and then fail to remove the rest.
    for entry in entries:
        parent = PurePosixPath(entry).parent
        while str(parent) not in {"", "."}:
            if str(parent) in entries:
                raise RuntimeError(
                    f"install manifest is invalid: {manifest}: {str(parent)!r} is both a file and a directory")
            parent = parent.parent
    return entries


def write_install_manifest(install_dir: Path, files: set[str]) -> None:
    """Records the product paths this installation owns, for the next upgrade to remove.

    The caller passes every path the package owns after the run, not a directory scan: a scan would
    also list whatever the user stored there, and the next upgrade would then delete it.
    """
    entries = sorted(_validate_manifest_entry(item, install_dir / MANIFEST_FILE) for item in files)
    missing = [item for item in entries if not (install_dir / item).is_file()]
    if missing:
        raise RuntimeError("install manifest names files this run did not leave behind: " + ", ".join(missing))
    payload = {"schema_version": MANIFEST_SCHEMA_VERSION, "files": entries}
    (install_dir / MANIFEST_FILE).write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


@dataclass
class InstallOwnership:
    """What this run has written, and what the previous package is allowed to replace."""

    root: Path
    replaceable: set[str] = field(default_factory=set)
    owned: set[str] = field(default_factory=set)

    def relative(self, destination: Path) -> str:
        absolute = Path(destination).absolute()
        try:
            relative = absolute.relative_to(self.root)
        except ValueError as error:
            raise RuntimeError(f"package write escaped the install directory: {destination}") from error
        return _validate_manifest_entry(relative.as_posix(), self.root / MANIFEST_FILE)

    def check(self, destination: Path) -> str:
        """Refuses to overwrite anything this package neither wrote now nor owned before."""
        destination = Path(destination)
        relative = self.relative(destination)
        if destination.is_symlink() or is_reparse_point(destination):
            raise RuntimeError(f"refusing to replace a link the package does not own: {relative}")
        if destination.exists():
            if destination.is_dir():
                raise RuntimeError(f"refusing to replace a directory with a file: {relative}")
            if relative not in self.replaceable and relative not in self.owned:
                raise RuntimeError(
                    f"refusing to overwrite a file the package does not own: {relative}")
        return relative

    def claim(self, destination: Path) -> None:
        self.owned.add(self.relative(destination))

    def manifest_entries(self) -> set[str]:
        """Every product path this installation owns once the run is over.

        Writing only what this run copied loses the files it left untouched: windeployqt skips an
        identical Qt binary, and a reinstall over an existing directory would then drop hundreds of
        product files from the manifest. Those paths stay the package's, so the next upgrade may
        still replace them; user files were never in the previous manifest and are never added.
        """
        surviving = {entry for entry in self.replaceable if (self.root / entry).is_file()}
        return self.owned | surviving


def owned_copy(source: Path, destination: Path, ownership: InstallOwnership | None = None) -> None:
    """Copies a product file and records it as owned by this run."""
    if ownership is not None:
        ownership.check(destination)
    copy_file(source, destination)
    if ownership is not None:
        ownership.claim(destination)


def remove_empty_directories(root: Path) -> None:
    """Removes directories the package emptied, never a user data root."""
    preserved = {root / name for name in PRESERVED_INSTALL_ENTRIES}
    directories = sorted((item for item in root.rglob("*") if item.is_dir() and not item.is_symlink()),
                         key=lambda item: len(item.parts), reverse=True)
    for directory in directories:
        if directory in preserved:
            continue
        try:
            directory.rmdir()
        except OSError:
            pass


def clear_install_directory(path: Path, clean: bool, force: bool) -> set[str]:
    """Prepares the install directory and returns the files the previous package owned."""
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
        return read_install_manifest(path)

    if not path.exists():
        path.mkdir(parents=True)
        return set()

    entries = list(path.iterdir())
    marker = path / MARKER_FILE
    if entries and not marker.exists() and not force:
        raise RuntimeError(
            f"install dir exists and has no {MARKER_FILE}: {path}; "
            "pass --force-clean to remove it or --no-clean to reuse it"
        )

    # A forced full replace does not consult the old manifest: the user asked to replace the
    # directory. The data roots are still preserved below.
    owned = set() if force else read_install_manifest(path)
    if entries and not owned and not force:
        # Without a manifest the package cannot tell its own files from user data. Refusing is the
        # only answer that cannot destroy something the user wrote.
        raise RuntimeError(
            f"install dir has no {MANIFEST_FILE}: {path}; the package cannot separate its own files "
            "from user data, so nothing was removed. Pass --force-clean to replace the whole "
            "directory, or --no-clean to keep it."
        )

    if not owned:
        # --force-clean on a directory that predates the manifest: the user asked for a full replace.
        for child in entries:
            if child.name in PRESERVED_INSTALL_ENTRIES:
                continue
            if child.is_dir() and not child.is_symlink():
                shutil.rmtree(child)
            else:
                child.unlink()
        return set()

    root = path.resolve()
    # Every record is checked before the first deletion, so a bad manifest cannot leave the
    # installation half removed.
    targets: list[Path] = []
    for relative in sorted(owned, key=lambda value: value.count("/"), reverse=True):
        candidate = root / relative
        resolved_candidate = candidate.resolve()
        if resolved_candidate == root or root not in resolved_candidate.parents:
            raise RuntimeError(f"install manifest points outside the install dir: {relative}")
        if candidate.is_symlink():
            raise RuntimeError(f"install manifest points at a link; refusing to remove it: {relative}")
        if candidate.is_file():
            targets.append(candidate)
        elif candidate.exists() and not candidate.is_dir():
            raise RuntimeError(f"install manifest names an unsupported entry: {relative}")
    for candidate in targets:
        candidate.unlink()
    remove_empty_directories(path)
    remaining = sum(1 for item in path.rglob("*") if item.is_file())
    if remaining:
        print(f"preserving {remaining} file(s) this package does not own under {path}")
    return owned


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
    ownership: InstallOwnership | None = None,
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
        owned_copy(quickui_dll, install_dir / "quickui.dll", ownership)

    # 1b. The Python bridge is a product component even though no interpreter ships with it: the
    # host resolves it next to the executable only when a user-selected environment is started.
    bridge_dll = None
    for candidate in (bin_dir / "visionrt_python_bridge.dll", build_dir / "visionrt_python_bridge.dll"):
        if candidate.is_file():
            bridge_dll = candidate
            break
    if bridge_dll is None:
        raise RuntimeError("package verification failed: visionrt_python_bridge.dll was not built")
    owned_copy(bridge_dll, install_dir / "visionrt_python_bridge.dll", ownership)

    # 2. 构建完成阶段生成的组件元数据：关于弹窗只从 exe 同目录读取它们
    for name, comp in (("build_info.json", "VisionRT"), ("contracts-build-info.json", "VisionRTContracts")):
        build_info = bin_dir / name
        if build_info.is_file():
            verify_build_metadata(build_info, expected_component=comp, allow_dirty=allow_dirty)
            owned_copy(build_info, install_dir / name, ownership)
        else:
            if not allow_dirty:
                raise RuntimeError(f"package verification failed: missing required build metadata {build_info}")

    # 3. PDB debug symbols if requested
    if include_pdb and bin_dir.is_dir():
        for pdb in bin_dir.glob("*.pdb"):
            owned_copy(pdb, install_dir / pdb.name, ownership)


def example_files(source: Path | None = None) -> list[Path]:
    """The shipped example assets, as the source tree lists them."""
    root = REPO_ROOT / EXAMPLES_DIRECTORY if source is None else source
    if not root.is_dir():
        raise RuntimeError(f"examples directory is missing: {root}")
    files = [
        item for item in sorted(root.rglob("*"))
        if item.is_file() and "__pycache__" not in item.parts
    ]
    if not files:
        raise RuntimeError(f"examples directory is empty: {root}")
    return files


def copy_examples(install_dir: Path, ownership: InstallOwnership | None = None) -> None:
    """Deploys the example projects exactly as the source tree has them."""
    source = REPO_ROOT / EXAMPLES_DIRECTORY
    for item in example_files(source):
        owned_copy(item, install_dir / EXAMPLES_DIRECTORY / item.relative_to(source), ownership)


def example_texts(value: object) -> Iterator[str]:
    """Every string a project document stores, wherever it sits."""
    if isinstance(value, str):
        yield value
    elif isinstance(value, dict):
        for item in value.values():
            yield from example_texts(item)
    elif isinstance(value, list):
        for item in value:
            yield from example_texts(item)


def example_project_problems(source: Path) -> list[str]:
    """Each shipped project is format 7 and names its resources relatively."""
    problems = []
    for project in sorted(source.rglob("*.vrt.json")):
        relative = project.relative_to(source).as_posix()
        try:
            document = json.loads(project.read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            problems.append(f"example project is not readable JSON: {relative}: {error}")
            continue
        if document.get("format_version") != 7:
            problems.append(f"example project is not format 7: {relative}")
        for text in example_texts(document):
            if ABSOLUTE_PATH_LITERAL.match(text):
                problems.append(f"example project stores an absolute path: {relative}: {text}")
    return problems


def verify_examples(install_dir: Path) -> list[str]:
    """Every shipped example file must be installed with the same content, and the first batch must
    still be complete with its own input images."""
    source = REPO_ROOT / EXAMPLES_DIRECTORY
    installed = install_dir / EXAMPLES_DIRECTORY
    if not installed.is_dir():
        return [f"examples directory missing: {installed}"]
    problems = []
    for item in example_files(source):
        target = installed / item.relative_to(source)
        relative = target.relative_to(install_dir).as_posix()
        if not target.is_file():
            problems.append(f"example file missing: {relative}")
        elif target.read_bytes() != item.read_bytes():
            problems.append(f"example file differs from the source tree: {relative}")
    for relative in REQUIRED_EXAMPLE_PROJECTS:
        if not (source / relative).is_file():
            problems.append(f"required example project is missing from the source tree: {relative}")
            continue
        project = installed / relative
        if not project.is_file():
            problems.append(f"required example project missing: {relative}")
            continue
        images = project.parent / "images"
        if not images.is_dir() or not any(item.is_file() for item in images.iterdir()):
            problems.append(f"required example has no input images: {relative}")
        if not (project.parent / "README.md").is_file():
            problems.append(f"required example has no README: {relative}")
    problems.extend(example_project_problems(source))
    return problems


def deploy_tools_package(
    install_dir: Path,
    package_zip: Path,
    validator: Path | None = None,
    external_index: Path | None = None,
    allow_dirty: bool = False,
    ownership: InstallOwnership | None = None,
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

        # 3. Validate tools package using host JSON validator (D-06)
        verify_tools_package(tmp_dir, validator)

        # 4. Validate build metadata in release mode (S47-01)
        tools_build_info = tmp_dir / "build_info.json"
        if not tools_build_info.is_file():
            if not allow_dirty:
                raise RuntimeError(f"package verification failed: tools package missing required build_info.json in {package_zip}")
        else:
            verify_build_metadata(tools_build_info, expected_component="VisionTools", allow_dirty=allow_dirty)

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

    if ownership is not None:
        for item in sorted(target_pkg.rglob("*")):
            if item.is_file():
                ownership.claim(item)
    print(f"deployed tools package from {package_zip} into {target_pkg}")


def copy_dependencies(
    build_dir: Path,
    install_dir: Path,
    dependency_file: Path,
    config: str,
    allow_missing: bool,
    ownership: InstallOwnership | None = None,
) -> None:
    """Copy external dependencies declared in dependencies.json."""
    platform = platform_key()
    target_dir = install_dir if platform == "windows" else install_dir / "lib"
    target_dir.mkdir(parents=True, exist_ok=True)
    missing_dependencies: list[str] = []
    copied_runtimes: dict[str, Path] = {}

    for dep in load_dependencies(dependency_file):
        dep_name = str(dep.get("name", "<unnamed>")).lower()
        # qt is deployed by windeployqt. The python entry only resolves the build interpreter, so no
        # interpreter file is ever copied into the installation.
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
                owned_copy(source, target_dir / runtime.name, ownership)
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
            owned_copy(source, target_dir / runtime.name, ownership)
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


def copy_msvc_runtime(install_dir: Path, architecture: str, ownership: InstallOwnership | None = None) -> None:
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
                owned_copy(dll, install_dir / dll.name, ownership)
                copied = True
            if copied:
                return
    warn("MSVC runtime DLLs were not found in standard paths")


def copy_windows_sdk_dependencies(install_dir: Path, architecture: str, ownership: InstallOwnership | None = None) -> None:
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
            owned_copy(candidate, install_dir / name, ownership)


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


def write_qt_conf(install_dir: Path, ownership: InstallOwnership | None = None) -> None:
    """Write qt.conf for relative runtime paths."""
    destination = install_dir / "qt.conf"
    if ownership is not None:
        ownership.check(destination)
    destination.write_text(
        "[Paths]\nPrefix=.\nLibraries=.\nPlugins=.\nQml2Imports=qml\nQmlImports=qml\n",
        encoding="utf-8",
    )
    if ownership is not None:
        ownership.claim(destination)


def deploy_qt_runtime(
    build_dir: Path,
    install_dir: Path,
    exe: Path,
    explicit: str,
    skip: bool,
    ownership: InstallOwnership,
) -> None:
    """Runs windeployqt and records exactly the files it wrote.

    A staging directory would make the written set obvious, but it also means moving hundreds of
    files twice; the snapshot records what actually changed instead. A file that existed before and
    was not owned by this package is a conflict: windeployqt must never silently replace user data.
    """
    if skip:
        print("skip windeployqt")
        return

    def fingerprint(path: Path) -> tuple[int, int]:
        stat = path.stat()
        return stat.st_size, stat.st_mtime_ns

    before = {item.relative_to(install_dir).as_posix(): fingerprint(item)
              for item in install_dir.rglob("*") if item.is_file()}
    invoke_windeployqt(build_dir, install_dir, exe, explicit, False)
    cleanup_release_debug_artifacts(install_dir)

    written: list[str] = []
    for item in install_dir.rglob("*"):
        if not item.is_file():
            continue
        relative = item.relative_to(install_dir).as_posix()
        if before.get(relative) != fingerprint(item):
            written.append(relative)

    conflicts = sorted(relative for relative in written
                       if relative in before
                       and relative not in ownership.replaceable
                       and relative not in ownership.owned)
    if conflicts:
        raise RuntimeError("windeployqt overwrote files this package does not own: " + ", ".join(conflicts))
    for relative in written:
        ownership.claim(install_dir / relative)


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


# A binary that names the interpreter in its import table pulls CPython into the process before any
# host decision is made. That is what this package must never do, so the check reads the loader's
# own tables instead of trusting the build configuration that was supposed to prevent it.
PYTHON_RUNTIME_IMPORT = re.compile(r"^python3[0-9]*\.dll$", re.IGNORECASE)
PE_IMPORT_DIRECTORY = 1
PE_DELAY_IMPORT_DIRECTORY = 13


def _rva_to_offset(rva: int, sections: list[tuple[int, int, int, int]]) -> int | None:
    for virtual_address, virtual_size, raw_size, raw_pointer in sections:
        span = max(virtual_size, raw_size)
        if virtual_address <= rva < virtual_address + span:
            return raw_pointer + (rva - virtual_address)
    return None


def _read_names(data: bytes, rva: int, sections: list[tuple[int, int, int, int]],
                entry_size: int, name_offset: int) -> list[str]:
    names: list[str] = []
    start = _rva_to_offset(rva, sections)
    if start is None:
        return names
    for index in range(4096):
        position = start + index * entry_size
        if position + entry_size > len(data):
            break
        if not any(data[position:position + entry_size]):
            break
        name_rva = struct.unpack_from("<I", data, position + name_offset)[0]
        if not name_rva:
            continue
        name_position = _rva_to_offset(name_rva, sections)
        if name_position is None or name_position >= len(data):
            continue
        end = data.find(b"\0", name_position, min(len(data), name_position + 260))
        if end < 0:
            continue
        names.append(data[name_position:end].decode("ascii", errors="replace"))
    return names


def read_pe_imports(path: Path) -> tuple[list[str], list[str]]:
    """DLL names a PE file imports, from the import table and the delay-load table.

    Both tables matter: a delay-loaded `python3*.dll` is still a runtime dependency the package
    would carry, just resolved later. Malformed files raise instead of reporting "no imports",
    because an unreadable binary must not look like a clean one.
    """
    data = path.read_bytes()
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise ValueError(f"not a PE file: {path}")
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if pe_offset + 0x18 > len(data) or data[pe_offset:pe_offset + 4] != b"PE\0\0":
        raise ValueError(f"not a PE file: {path}")
    section_count = struct.unpack_from("<H", data, pe_offset + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe_offset + 20)[0]
    optional = pe_offset + 24
    if optional + 2 > len(data):
        raise ValueError(f"truncated PE header: {path}")
    magic = struct.unpack_from("<H", data, optional)[0]
    if magic == 0x10B:
        count_offset, directory_offset = optional + 92, optional + 96
    elif magic == 0x20B:
        count_offset, directory_offset = optional + 108, optional + 112
    else:
        raise ValueError(f"unknown PE optional header magic 0x{magic:x}: {path}")
    directory_count = struct.unpack_from("<I", data, count_offset)[0]
    sections: list[tuple[int, int, int, int]] = []
    section_table = optional + optional_size
    for index in range(section_count):
        entry = section_table + index * 40
        if entry + 40 > len(data):
            raise ValueError(f"truncated section table: {path}")
        virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from("<IIII", data, entry + 8)
        sections.append((virtual_address, virtual_size, raw_size, raw_pointer))

    def directory(index: int) -> int:
        if index >= directory_count:
            return 0
        return struct.unpack_from("<I", data, directory_offset + index * 8)[0]

    return (
        _read_names(data, directory(PE_IMPORT_DIRECTORY), sections, 20, 12),
        _read_names(data, directory(PE_DELAY_IMPORT_DIRECTORY), sections, 32, 4),
    )


def shipped_pe_files(install_dir: Path) -> list[Path]:
    return sorted(p for p in install_dir.rglob("*") if p.suffix.lower() in {".exe", ".dll", ".pyd"})


def python_runtime_imports(install_dir: Path, executable: Path) -> tuple[list[str], list[str]]:
    """Files whose import tables name a Python runtime, and the startup chain that was walked.

    The walk starts at the executable and follows shipped modules it imports, because that is the
    chain the process loads before it can decide anything. A module no shipped binary imports (the
    bridge included) is not on that chain: it is loaded only after an environment was accepted.
    """
    by_name: dict[str, Path] = {}
    for candidate in shipped_pe_files(install_dir):
        by_name.setdefault(candidate.name.lower(), candidate)

    offenders: list[str] = []
    chain: list[str] = []
    pending = [executable]
    visited: set[Path] = set()
    while pending:
        current = pending.pop(0)
        if current in visited or not current.is_file():
            continue
        visited.add(current)
        chain.append(current.name)
        try:
            imports, delay_imports = read_pe_imports(current)
        except ValueError as error:
            raise RuntimeError(f"package verification failed: {error}") from error
        for name in imports + delay_imports:
            if PYTHON_RUNTIME_IMPORT.match(name):
                offenders.append(f"{current.name} imports {name}")
            dependency = by_name.get(name.lower())
            if dependency is not None and dependency not in visited:
                pending.append(dependency)
    return offenders, chain


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


def bundled_python_runtime(install_dir: Path) -> list[str]:
    """Every place the package carries an interpreter, its standard library or its DLL directory.

    The scan is recursive on purpose: a nested copy is exactly what a top-level glob misses, and an
    interpreter nobody chose must never be reachable from the installation.
    """
    found = sorted(str(candidate) for pattern in ("python3*.dll", "python.exe", "pythonw.exe")
                   for candidate in install_dir.rglob(pattern))
    found.extend(str(path) for path in (install_dir / "python" / "Lib", install_dir / "python" / "DLLs")
                 if path.exists())
    return found


def write_package_evidence(install_dir: Path, path: Path, architecture: str) -> Path:
    """Records what the verified package ships and what its startup chain imports.

    The file is the machine-readable half of the release claim: no interpreter in the tree, none in
    the import tables the loader walks, and the bridge present for the on-demand path. It is written
    only after verification passed, so a failing package never leaves evidence behind.
    """
    executable = install_dir / f"{PROJECT_NAME}.exe"
    offenders, startup_chain = python_runtime_imports(install_dir, executable)
    product_prefixes = ("visionrt", "quickui")
    binaries = []
    for candidate in shipped_pe_files(install_dir):
        name = candidate.name.lower()
        if not name.startswith(product_prefixes) and not PYTHON_RUNTIME_IMPORT.match(name):
            continue
        try:
            imports, delay_imports = read_pe_imports(candidate)
        except (OSError, ValueError) as error:
            # Recorded, not hidden: evidence that silently drops a file it could not read would read
            # like a clean import table.
            binaries.append({"name": candidate.name, "unreadable": str(error)})
            continue
        binaries.append({
            "name": candidate.name,
            "imports": imports,
            "delayImports": delay_imports,
            "pythonImports": sorted({item for item in imports + delay_imports
                                     if PYTHON_RUNTIME_IMPORT.match(item)}),
        })
    payload = {
        "schemaVersion": 1,
        "product": PROJECT_NAME,
        "version": project_version(),
        "architecture": architecture,
        "generatedAt": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "fileCount": sum(1 for item in install_dir.rglob("*") if item.is_file()),
        "bundledPythonRuntime": bundled_python_runtime(install_dir),
        "startupChain": startup_chain,
        "startupPythonImports": offenders,
        "bridgePresent": (install_dir / "visionrt_python_bridge.dll").is_file(),
        "binaries": binaries,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"package evidence written: {path}")
    return path


def verify_package(
    install_dir: Path,
    require_qt: bool,
    validator: Path | None = None,
    allow_dirty: bool = False,
) -> None:
    """Verify package completeness strictly according to RS-06 and RT-04."""
    if not install_dir.is_dir():
        raise RuntimeError(f"package verification failed: installation directory does not exist: {install_dir}")

    required_files = [
        install_dir / f"{PROJECT_NAME}.exe",
        install_dir / MARKER_FILE,
        install_dir / "quickui.dll",
        # The bridge is a product component and ships with the host; the interpreter does not.
        install_dir / "visionrt_python_bridge.dll",
    ]
    if not allow_dirty:
        required_files.extend([
            install_dir / "build_info.json",
            install_dir / "contracts-build-info.json",
        ])

    if require_qt:
        required_files.extend([
            install_dir / "Qt6Core.dll",
            install_dir / "Qt6Gui.dll",
            install_dir / "Qt6Qml.dll",
            install_dir / "Qt6Quick.dll",
            install_dir / "platforms" / "qwindows.dll",
        ])

    # The interpreter, its standard library and NumPy are the user's environment. A package that
    # carries them would run a second runtime nobody selected, so their presence anywhere in the tree
    # is a failure: a nested copy is exactly what a top-level glob misses.
    bundled_runtime = bundled_python_runtime(install_dir)

    missing_files = [str(p) for p in required_files if not p.is_file()]
    linked_paths = [str(p) for p in install_dir.rglob("*") if is_reparse_point(p)]

    # A recursive file scan proves no interpreter was copied in; the import tables prove the shipped
    # binaries do not pull one in either. A package can pass the first check and still load CPython
    # before the host has decided anything, so both are part of the same claim. Import tables are a
    # property of the Windows loader, so the walk applies where the product actually ships.
    if platform_key() == "windows":
        python_imports, startup_chain = python_runtime_imports(install_dir, install_dir / f"{PROJECT_NAME}.exe")
    else:
        python_imports, startup_chain = [], []

    problems = []
    if missing_files:
        problems.append("missing files: " + ", ".join(missing_files))
    if bundled_runtime:
        problems.append("package ships a Python runtime: " + ", ".join(bundled_runtime))
    if python_imports:
        problems.append("startup chain imports a Python runtime: " + "; ".join(python_imports))
    if linked_paths:
        problems.append("package contains links: " + ", ".join(linked_paths))
    problems.extend(verify_examples(install_dir))

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
        if not tools_build_info.is_file():
            if not allow_dirty:
                raise RuntimeError(f"package verification failed: required tools build_info.json missing in {tools_pkg_dir}")
        else:
            try:
                verify_build_metadata(tools_build_info, expected_component="VisionTools", allow_dirty=allow_dirty)
            except RuntimeError as exc:
                raise RuntimeError(f"package verification failed: {exc}") from exc
        if validator is not None:
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

        previous = read_install_manifest(install_dir)
        if not previous:
            raise RuntimeError(
                f"install dir has no {MANIFEST_FILE}: {install_dir}; deploy a full package first so the "
                "ownership record has a baseline")
        ownership = InstallOwnership(install_dir, replaceable=set(previous))
        ext_index = resolve_project_path(args.tools_index) if args.tools_index else None
        deploy_tools_package(
            install_dir,
            resolve_project_path(args.tools_package),
            validator=validator_path,
            external_index=ext_index,
            allow_dirty=args.allow_dirty,
            ownership=ownership,
        )
        # Only the tools subtree changed; everything else the previous package owned keeps its
        # claim, and files this deployment did not write are never added.
        new_files = {item for item in previous if not item.startswith("tools-package/")}
        new_files.update(item for item in ownership.owned if item.startswith("tools-package/"))
        write_install_manifest(install_dir, new_files)
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
    previous = clear_install_directory(install_dir, not args.no_clean, args.force_clean)
    ownership = InstallOwnership(install_dir, replaceable=set(previous))
    write_package_marker(install_dir, build_dir, version, ("state=incomplete",))

    packaged_exe = install_dir / source_exe.name
    owned_copy(source_exe, packaged_exe, ownership)
    copy_project_runtime(build_dir, install_dir, args.include_pdb, allow_dirty=args.allow_dirty,
                         ownership=ownership)
    copy_examples(install_dir, ownership)

    if args.tools_package:
        ext_index = resolve_project_path(args.tools_index) if args.tools_index else None
        deploy_tools_package(
            install_dir,
            resolve_project_path(args.tools_package),
            validator=validator_path,
            external_index=ext_index,
            allow_dirty=args.allow_dirty,
            ownership=ownership,
        )

    if args.skip_dependencies:
        print("skip manifest dependencies")
    elif dependency_file.is_file():
        copy_dependencies(build_dir, install_dir, dependency_file, args.config, args.allow_missing_dependencies,
                          ownership=ownership)
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
            copy_msvc_runtime(install_dir, arch, ownership)
            copy_windows_sdk_dependencies(install_dir, arch, ownership)
        deploy_qt_runtime(build_dir, install_dir, packaged_exe, args.windeployqt, args.skip_windeployqt,
                          ownership)
        write_qt_conf(install_dir, ownership)
        write_windows_marker(install_dir, build_dir, arch, version, args.skip_dependencies)
        if not args.skip_verify:
            verify_package(install_dir, not args.skip_windeployqt, validator=validator_path, allow_dirty=args.allow_dirty)
            evidence = resolve_project_path(args.evidence) if args.evidence else build_dir / "evidence" / "package-evidence.json"
            write_package_evidence(install_dir, evidence, arch)
        # Written only after verification: a failed run must not leave a record that claims files
        # the next upgrade may then delete.
        write_install_manifest(install_dir, ownership.manifest_entries())
        print(f"\npackage complete: {install_dir}")
        print(f"double-click to run: {packaged_exe}")
    else:
        write_qt_conf(install_dir, ownership)
        write_package_marker(install_dir, build_dir, version)
        if not args.skip_verify:
            verify_package(install_dir, not args.skip_windeployqt, validator=validator_path, allow_dirty=args.allow_dirty)
            evidence = resolve_project_path(args.evidence) if args.evidence else build_dir / "evidence" / "package-evidence.json"
            write_package_evidence(install_dir, evidence, target_architecture(build_dir))
        write_install_manifest(install_dir, ownership.manifest_entries())
        print(f"\npackage complete: {install_dir}")

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
