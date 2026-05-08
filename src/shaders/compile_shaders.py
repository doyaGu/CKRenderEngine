#!/usr/bin/env python3
"""Compile CK2_3D fixed-function shaders with bgfx shaderc.

The generated headers contain bgfx shader binary blobs for each supported
renderer backend. Runtime code selects the matching set after bgfx initializes.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


SHADERS = [
    {"source": "vs_ff_3d.sc", "stage": "vertex", "name": "vs_ff_3d"},
    {"source": "vs_ff_positiont.sc", "stage": "vertex", "name": "vs_ff_positiont"},
    {"source": "fs_ff_stage.sc", "stage": "fragment", "name": "fs_ff_stage"},
]

BACKENDS = [
    {"name": "dx11", "platform": "windows", "profile": "s_5_0"},
    {"name": "dx12", "platform": "windows", "profile": "s_6_0"},
    {"name": "spirv", "platform": "linux", "profile": "spirv"},
    {"name": "glsl", "platform": "linux", "profile": "150"},
]


def _exe_name(name: str) -> str:
    return name + (".exe" if os.name == "nt" else "")


def find_shaderc(explicit: str | None) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit))

    env_shaderc = os.environ.get("CK2_3D_SHADERC")
    if env_shaderc:
        candidates.append(Path(env_shaderc))

    path_shaderc = shutil.which(_exe_name("shaderc"))
    if path_shaderc:
        candidates.append(Path(path_shaderc))

    script_dir = Path(__file__).resolve().parent
    renderengine_root = script_dir.parent.parent
    workspace_root = renderengine_root.parent.parent
    for build_root in (workspace_root / "build", workspace_root / "out"):
        if build_root.exists():
            candidates.extend(build_root.rglob(_exe_name("shaderc")))

    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()

    raise FileNotFoundError(
        "Unable to find bgfx shaderc. Build the shaderc target, pass --shaderc, "
        "set CK2_3D_SHADERC, or put shaderc on PATH."
    )


def include_dirs(script_dir: Path) -> list[Path]:
    renderengine_root = script_dir.parent.parent
    bgfx_root = renderengine_root / "deps" / "bgfx" / "bgfx"
    return [
        script_dir,
        bgfx_root / "src",
        bgfx_root / "examples" / "common",
    ]


def run_shaderc(shaderc: Path, script_dir: Path, shader: dict[str, str],
                backend: dict[str, str], output: Path) -> None:
    cmd = [
        str(shaderc),
        "-f", str(script_dir / shader["source"]),
        "-o", str(output),
        "--type", shader["stage"],
        "--platform", backend["platform"],
        "-p", backend["profile"],
        "--varyingdef", str(script_dir / "varying.def.sc"),
    ]
    for inc in include_dirs(script_dir):
        cmd.extend(["-i", str(inc)])

    ensure_dxc_runtime(script_dir, shaderc)
    env = os.environ.copy()
    env["PATH"] = os.pathsep.join([str(p) for p in shaderc_runtime_dirs(script_dir, shaderc)]
                                  + [env.get("PATH", "")])

    print(f"Compiling {shader['source']} -> {backend['name']}/{shader['name']}.bin")
    result = subprocess.run(cmd, cwd=script_dir, text=True, env=env,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode != 0:
        raise RuntimeError("shaderc failed:\n" + " ".join(cmd) + "\n" + result.stdout)
    if result.stdout.strip():
        print(result.stdout.strip())


def ensure_dxc_runtime(script_dir: Path, shaderc: Path) -> None:
    if os.name != "nt":
        return

    dst_dir = shaderc.parent
    runtime_dlls = ("dxcompiler.dll", "dxil.dll")
    for src_dir in shaderc_runtime_dirs(script_dir, shaderc)[1:]:
        if all((src_dir / name).is_file() for name in runtime_dlls):
            for name in runtime_dlls:
                shutil.copy2(src_dir / name, dst_dir / name)
            return


def shaderc_runtime_dirs(script_dir: Path, shaderc: Path) -> list[Path]:
    dirs = [shaderc.parent]

    program_files_x86 = os.environ.get("ProgramFiles(x86)")
    if program_files_x86:
        arch_dir = "x86" if pe_machine(shaderc) == 0x014c else "x64"
        kits_bin = Path(program_files_x86) / "Windows Kits" / "10" / "bin"
        if kits_bin.exists():
            for candidate in sorted(kits_bin.glob(f"*/{arch_dir}/dxcompiler.dll"), reverse=True):
                dirs.append(candidate.parent)
                break

    renderengine_root = script_dir.parent.parent
    dirs.append(renderengine_root / "deps" / "bgfx" / "bgfx" / "tools" / "bin" / "windows")
    return dirs


def pe_machine(path: Path) -> int:
    try:
        data = path.read_bytes()
        if len(data) < 0x40 or data[:2] != b"MZ":
            return 0
        pe_offset = int.from_bytes(data[0x3c:0x40], "little")
        if pe_offset + 6 > len(data) or data[pe_offset:pe_offset + 4] != b"PE\0\0":
            return 0
        return int.from_bytes(data[pe_offset + 4:pe_offset + 6], "little")
    except OSError:
        return 0


def write_header(path: Path, var_name: str, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as f:
        f.write("// Auto-generated by compile_shaders.py - DO NOT EDIT\n")
        f.write("#pragma once\n\n")
        f.write(f"static const unsigned char {var_name}[] = {{\n")
        for i in range(0, len(data), 12):
            chunk = data[i:i + 12]
            values = ", ".join(f"0x{b:02x}" for b in chunk)
            f.write(f"    {values},\n")
        f.write("};\n")


def write_specialized_module_table(generated_dir: Path) -> None:
    path = generated_dir / "CKFFSpecializedModuleTable.generated.h"
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as f:
        f.write("// Auto-generated by compile_shaders.py - DO NOT EDIT\n")
        f.write("#pragma once\n\n")
        f.write("static const CKFFSpecializedModuleEntry *g_CKFFSpecializedModules = nullptr;\n")
        f.write("static const std::size_t g_CKFFSpecializedModuleCount = 0;\n")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compile CK2_3D fixed-function shaders with bgfx shaderc."
    )
    parser.add_argument("--shaderc", help="Path to bgfx shaderc executable.")
    parser.add_argument("--backend", choices=[b["name"] for b in BACKENDS],
                        action="append", help="Backend to compile. May be repeated.")
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    generated_dir = script_dir / "generated"
    shaderc = find_shaderc(args.shaderc)
    selected = [b for b in BACKENDS if not args.backend or b["name"] in args.backend]

    print(f"Using shaderc: {shaderc}")
    with tempfile.TemporaryDirectory(prefix="ck2_3d_shaders_") as tmp:
        tmp_dir = Path(tmp)
        for backend in selected:
            for shader in SHADERS:
                bin_path = tmp_dir / backend["name"] / (shader["name"] + ".bin")
                bin_path.parent.mkdir(parents=True, exist_ok=True)
                run_shaderc(shaderc, script_dir, shader, backend, bin_path)
                var_name = f"s_{backend['name']}_{shader['name']}"
                header = generated_dir / backend["name"] / (shader["name"] + ".bin.h")
                write_header(header, var_name, bin_path.read_bytes())
        write_specialized_module_table(generated_dir)

    print("All shaders compiled successfully.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        raise SystemExit(1)
