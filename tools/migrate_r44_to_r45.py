#!/usr/bin/env python3
"""One-off migration script for R44 to R45 (Schema 6 / Project Format 7).

Converts legacy string port references in project files (.vrt.json) to numeric PortId references.
Preserves original files, reports missing mappings explicitly, and conforms to AGENTS.md and R45 specifications.
"""

import argparse
import copy
import json
import os
import pathlib
import sys
from typing import Any, Dict, List, Optional, Tuple


STANDARD_TOOL_PORT_MAPS = {
    "visionrt.image.read": {
        "init": {},
        "inputs": {
            "path": 1,
            "recursive": 2,
            "traversal": 3,
            "order": 4,
            "count": 5,
            "reload": 6,
        },
        "outputs": {
            "__status_code": 0,
            "__status_message": 1,
            "images": 2,
            "paths": 3,
            "current_index": 4,
            "total_count": 5,
            "end_reached": 6,
        },
    },
    "visionrt.roi.projection": {
        "init": {},
        "inputs": {
            "images": 1,
            "axis": 2,
            "polarity": 3,
            "threshold": 4,
            "min_pixels": 5,
            "min_extent": 6,
            "merge_gap": 7,
            "padding": 8,
        },
        "outputs": {
            "__status_code": 0,
            "__status_message": 1,
            "images": 2,
            "rois": 3,
        },
    },
    "visionrt.image.crop": {
        "init": {},
        "inputs": {
            "images": 1,
            "rect": 2,
        },
        "outputs": {
            "__status_code": 0,
            "__status_message": 1,
            "images": 2,
            "origins": 3,
        },
    },
    "visionrt.image.preprocess": {
        "init": {},
        "inputs": {
            "images": 1,
            "operation": 2,
            "kernel": 3,
            "threshold": 4,
        },
        "outputs": {
            "__status_code": 0,
            "__status_message": 1,
            "images": 2,
        },
    },
    "visiontools.preprocess.adaptive_binary": {
        "init": {},
        "inputs": {
            "images": 1,
            "block_size": 2,
            "constant_offset": 3,
        },
        "outputs": {
            "__status_code": 0,
            "__status_message": 1,
            "images": 2,
        },
    },
    "visiontools.preprocess.image_brightness": {
        "init": {},
        "inputs": {
            "images": 1,
            "delta": 2,
        },
        "outputs": {
            "__status_code": 0,
            "__status_message": 1,
            "images": 2,
        },
    },
    "visiontools.preprocess.image_rotate": {
        "init": {},
        "inputs": {
            "images": 1,
            "angle": 2,
            "expand": 3,
        },
        "outputs": {
            "__status_code": 0,
            "__status_message": 1,
            "images": 2,
        },
    },
    "visionrt.dl.yolo": {
        "init": {
            "model_name": 0,
            "mode": 1,
            "load_mode": 1,
            "model_path": 2,
            "engine_output_path": 3,
            "label_path": 4,
            "labels_path": 4,
            "precision": 5,
            "device_id": 6,
            "gpu_index": 6,
            "input_width": 7,
            "input_height": 8,
            "num_classes": 9,
        },
        "inputs": {
            "images": 1,
            "conf_threshold": 2,
            "nms_threshold": 3,
            "max_detections": 4,
        },
        "outputs": {
            "__status_code": 0,
            "__status_message": 1,
            "images": 2,
            "rois": 3,
            "scores": 4,
            "classes": 5,
            "class_names": 6,
        },
    },
    "unavailable.custom.plugin": {
        "init": {},
        "inputs": {
            "images": 1,
        },
        "outputs": {
            "__status_code": 0,
            "__status_message": 1,
            "images": 2,
        },
    },
    "visionrt.detection.filter": {
        "init": {},
        "inputs": {
            "images": 1,
            "rois": 2,
            "scores": 3,
            "classes": 4,
            "class_names": 5,
            "min_score": 6,
            "min_area": 7,
            "allowed_classes": 8,
        },
        "outputs": {
            "__status_code": 0,
            "__status_message": 1,
            "images": 2,
            "rois": 3,
            "scores": 4,
            "classes": 5,
            "class_names": 6,
            "counts": 7,
        },
    },
    "custom.image.mean": {
        "init": {},
        "inputs": {
            "images": 1,
        },
        "outputs": {
            "__status_code": 0,
            "__status_message": 1,
            "means": 2,
        },
    },
    "custom.image.add_one": {
        "init": {},
        "inputs": {
            "images": 1,
        },
        "outputs": {
            "__status_code": 0,
            "__status_message": 1,
            "images": 2,
        },
    },
    "custom.general.counter": {
        "init": {
            "initial": 0,
        },
        "inputs": {
            "step": 1,
        },
        "outputs": {
            "__status_code": 0,
            "__status_message": 1,
            "count": 2,
        },
    },
    "custom.roi.geometry": {
        "init": {},
        "inputs": {
            "rois": 1,
        },
        "outputs": {
            "__status_code": 0,
            "__status_message": 1,
            "areas": 2,
            "centers": 3,
        },
    },
}


