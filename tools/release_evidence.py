#!/usr/bin/env python3
"""Assembles the release evidence the acceptance report asks for.

The report requires one machine-readable record per release: the three commits
involved, the build facts, the package import chain, and the environment report
of every installed scenario. Collecting them by hand is how a release ends up
quoting a SHA nobody can reproduce, so this script refuses to write a complete
record when a required piece is missing or contradicts another.

It never runs the application. The reports it consumes are produced by
``VisionRT.exe --python-report``; the package evidence by ``package_app.py``.
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))

import package_app  # noqa: E402  (resolved through the path above)

# A report role states what the scenario is meant to prove. The release record
# is incomplete without "未配置" and one usable environment, because those are
# the two ends the host has to keep working without changing its own code.
REPORT_ROLES = ("unconfigured", "ready", "invalid", "failed")
REQUIRED_ROLES = ("unconfigured", "ready")


def git(*arguments: str, cwd: Path) -> str:
    completed = subprocess.run(
        ["git", *arguments],
        cwd=cwd,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if completed.returncode != 0:
        raise RuntimeError(f"git {' '.join(arguments)} failed in {cwd}: {completed.stderr.strip()}")
    return completed.stdout.strip()


def repository_facts(path: Path, contracts: str) -> dict:
    """The commit triple a release is signed against."""
    return {
        "path": str(path),
        "head": git("rev-parse", "HEAD", cwd=path),
        "dirty": bool(git("status", "--porcelain", cwd=path)),
        "contracts": git("rev-parse", "HEAD", cwd=path / contracts),
    }


def build_facts(build_dir: Path) -> dict:
    """Compiler and configuration actually used, not the ones the plan named."""
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        return {"buildDirectory": str(build_dir), "configured": False}
    values: dict[str, str] = {}
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("//") or "=" not in line or ":" not in line.split("=", 1)[0]:
            continue
        key, value = line.split("=", 1)
        values[key.split(":", 1)[0]] = value
    return {
        "buildDirectory": str(build_dir),
        "configured": True,
        "generator": values.get("CMAKE_GENERATOR", ""),
        "buildType": values.get("CMAKE_BUILD_TYPE", ""),
        "compiler": values.get("CMAKE_CXX_COMPILER", ""),
        "pythonExecutable": values.get("Python3_EXECUTABLE", ""),
    }


def normalize_path(value: str) -> str:
    return os.path.normcase(os.path.normpath(value.replace("\\", "/")))


def is_within(path: str, root: str) -> bool:
    if not path or not root:
        return False
    normalized_path = normalize_path(path)
    normalized_root = normalize_path(root)
    return normalized_path == normalized_root or normalized_path.startswith(normalized_root + os.sep)


def summarize_report(role: str, path: Path) -> dict:
    data = json.loads(path.read_text(encoding="utf-8"))
    environment = data.get("environment", {})
    process = data.get("process", {})
    return {
        "role": role,
        "path": str(path),
        "application": data.get("application", ""),
        "phase": environment.get("phase", ""),
        "selected": environment.get("selected", ""),
        "executable": environment.get("executable", ""),
        "version": environment.get("version", ""),
        "prefix": environment.get("prefix", ""),
        "basePrefix": environment.get("basePrefix", ""),
        "numpyVersion": environment.get("numpyVersion", ""),
        "numpyFile": environment.get("numpyFile", ""),
        "reason": environment.get("reason", ""),
        "runtime": process.get("runtime", ""),
        "bridge": bool(process.get("bridge")),
        "modules": [module.get("name", "") for module in process.get("modules", [])],
    }


def audit(evidence: dict, allow_dirty: bool, require_clean: bool) -> list[str]:
    """Everything that makes the record unusable as a release claim.

    Kept separate from collection so the same rules can be exercised without a
    build, a package or a git repository.
    """
    problems: list[str] = []

    repositories = evidence.get("repositories", {})
    visionrt = repositories.get("visionrt", {})
    visiontools = repositories.get("visiontools", {})
    if not visionrt.get("head") or not visiontools.get("head"):
        problems.append("缺少 VisionRT 或 VisionTools 的提交 SHA")
    elif visionrt.get("contracts") != visiontools.get("contracts"):
        problems.append(
            "两仓 Contracts 子模块不一致："
            f"VisionRT={visionrt.get('contracts', '')} VisionTools={visiontools.get('contracts', '')}"
        )
    for name, facts in (("VisionRT", visionrt), ("VisionTools", visiontools)):
        if facts.get("dirty") and require_clean:
            problems.append(f"{name} 工作区有未提交改动，不能作为发布证据（--allow-dirty 可仅作本地核对）")

    build = evidence.get("build", {})
    if not build.get("configured"):
        problems.append("构建目录没有 CMakeCache.txt，无法记录编译器与配置")

    package = evidence.get("package", {})
    if not package:
        problems.append("缺少安装包证据")
    else:
        if package.get("bundledPythonRuntime"):
            problems.append("安装包携带 Python 运行时：" + ", ".join(package["bundledPythonRuntime"]))
        if package.get("startupPythonImports"):
            problems.append("启动链隐式导入 Python：" + ", ".join(package["startupPythonImports"]))
        if not package.get("bridgePresent"):
            problems.append("安装包缺少 visionrt_python_bridge.dll")

    reports = evidence.get("pythonReports", [])
    roles = {report.get("role") for report in reports}
    for role in REQUIRED_ROLES:
        if role not in roles:
            problems.append(f"缺少 {role} 场景的 Python 环境报告")
    for report in reports:
        role = report.get("role", "")
        phase = report.get("phase", "")
        label = f"{role}（{report.get('path', '')}）"
        if role not in REPORT_ROLES:
            problems.append(f"未知场景角色：{label}")
        if role == "ready":
            if phase != "ready":
                problems.append(f"ready 场景实际为 {phase}：{label}；{report.get('reason', '')}")
                continue
            runtime = report.get("runtime", "")
            if not runtime:
                problems.append(f"ready 场景没有实际加载的 python3*.dll：{label}")
            else:
                prefixes = (report.get("prefix", ""), report.get("basePrefix", ""))
                if not any(is_within(runtime, prefix) for prefix in prefixes if prefix):
                    problems.append(f"加载的运行时不在所选环境内：{runtime} 不属于 {prefixes}")
            numpy_file = report.get("numpyFile", "")
            if not numpy_file:
                problems.append(f"ready 场景没有 NumPy 来源：{label}")
            else:
                prefixes = (report.get("prefix", ""), report.get("basePrefix", ""))
                if not any(is_within(numpy_file, prefix) for prefix in prefixes if prefix):
                    problems.append(f"NumPy 不在所选环境内：{numpy_file} 不属于 {prefixes}")
            if not report.get("bridge"):
                problems.append(f"ready 场景没有加载桥接组件：{label}")
        if role == "invalid" and phase != "invalid":
            problems.append(f"invalid 场景实际为 {phase}：{label}")
        if role == "failed" and phase != "backendFailed":
            problems.append(f"failed 场景实际为 {phase}：{label}")
        if role in ("invalid", "failed") and not report.get("reason"):
            problems.append(f"{role} 场景没有给出原因：{label}")
        if role == "unconfigured":
            if phase != "unconfigured":
                problems.append(f"unconfigured 场景实际为 {phase}：{label}")
            if report.get("runtime") or report.get("modules"):
                problems.append(f"未配置进程加载了 Python 模块：{label}")

    if allow_dirty and any(facts.get("dirty") for facts in repositories.values()):
        evidence.setdefault("notes", []).append("工作区存在未提交改动，此记录仅用于本地核对")
    return problems


def parse_report(argument: str) -> tuple[str, Path]:
    role, separator, value = argument.partition("=")
    if not separator or not value:
        raise argparse.ArgumentTypeError("报告的格式是 角色=路径，例如 ready=F:/tmp/report.json")
    if role not in REPORT_ROLES:
        raise argparse.ArgumentTypeError(f"未知场景角色：{role}（可用：{', '.join(REPORT_ROLES)}）")
    return role, Path(value)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--project", type=Path, default=REPO_ROOT, help="VisionRT 仓库根目录")
    parser.add_argument("--visiontools", type=Path, default=REPO_ROOT.parent / "VisionTools",
                        help="VisionTools 仓库根目录")
    parser.add_argument("--build", type=Path, default=REPO_ROOT / "build", help="VisionRT 构建目录")
    parser.add_argument("--package", type=Path, required=True, help="已部署的安装目录")
    parser.add_argument("--package-evidence", type=Path,
                        help="已有的 package-evidence.json；省略时重新从安装目录生成")
    parser.add_argument("--report", action="append", default=[], type=parse_report,
                        metavar="ROLE=PATH", help="python 环境报告；可重复")
    parser.add_argument("--output", type=Path, default=REPO_ROOT / "build/evidence/release-evidence.json")
    parser.add_argument("--architecture", default="x64")
    parser.add_argument("--allow-dirty", action="store_true",
                        help="记录未提交改动；记录会标注仅用于本地核对")
    args = parser.parse_args(argv)

    if not args.project.is_dir():
        parser.error(f"VisionRT 仓库不存在：{args.project}")
    if not args.visiontools.is_dir():
        parser.error(f"VisionTools 仓库不存在：{args.visiontools}（用 --visiontools 指定）")
    if not args.package.is_dir():
        parser.error(f"安装目录不存在：{args.package}")

    repositories = {}
    try:
        repositories = {
            "visionrt": repository_facts(args.project, "3rdparty/VisionRTContracts"),
            "visiontools": repository_facts(args.visiontools, "3rdparty/VisionRTContracts"),
        }
    except RuntimeError as error:
        print(f"无法读取提交信息：{error}", file=sys.stderr)
        return 2

    reports = []
    for role, path in args.report:
        if not path.is_file():
            parser.error(f"报告不存在：{path}")
        try:
            reports.append(summarize_report(role, path))
        except (OSError, json.JSONDecodeError) as error:
            print(f"无法读取报告 {path}：{error}", file=sys.stderr)
            return 2
    # A role may be evidenced more than once (two usable environments); the
    # caller's order is kept but duplicates are not hidden.
    reports.sort(key=lambda report: REPORT_ROLES.index(report["role"]))

    evidence_path = args.package_evidence
    try:
        if evidence_path is None:
            evidence_path = args.build / "evidence/package-evidence.json"
            package_app.write_package_evidence(args.package, evidence_path, args.architecture)
        package_evidence = json.loads(evidence_path.read_text(encoding="utf-8"))
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(f"无法生成安装包证据：{error}", file=sys.stderr)
        return 2

    record = {
        "schemaVersion": 1,
        "generatedAt": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "repositories": repositories,
        "build": build_facts(args.build),
        "package": {
            key: package_evidence.get(key)
            for key in (
                "version",
                "architecture",
                "fileCount",
                "bundledPythonRuntime",
                "startupPythonImports",
                "startupChain",
                "bridgePresent",
            )
        },
        "pythonReports": reports,
        "notes": [],
    }

    problems = audit(record, allow_dirty=args.allow_dirty, require_clean=not args.allow_dirty)
    record["complete"] = not problems
    record["problems"] = problems
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

    print(f"release evidence written: {args.output}")
    print(f"  VisionRT      {repositories['visionrt']['head'][:12]}"
          f"{' (dirty)' if repositories['visionrt']['dirty'] else ''}")
    print(f"  VisionTools   {repositories['visiontools']['head'][:12]}"
          f"{' (dirty)' if repositories['visiontools']['dirty'] else ''}")
    print(f"  Contracts     {repositories['visionrt']['contracts'][:12]}")
    for report in reports:
        print(f"  {report['role']:<12} {report['phase']:<13} {report['runtime'] or '-'}")
    if problems:
        print("\nrelease evidence is incomplete:", file=sys.stderr)
        for problem in problems:
            print(f"  - {problem}", file=sys.stderr)
        return 2
    print("\nrelease evidence complete")
    return 0


if __name__ == "__main__":
    sys.exit(main())
