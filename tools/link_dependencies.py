"""VisionRT runtime dependency linking script for development.

Links external runtime libraries and Qt environment to build/bin
so developers can directly run build/bin/VisionRT.exe or test binaries.
"""

from __future__ import annotations

import argparse
import os
import shutil
import sys
from pathlib import Path

if str(Path(__file__).resolve().parent) not in sys.path:
    sys.path.insert(0, str(Path(__file__).resolve().parent))

from dependency_utils import (
    PROJECT_NAME,
    REPO_ROOT,
    build_dll_variant_sets,
    dependency_destinations,
    dependency_matches_config,
    dependency_patterns,
    dll_matches_config,
    expand_dependency_pattern,
    is_project_dll,
    link_dir,
    link_file,
    load_dependencies,
    platform_key,
    resolve_dependency_root,
    resolve_project_path,
    warn,
)


def parse_args() -> argparse.Namespace:
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(description="Link VisionRT runtime dependencies into build/bin.")
    parser.add_argument("--build-dir", "-BuildDir", default="build", help="CMake build directory.")
    parser.add_argument("--config", "-Config", choices=["release", "debug"], default="release", help="Build configuration.")
    parser.add_argument("--dependencies", default="tools/dependencies.json", help="Path to dependencies.json.")
    parser.add_argument("--skip-external", action="store_true", help="Skip linking external dependencies.")
    parser.add_argument("--skip-qt", action="store_true", help="Skip linking Qt DLLs and plugins.")
    return parser.parse_args()


def link_external_dependencies(build_dir: Path, dependency_file: Path, config: str) -> None:
    """Link external libraries according to dependencies.json."""
    platform = platform_key()
    dependencies = load_dependencies(dependency_file)
    for dep in dependencies:
        dep_name = str(dep.get("name", "<unnamed>")).lower()
        if dep_name in {"qt", "qt6"}:
            continue
        if not dependency_matches_config(dep, config):
            continue

        patterns = dependency_patterns(dep, platform)
        if not patterns:
            continue

        destinations = [resolve_project_path(dest) for dest in dependency_destinations(dep)]
        if not destinations:
            continue

        root = resolve_dependency_root(dep, build_dir)
        if root is None or not root.is_dir():
            warn(f"skip dependency {dep_name}, root was not found: {root or dep.get('root')}")
            continue

        matched: list[Path] = []
        for pattern in patterns:
            matches = expand_dependency_pattern(root, pattern, config)
            matched.extend(matches)

        debug_names, release_names = build_dll_variant_sets(matched)
        for runtime in matched:
            if platform == "windows" and is_project_dll(runtime):
                continue
            if platform == "windows" and not dll_matches_config(runtime, config, debug_names, release_names):
                continue
            for destination in destinations:
                link_file(runtime, destination / runtime.name)


