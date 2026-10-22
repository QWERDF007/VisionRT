#!/usr/bin/env python3
"""
CLI contract tests for visionrt_validate_tool.
Verifies tool yaml, --index, and --package modes against valid and negative cases,
including paths containing spaces and Chinese characters.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

def run_cmd(args, env=None):
    return subprocess.run(args, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--validator", required=True, help="Path to visionrt_validate_tool binary")
    parser.add_argument("--opencv-bin", default="", help="Path to OpenCV bin directory")
    args = parser.parse_args()

    validator_bin = Path(args.validator).resolve()
    if not validator_bin.is_file():
        print(f"Validator binary not found: {validator_bin}", file=sys.stderr)
        return 1

    test_env = os.environ.copy()
    if args.opencv_bin and Path(args.opencv_bin).is_dir():
        test_env["PATH"] = str(Path(args.opencv_bin).resolve()) + os.pathsep + test_env["PATH"]

    temp_dir_obj = tempfile.TemporaryDirectory(prefix="vrt_cli_test_")
    base_dir = Path(temp_dir_obj.name).resolve()

    try:
        # 1. Single tool validation
        valid_tool_yaml = base_dir / "valid_tool.yaml"
        valid_tool_yaml.write_text(
            """schema_version: 1
tool_id: test.cli.pass
display_name: CLI Pass Tool
version: 1.0.0
kind: cpp_function
entry:
  registry_name: test.cli.pass
init_params: []
inputs:
  - id: count
    name: 数量
    type: Int
    default: 1
outputs: []
""",
            encoding="utf-8"
        )
        res = run_cmd([str(validator_bin), str(valid_tool_yaml)], env=test_env)
        assert res.returncode == 0, f"Expected 0 for valid tool yaml, got {res.returncode}. Output:\n{res.stdout}\n{res.stderr}"

        # 1b. Invalid tool validation
        invalid_tool_yaml = base_dir / "invalid_tool.yaml"
        invalid_tool_yaml.write_text(
            """schema_version: 2
tool_id: test.cli.fail
""",
            encoding="utf-8"
        )
        res = run_cmd([str(validator_bin), str(invalid_tool_yaml)], env=test_env)
        assert res.returncode != 0, f"Expected non-zero for invalid schema_version, got {res.returncode}"

        # 2. Module index validation (--index)
        valid_index_yaml = base_dir / "module_index.yaml"
        valid_index_yaml.write_text(
            """schema_version: 2
variant: cpu
modules:
  mod_test:
    library: bin/mod_test.dll
tools:
  test.cli.pass:
    module: mod_test
    definition: tools/tool.yaml
runtime_files: []
""",
            encoding="utf-8"
        )
        res = run_cmd([str(validator_bin), "--index", str(valid_index_yaml)], env=test_env)
        assert res.returncode == 0, f"Expected 0 for valid index, got {res.returncode}. Output:\n{res.stdout}\n{res.stderr}"

        # 2b. Negative index: dangling module
        dangling_index_yaml = base_dir / "dangling_index.yaml"
        dangling_index_yaml.write_text(
            """schema_version: 2
variant: cpu
modules:
  mod_test:
    library: bin/mod_test.dll
tools:
  test.cli.pass:
    module: nonexistent_mod
    definition: tools/tool.yaml
""",
            encoding="utf-8"
        )
        res = run_cmd([str(validator_bin), "--index", str(dangling_index_yaml)], env=test_env)
        assert res.returncode != 0, f"Expected non-zero for dangling module index, got {res.returncode}"

        # 2c. Negative index: duplicate key
        dup_key_yaml = base_dir / "dup_key_index.yaml"
        dup_key_yaml.write_text(
            """schema_version: 2
