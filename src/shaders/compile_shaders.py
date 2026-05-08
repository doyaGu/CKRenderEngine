#!/usr/bin/env python3
"""Compile CK2_3D fixed-function shaders with bgfx shaderc.

The generated headers contain bgfx shader binary blobs for each supported
renderer backend. Runtime code selects the matching set after bgfx initializes.
"""

from __future__ import annotations

import argparse
import json
import os
import re
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

PROFILE_ENUMS = {
    "dx11": "CKRST_SHADER_PROFILE_DX11",
    "dx12": "CKRST_SHADER_PROFILE_DX12",
    "spirv": "CKRST_SHADER_PROFILE_SPIRV",
    "glsl": "CKRST_SHADER_PROFILE_GLSL",
}

FFP_VARIANT_MANIFEST = "ffp_specialized_variants.json"


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
                backend: dict[str, str], output: Path,
                defines: list[str] | None = None) -> None:
    cmd = [
        str(shaderc),
        "-f", str(script_dir / shader["source"]),
        "-o", str(output),
        "--type", shader["stage"],
        "--platform", backend["platform"],
        "-p", backend["profile"],
        "--varyingdef", str(script_dir / "varying.def.sc"),
    ]
    if defines:
        cmd.extend(["--define", ";".join(defines)])
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


def ffp_specialized_shader_defines(spec_dwords: list[int]) -> list[str]:
    if len(spec_dwords) != 10:
        raise ValueError("FFP specialized shader payload must contain exactly 10 dwords")

    defines = ["CKFF_FULL_SPECIALIZED=1"]
    for index, dword in enumerate(spec_dwords):
        if not isinstance(dword, int) or dword < 0 or dword > 0xffffffff:
            raise ValueError(f"FFP specialization dword {index} must be a uint32")
        defines.append(f"CKFF_SPEC_DWORD{index}={dword}")
    return defines


def sanitize_identifier(value: str) -> str:
    ident = re.sub(r"[^0-9A-Za-z_]", "_", value)
    if not ident or ident[0].isdigit():
        ident = "variant_" + ident
    return ident