def load_tools_package_maps(package_dir: pathlib.Path) -> Dict[str, Dict[str, Dict[str, int]]]:
    """Scans tool declarations in a tools package directory to build name_en -> PortId maps."""
    result: Dict[str, Dict[str, Dict[str, int]]] = {}
    tools_dir = package_dir / "tools"
    if not tools_dir.is_dir():
        tools_dir = package_dir

    for json_file in tools_dir.rglob("*.json"):
        if json_file.name in ("module_index.json", "manifest.json", "build_info.json"):
            continue
        try:
            with open(json_file, "r", encoding="utf-8") as f:
                data = json.load(f)
            if not isinstance(data, dict):
                continue
            tool_id = data.get("name_en")
            if not tool_id or not isinstance(tool_id, str):
                continue
            ports = data.get("ports", {})
            mapping = {"init": {}, "inputs": {}, "outputs": {}}
            for stage in ("init", "inputs", "outputs"):
                for port in ports.get(stage, []):
                    if isinstance(port, dict) and "id" in port:
                        port_num = int(port["id"])
                        name_en = port.get("name_en")
                        name_zh = port.get("name_zh")
                        if name_en:
                            mapping[stage][name_en] = port_num
                        if name_zh:
                            mapping[stage][name_zh] = port_num
            result[tool_id] = mapping
        except Exception:
            continue
    return result


def normalize_type_token(raw_type: Any) -> Dict[str, Any]:
    """Normalizes legacy type representation to Schema 6 / Project 7 TypeSpec dictionary."""
    if isinstance(raw_type, dict):
        raw_t = raw_type.get("type", "Int")
        t_lower = str(raw_t).lower()
        if t_lower in ("int", "int64"):
            return {"type": "Int"}
        elif t_lower in ("float", "float64"):
            return {"type": "Float"}
        elif t_lower == "bool":
            return {"type": "Bool"}
        elif t_lower == "string":
            return {"type": "String"}
        elif t_lower == "image":
            return {"type": "Image"}
        elif t_lower == "roi":
            return {"type": "ROI"}
        elif t_lower == "array":
            dtype = str(raw_type.get("dtype", "int64")).lower()
            rank = int(raw_type.get("rank", 1))
            return {"type": "Array", "dtype": dtype, "rank": rank}
        return {"type": raw_t}
    elif isinstance(raw_type, str):
        t_lower = raw_type.lower()
        if t_lower in ("int", "int64"):
            return {"type": "Int"}
        elif t_lower in ("float", "float64"):
            return {"type": "Float"}
        elif t_lower == "bool":
            return {"type": "Bool"}
        elif t_lower == "string":
            return {"type": "String"}
        elif t_lower == "image":
            return {"type": "Image"}
        elif t_lower == "roi":
            return {"type": "ROI"}
        elif t_lower.startswith("vec_"):
            parts = t_lower.split("vec_")
            rank = len(parts) - 1
            element = parts[-1]
            if element == "int":
                element = "int64"
            elif element == "float":
                element = "float64"
            return {"type": "Array", "dtype": element, "rank": rank}
        return {"type": raw_type}
    return {"type": "String"}