variant: cpu
variant: gpu
modules: {}
tools: {}
""",
            encoding="utf-8"
        )
        res = run_cmd([str(validator_bin), "--index", str(dup_key_yaml)], env=test_env)
        assert res.returncode != 0, f"Expected non-zero for duplicate key index, got {res.returncode}"

        # 3. Package validation (--package) including Chinese and space paths
        pkg_dir = base_dir / "测试 目录" / "工具 包 1.0"
        (pkg_dir / "bin").mkdir(parents=True, exist_ok=True)
        (pkg_dir / "tools").mkdir(parents=True, exist_ok=True)

        (pkg_dir / "bin" / "mod_test.dll").write_bytes(b"dummy dll")
        (pkg_dir / "bin" / "opencv_core.dll").write_bytes(b"dummy opencv")

        pkg_tool_yaml = pkg_dir / "tools" / "tool.yaml"
        pkg_tool_yaml.write_text(
            """schema_version: 1
tool_id: test.pkg.tool
display_name: 中文路径工具
version: 1.0.0
kind: cpp_function
entry:
  registry_name: test.pkg.tool
init_params: []
inputs: []
outputs: []
""",
            encoding="utf-8"
        )

        pkg_index_yaml = pkg_dir / "module_index.yaml"
        pkg_index_yaml.write_text(
            """schema_version: 2
variant: cpu
modules:
  mod_test:
    library: bin/mod_test.dll
tools:
  test.pkg.tool:
    module: mod_test
    definition: tools/tool.yaml
runtime_files:
  - bin/opencv_core.dll
""",
            encoding="utf-8"
        )

        res = run_cmd([str(validator_bin), "--package", str(pkg_dir)], env=test_env)
        assert res.returncode == 0, f"Expected 0 for valid package with Chinese/space path, got {res.returncode}. Output:\n{res.stdout}\n{res.stderr}"

        # 3b. Negative package: missing runtime file
        (pkg_dir / "bin" / "opencv_core.dll").unlink()
        res = run_cmd([str(validator_bin), "--package", str(pkg_dir)], env=test_env)
        assert res.returncode != 0, f"Expected non-zero for missing runtime_files in package, got {res.returncode}"

        # 3c. Negative package: missing definition file
        (pkg_dir / "bin" / "opencv_core.dll").write_bytes(b"dummy")
        pkg_tool_yaml.unlink()
        res = run_cmd([str(validator_bin), "--package", str(pkg_dir)], env=test_env)
        assert res.returncode != 0, f"Expected non-zero for missing definition in package, got {res.returncode}"

        # 4. Normalized YAML export: complete constraints, stable ordering, relative entry.
        normalized_dir = base_dir / "normalized 目录"
        normalized_dir.mkdir(parents=True, exist_ok=True)
        tool_a = normalized_dir / "tool_a.yaml"
        tool_b = normalized_dir / "tool_b.yaml"
        tool_a.write_text(
            """schema_version: 1
tool_id: test.cli.normalize
display_name: 规范化工具
version: 2.0.0
kind: cpp_class
category: 测试
subcategory: 子类
entry: {registry_name: test.cli.normalize}
resources: [data/模型.bin]
init_params:
  - {id: source_flow, name: 源流程, type: String, editor: text}
  - {id: source_node, name: 源节点, type: String, editor: text}
  - {id: source_port, name: 源端口, type: String, editor: text}
inputs:
  - {id: image, name: 图像, type: ImageBatch}
  - {id: threshold, name: 阈值, type: Int, default: 5, min: 0, max: 10, step: 1, options: [5, 7], tab: 预处理, group: 基础}
  - {id: ratio, name: 比例, type: Float, default: 0.5, min: 0.0, max: 1.0, step: 0.1, decimals: 2}
  - {id: mode, name: 模式, type: String, editor: text}
  - {id: file, name: 文件, type: String, editor: path, path_mode_from: mode}
outputs:
  - {id: result, name: 结果, type: Int}
  - {id: typed, name: 派生, type_from: {flow: source_flow, node: source_node, port: source_port}}
""",
            encoding="utf-8"
        )
        tool_b.write_text(
            """kind: cpp_class
entry: {registry_name: test.cli.normalize}
version: 2.0.0
tool_id: test.cli.normalize
display_name: 规范化工具
schema_version: 1
category: 测试
subcategory: 子类
outputs:
  - {name: 结果, type: Int, id: result}
  - {type_from: {port: source_port, node: source_node, flow: source_flow}, id: typed, name: 派生}
