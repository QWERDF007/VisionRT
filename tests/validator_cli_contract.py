import argparse
import copy
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--validator", required=True)
    parser.add_argument("--opencv-bin", default="")
    parser.add_argument("--module", required=True)
    parser.add_argument("--bad-module", required=True)
    args = parser.parse_args()
    environment = os.environ.copy()
    environment["PATH"] = args.opencv_bin + os.pathsep + environment["PATH"]
    validator = str(Path(args.validator).resolve())

    with tempfile.TemporaryDirectory(prefix="visionrt JSON 中文 ") as temporary:
        root = Path(temporary)

        def write(name, value):
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(value, ensure_ascii=False), encoding="utf-8")
            return str(path)

        def run(*arguments, valid=True):
            result = subprocess.run(
                [validator, *arguments], env=environment, capture_output=True,
                text=True, encoding="utf-8", errors="replace"
            )
            assert (result.returncode == 0) == valid, (arguments, result.returncode, result.stdout, result.stderr)
            return result

        tool = {
            "schema_version": 2, "tool_id": "test.cli", "display_name": "校验工具",
            "version": "1", "kind": "cpp", "init_params": [],
            "inputs": [{"id": "count", "name": "数量", "type": "Int", "default": 9007199254740993}],
            "outputs": [{"id": "value", "name": "值", "type": "Int"}]
        }
        path = write("tools/builtin/工具.json", tool)
        run(path)
        normalized = json.loads(run("--emit-normalized-json", path).stdout)
        assert normalized["inputs"][0]["default"] == 9007199254740993
        assert len(normalized["outputs"]) == 1 and "entry" not in normalized
        bad = copy.deepcopy(tool)
        bad["inputs"][0]["required"] = "false"
        run(write("invalid.json", bad), valid=False)
        bad = copy.deepcopy(tool)
        bad["outputs"][0]["id"] = "__status_code"
        run(write("invalid.json", bad), valid=False)
        run(write("legacy.yaml", tool), valid=False)

        index = {
            "schema_version": 1, "variant": "cpu",
            "modules": [{"id": "module", "library": "module.dll"}],
            "tools": [{"tool_id": "test.cli", "definition": "tools/builtin/工具.json", "module": "module"}],
            "runtime_files": []
        }
        index_path = write("module_index.json", index)
        run("--index", index_path)
        run(index_path)
        run("--package", str(root), valid=False)
        (root / "module.dll").write_bytes(b"delivery validation checks file presence, not DLL execution")
        run("--package", str(root))
        run("--load", "--package", str(root), valid=False)
        shutil.copyfile(args.module, root / "module.dll")
        native_tool = copy.deepcopy(tool)
        native_tool["tool_id"] = "test.registration.first"
        native_index = copy.deepcopy(index)
        native_index["tools"][0]["tool_id"] = native_tool["tool_id"]
        write("tools/builtin/工具.json", native_tool)
        write("module_index.json", native_index)
        loaded = run("--package", str(root), "--load")
        assert "Native factory available: test.registration.first" in loaded.stdout
        native_tool["tool_id"] = "test.missing.factory"
        native_index["tools"][0]["tool_id"] = native_tool["tool_id"]
        write("tools/builtin/工具.json", native_tool)
        write("module_index.json", native_index)
        run("--package", str(root))
        missing = run("--package", str(root), "--load", valid=False)
        assert "test.missing.factory" in missing.stderr
        native_tool["tool_id"] = "test.registration.first"
        native_index["tools"][0]["tool_id"] = native_tool["tool_id"]
        write("tools/builtin/工具.json", native_tool)
        write("module_index.json", native_index)
        shutil.copyfile(args.bad_module, root / "module.dll")
        run("--package", str(root))
        incompatible = run("--load", "--package", str(root), valid=False)
        assert "ABI" in incompatible.stderr
        write("tools/builtin/工具.json", tool)
        write("module_index.json", index)
        run("--load", path, valid=False)
        run("--unknown", path, valid=False)
        run("--package", str(root), "--index", index_path, valid=False)
        bad = copy.deepcopy(index)
        bad["tools"].append(bad["tools"][0])
        run("--index", write("duplicate.json", bad), valid=False)
        bad = copy.deepcopy(index)
        bad["tools"][0]["host_binding"] = "visionrt.receiver"
        run("--index", write("bad.json", bad), valid=False)
        bad = copy.deepcopy(index)
        bad["modules"][0]["library"] = "../escape.dll"
        run("--index", write("bad.json", bad), valid=False)
        bad = copy.deepcopy(tool)
        bad["tool_id"] = "different.id"
        write("tools/builtin/工具.json", bad)
        run("--package", str(root), valid=False)
        write("tools/builtin/工具.json", tool)
        (root / "module_index.json").unlink()
        write("module_index.yaml", index)
        run("--package", str(root), valid=False)
        run(valid=False)
    print("JSON validator declaration, normalization, index and delivery modes verified")


if __name__ == "__main__":
    main()