def get_tool_map(tool_maps: Dict[str, Dict[str, Dict[str, int]]], tool_id: str) -> Dict[str, Dict[str, int]]:
    """Returns merged port mapping from standard tables and discovered package maps."""
    base = copy.deepcopy(STANDARD_TOOL_PORT_MAPS.get(tool_id, {"init": {}, "inputs": {}, "outputs": {}}))
    if tool_id in tool_maps:
        for stage in ("init", "inputs", "outputs"):
            base[stage].update(tool_maps[tool_id].get(stage, {}))
    return base


def migrate_project_json(
    project_data: dict,
    tool_maps: Dict[str, Dict[str, Dict[str, int]]],
) -> Tuple[dict, List[str]]:
    """Converts a project dictionary from format 4/5/6 to format 7."""
    diagnostics: List[str] = []
    migrated = copy.deepcopy(project_data)
    migrated["format_version"] = 7

    # Build node_seq -> tool_id lookup map across flows
    node_tool_map: Dict[str, str] = {}
    for flow in migrated.get("flows", []):
        for node in flow.get("nodes", []):
            node_seq = str(node.get("node_seq"))
            tool_id = node.get("tool_id", "")
            node_tool_map[node_seq] = tool_id

    for flow in migrated.get("flows", []):
        flow_name = flow.get("name", flow.get("flow_id", "flow"))
        for node in flow.get("nodes", []):
            node_seq = str(node.get("node_seq"))
            tool_id = node.get("tool_id", "")
            t_map = get_tool_map(tool_maps, tool_id)

            if not t_map["inputs"] and not t_map["outputs"] and not t_map["init"] and tool_id != "unavailable.custom.plugin":
                diagnostics.append(f"Flow '{flow_name}', Node {node_seq} ({tool_id}): 未知工具，无法映射端口")

            # 1. Migrate init_params
            old_init = node.get("init_params", {})
            new_init_list = []
            if isinstance(old_init, dict):
                for param_name, param_val in old_init.items():
                    port_id = None
                    if t_map and param_name in t_map.get("init", {}):
                        port_id = t_map["init"][param_name]
                    elif param_name.isdigit():
                        port_id = int(param_name)
                    else:
                        diagnostics.append(
                            f"Flow '{flow_name}', Node {node_seq} ({tool_id}): 无法识别初始化参数 '{param_name}'"
                        )
                        continue
                    new_init_list.append({"port_id": port_id, "value": param_val})
            elif isinstance(old_init, list):
                new_init_list = old_init
            node["init_params"] = new_init_list

            # 2. Migrate type_snapshot
            old_snapshots = node.get("type_snapshot", {})
            new_type_snapshot: Dict[str, list] = {"init_params": [], "inputs": [], "outputs": []}
            if isinstance(old_snapshots, dict):
                for stage_key in ("init_params", "inputs", "outputs"):
                    lookup_stage = "init" if stage_key == "init_params" else stage_key
                    stage_items = old_snapshots.get(stage_key, {})
                    if isinstance(stage_items, dict):
                        for port_key, type_val in stage_items.items():
                            pid = None
                            if t_map and port_key in t_map.get(lookup_stage, {}):
                                pid = t_map[lookup_stage][port_key]
                            elif port_key.isdigit():
                                pid = int(port_key)
                            else:
                                continue
                            new_type_snapshot[stage_key].append({
                                "port_id": pid,
                                "type": normalize_type_token(type_val)
                            })
                    elif isinstance(stage_items, list):
                        new_type_snapshot[stage_key] = stage_items
            node["type_snapshot"] = new_type_snapshot

            # 3. Migrate inputs
            old_inputs = node.get("inputs", {})
            new_input_list = []
            if isinstance(old_inputs, dict):
                for input_key, binding in old_inputs.items():
                    port_id = None
                    if t_map and input_key in t_map.get("inputs", {}):
                        port_id = t_map["inputs"][input_key]
                    elif input_key.isdigit():
                        port_id = int(input_key)
                    else:
                        diagnostics.append(
                            f"Flow '{flow_name}', Node {node_seq} ({tool_id}): 无法识别输入端口 '{input_key}'"
                        )
                        continue

                    mode = binding.get("mode", "literal")
                    literal_val = binding.get("literal")
                    new_binding: Dict[str, Any] = {
                        "port_id": port_id,
                        "mode": mode,
                        "literal": literal_val,
                    }

                    source = binding.get("source")
                    if source and isinstance(source, dict):
                        src_node = str(source.get("node_seq"))
                        src_port_raw = source.get("port_id")
                        src_tool = node_tool_map.get(src_node, "")
                        src_t_map = get_tool_map(tool_maps, src_tool)

                        src_port_id = None
                        if isinstance(src_port_raw, int):
                            src_port_id = src_port_raw
                        elif isinstance(src_port_raw, str):
                            if src_port_raw.isdigit():
                                src_port_id = int(src_port_raw)
                            elif src_t_map and src_port_raw in src_t_map.get("outputs", {}):
                                src_port_id = src_t_map["outputs"][src_port_raw]
                            else:
                                diagnostics.append(
                                    f"Flow '{flow_name}', Node {node_seq}: 上游引用 Node {src_node} ({src_tool}) 的端口 '{src_port_raw}' 无法映射"
                                )

                        if src_port_id is not None:
                            new_binding["source"] = {
                                "node_seq": src_node,
                                "port_id": src_port_id
                            }
                    new_input_list.append(new_binding)
            elif isinstance(old_inputs, list):
                new_input_list = old_inputs
            node["inputs"] = new_input_list

    return migrated, diagnostics