inputs:
  - {name: 图像, type: ImageBatch, id: image}
  - {tab: 预处理, options: [5, 7], step: 1, max: 10, min: 0, default: 5, type: Int, name: 阈值, id: threshold, group: 基础}
  - {decimals: 2, step: 0.1, max: 1.0, min: 0.0, default: 0.5, type: Float, name: 比例, id: ratio}
  - {editor: text, type: String, name: 模式, id: mode}
  - {path_mode_from: mode, editor: path, type: String, name: 文件, id: file}
init_params:
  - {name: 源流程, type: String, id: source_flow, editor: text}
  - {editor: text, id: source_node, type: String, name: 源节点}
  - {type: String, id: source_port, name: 源端口, editor: text}
resources: [data/模型.bin]
""",
            encoding="utf-8"
        )

        res_a = run_cmd([str(validator_bin), "--emit-normalized-yaml", str(tool_a)], env=test_env)
        assert res_a.returncode == 0, f"Expected normalized export to succeed. Output:\n{res_a.stdout}\n{res_a.stderr}"
        assert res_a.stderr == "", f"Normalized export must not write diagnostics on success: {res_a.stderr}"
        res_b = run_cmd([str(validator_bin), "--emit-normalized-yaml", str(tool_b)], env=test_env)
        assert res_b.returncode == 0, f"Expected reordered normalized export to succeed. Output:\n{res_b.stdout}\n{res_b.stderr}"
        assert res_a.stdout == res_b.stdout, "Normalized output must be stable across reordered keys"

        normalized_tool = normalized_dir / "normalized.yaml"
        normalized_tool.write_text(res_a.stdout, encoding="utf-8")
        res = run_cmd([str(validator_bin), str(normalized_tool)], env=test_env)
        assert res.returncode == 0, f"Normalized output must validate. Output:\n{res.stdout}\n{res.stderr}"

        import yaml
        model = yaml.safe_load(res_a.stdout)
        ports = {port["id"]: port for port in model["inputs"]}
        assert ports["threshold"]["default"] == 5
        assert ports["threshold"]["min"] == 0 and ports["threshold"]["max"] == 10
        assert ports["threshold"]["options"] == [5, 7]
        assert ports["threshold"]["tab"] == "预处理"
        assert ports["threshold"]["group"] == "基础"
        assert ports["ratio"]["decimals"] == 2 and ports["ratio"]["step"] == 0.1
        assert ports["file"]["editor"] == "path" and ports["file"]["path_mode_from"] == "mode"
        assert model["outputs"][0]["id"] == "__status_code"
        typed = next(port for port in model["outputs"] if port["id"] == "typed")
        assert typed["type_from"] == {"flow": "source_flow", "node": "source_node", "port": "source_port"}
        assert model["resources"] == ["data/模型.bin"]

        python_dir = normalized_dir / "python"
        python_dir.mkdir()
        (python_dir / "helper.py").write_text("def execute():\n    return 1\n", encoding="utf-8")
        python_tool = python_dir / "tool.yaml"
        python_tool.write_text(
            """schema_version: 1
tool_id: test.cli.python
display_name: Python规范化
version: 1.0.0
kind: python_function
entry: {file: helper.py, symbol: execute}
init_params: []
inputs: []
outputs: []
""",
            encoding="utf-8"
        )
        res = run_cmd([str(validator_bin), "--emit-normalized-yaml", str(python_tool)], env=test_env)
        assert res.returncode == 0, f"Expected Python normalized export to succeed: {res.stderr}"
        assert yaml.safe_load(res.stdout)["entry"]["file"] == "helper.py", "Python entry must stay relative"

        invalid_normalized = normalized_dir / "invalid.yaml"
        invalid_normalized.write_text("schema_version: 2\ntool_id: test.cli.invalid\n", encoding="utf-8")
        res = run_cmd([str(validator_bin), "--emit-normalized-yaml", str(invalid_normalized)], env=test_env)
        assert res.returncode != 0, "Invalid definition must fail normalized export"
        assert res.stdout == "", "Invalid definition must not emit a partial model"

        print("[CLI Test] All CLI contract tests passed successfully!")
        return 0

    finally:
        temp_dir_obj.cleanup()

if __name__ == "__main__":
    sys.exit(main())
