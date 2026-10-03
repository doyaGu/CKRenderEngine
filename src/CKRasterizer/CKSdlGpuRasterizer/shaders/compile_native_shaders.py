#!/usr/bin/env python3
"""Build SDL-native shaders from the shared FFP calculations using DXC.

Only declarations/entry points are adapted: no bgfx containers are read or
unwrapped. The native resource ABI is explicit and checked against reflection.
Intermediate source, assembly and reflection live in an out-of-source directory.

FXC additionally builds Shader Model 5.1 DXBC variants of the fixed-function
vertex shaders. D3D12 accepts no pipeline that mixes DXBC with DXIL, and the
fragment shaders compiled at runtime are DXBC, so they pair with these.

The artifacts of each format are embedded as one compressed pack (see
shader_pack.py), indexed by the shader ids of generated/shaders.h.

--formats limits generation and verification to some formats, such as the
ones a platform embeds. Generation then needs only their compilers and keeps
the artifacts of the other formats; verification checks only their presence.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent
CKFF_ROOT = HERE.parent.parent / "CKFFPLib"
SHARED = CKFF_ROOT / "ShaderModel" / "shaders"
sys.dont_write_bytecode = True
sys.path.insert(0, str(CKFF_ROOT / "ShaderModel"))
from shader_abi_codegen import sampler_layouts, sync_sampler_layout, sync_sampler_shader_state
import shader_pack
SAMPLER_LAYOUT_DEF = CKFF_ROOT / "ShaderModel" / "CKFFSamplerLayout.def"
SAMPLER_LAYOUT_SHADER = SHARED / "ff_sampler_layout.sh"
SAMPLER_LAYOUTS = sampler_layouts(SAMPLER_LAYOUT_DEF)
SHADERS = [
    ("vs_ff_3d", "vs_ff_3d", False, 0, 0),
    ("vs_ff_3d_clip", "vs_ff_3d", True, 0, 0),
    ("vs_ff_positiont", "vs_ff_positiont", False, 0, 0),
    ("vs_ff_positiont_clip", "vs_ff_positiont", True, 0, 0),
    ("vs_ff_positiont_depth_pad", "vs_ff_positiont", False, 0, 0),
    ("vs_ff_positiont_clip_depth_pad", "vs_ff_positiont", True, 0, 0),
    ("fs_ff_stage", "fs_ff_stage", False, 0, 0),
    ("fs_ff_stage_native", "fs_ff_stage", False, 0, 0),
    *[(f"fs_ff_stage_compare{count}", "fs_ff_stage", False, count, 0)
      for count in range(1, 9)],
    ("fs_ff_stage_cube", "fs_ff_stage", False, 0, 1),
    ("fs_ff_stage_cube_native", "fs_ff_stage", False, 0, 1),
    ("fs_ff_stage_volume", "fs_ff_stage", False, 0, 2),
    ("fs_ff_stage_volume_native", "fs_ff_stage", False, 0, 2),
    ("vs_postprocess", "vs_postprocess", False, 0, 0),
    ("fs_postprocess", "fs_postprocess", False, 0, 0),
    ("vs_clear", "vs_clear", False, 0, 0),
    ("fs_clear", "fs_clear", False, 0, 0),
    ("fs_volume_mip", "fs_volume_mip", False, 0, 0),
    ("fs_dither_resolve", "fs_dither_resolve", False, 0, 0),
]
DXBC_SHADERS = [name for name, source, _, _, _ in SHADERS
                if source in ("vs_ff_3d", "vs_ff_positiont")]
BLOCKS = [
    ("float4x4", "u_ffMatrices", 8),
    ("float4x4", "u_vertexBlendMatrices", 4),
    ("float4", "u_ffDrawParams", 20),
    ("float4x4", "u_texMatrix", 8),
    ("float4", "u_lights", 56),
    ("float4", "u_bumpEnv", 16),
    ("float4", "u_viewport", 1),
    ("float4", "u_stageParams", 16),
    ("float4", "u_borderColor", 8),
    ("float4", "u_borderSampler", 16),
    ("float4", "u_ffProgram", 5),
    ("float4", "u_clipPlanes", 6),
    ("float4", "u_clipParams", 1),
    ("float4", "u_postParams", 1),
]
ATTRIBUTES = [
    ("float4", "a_position"), ("float3", "a_normal"),
    ("float3", "a_tangent"), ("float3", "a_bitangent"),
    ("float4", "a_color0"), ("float4", "a_color1"),
    ("uint4", "a_indices"), ("float3", "a_weight"),
] + [("float4", f"a_texcoord{i}") for i in range(8)]
VARYINGS = ["v_color0", "v_color1", "v_flatColor0", "v_flatColor1"] + [
    f"v_texcoord{i}" for i in range(7)] + [
        "v_texcoord7Fog", "v_fogPos", "v_lineOffset"]


def varying_type(name: str) -> str:
    return "float2" if name == "v_lineOffset" else "float4"


def source_body(path: Path) -> str:
    result = []
    bgfx_only = False
    # Strip bgfx-only branches before DXC sees the source. The native FFP
    # shader is already near the D3D12 driver's pipeline complexity limit;
    # leaving even a dead border branch in that source prevents pipeline creation.
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.strip() == "// CKFF_BGFX_ONLY_BEGIN":
            assert not bgfx_only
            bgfx_only = True
            continue
        if line.strip() == "// CKFF_BGFX_ONLY_END":
            assert bgfx_only
            bgfx_only = False
            continue
        if bgfx_only:
            continue
        if line.startswith("$") or re.match(r"^uniform\s", line):
            continue
        include = re.fullmatch(r'#include "([^"]+)"', line)
        if include:
            if include[1] != "bgfx_shader.sh":
                result.append(source_body(path.parent / include[1]))
                if include[1] == "ff_sampler_layout.sh":
                    # Native sampling takes handles of the resources the
                    # layout declares.
                    for helper in ("sampler_handles.hlsli",
                                   "native_sampling.hlsli",
                                   "depth_compare_sampling.hlsli"):
                        result.append(HERE.joinpath(helper).read_text(
                            encoding="utf-8"))
        else:
            result.append(line)
    assert not bgfx_only
    return "\n".join(result).replace("void main()", "void ckffEvaluate()")


def native_layout_schema():
    shader_model = CKFF_ROOT / "ShaderModel"
    interface = CKFF_ROOT / "Interface"
    enum = (shader_model / "CKFFShaderInterface.h").read_text(encoding="utf-8").split("enum CKFFConstantBlock {")[1].split("};")[0]
    names = re.findall(r"CKRST_BLOCK_(\w+)", enum)
    assert names.pop() == "COUNT" and len(names) == len(BLOCKS)
    blocks = dict(zip(names, BLOCKS))
    groups = {name: {} for name in ("VERTEX", "FRAGMENT", "PRESENT")}
    metadata_buffers = set()
    canonical = ""
    for line in (interface / "CKFFNativeLayout.def").read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.lstrip().startswith("//"):
            continue
        match = re.fullmatch(r"CKFF_NATIVE_(BLOCK|METADATA)\((\w+), (\d+), (\w+)\)", line)
        assert match, f"Invalid native layout row: {line}"
        kind, group, slot_text, value = match.groups()
        assert group in groups
        slot = int(slot_text)
        assert 0 <= slot < 4
        canonical += f"{kind.lower()}:{group}:{slot}:{value};"
        members = groups[group].setdefault(slot, [])
        if kind == "BLOCK":
            assert (group, slot) not in metadata_buffers, "Metadata must follow a buffer's logical blocks"
            assert not any(blocks[value] in buffer for buffer in groups[group].values()), "Duplicate native block"
            members.append(blocks[value])
        else:
            count = int(value)
            assert (group, slot) not in metadata_buffers and 0 < count <= 16
            metadata_buffers.add((group, slot))
            members += [("float4", "ck_borderColor", count), ("float4", "ck_samplerInfo", count)]
    ordered = {}
    for group, buffers in groups.items():
        slots = sorted(buffers)
        assert slots == list(range(len(slots))), f"{group} native buffer slots must be contiguous"
        ordered[group] = [buffers[slot] for slot in slots]
    return ordered, canonical


def uniform_layout(source: str):
    if source.endswith("_clear"):
        return [("CKNativeClear", [("float4", "ckClear", 1)], 16)]
    if source == "fs_volume_mip":
        return [("CKNativeVolume", [("float4", "ckVolumeParams", 2)], 32)]
    if source == "fs_dither_resolve":
        return [("CKNativeDither", [("float4", "ckDitherParams", 1)], 16)]
    if source == "vs_postprocess":
        return []
    group = "PRESENT" if source == "fs_postprocess" else "VERTEX" if source.startswith("vs_") else "FRAGMENT"
    prefix = {"VERTEX": "CKVertex", "FRAGMENT": "CKFragment", "PRESENT": "CKPresent"}[group]
    buffers = native_layout_schema()[0][group]
    if source == "vs_ff_positiont":
        buffers = buffers[1:]
    result = []
    for slot, members in enumerate(buffers):
        size = sum(count * (64 if kind == "float4x4" else 16) for kind, _, count in members)
        result.append((f"{prefix}{slot}", members, size))
    if len(result) == 1:
        result[0] = (prefix, result[0][1], result[0][2])
    return result


def shader_resources(source: str):
    vertex = source.startswith("vs_")
    samplers = 0 if vertex or source == "fs_clear" else (1 if source in ("fs_postprocess", "fs_volume_mip", "fs_dither_resolve") else 16)
    return len(uniform_layout(source)), samplers


def uniform_declaration(source: str) -> str:
    vertex = source.startswith("vs_")
    result = []
    for slot, (name, blocks, _) in enumerate(uniform_layout(source)):
        result.append(f"cbuffer {name} : register(b{slot}, space{1 if vertex else 3}) {{")
        row = 0
        for kind, member, count in blocks:
            array = f"[{count}]" if count != 1 or member in ("ck_borderColor", "ck_samplerInfo") else ""
            result.append(f"    {kind} {member}{array} : packoffset(c{row});")
            row += count * (4 if kind == "float4x4" else 1)
        result.append("};")
    return "\n".join(result)


def make_source(shader_name: str, source: str, clipping: bool,
                compare_count: int, sampler_layout: int) -> str:
    vertex = source.startswith("vs_")
    if source == "fs_volume_mip":
        return "\n".join([uniform_declaration(source), HERE.joinpath("volume_mip.hlsl").read_text(encoding="utf-8")])
    if source == "fs_dither_resolve":
        return "\n".join([uniform_declaration(source), HERE.joinpath("dither_resolve.hlsl").read_text(encoding="utf-8")])
    if source.endswith("_clear"):
        body = ("float4 main(uint vertex : SV_VertexID) : SV_Position { "
                "float2 p = vertex == 0 ? float2(-1,-1) : (vertex == 1 ? float2(3,-1) : float2(-1,3)); "
                "return float4(p, ckClear.x, 1); }" if vertex else
                "float4 main() : SV_Target0 { return ckClear; }")
        return uniform_declaration(source) + "\n" + body
    present = source.endswith("postprocess")
    varying = ["v_texcoord0"] if present else VARYINGS
    declarations = ["struct CKVaryings {", "    float4 position : SV_Position;"]
    for i, name in enumerate(varying):
        qualifier = "nointerpolation " if name.startswith("v_flat") else ""
        declarations.append(
            f"    CK_LOCATION({i}) {qualifier}{varying_type(name)} {name} : TEXCOORD{i};")
    if clipping:
        declarations += ["    float4 v_clipDistance0 : SV_ClipDistance0;",
                         "    float4 v_clipDistance1 : SV_ClipDistance1;"]
    declarations += ["};", "static float4 gl_Position, gl_FragColor, gl_FragCoord;"]
    globals_ = [(name, varying_type(name)) for name in varying]
    if clipping:
        globals_ += [("v_clipDistance0", "float4"),
                     ("v_clipDistance1", "float4")]
    declarations += [f"static {kind} {name};" for name, kind in globals_]
    if vertex:
        declarations += ["struct CKInput {"]
        used = [(i, kind, name) for i, (kind, name) in enumerate(ATTRIBUTES)
                if not present or name in ("a_position", "a_texcoord0")]
        declarations += [f"    CK_LOCATION({i}) {kind} {name} : TEXCOORD{i};" for i, kind, name in used]
        declarations += ["};"]
        declarations += [f"static {kind} {name};" for _, kind, name in used]
        entry = ["CKVaryings main(CKInput input) {"]
        entry += [f"    {name} = input.{name};" for _, _, name in used]
        entry += ["    ckffEvaluate();", "    CKVaryings output;", "    output.position = gl_Position;"]
        entry += [f"    output.{name} = {name};" for name, _ in globals_]
        entry += ["    return output;", "}"]
    else:
        entry = ["float4 main(CKVaryings input) : SV_Target0 {"]
        entry += [f"    {name} = input.{name};" for name in varying]
        entry += ["    gl_FragCoord = input.position;"]
        entry += ["    ckffEvaluate();", "    return gl_FragColor;", "}"]
    return "\n".join([f"#define CKFF_VS_CLIP_DISTANCE {int(clipping)}",
                       f"#define CKFF_VS_DEPTH_PAD {int(shader_name.endswith('_depth_pad'))}",
                       f"#define CKFF_NATIVE_FFP_STAGE {int(source == 'fs_ff_stage')}",
                       f"#define CKFF_HARDWARE_SAMPLING {int(shader_name.endswith('_native'))}",
                       f"#define CKFF_DEPTH_COMPARE_SAMPLER_COUNT {compare_count}",
                       f"#define CKFF_NATIVE_SAMPLER_LAYOUT {sampler_layout}",
                       HERE.joinpath("native_compat.hlsli").read_text(encoding="utf-8") +
                       ("\n" + HERE.joinpath("dxbc_vertex_math.hlsli").read_text(encoding="utf-8")
                        if source == "vs_ff_3d" else ""),
                       uniform_declaration(source), *declarations,
                       source_body(SHARED / f"{source}.sc"), *entry])


def expected_members(blocks):
    row = 0
    for kind, name, count in blocks:
        yield kind, name, count, row * 16
        row += count * (4 if kind == "float4x4" else 1)


def reflection_binding(resource):
    return resource["binding"]


def validate_spirv(reflection, source, vertex, samplers, uniforms, sampler_layout):
    assert reflection["entryPoints"] == [{"name": "main", "mode": "vert" if vertex else "frag"}]
    ubos, textures = reflection.get("ubos", []), reflection.get("textures", [])
    assert len(ubos) == uniforms
    assert not any(reflection.get(kind) for kind in ("ssbos", "images", "separate_images", "separate_samplers"))
    layouts = uniform_layout(source)
    for slot, (ubo, (buffer_name, blocks, buffer_size)) in enumerate(
            zip(sorted(ubos, key=reflection_binding), layouts)):
        assert ubo["name"] in (buffer_name, f"type.{buffer_name}")
        assert (ubo["set"], ubo["binding"], ubo["block_size"]) == (1 if vertex else 3, slot, buffer_size)
        members = reflection["types"][ubo["type"]]["members"]
        assert len(members) == len(list(expected_members(blocks)))
        for member, (kind, name, count, offset) in zip(members, expected_members(blocks)):
            assert member["name"] == name and member["offset"] == offset
            assert member["type"] == ("mat4" if kind == "float4x4" else "vec4")
            assert member.get("array", [1]) == [count]
            if count > 1 or name in ("ck_borderColor", "ck_samplerInfo"): assert member["array_stride"] == (64 if kind == "float4x4" else 16)
            # DXC transposes the SPIR-V matrix type; RowMajor here stores the
            # same bytes as HLSL column_major, with corresponding operations.
            if kind == "float4x4": assert member["matrix_stride"] == 16 and member["row_major"]
    assert len(textures) == samplers
    assert all(t["set"] == 2 for t in textures)
    assert sorted(t["binding"] for t in textures) == list(range(samplers))
    if source == "fs_volume_mip":
        dimensions = ["sampler3D"]
    elif source in ("fs_postprocess", "fs_dither_resolve"):
        dimensions = ["sampler2D"]
    elif samplers:
        counts = SAMPLER_LAYOUTS[sampler_layout].counts
        dimensions = (["sampler2D"] * counts[0] +
                      ["samplerCube"] * counts[1] +
                      ["sampler3D"] * counts[2])
    else:
        dimensions = []
    assert [t["type"] for t in sorted(textures, key=lambda t: t["binding"])] == dimensions


def validate_dxil(assembly, source, vertex, samplers, uniforms):
    assert "EntryFunctionName: main" in assembly
    assert ("; Vertex Shader" if vertex else "; Pixel Shader") in assembly
    bindings = re.search(r"; Resource Bindings:(.*?)\n; ViewId state:", assembly, re.S)[1]
    rows = re.findall(r"^; (\w+)\s+(cbuffer|sampler|texture)\s+\S+\s+\S+\s+\S+\s+(\w+),space(\d+)\s+(\d+)$", bindings, re.M)
    expanded = []
    for name, kind, binding, space, count in rows:
        parsed = re.fullmatch(r"([a-z]+)(\d+)", binding)
        assert parsed
        prefix, first = parsed[1], int(parsed[2])
        expanded.extend((name, kind, f"{prefix}{first + index}", space, "1")
                        for index in range(int(count)))
    rows = expanded
    sampler_bindings = list(range(samplers))
    assert len(rows) == uniforms + 2 * samplers
    for kind, prefix, count, space in (("cbuffer", "cb", uniforms, 1 if vertex else 3),
                                      ("sampler", "s", samplers, 2), ("texture", "t", samplers, 2)):
        selected = [r for r in rows if r[1] == kind]
        indices = sampler_bindings if kind == "sampler" else list(range(count))
        assert sorted(r[2] for r in selected) == sorted(f"{prefix}{i}" for i in indices)
        assert all(int(r[3]) == space and r[4] == "1" for r in selected)
    if uniforms:
        for buffer_name, blocks, buffer_size in uniform_layout(source):
            for kind, name, count, offset in expected_members(blocks):
                array = rf"\[{count}\]" if count > 1 or name in ("ck_borderColor", "ck_samplerInfo") else ""
                major = "column_major " if kind == "float4x4" else ""
                assert re.search(rf"{major}{kind} {name}{array};\s*; Offset:\s*{offset}\b", assembly)
            assert re.search(rf"{buffer_name};.*Size:\s*{buffer_size}\b", assembly)


def dxbc_chunks(code: bytes):
    assert code[:4] == b"DXBC" and int.from_bytes(code[24:28], "little") == len(code)
    count = int.from_bytes(code[28:32], "little")
    offsets = [int.from_bytes(code[32 + 4 * i:36 + 4 * i], "little") for i in range(count)]
    return [code[offset:offset + 4] for offset in offsets]


def dxbc_part(code: bytes, fourcc: bytes) -> bytes:
    for offset in (int.from_bytes(code[32 + 4 * i:36 + 4 * i], "little")
                   for i in range(int.from_bytes(code[28:32], "little"))):
        if code[offset:offset + 4] == fourcc:
            return code[offset + 8:offset + 8 + int.from_bytes(code[offset + 4:offset + 8], "little")]
    return None


def strip_reflection(dxc, output: Path, validated: bytes) -> bytes:
    """Rebuilds a validated DXIL container without reflection.

    Resources are bound from the shader descriptors and never through
    reflection, but the listing that validation reads is derived from it.
    The stripped build must carry the validated program unchanged.
    """
    subprocess.run(dxc + ["-Qstrip_reflect", "-Fo", str(output)], check=True)
    code = output.read_bytes()
    assert dxbc_part(code, b"DXIL") == dxbc_part(validated, b"DXIL"), "Stripping reflection changed the DXIL program"
    assert b"STAT" not in dxbc_chunks(code)
    return code


def validate_dxbc(assembly, code, source, clipping, uniforms):
    # FXC listings: every FF vertex shader binds only its uniform buffers, in
    # space 1, and writes the varyings at the registers of the DXIL variant,
    # which the runtime fragment shaders read.
    assert re.search(r"^vs_5_1$", assembly, re.M)
    chunks = dxbc_chunks(code)
    assert b"SHEX" in chunks and b"DXIL" not in chunks
    bindings = re.search(r"// Resource Bindings:(.*?)\n//\n//\n", assembly, re.S)[1]
    rows = re.findall(r"^// (\w+)\s+(\w+)\s+\S+\s+\S+\s+\S+\s+(\w+),space(\d+)\s+(\d+)\s*$", bindings, re.M)
    layouts = uniform_layout(source)
    assert len(layouts) == uniforms
    assert rows == [(name, "cbuffer", f"cb{slot}", "1", "1") for slot, (name, _, _) in enumerate(layouts)]
    for buffer_name, blocks, buffer_size in layouts:
        definition = re.search(rf"// cbuffer {buffer_name}\n// {{\n(.*?)\n// }}", assembly, re.S)[1]
        size = 0
        for kind, name, count, offset in expected_members(blocks):
            array = rf"\[{count}\]" if count > 1 else ""
            member = count * (64 if kind == "float4x4" else 16)
            assert re.search(rf"^//\s+{kind} {name}{array};\s*// Offset:\s*{offset} Size:\s*{member}\b",
                             definition, re.M), f"{buffer_name}.{name}: layout mismatch"
            size += member
        assert size == buffer_size
    signature = re.search(r"// Output signature:\n//\n.*?\n// -.*?\n(.*?)\n//\n", assembly, re.S)[1]
    outputs = re.findall(r"^// (\w+)\s+(\d+)\s+(\w+)\s+(\d+)\s+(\w+)\s+(\w+)", signature, re.M)
    expected = [("SV_Position", "0", "xyzw", "0", "POS", "float")]
    expected += [("TEXCOORD", str(i), "xy" if varying_type(name) == "float2" else "xyzw", str(i + 1), "NONE", "float")
                 for i, name in enumerate(VARYINGS)]
    if clipping:
        expected += [("SV_ClipDistance", str(i), "xyzw", str(len(VARYINGS) + 1 + i), "CLIPDST", "float")
                     for i in range(2)]
    assert outputs == expected, "Vertex output signature differs from the fragment input ABI"


PACK_FORMATS = ("dxil", "spirv", "dxbc")


def shader_formats(name: str):
    return ("dxil", "spirv", "dxbc") if name in DXBC_SHADERS else ("dxil", "spirv")


def format_list(text: str):
    """A comma separated --formats list, in pack order."""
    requested = set(filter(None, text.split(",")))
    if not requested or not requested <= set(PACK_FORMATS):
        raise argparse.ArgumentTypeError(f"expected formats among {','.join(PACK_FORMATS)}")
    return tuple(format_ for format_ in PACK_FORMATS if format_ in requested)


def shader_ids_header() -> str:
    lines = ["// Generated by compile_native_shaders.py. Shader ids, in pack order.",
             "enum CKSdlShader {"]
    lines += [f"    CKSDL_SHADER_{name.upper()}," for name, _, _, _, _ in SHADERS]
    lines += ["    CKSDL_SHADER_COUNT", "};"]
    return "\n".join(lines) + "\n"


def pack_header(format_: str, pack: bytes) -> str:
    lines = [f"// Generated by compile_native_shaders.py. {format_.upper()} shader pack; see shader_pack.py.",
             f"static const unsigned char s_sdl_{format_}_pack[] = {{"]
    lines += ["    " + ",".join(f"0x{b:02x}" for b in pack[i:i + 24]) + ","
              for i in range(0, len(pack), 24)]
    lines.append("};")
    return "\n".join(lines) + "\n"


def abi_header(abi, abi_hash) -> str:
    # Uniform buffer counts follow the native layout so the C++ shader
    # descriptors never restate how the blocks are split across slots.
    families = (("FF_3D", "vs_ff_3d"), ("FF_POSITIONT", "vs_ff_positiont"),
                ("FF_FRAGMENT", "fs_ff_stage"), ("PRESENT", "fs_postprocess"))
    lines = [f"static constexpr unsigned CKSDL_SHADER_ABI_VERSION = {abi};",
             f"static constexpr unsigned CKSDL_SHADER_INTERFACE_HASH = 0x{abi_hash:08x};"]
    lines += [f"static constexpr unsigned CKSDL_SHADER_{family}_UNIFORM_BUFFERS = {shader_resources(source)[0]};"
              for family, source in families]
    return "\n".join(lines) + "\n"


def source_hashes():
    return {name: hashlib.sha256(make_source(name, source, clipping, compare_count,
                                             sampler_layout).encode()).hexdigest()
            for name, source, clipping, compare_count, sampler_layout in SHADERS}


def read_pack(directory, format_) -> bytes:
    header = (directory / f"{format_}_pack.h").read_text(encoding="utf-8")
    return bytes(int(b, 16) for b in re.findall(r"0x([0-9a-f]{2})", header))


def verify_artifacts(directory, abi, abi_hash, formats):
    manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
    assert (manifest["abi_version"], manifest["interface_hash"]) == (abi, abi_hash), "Shader ABI is stale"
    assert (directory / "abi.h").read_text(encoding="utf-8") == abi_header(abi, abi_hash), "Compiled shader identity is stale"
    assert (directory / "shaders.h").read_text(encoding="utf-8") == shader_ids_header(), "Shader ids are stale"
    expected = {(name, format_) for name, _, _, _, _ in SHADERS for format_ in shader_formats(name)}
    assert {(s["name"], s["format"]) for s in manifest["shaders"]} == expected
    assert [p["format"] for p in manifest["packs"]] == list(PACK_FORMATS)
    # Only the listed formats are checked beyond their presence.
    sources = source_hashes()
    layouts = {name: sum(buffer[2] for buffer in uniform_layout(source))
               for name, source, _, _, _ in SHADERS}
    source_by_name = {name: source for name, source, _, _, _ in SHADERS}
    for shader in manifest["shaders"]:
        name, format_ = shader["name"], shader["format"]
        if format_ not in formats:
            continue
        assert (shader["uniform_buffers"], shader["samplers"]) == shader_resources(source_by_name[name]), f"{name}: resource count mismatch"
        assert shader["source_sha256"] == sources[name], f"{name}: source changed; regenerate native shaders"
        assert shader["entry"] == "main" and shader["reflected"]
        assert shader["uniform_bytes"] == layouts[name], f"{name}: uniform layout mismatch"
    # Generation proved that every pack decodes to its artifacts; the build
    # only checks that the packs are the generated ones.
    sizes = {(s["name"], s["format"]): s["size"] for s in manifest["shaders"]}
    for entry in manifest["packs"]:
        format_ = entry["format"]
        if format_ not in formats:
            continue
        pack = read_pack(directory, format_)
        assert hashlib.sha256(pack).hexdigest() == entry["sha256"], f"{format_}: shader pack hash mismatch"
        assert shader_pack.sizes(pack) == [sizes.get((name, format_), 0) for name, _, _, _, _ in SHADERS], \
            f"{format_}: shader pack directory mismatch"
    print(f"Verified the {','.join(formats)} native shaders, source hashes and ABI.")


def kept_artifacts(directory, abi, abi_hash, formats):
    """The manifest entries of the formats a generation keeps.

    They must be of the current ABI and shaders. Artifacts of older sources
    are kept with a warning, and their formats fail verification until they
    are regenerated.
    """
    regenerate = f"regenerate {','.join(formats)} too"
    assert (directory / "manifest.json").is_file(), f"No artifacts to keep; {regenerate}"
    manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
    assert (manifest["abi_version"], manifest["interface_hash"]) == (abi, abi_hash), f"Shader ABI changed; {regenerate}"
    assert (directory / "shaders.h").read_text(encoding="utf-8") == shader_ids_header(), f"Shader ids changed; {regenerate}"
    shaders = {(s["name"], s["format"]): s for s in manifest["shaders"] if s["format"] in formats}
    assert set(shaders) == {(name, format_) for name, _, _, _, _ in SHADERS
                            for format_ in shader_formats(name) if format_ in formats}, f"Shaders are missing; {regenerate}"
    packs = {p["format"]: p for p in manifest["packs"] if p["format"] in formats}
    for format_ in formats:
        assert format_ in packs and hashlib.sha256(read_pack(directory, format_)).hexdigest() == packs[format_]["sha256"], \
            f"{format_}: shader pack differs from the manifest; {regenerate}"
    sources = source_hashes()
    stale = [format_ for format_ in formats
             if any(shader["source_sha256"] != sources[name] for (name, f), shader in shaders.items() if f == format_)]
    if stale:
        print(f"warning: the kept {','.join(stale)} shaders are of older sources; regenerate them", file=sys.stderr)
    return shaders, packs


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", type=Path)
    parser.add_argument("--verify", action="store_true", help="Validate checked-in artifacts without a compiler")
    parser.add_argument("--output-dir", type=Path, default=HERE / "generated")
    parser.add_argument("--dxc", default=shutil.which("dxc"))
    parser.add_argument("--spirv-cross", default=shutil.which("spirv-cross"))
    parser.add_argument("--fxc", default=shutil.which("fxc"))
    parser.add_argument("--spirv-grammar", type=Path,
                        help="spirv.core.grammar.json; defaults to the one of the SDK of spirv-cross")
    parser.add_argument("--formats", type=format_list, default=PACK_FORMATS,
                        help=f"Comma separated formats to generate or verify; defaults to {','.join(PACK_FORMATS)}")
    args = parser.parse_args()
    abi_text = (CKFF_ROOT / "Interface" / "CKBuiltinShaderIdentity.h").read_text(encoding="utf-8")
    abi = int(re.search(r"CKFF_SHADER_ABI_VERSION = (\d+)", abi_text)[1])
    abi_hash = int(re.search(r"CKFF_SHADER_INTERFACE_HASH = (0x[0-9a-fA-F]+)", abi_text)[1], 16)
    # Hash exactly the ordered schema consumed by the rasterizer shader identity.
    for byte in native_layout_schema()[1].encode("ascii"):
        abi_hash = ((abi_hash ^ byte) * 16777619) & 0xffffffff
    sync_sampler_shader_state(
        CKFF_ROOT / "ShaderModel" / "CKFFShaderABI.h",
        SHARED / "ff_sampler_shader_state.sh",
        verify=args.verify,
    )
    sync_sampler_layout(
        SAMPLER_LAYOUT_DEF, SAMPLER_LAYOUT_SHADER, verify=args.verify)
    if args.verify:
        verify_artifacts(args.output_dir, abi, abi_hash, args.formats)
        return
    formats = args.formats
    # Each format needs only its own compilers.
    tools = (("DXC", args.dxc, ("dxil", "spirv")), ("spirv-cross", args.spirv_cross, ("spirv",)),
             ("FXC", args.fxc, ("dxbc",)))
    missing = [tool for tool, path, users in tools if not path and set(users) & set(formats)]
    if not args.work_dir:
        missing.append("--work-dir")
    if missing:
        parser.error(f"{', '.join(missing)} required to generate the {','.join(formats)} shaders")
    grammar = None
    if "spirv" in formats:
        grammar = args.spirv_grammar or (Path(args.spirv_cross).resolve().parent.parent / "Include" /
                                         "spirv-headers" / "spirv.core.grammar.json")
        if not grammar.is_file():
            parser.error(f"SPIR-V grammar not found at {grammar}; pass --spirv-grammar")
    kept = tuple(format_ for format_ in PACK_FORMATS if format_ not in formats)
    kept_shaders, kept_packs = kept_artifacts(args.output_dir, abi, abi_hash, kept) if kept else ({}, {})
    args.work_dir.mkdir(parents=True, exist_ok=True)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    codes = {}
    manifest = {"abi_version": abi, "interface_hash": abi_hash, "shaders": [], "packs": []}
    for name, source, clipping, compare_count, sampler_layout in SHADERS:
        vertex = source.startswith("vs_")
        uniforms, samplers = shader_resources(source)
        hlsl = args.work_dir / f"{name}.hlsl"
        hlsl.write_text(make_source(name, source, clipping, compare_count,
                                    sampler_layout))
        for format_ in shader_formats(name):
            if format_ in kept:
                manifest["shaders"].append(kept_shaders[name, format_])
                continue
            output = args.work_dir / f"{format_}_{name}.bin"
            assembly = args.work_dir / f"{format_}_{name}.asm"
            if format_ == "dxbc":
                command = [args.fxc, "/nologo", "/T", "vs_5_1", "/E", "main", "/O3",
                           "/D", "CKFF_NATIVE_DXBC=1",
                           "/Fo", str(output), "/Fc", str(assembly), str(hlsl)]
            else:
                optimization = "-O3"
                dxc = [args.dxc, "-T", "vs_6_0" if vertex else "ps_6_0", "-E", "main",
                       optimization, str(hlsl)]
                if format_ == "spirv":
                    dxc += ["-spirv", "-fspv-target-env=vulkan1.0", "-fvk-use-gl-layout"]
                command = dxc + ["-Fo", str(output), "-Fc", str(assembly)]
            subprocess.run(command, check=True)
            code = output.read_bytes()
            if format_ == "spirv":
                reflection = json.loads(subprocess.check_output([args.spirv_cross, str(output), "--reflect"]))
                validate_spirv(reflection, source, vertex, samplers, uniforms,
                               sampler_layout)
                (args.work_dir / f"{format_}_{name}.json").write_text(json.dumps(reflection, indent=2))
            elif format_ == "dxbc":
                validate_dxbc(assembly.read_text(encoding="utf-8"), code, source, clipping, uniforms)
            else:
                validate_dxil(assembly.read_text(encoding="utf-8"), source, vertex, samplers, uniforms)
                code = strip_reflection(dxc, args.work_dir / f"{format_}_{name}_stripped.bin", code)
            assert code[:4] == (b"\x03\x02\x23\x07" if format_ == "spirv" else b"DXBC")
            codes[format_, name] = code
            manifest["shaders"].append({"name": name, "format": format_, "entry": "main",
                "samplers": samplers, "uniform_buffers": uniforms, "reflected": True,
                "uniform_bytes": sum(buffer[2] for buffer in uniform_layout(source)),
                "source_sha256": hashlib.sha256(make_source(name, source, clipping,
                                                              compare_count, sampler_layout).encode()).hexdigest(),
                "size": len(code), "sha256": hashlib.sha256(code).hexdigest()})
    headers = {}
    for format_ in PACK_FORMATS:
        if format_ in kept:
            manifest["packs"].append(kept_packs[format_])
            continue
        pack = shader_pack.encode(format_, [codes.get((format_, name)) for name, _, _, _, _ in SHADERS],
                                  spirv_grammar=grammar)
        headers[f"{format_}_pack.h"] = pack_header(format_, pack)
        manifest["packs"].append({"format": format_, "size": len(pack),
                                  "sha256": hashlib.sha256(pack).hexdigest()})
        print(f"{format_}: {sum(len(c) for (f, _), c in codes.items() if f == format_)} bytes "
              f"of artifacts packed into {len(pack)}")
    for name, text in headers.items():
        (args.output_dir / name).write_text(text)
    (args.output_dir / "shaders.h").write_text(shader_ids_header())
    (args.output_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (args.output_dir / "abi.h").write_text(abi_header(abi, abi_hash))
    print(f"Generated and reflected the {','.join(formats)} native shaders" +
          (f"; kept {','.join(kept)}." if kept else "."))


if __name__ == "__main__":
    main()
