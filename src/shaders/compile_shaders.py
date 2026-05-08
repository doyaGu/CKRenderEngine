#!/usr/bin/env python3
"""Compile CK2_3D fixed-function shaders with bgfx shaderc.

The generated headers contain bgfx shader binary blobs for each supported
renderer backend. Runtime code selects the matching set after bgfx initializes.
"""

from __future__ import annotations

import argparse
import hashlib
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


def ffp_specialized_vs_defines(variant: dict[str, object]) -> list[str]:
    key = variant["key"]
    vs_bits = key["vsBits"]
    defines = [
        "CKFF_FULL_SPECIALIZED=1",
        f"CKFF_VS_BITS={vs_bits}",
        f"CKFF_VS_DIFFUSE_SOURCE={(vs_bits >> 25) & 3}",
        f"CKFF_VS_AMBIENT_SOURCE={(vs_bits >> 27) & 3}",
        f"CKFF_VS_SPECULAR_SOURCE={(vs_bits >> 29) & 3}",
        f"CKFF_VS_EMISSIVE_SOURCE={(vs_bits >> 31) & 3}",
        f"CKFF_VS_FOG_MODE={(vs_bits >> 21) & 3}",
    ]
    for index, value in enumerate(key["vsTexGen"]):
        defines.append(f"CKFF_VS_TEXGEN{index}={value}")
    for index, value in enumerate(key["vsTexCoordIndex"]):
        defines.append(f"CKFF_VS_TEXCOORD{index}={value}")
    for index, value in enumerate(key["vsTexTransformFlags"]):
        defines.append(f"CKFF_VS_TEXFLAGS{index}={value}")
    defines.append(f"CKFF_VS_TEXCOORD_DECL_MASK={key['vsTexcoordDeclMask']}")
    return defines


def sanitize_identifier(value: str) -> str:
    ident = re.sub(r"[^0-9A-Za-z_]", "_", value)
    if not ident or ident[0].isdigit():
        ident = "variant_" + ident
    return ident


def specialization_identifier(spec_dwords: list[int]) -> str:
    payload = ",".join(str(value) for value in spec_dwords).encode("ascii")
    return "spec_" + hashlib.sha1(payload).hexdigest()[:16]


def vs_specialization_identifier(vs: str, key: dict[str, object]) -> str:
    payload = json.dumps({
        "vs": vs,
        "vsBits": key["vsBits"],
        "vsTexcoordDeclMask": key["vsTexcoordDeclMask"],
        "vsTexGen": key["vsTexGen"],
        "vsTexCoordIndex": key["vsTexCoordIndex"],
        "vsTexTransformFlags": key["vsTexTransformFlags"],
    }, sort_keys=True, separators=(",", ":")).encode("ascii")
    return "vsspec_" + hashlib.sha1(payload).hexdigest()[:16]


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
    set_spec_bits(dwords, 5, 4, 1, 1 if key["alphaTestEnable"] else 0)
    set_spec_bits(dwords, 5, 5, 4, key["alphaFunc"])
    set_spec_bits(dwords, 5, 9, 1, 1 if key["fogEnable"] else 0)
    set_spec_bits(dwords, 5, 10, 2, key["vertexFogMode"])
    set_spec_bits(dwords, 5, 12, 2, key["pixelFogMode"])
    set_spec_bits(dwords, 5, 14, 1, 1 if key["rangeFog"] else 0)
    projected_sampler_mask = 0

    for stage_index, stage in enumerate(key["stages"][:4]):
        if stage["projectedSampler"]:
            projected_sampler_mask |= (1 << stage_index)
        word = 6 + stage_index
        set_spec_bits(dwords, 1, stage_index * 5, 5, repack_ffp_arg(stage["colorArg0"]))
        set_spec_bits(dwords, 2, stage_index * 5, 5, repack_ffp_arg(stage["alphaArg0"]))
        set_spec_bits(dwords, word, 0, 5, stage["colorOp"])
        set_spec_bits(dwords, word, 5, 5, repack_ffp_arg(stage["colorArg1"]))
        set_spec_bits(dwords, word, 10, 5, repack_ffp_arg(stage["colorArg2"]))
        set_spec_bits(dwords, word, 15, 5, stage["alphaOp"])
        set_spec_bits(dwords, word, 20, 5, repack_ffp_arg(stage["alphaArg1"]))
        set_spec_bits(dwords, word, 25, 5, repack_ffp_arg(stage["alphaArg2"]))
        set_spec_bits(dwords, word, 30, 1, 1 if stage["resultIsTemp"] else 0)

    set_spec_bits(dwords, 5, 0, 4, projected_sampler_mask)
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
        "projectedSampler": read_bool(stage.get("projectedSampler", False), f"{field}.projectedSampler"),
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
        "projectedSampler": False,
    }