def link_qt_runtime(build_dir: Path, dependency_file: Path) -> None:
    """Link Qt core DLLs, platform plugin, and qt.conf to build/bin."""
    bin_dir = build_dir / "bin"
    bin_dir.mkdir(parents=True, exist_ok=True)

    qt_dep = next((d for d in load_dependencies(dependency_file) if str(d.get("name", "")).lower() in {"qt", "qt6"}), None)
    if not qt_dep:
        return
    qt_root = resolve_dependency_root(qt_dep, build_dir)
    if not qt_root or not qt_root.is_dir():
        warn(f"skip Qt linking, Qt root not found: {qt_root}")
        return

    qt_bin = qt_root / "bin"
    qt_plugins = qt_root / "plugins"

    # 1. Essential Qt DLLs
    qt_dll_names = [
        "Qt6Core.dll",
        "Qt6Gui.dll",
        "Qt6Qml.dll",
        "Qt6QmlModels.dll",
        "Qt6QmlWorkerScript.dll",
        "Qt6Quick.dll",
        "Qt6QuickControls2.dll",
        "Qt6QuickControls2Impl.dll",
        "Qt6QuickDialogs2.dll",
        "Qt6QuickDialogs2QuickImpl.dll",
        "Qt6QuickDialogs2Utils.dll",
        "Qt6QuickEffects.dll",
        "Qt6QuickLayouts.dll",
        "Qt6QuickShapes.dll",
        "Qt6QuickTemplates2.dll",
        "Qt6Network.dll",
        "Qt6OpenGL.dll",
        "Qt6Svg.dll",
        "Qt6LabsFolderListModel.dll",
        "Qt6LabsQmlModels.dll",
        "opengl32sw.dll",
        "d3dcompiler_47.dll",
    ]
    for name in qt_dll_names:
        dll_path = qt_bin / name
        if dll_path.is_file():
            link_file(dll_path, bin_dir / name)

    # 2. Platforms plugin (platforms/qwindows.dll)
    platform_src = qt_plugins / "platforms" / "qwindows.dll"
    if platform_src.is_file():
        link_file(platform_src, bin_dir / "platforms" / "qwindows.dll")

    # 3. QuickUI QML module link & plugin DLL
    quickui_src = None
    for cand in (
        build_dir / "qml" / "quickui",
        REPO_ROOT / "build" / "qml" / "quickui",
        REPO_ROOT / "3rdparty" / "QuickUI" / "quickui",
    ):
        if cand.is_dir() and (cand / "qmldir").is_file():
            quickui_src = cand
            break

    if quickui_src:
        link_dir(quickui_src, bin_dir / "qml" / "quickui")
        if (bin_dir / "quickui.dll").exists():
            link_file(bin_dir / "quickui.dll", bin_dir / "qml" / "quickui" / "quickui.dll")
        if (build_dir / "qml" / "quickui").is_dir() and (bin_dir / "quickui.dll").exists():
            link_file(bin_dir / "quickui.dll", build_dir / "qml" / "quickui" / "quickui.dll")

    # 4. qt.conf in build/bin
    qt_conf_text = (
        "[Paths]\n"
        "Prefix=.\n"
        "Plugins=.\n"
        f"Qml2Imports={qt_root.as_posix()}/qml;qml\n"
        f"QmlImports={qt_root.as_posix()}/qml;qml\n"
    )
    (bin_dir / "qt.conf").write_text(qt_conf_text, encoding="utf-8")
    print(f"wrote {bin_dir / 'qt.conf'}")


def link_tools(build_dir: Path) -> None:
    """Link assets/tools into build/bin/tools for local development and testing."""
    tools_src = REPO_ROOT / "assets" / "tools"
    if not tools_src.is_dir():
        warn(f"skip tools link, missing {tools_src}")
        return

    bin_dir = build_dir / "bin"
    tools_dest = bin_dir / "tools"
    if tools_dest.is_dir() and not tools_dest.is_symlink():
        is_junction = False
        if os.name == "nt":
            try:
                st = os.stat(tools_dest, follow_symlinks=False)
                is_junction = bool(getattr(st, "st_file_attributes", 0) & 0x400)
            except OSError:
                pass
        if not is_junction:
            shutil.rmtree(tools_dest)
    link_dir(tools_src, tools_dest)
    print(f"linked {tools_src} -> {tools_dest}")


def main() -> int:
    """Main link entry point."""
    args = parse_args()
    build_dir = resolve_project_path(args.build_dir)
    dependency_file = resolve_project_path(args.dependencies)

    if not build_dir.is_dir():
        raise RuntimeError(f"build dir does not exist: {build_dir}")
    if not dependency_file.is_file():
        raise RuntimeError(f"dependency file does not exist: {dependency_file}")

    if not args.skip_external:
        link_external_dependencies(build_dir, dependency_file, args.config)

    if not args.skip_qt and platform_key() == "windows":
        link_qt_runtime(build_dir, dependency_file)

    link_tools(build_dir)

    print(f"\ndependencies linked to {build_dir / 'bin'}")
    print(f"you can now run: {build_dir / 'bin' / (PROJECT_NAME + '.exe')}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