def read_uint32(value: object, field: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0 or value > 0xffffffff:
        raise ValueError(f"{field} must be a uint32")
    return value


def read_bool(value: object, field: str) -> bool:
    if not isinstance(value, bool):
        raise ValueError(f"{field} must be a bool")
    return value


def repack_ffp_arg(arg: int) -> int:
    return (arg & 0b111) | ((arg & 0b110000) >> 1)


def set_spec_bits(dwords: list[int], word: int, offset: int, bits: int, value: int) -> None:
    mask = ((1 << bits) - 1) << offset
    dwords[word] &= ~mask
    dwords[word] |= (value << offset) & mask


def ffp_specialization_dwords_from_key(key: dict[str, object]) -> list[int]:
    dwords = [0] * 10
    dwords[0] = 1
    set_spec_bits(dwords, 4, 16, 3, key["lastActiveTextureStage"])
    set_spec_bits(dwords, 6, 31, 1, 1 if key["globalSpecularEnable"] else 0)

    for stage_index, stage in enumerate(key["stages"][:4]):
        word = 6 + stage_index
        set_spec_bits(dwords, word, 0, 5, stage["colorOp"])
        set_spec_bits(dwords, word, 5, 5, repack_ffp_arg(stage["colorArg1"]))
        set_spec_bits(dwords, word, 10, 5, repack_ffp_arg(stage["colorArg2"]))
        set_spec_bits(dwords, word, 15, 5, stage["alphaOp"])
        set_spec_bits(dwords, word, 20, 5, repack_ffp_arg(stage["alphaArg1"]))
        set_spec_bits(dwords, word, 25, 5, repack_ffp_arg(stage["alphaArg2"]))
        set_spec_bits(dwords, word, 30, 1, 1 if stage["resultIsTemp"] else 0)

    return dwords


def normalize_specialized_stage(stage: object, field: str) -> dict[str, object]:
    if not isinstance(stage, dict):
        raise ValueError(f"{field} must be an object")

    return {
        "colorOp": read_uint32(stage.get("colorOp"), f"{field}.colorOp"),
        "colorArg0": read_uint32(stage.get("colorArg0"), f"{field}.colorArg0"),
        "colorArg1": read_uint32(stage.get("colorArg1"), f"{field}.colorArg1"),
        "colorArg2": read_uint32(stage.get("colorArg2"), f"{field}.colorArg2"),
        "alphaOp": read_uint32(stage.get("alphaOp"), f"{field}.alphaOp"),
        "alphaArg0": read_uint32(stage.get("alphaArg0"), f"{field}.alphaArg0"),
        "alphaArg1": read_uint32(stage.get("alphaArg1"), f"{field}.alphaArg1"),
        "alphaArg2": read_uint32(stage.get("alphaArg2"), f"{field}.alphaArg2"),
        "resultIsTemp": read_bool(stage.get("resultIsTemp"), f"{field}.resultIsTemp"),
    }


def default_specialized_stage() -> dict[str, object]:
    return {
        "colorOp": 0,
        "colorArg0": 0,
        "colorArg1": 0,
        "colorArg2": 0,
        "alphaOp": 0,
        "alphaArg0": 0,
        "alphaArg1": 0,
        "alphaArg2": 0,
        "resultIsTemp": False,
    }


def normalize_specialized_key(key: object, field: str) -> dict[str, object]:
    if not isinstance(key, dict):
        raise ValueError(f"{field} must be an object")

    tex_gen = key.get("vsTexGen")
    if not isinstance(tex_gen, list) or len(tex_gen) != 8:
        raise ValueError(f"{field}.vsTexGen must contain exactly 8 uint32 values")

    stages = key.get("stages")
    if not isinstance(stages, list) or len(stages) > 8:
        raise ValueError(f"{field}.stages must contain up to 8 stage objects")
    normalized_stages = [normalize_specialized_stage(stage, f"{field}.stages[{index}]")
                         for index, stage in enumerate(stages)]
    while len(normalized_stages) < 8:
        normalized_stages.append(default_specialized_stage())

    return {
        "vsBits": read_uint32(key.get("vsBits"), f"{field}.vsBits"),
        "vsTexGen": [read_uint32(value, f"{field}.vsTexGen[{index}]")
                     for index, value in enumerate(tex_gen)],
        "lastActiveTextureStage": read_uint32(key.get("lastActiveTextureStage"),
                                              f"{field}.lastActiveTextureStage"),
        "globalSpecularEnable": read_bool(key.get("globalSpecularEnable"),
                                          f"{field}.globalSpecularEnable"),
        "stages": normalized_stages,
    }


def normalize_specialized_variant(variant: dict[str, object], index: int) -> dict[str, object]:
    name = variant.get("name")
    if not isinstance(name, str) or not name:
        raise ValueError(f"{FFP_VARIANT_MANIFEST} variants[{index}].name must be a non-empty string")

    vs = variant.get("vs")
    if vs not in ("3d", "positiont"):
        raise ValueError(f"{FFP_VARIANT_MANIFEST} variants[{index}].vs must be '3d' or 'positiont'")

    key = normalize_specialized_key(variant.get("key"),
                                    f"{FFP_VARIANT_MANIFEST} variants[{index}].key")
    spec_dwords = ffp_specialization_dwords_from_key(key)
    explicit_spec_dwords = variant.get("specDwords")
    if explicit_spec_dwords is not None:
        if not isinstance(explicit_spec_dwords, list):
            raise ValueError(f"{FFP_VARIANT_MANIFEST} variants[{index}].specDwords must be an array")
        explicit_spec_dwords = [
            read_uint32(value, f"{FFP_VARIANT_MANIFEST} variants[{index}].specDwords[{dword_index}]")
            for dword_index, value in enumerate(explicit_spec_dwords)
        ]
        if explicit_spec_dwords != spec_dwords:
            raise ValueError(f"{FFP_VARIANT_MANIFEST} variants[{index}].specDwords does not match key-derived FFP specialization payload")

    return {
        "name": name,
        "identifier": sanitize_identifier(name),
        "vs": vs,
        "specDwords": spec_dwords,
        "defines": ffp_specialized_shader_defines(spec_dwords),
        "key": key,
    }


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


def specialized_fs_var_name(backend: dict[str, str], variant: dict[str, object]) -> str:
    return f"s_{backend['name']}_ffp_{variant['identifier']}_fs_ff_stage"


def write_specialized_key_function(f, variant: dict[str, object]) -> None:
    ident = variant["identifier"]
    key = variant["key"]
    f.write(f"static CKFFShaderKey CKFFSpecializedKey_{ident}() {{\n")
    f.write("    CKFFShaderKey key;\n")
    f.write(f"    key.VS.Bits = {key['vsBits']}ull;\n")
    for index, value in enumerate(key["vsTexGen"]):
        f.write(f"    key.VS.TexGen[{index}] = {value}u;\n")
    f.write(f"    key.FS.LastActiveTextureStage = {key['lastActiveTextureStage']}u;\n")
    f.write(f"    key.FS.GlobalSpecularEnable = {'true' if key['globalSpecularEnable'] else 'false'};\n")
    for index, stage in enumerate(key["stages"]):
        prefix = f"    key.FS.Stages[{index}]"
        f.write(f"{prefix}.ColorOp = {stage['colorOp']}u;\n")
        f.write(f"{prefix}.ColorArg0 = {stage['colorArg0']}u;\n")
        f.write(f"{prefix}.ColorArg1 = {stage['colorArg1']}u;\n")
        f.write(f"{prefix}.ColorArg2 = {stage['colorArg2']}u;\n")
        f.write(f"{prefix}.AlphaOp = {stage['alphaOp']}u;\n")
        f.write(f"{prefix}.AlphaArg0 = {stage['alphaArg0']}u;\n")
        f.write(f"{prefix}.AlphaArg1 = {stage['alphaArg1']}u;\n")
        f.write(f"{prefix}.AlphaArg2 = {stage['alphaArg2']}u;\n")
        f.write(f"{prefix}.ResultIsTemp = {'true' if stage['resultIsTemp'] else 'false'};\n")
    f.write("    return key;\n")
    f.write("}\n\n")


def write_specialized_spec_function(f, variant: dict[str, object]) -> None:
    ident = variant["identifier"]
    dwords = ", ".join(f"{value}u" for value in variant["specDwords"])
    f.write(f"static CKFFSpecializationInfo CKFFSpecializedSpec_{ident}() {{\n")
    f.write(f"    const CKDWORD dwords[CKFFSpecializationInfo::MaxSpecDwords] = {{ {dwords} }};\n")
    f.write("    CKFFSpecializationInfo info;\n")
    f.write("    info.SetDwords(dwords, CKFFSpecializationInfo::MaxSpecDwords);\n")
    f.write("    return info;\n")
    f.write("}\n\n")


def write_specialized_module_function(f, backend: dict[str, str], variant: dict[str, object]) -> None:
    backend_name = backend["name"]
    ident = variant["identifier"]
    vs_name = f"s_{backend_name}_vs_ff_{'positiont' if variant['vs'] == 'positiont' else '3d'}"
    fs_name = specialized_fs_var_name(backend, variant)
    f.write(f"static CKFFSpecializedModule CKFFSpecializedModule_{backend_name}_{ident}() {{\n")
    f.write("    CKFFSpecializedModule module = {};\n")
    f.write(f"    module.VSData = {vs_name};\n")
    f.write(f"    module.VSSize = sizeof({vs_name});\n")
    f.write(f"    module.FSData = {fs_name};\n")
    f.write(f"    module.FSSize = sizeof({fs_name});\n")
    f.write(f"    module.Specialization = CKFFSpecializedSpec_{ident}();\n")
    f.write("    return module;\n")
    f.write("}\n\n")


def write_specialized_module_table(generated_dir: Path, backends: list[dict[str, str]],
                                   variants: list[dict[str, object]]) -> None:
    path = generated_dir / "CKFFSpecializedModuleTable.generated.h"
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as f:
        f.write("// Auto-generated by compile_shaders.py - DO NOT EDIT\n")
        f.write("#pragma once\n\n")
        if not variants:
            f.write("static const CKFFSpecializedModuleEntry *g_CKFFSpecializedModules = nullptr;\n")
            f.write("static const std::size_t g_CKFFSpecializedModuleCount = 0;\n")
            return

        for backend in backends:
            backend_name = backend["name"]
            f.write(f"#include \"shaders/generated/{backend_name}/vs_ff_3d.bin.h\"\n")
            f.write(f"#include \"shaders/generated/{backend_name}/vs_ff_positiont.bin.h\"\n")
            for variant in variants:
                ident = variant["identifier"]
                f.write(f"#include \"shaders/generated/{backend_name}/specialized/{ident}_fs_ff_stage.bin.h\"\n")
        f.write("\n")

        for variant in variants:
            write_specialized_key_function(f, variant)
            write_specialized_spec_function(f, variant)
        for backend in backends:
            for variant in variants:
                write_specialized_module_function(f, backend, variant)

        f.write("static const CKFFSpecializedModuleEntry g_CKFFSpecializedModuleEntries[] = {\n")
        for backend in backends:
            for variant in variants:
                ident = variant["identifier"]
                f.write(f"    {{ {PROFILE_ENUMS[backend['name']]}, CKFFSpecializedKey_{ident}(), "
                        f"CKFFSpecializedModule_{backend['name']}_{ident}() }},\n")
        f.write("};\n\n")
        f.write("static const CKFFSpecializedModuleEntry *g_CKFFSpecializedModules = g_CKFFSpecializedModuleEntries;\n")
        f.write("static const std::size_t g_CKFFSpecializedModuleCount = "
                "sizeof(g_CKFFSpecializedModuleEntries) / sizeof(g_CKFFSpecializedModuleEntries[0]);\n")


def load_specialized_variant_manifest(script_dir: Path) -> list[dict[str, object]]:
    manifest_path = script_dir / FFP_VARIANT_MANIFEST
    with manifest_path.open("r", encoding="utf-8") as f:
        manifest = json.load(f)

    if not isinstance(manifest, dict):
        raise ValueError(f"{FFP_VARIANT_MANIFEST} must contain a JSON object")

    variants = manifest.get("variants")
    if not isinstance(variants, list):
        raise ValueError(f"{FFP_VARIANT_MANIFEST} must contain a 'variants' array")

    for index, variant in enumerate(variants):
        if not isinstance(variant, dict):
            raise ValueError(f"{FFP_VARIANT_MANIFEST} variants[{index}] must be an object")

    return [normalize_specialized_variant(variant, index)
            for index, variant in enumerate(variants)]


def compile_specialized_variants(shaderc: Path, script_dir: Path, generated_dir: Path,
                                 tmp_dir: Path, backends: list[dict[str, str]],
                                 variants: list[dict[str, object]]) -> None:
    for backend in backends:
        for variant in variants:
            ident = variant["identifier"]
            bin_path = tmp_dir / backend["name"] / "specialized" / f"{ident}_fs_ff_stage.bin"
            bin_path.parent.mkdir(parents=True, exist_ok=True)
            run_shaderc(shaderc, script_dir, SHADERS[2], backend, bin_path, variant["defines"])
            header = generated_dir / backend["name"] / "specialized" / f"{ident}_fs_ff_stage.bin.h"
            write_header(header, specialized_fs_var_name(backend, variant), bin_path.read_bytes())


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
    specialized_variants = load_specialized_variant_manifest(script_dir)

    print(f"Using shaderc: {shaderc}")
    print(f"FFP specialized variants: {len(specialized_variants)}")
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
        compile_specialized_variants(shaderc, script_dir, generated_dir, tmp_dir, selected, specialized_variants)
        write_specialized_module_table(generated_dir, selected, specialized_variants)

    print("All shaders compiled successfully.")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        raise SystemExit(1)