def normalize_specialized_key(key: object, field: str) -> dict[str, object]:
    if not isinstance(key, dict):
        raise ValueError(f"{field} must be an object")

    tex_gen = key.get("vsTexGen")
    if not isinstance(tex_gen, list) or len(tex_gen) != 8:
        raise ValueError(f"{field}.vsTexGen must contain exactly 8 uint32 values")
    tex_coord_index = key.get("vsTexCoordIndex", [0, 1, 2, 3, 4, 5, 6, 7])
    if not isinstance(tex_coord_index, list) or len(tex_coord_index) != 8:
        raise ValueError(f"{field}.vsTexCoordIndex must contain exactly 8 uint32 values")
    tex_transform_flags = key.get("vsTexTransformFlags", [0, 0, 0, 0, 0, 0, 0, 0])
    if not isinstance(tex_transform_flags, list) or len(tex_transform_flags) != 8:
        raise ValueError(f"{field}.vsTexTransformFlags must contain exactly 8 uint32 values")
    default_texcoord_decl_mask = sum(2 << (index * 3) for index in range(8))

    stages = key.get("stages")
    if not isinstance(stages, list) or len(stages) > 8:
        raise ValueError(f"{field}.stages must contain up to 8 stage objects")
    normalized_stages = [normalize_specialized_stage(stage, f"{field}.stages[{index}]")
                         for index, stage in enumerate(stages)]
    while len(normalized_stages) < 8:
        normalized_stages.append(default_specialized_stage())

    alpha_test_enable = read_bool(key.get("alphaTestEnable", False), f"{field}.alphaTestEnable")
    alpha_func = read_uint32(key.get("alphaFunc", 0), f"{field}.alphaFunc")
    if alpha_func > 0xf:
        raise ValueError(f"{field}.alphaFunc must fit in 4 bits")
    if not alpha_test_enable and alpha_func != 0:
        raise ValueError(f"{field}.alphaFunc must be 0 when alphaTestEnable is false")

    vs_bits = read_uint32(key.get("vsBits"), f"{field}.vsBits")
    fog_enable = read_bool(key.get("fogEnable", False), f"{field}.fogEnable")
    vertex_fog_mode = read_uint32(key.get("vertexFogMode", (vs_bits >> 21) & 3 if fog_enable else 0),
                                  f"{field}.vertexFogMode")
    pixel_fog_mode = read_uint32(key.get("pixelFogMode", 0), f"{field}.pixelFogMode")
    if vertex_fog_mode > 3:
        raise ValueError(f"{field}.vertexFogMode must fit in 2 bits")
    if pixel_fog_mode > 3:
        raise ValueError(f"{field}.pixelFogMode must fit in 2 bits")

    return {
        "vsBits": vs_bits,
        "vsTexcoordDeclMask": read_uint32(key.get("vsTexcoordDeclMask", default_texcoord_decl_mask),
                                          f"{field}.vsTexcoordDeclMask") & 0xffffff,
        "vsTexGen": [read_uint32(value, f"{field}.vsTexGen[{index}]")
                     for index, value in enumerate(tex_gen)],
        "vsTexCoordIndex": [read_uint32(value, f"{field}.vsTexCoordIndex[{index}]") & 7
                            for index, value in enumerate(tex_coord_index)],
        "vsTexTransformFlags": [read_uint32(value, f"{field}.vsTexTransformFlags[{index}]") & 0x1ff
                                for index, value in enumerate(tex_transform_flags)],
        "lastActiveTextureStage": read_uint32(key.get("lastActiveTextureStage"),
                                              f"{field}.lastActiveTextureStage"),
        "globalSpecularEnable": read_bool(key.get("globalSpecularEnable"),
                                          f"{field}.globalSpecularEnable"),
        "alphaTestEnable": alpha_test_enable,
        "alphaFunc": alpha_func,
        "fogEnable": fog_enable,
        "vertexFogMode": vertex_fog_mode if fog_enable else 0,
        "pixelFogMode": pixel_fog_mode if fog_enable else 0,
        "rangeFog": read_bool(key.get("rangeFog", False), f"{field}.rangeFog") if fog_enable else False,
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
        "vsIdentifier": vs_specialization_identifier(vs, key),
        "specDwords": spec_dwords,
        "fsIdentifier": specialization_identifier(spec_dwords),
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
    return f"s_{backend['name']}_ffp_{variant['fsIdentifier']}_fs_ff_stage"


def specialized_vs_var_name(backend: dict[str, str], variant: dict[str, object]) -> str:
    suffix = "positiont" if variant["vs"] == "positiont" else "3d"
    return f"s_{backend['name']}_ffp_{variant['vsIdentifier']}_vs_ff_{suffix}"


def unique_specialized_fs_variants(variants: list[dict[str, object]]) -> list[dict[str, object]]:
    unique: dict[str, dict[str, object]] = {}
    for variant in variants:
        unique.setdefault(variant["fsIdentifier"], variant)
    return list(unique.values())


def unique_specialized_vs_variants(variants: list[dict[str, object]]) -> list[dict[str, object]]:
    unique: dict[str, dict[str, object]] = {}
    for variant in variants:
        unique.setdefault(variant["vsIdentifier"], variant)
    return list(unique.values())


def write_specialized_key_function(f, variant: dict[str, object]) -> None:
    ident = variant["identifier"]
    key = variant["key"]
    f.write(f"static CKFFShaderKey CKFFSpecializedKey_{ident}() {{\n")
    f.write("    CKFFShaderKey key;\n")
    f.write(f"    key.VS.Bits = {key['vsBits']}ull;\n")
    f.write(f"    key.VS.VertexTexcoordDeclMask = {key['vsTexcoordDeclMask']}u;\n")
    for index, value in enumerate(key["vsTexGen"]):
        f.write(f"    key.VS.TexGen[{index}] = {value}u;\n")
    for index, value in enumerate(key["vsTexCoordIndex"]):
        f.write(f"    key.VS.TexCoordIndex[{index}] = {value}u;\n")
    for index, value in enumerate(key["vsTexTransformFlags"]):
        f.write(f"    key.VS.TexTransformFlags[{index}] = {value}u;\n")
    f.write(f"    key.FS.LastActiveTextureStage = {key['lastActiveTextureStage']}u;\n")
    f.write(f"    key.FS.AlphaFunc = {key['alphaFunc']}u;\n")
    f.write(f"    key.FS.VertexFogMode = {key['vertexFogMode']}u;\n")
    f.write(f"    key.FS.PixelFogMode = {key['pixelFogMode']}u;\n")
    f.write(f"    key.FS.GlobalSpecularEnable = {'true' if key['globalSpecularEnable'] else 'false'};\n")
    f.write(f"    key.FS.AlphaTestEnable = {'true' if key['alphaTestEnable'] else 'false'};\n")
    f.write(f"    key.FS.FogEnable = {'true' if key['fogEnable'] else 'false'};\n")
    f.write(f"    key.FS.RangeFog = {'true' if key['rangeFog'] else 'false'};\n")
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
        f.write(f"{prefix}.ProjectedSampler = {'true' if stage['projectedSampler'] else 'false'};\n")
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
    vs_name = specialized_vs_var_name(backend, variant)
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
            for variant in unique_specialized_vs_variants(variants):
                ident = variant["vsIdentifier"]
                suffix = "positiont" if variant["vs"] == "positiont" else "3d"
                f.write(f"#include \"shaders/generated/{backend_name}/specialized/{ident}_vs_ff_{suffix}.bin.h\"\n")
            for variant in unique_specialized_fs_variants(variants):
                ident = variant["fsIdentifier"]
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

    normalized = [normalize_specialized_variant(variant, index)
                  for index, variant in enumerate(variants)]
    validate_specialized_variants(normalized)
    return normalized


def variant_key_fingerprint(variant: dict[str, object]) -> str:
    key = variant["key"]
    return json.dumps(key, sort_keys=True, separators=(",", ":"))


def validate_specialized_variants(variants: list[dict[str, object]]) -> None:
    identifiers: dict[str, str] = {}
    keys: dict[str, str] = {}
    for variant in variants:
        name = variant["name"]
        ident = variant["identifier"]
        previous_name = identifiers.get(ident)
        if previous_name is not None:
            raise ValueError(f"{FFP_VARIANT_MANIFEST} variants '{previous_name}' and '{name}' produce duplicate identifier '{ident}'")
        identifiers[ident] = name

        fingerprint = variant_key_fingerprint(variant)
        previous_key_name = keys.get(fingerprint)
        if previous_key_name is not None:
            raise ValueError(f"{FFP_VARIANT_MANIFEST} variants '{previous_key_name}' and '{name}' produce duplicate FFP shader key")
        keys[fingerprint] = name


def compile_specialized_variants(shaderc: Path, script_dir: Path, generated_dir: Path,
                                 tmp_dir: Path, backends: list[dict[str, str]],
                                 variants: list[dict[str, object]]) -> None:
    vs_variants = unique_specialized_vs_variants(variants)
    fs_variants = unique_specialized_fs_variants(variants)
    clean_stale_specialized_headers(generated_dir, backends, vs_variants, fs_variants)
    for backend in backends:
        for variant in vs_variants:
            ident = variant["vsIdentifier"]
            suffix = "positiont" if variant["vs"] == "positiont" else "3d"
            shader = SHADERS[1] if variant["vs"] == "positiont" else SHADERS[0]
            bin_path = tmp_dir / backend["name"] / "specialized" / f"{ident}_vs_ff_{suffix}.bin"
            bin_path.parent.mkdir(parents=True, exist_ok=True)
            run_shaderc(shaderc, script_dir, shader, backend, bin_path, ffp_specialized_vs_defines(variant))
            header = generated_dir / backend["name"] / "specialized" / f"{ident}_vs_ff_{suffix}.bin.h"
            write_header(header, specialized_vs_var_name(backend, variant), bin_path.read_bytes())
        for variant in fs_variants:
            ident = variant["fsIdentifier"]
            bin_path = tmp_dir / backend["name"] / "specialized" / f"{ident}_fs_ff_stage.bin"
            bin_path.parent.mkdir(parents=True, exist_ok=True)
            run_shaderc(shaderc, script_dir, SHADERS[2], backend, bin_path, variant["defines"])
            header = generated_dir / backend["name"] / "specialized" / f"{ident}_fs_ff_stage.bin.h"
            write_header(header, specialized_fs_var_name(backend, variant), bin_path.read_bytes())


def clean_stale_specialized_headers(generated_dir: Path, backends: list[dict[str, str]],
                                    vs_variants: list[dict[str, object]],
                                    fs_variants: list[dict[str, object]]) -> None:
    expected = {f"{variant['fsIdentifier']}_fs_ff_stage.bin.h" for variant in fs_variants}
    expected.update(
        f"{variant['vsIdentifier']}_vs_ff_{'positiont' if variant['vs'] == 'positiont' else '3d'}.bin.h"
        for variant in vs_variants)
    for backend in backends:
        specialized_dir = generated_dir / backend["name"] / "specialized"
        if not specialized_dir.is_dir():
            continue
        for header in specialized_dir.glob("*.bin.h"):
            if header.name not in expected:
                header.unlink()


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
