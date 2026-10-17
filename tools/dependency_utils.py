"""Packaging and dependency management utility functions for VisionRT."""

from __future__ import annotations

import glob
import locale
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any, Iterable

import yaml

PROJECT_NAME = "VisionRT"
PROJECT_NAME_LONG = "VisionRT"
PROJECT_DLL_PREFIX = "VisionRT"
MARKER_FILE = ".visionrt_package"
CONFIG_DIR_NAMES = {"debug", "release", "relwithdebinfo", "minsizerel"}
BUILD_CONFIG_DIRECTORIES = {
    "debug": "Debug",
    "release": "Release",
    "relwithdebinfo": "RelWithDebInfo",
    "minsizerel": "MinSizeRel",
}
REPO_ROOT = Path(__file__).resolve().parents[1]


def warn(message: str) -> None:
    """Print warning message to stderr."""
    print(f"warning: {message}", file=sys.stderr)


def platform_key() -> str:
    """Return platform key for dependencies.yaml."""
    if os.name == "nt":
        return "windows"
    if sys.platform == "darwin":
        return "macos"
    if sys.platform.startswith("linux"):
        return "linux"
    return sys.platform


def resolve_project_path(path: str | Path, repo_root: Path = REPO_ROOT) -> Path:
    """Resolve path to absolute path relative to repo root if relative."""
    raw = Path(os.path.expanduser(str(path)))
    if raw.is_absolute():
        return raw.resolve(strict=False)
    return (repo_root / raw).resolve(strict=False)


def normalize_list(value: Any) -> list[str]:
    """Convert scalar or list in YAML into a list of strings."""
    if value is None:
        return []
    if isinstance(value, list):
        return [str(item) for item in value]
    return [str(value)]