def main():
    parser = argparse.ArgumentParser(description="Migrate VisionRT projects from R44 (format 4/5/6) to R45 (format 7)")
    parser.add_argument("path", help="Path to .vrt.json project file or directory")
    parser.add_argument("--tools-package", help="Optional path to deployed tools package to harvest port maps")
    parser.add_argument("--output", help="Optional output file or directory")
    parser.add_argument("--in-place", action="store_true", help="Overwrite original files after creating .bak backups")
    args = parser.parse_args()

    tool_maps: Dict[str, Dict[str, Dict[str, int]]] = {}
    if args.tools_package:
        pkg_path = pathlib.Path(args.tools_package)
        if pkg_path.is_dir():
            tool_maps = load_tools_package_maps(pkg_path)

    input_path = pathlib.Path(args.path)
    if not input_path.exists():
        print(f"Error: path does not exist: {input_path}", file=sys.stderr)
        sys.exit(1)

    files_to_process = []
    if input_path.is_file() and input_path.name.endswith(".vrt.json"):
        files_to_process.append(input_path)
    elif input_path.is_dir():
        files_to_process.extend(input_path.rglob("*.vrt.json"))

    if not files_to_process:
        print(f"No .vrt.json files found at {input_path}")
        sys.exit(0)

    total_converted = 0
    total_failed = 0

    for file_path in files_to_process:
        try:
            with open(file_path, "r", encoding="utf-8") as f:
                data = json.load(f)

            current_ver = data.get("format_version")
            if current_ver == 7:
                print(f"[SKIP] {file_path}: already format_version 7")
                continue

            migrated, diags = migrate_project_json(data, tool_maps)

            if diags:
                print(f"[WARN] Diagnostics for {file_path}:")
                for d in diags:
                    print(f"  - {d}")

            if args.output:
                out_path = pathlib.Path(args.output)
                if len(files_to_process) == 1 and not out_path.is_dir():
                    target_file = out_path
                else:
                    rel = file_path.relative_to(input_path) if input_path.is_dir() else file_path.name
                    target_file = out_path / rel
                target_file.parent.mkdir(parents=True, exist_ok=True)
            elif args.in_place:
                bak_path = file_path.with_name(f"{file_path.name}.bak")
                if not bak_path.exists():
                    file_path.rename(bak_path)
                target_file = file_path
            else:
                target_file = file_path.with_name(f"{file_path.stem}.r45.vrt.json")

            with open(target_file, "w", encoding="utf-8") as f:
                json.dump(migrated, f, ensure_ascii=False, indent=2)

            print(f"[SUCCESS] Migrated {file_path} -> {target_file}")
            total_converted += 1
        except Exception as e:
            print(f"[ERROR] Failed to migrate {file_path}: {e}", file=sys.stderr)
            total_failed += 1

    print(f"\nMigration Summary: {total_converted} converted, {total_failed} failed.")
    if total_failed > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
