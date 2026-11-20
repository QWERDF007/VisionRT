"""PE dependency closure and product module catalog."""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import re
import struct

from dependency_utils import load_json_file

MODULE_CATALOG = "visionrt-modules.json"


@dataclass(frozen=True)
class ProductModule:
    component: str
    library: str
    metadata: str


def load_modules(directory: Path) -> list[ProductModule]:
    data = load_json_file(directory / MODULE_CATALOG)
    if data["schema_version"] != 1:
        raise RuntimeError(f"unsupported module catalog: {directory / MODULE_CATALOG}")
    modules = [ProductModule(**entry) for entry in data["modules"]]
    for module in modules:
        if any(Path(name).name != name for name in (module.library, module.metadata)):
            raise RuntimeError(f"module catalog must use file names: {module.component}")
    return modules


def runtime_closure(directory: Path, roots: list[Path]) -> list[Path]:
    available = {path.name.lower(): path for path in directory.glob("*.dll")}
    visited: set[Path] = set()
    pending = list(roots)
    while pending:
        current = pending.pop(0)
        if current in visited:
            continue
        visited.add(current)
        imports, delayed = read_pe_imports(current)
        for name in imports + delayed:
            if PYTHON_RUNTIME_IMPORT.match(name):
                continue
            dependency = available.get(name.lower())
            if dependency is not None:
                pending.append(dependency)
            elif name.lower().startswith("visionrt_"):
                raise RuntimeError(f"missing runtime dependency: {current.name} imports {name}")
    return sorted(visited)

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