def load_yaml_file(path: Path) -> dict[str, Any]:
    """Read YAML file and verify top-level object is a dict."""
    data = yaml.safe_load(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError(f"invalid YAML root in {path}")
    return data


def load_dependencies(path: Path) -> list[dict[str, Any]]:
    """Read dependencies list from dependencies.yaml."""
    data = load_yaml_file(path)
    deps = data.get("dependencies", [])
    if not isinstance(deps, list):
        raise ValueError(f"dependencies must be a list in {path}")
    return [dep for dep in deps if isinstance(dep, dict)]


def dependency_matches_config(dep: dict[str, Any], config: str) -> bool:
    """Check if dependency applies to current build config."""
    dep_config = str(dep.get("config", "all")).lower()
    return dep_config in {"", "all", "any"} or dep_config == config.lower()


def dependency_patterns(dep: dict[str, Any], platform: str) -> list[str]:
    """Return file patterns for current platform."""
    return normalize_list(dep.get("all")) + normalize_list(dep.get(platform))


def dependency_destinations(dep: dict[str, Any]) -> list[str]:
    """Return link destination directories."""
    return normalize_list(dep.get("destinations", dep.get("dest")))


def build_config_directory(config: str) -> str:
    """Return directory name corresponding to build config."""
    return BUILD_CONFIG_DIRECTORIES.get(config.lower(), config)


def dependency_default_value(dep: dict[str, Any], platform: str | None = None) -> str | None:
    """Return default dependency path for platform with fallback to default."""
    if platform is None:
        platform = platform_key()
    platform_value = dep.get(f"{platform}_default")
    if platform_value not in (None, ""):
        return str(platform_value)
    fallback = dep.get("default")
    return None if fallback in (None, "") else str(fallback)


def is_direct_root(value: str) -> bool:
    """Check if root field is already a path rather than CMake variable."""
    return (
        ":" in value
        or "/" in value
        or "\\" in value
        or value.startswith(".")
        or value.startswith("~")
    )


def read_cmake_cache_value(cache_file: Path, key: str) -> str | None:
    """Read variable value from CMakeCache.txt."""
    if not cache_file.exists():
        return None
    pattern = re.compile(rf"^{re.escape(key)}(?::[^=]+)?=(.*)$", re.IGNORECASE)
    for line in cache_file.read_text(encoding="utf-8", errors="ignore").splitlines():
        match = pattern.match(line.strip())
        if match:
            value = match.group(1).strip()
            return None if value.endswith("-NOTFOUND") else value
    return None


def read_cmake_set(cmake_file: Path, name: str) -> str | None:
    """Read variable value from simple CMake set(...) statement."""
    if not cmake_file.exists():
        return None
    pattern = re.compile(rf"^\s*set\s*\(\s*{re.escape(name)}\s+(.+?)\s*\)", re.IGNORECASE)
    for line in cmake_file.read_text(encoding="utf-8", errors="ignore").splitlines():
        match = pattern.match(line)
        if not match:
            continue
        rest = match.group(1).strip()
        if rest.startswith('"'):
            end = rest.find('"', 1)
            if end >= 0:
                return rest[1:end]
        return rest.split()[0].strip('"')
    return None


def read_cmake_set_expanded(cmake_file: Path, name: str) -> str | None:
    """Read CMake variable and expand limited levels of ${VAR} references."""
    value = read_cmake_set(cmake_file, name)
    if not value:
        return None
    for _ in range(8):
        match = re.search(r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}", value)
        if not match:
            break
        ref_value = read_cmake_set(cmake_file, match.group(1))
        if not ref_value:
            break
        value = value.replace(match.group(0), ref_value)
    return value


def _candidate_env_names(dep: dict[str, Any], root_spec: str) -> list[str]:
    names = [root_spec]
    names.extend(normalize_list(dep.get("env")))
    unique: list[str] = []
    seen: set[str] = set()
    for name in names:
        key = name.upper()
        if key not in seen:
            unique.append(name)
            seen.add(key)
    return unique


def resolve_dependency_root(
    dep: dict[str, Any],
    build_dir: Path,
    repo_root: Path = REPO_ROOT,
    platform: str | None = None,
) -> Path | None:
    """Resolve dependency root directory from paths, env, cache, and defaults."""
    if platform is None:
        platform = platform_key()

    platform_root = dep.get(f"{platform}_root") if platform else None
    root_spec = str(platform_root if platform_root not in (None, "") else dep.get("root", "")).strip()
    if not root_spec:
        return None

    if is_direct_root(root_spec) or not dep.get("cmake"):
        return resolve_project_path(root_spec, repo_root)

    for env_name in _candidate_env_names(dep, root_spec):
        value = os.environ.get(env_name)
        if value:
            return resolve_project_path(value, repo_root)

    if platform_root not in (None, ""):
        cache_value = read_cmake_cache_value(build_dir / "CMakeCache.txt", root_spec)
        if cache_value:
            cache_root = resolve_project_path(cache_value, repo_root)
            if cache_root.exists():
                return cache_root

    default_value = dependency_default_value(dep, platform)
    if default_value:
        candidate = resolve_project_path(str(default_value), repo_root)
        if candidate.exists():
            return candidate

    cache_value = read_cmake_cache_value(build_dir / "CMakeCache.txt", root_spec)
    if cache_value:
        return resolve_project_path(cache_value, repo_root)

    if default_value:
        return resolve_project_path(str(default_value), repo_root)

    cmake_file = resolve_project_path(str(dep["cmake"]), repo_root)
    cmake_value = read_cmake_set_expanded(cmake_file, root_spec)
    if cmake_value:
        return resolve_project_path(cmake_value, repo_root)

    return None


def expand_dependency_pattern(root: Path, pattern: str, config: str = "release") -> list[Path]:
    """Expand pattern under root directory."""
    build_config = build_config_directory(config)
    rendered_pattern = pattern.replace("{build_config}", build_config)
    full_pattern = str(root / rendered_pattern)
    matches = [Path(match).resolve(strict=False) for match in glob.glob(full_pattern)]
    if matches:
        return [path for path in matches if path.is_file()]
    if "*" not in rendered_pattern and "?" not in rendered_pattern:
        candidate = (root / rendered_pattern).resolve(strict=False)
        if candidate.is_file():
            return [candidate]
    return []


def copy_file(source: Path, destination: Path) -> None:
    """Copy file and preserve metadata."""
    source = source.resolve(strict=True)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    print(f"copy {source} -> {destination}")


def remove_existing_file(path: Path) -> None:
    """Remove file or link, refuse deleting real directory."""
    if path.exists() or path.is_symlink():
        if path.is_dir() and not path.is_symlink():
            raise RuntimeError(f"existing path is a directory, refusing to overwrite as file link: {path}")
        path.unlink()


def remove_existing_dir_link(path: Path) -> None:
    """Remove directory link or empty placeholder directory."""
    if not path.exists() and not path.is_symlink():
        return
    is_junction = bool(getattr(path, "is_junction", lambda: False)())
    if not is_junction and os.name == "nt":
        try:
            st = os.stat(path, follow_symlinks=False)
            is_junction = bool(getattr(st, "st_file_attributes", 0) & 0x400)
        except OSError:
            pass
    if is_junction:
        path.rmdir()
        return
    if path.is_symlink():
        path.unlink()
        return
    if path.is_dir() and not any(path.iterdir()):
        path.rmdir()
        return
    raise RuntimeError(f"existing path is not a link, refusing to overwrite: {path}")


def link_file(source: Path, link: Path) -> None:
    """Create symlink, fallback to hardlink, fallback to copy."""
    source = source.resolve(strict=True)
    link.parent.mkdir(parents=True, exist_ok=True)
    remove_existing_file(link)
    try:
        os.symlink(source, link)
        print(f"create symlink {link} -> {source}")
    except OSError:
        try:
            os.link(source, link)
            print(f"create hardlink {link} -> {source}")
        except OSError:
            shutil.copy2(source, link)
            print(f"copy file {link} -> {source}")


def link_dir(source: Path, link: Path) -> None:
    """Create directory link, fallback to junction on Windows."""
    source = source.resolve(strict=True)
    link.parent.mkdir(parents=True, exist_ok=True)
    remove_existing_dir_link(link)
    try:
        os.symlink(source, link, target_is_directory=True)
        print(f"create symlink {link} -> {source}")
    except OSError:
        if os.name != "nt":
            raise
        result = subprocess.run(
            ["cmd", "/c", "mklink", "/J", str(link), str(source)],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding=locale.getpreferredencoding(False),
            errors="replace",
        )
        if result.returncode != 0:
            raise RuntimeError(result.stdout.strip() or f"failed to create junction {link} -> {source}")
        print(f"create junction {link} -> {source}")


def build_dll_variant_sets(paths: Iterable[Path]) -> tuple[set[str], set[str]]:
    """Identify paired debug/release DLL names."""
    names = {path.name.lower() for path in paths if path.suffix.lower() == ".dll"}
    debug_names: set[str] = set()
    release_names: set[str] = set()
    for name in names:
        if name.endswith("_debug.dll"):
            release_name = f"{name[:-10]}.dll"
        elif name.endswith("d.dll"):
            release_name = f"{name[:-5]}.dll"
        else:
            continue
        if release_name in names:
            debug_names.add(name)
            release_names.add(release_name)
    return debug_names, release_names


def dll_matches_config(path: Path, config: str, debug_names: set[str], release_names: set[str]) -> bool:
    """Check if DLL matches target config (filter paired debug DLLs in release)."""
    name = path.name.lower()
    if not name.endswith(".dll"):
        return True
    if config.lower() == "debug":
        return name not in release_names
    if name in debug_names:
        return False
    return True


def is_project_dll(path: Path) -> bool:
    """Check if DLL is a project DLL."""
    name = path.name.lower()
    return name.startswith(PROJECT_DLL_PREFIX.lower()) and name.endswith(".dll")


def immediate_project_dlls(bin_dir: Path) -> list[Path]:
    """List project DLLs under bin directory."""
    if not bin_dir.is_dir():
        return []
    return sorted(path for path in bin_dir.glob(f"{PROJECT_DLL_PREFIX}*.dll") if path.is_file())


def should_skip_config_dir(path: Path) -> bool:
    """Check if path is inside configuration directory like Debug/Release."""
    return any(part.lower() in CONFIG_DIR_NAMES for part in path.parts)
