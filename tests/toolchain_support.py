from __future__ import annotations

import os
from pathlib import Path
import shutil


def find_git() -> str:
    """Return the git executable, tolerating a test process started outside a developer shell."""
    found = shutil.which("git")
    if found:
        return found
    for candidate in (
        r"D:\Software\Git\cmd\git.exe",
        r"C:\Program Files\Git\cmd\git.exe",
        r"C:\Program Files (x86)\Git\cmd\git.exe",
    ):
        if Path(candidate).is_file():
            return candidate
    return "git"


def find_cmake() -> str:
    found = os.environ.get("CMAKE_COMMAND") or shutil.which("cmake")
    if found:
        return found
    for candidate in (
        r"C:\Program Files\CMake\bin\cmake.exe",
        r"D:\Software\CMake\bin\cmake.exe",
    ):
        if Path(candidate).is_file():
            return candidate
    return "cmake"
