#!/usr/bin/env python3
"""Build SDL-native shaders from the shared FFP calculations using DXC.

Only declarations/entry points are adapted: no bgfx containers are read or
unwrapped. The native resource ABI is explicit and checked against reflection.
Intermediate source, assembly and reflection live in an out-of-source directory.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
CKFF_ROOT = HERE.parent.parent / "CKFFPLib"
SHARED = CKFF_ROOT / "ShaderModel" / "shaders"
SHADERS = [
    ("vs_ff_3d", "vs_ff_3d", False, 0, 0),
    ("vs_ff_3d_clip", "vs_ff_3d", True, 0, 0),
    ("vs_ff_positiont", "vs_ff_positiont", False, 0, 0),
    ("vs_ff_positiont_clip", "vs_ff_positiont", True, 0, 0),
    ("vs_ff_positiont_depth_pad", "vs_ff_positiont", False, 0, 0),
    ("vs_ff_positiont_clip_depth_pad", "vs_ff_positiont", True, 0, 0),
    ("fs_ff_stage", "fs_ff_stage", False, 0, 0),
    *[(f"fs_ff_stage_compare{count}", "fs_ff_stage", False, count, 0)
      for count in range(1, 9)],
    *[("fs_ff_stage_cube" + (f"_compare{count}" if count else ""),
       "fs_ff_stage", False, count, 1) for count in range(5)],
    *[("fs_ff_stage_volume" + (f"_compare{count}" if count else ""),
       "fs_ff_stage", False, count, 2) for count in range(5)],
    ("vs_postprocess", "vs_postprocess", False, 0, 0),
    ("fs_postprocess", "fs_postprocess", False, 0, 0),
    ("vs_clear", "vs_clear", False, 0, 0),
    ("fs_clear", "fs_clear", False, 0, 0),
    ("fs_volume_mip", "fs_volume_mip", False, 0, 0),
]
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
    ("float4", "u_ffSpec", 5),
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
    f"v_texcoord{i}" for i in range(7)] + ["v_texcoord7Fog", "v_fogPos"]


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
    samplers = 0 if vertex or source == "fs_clear" else (1 if source in ("fs_postprocess", "fs_volume_mip") else 16)
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
        declarations.append(f"    CK_LOCATION({i}) {qualifier}float4 {name} : TEXCOORD{i};")
    if clipping:
        declarations += ["    float4 v_clipDistance0 : SV_ClipDistance0;",
                         "    float4 v_clipDistance1 : SV_ClipDistance1;"]
    declarations += ["};", "static float4 gl_Position, gl_FragColor;"]
    globals_ = varying + (["v_clipDistance0", "v_clipDistance1"] if clipping else [])
    declarations += [f"static float4 {name};" for name in globals_]
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
        entry += [f"    output.{name} = {name};" for name in globals_]
        entry += ["    return output;", "}"]
    else:
        entry = ["float4 main(CKVaryings input) : SV_Target0 {"]
        entry += [f"    {name} = input.{name};" for name in varying]
        entry += ["    ckffEvaluate();", "    return gl_FragColor;", "}"]
    return "\n".join([f"#define CKFF_VS_CLIP_DISTANCE {int(clipping)}",
                       f"#define CKFF_VS_DEPTH_PAD {int(shader_name.endswith('_depth_pad'))}",
                       f"#define CKFF_NATIVE_FFP_STAGE {int(source == 'fs_ff_stage')}",
                       f"#define CKFF_NATIVE_COMPARE_COUNT {compare_count}",
                       f"#define CKFF_NATIVE_SAMPLER_LAYOUT {sampler_layout}",
                       HERE.joinpath("native_compat.hlsli").read_text(encoding="utf-8"),
                       uniform_declaration(source),
                       "" if vertex else HERE.joinpath("native_sampling.hlsli").read_text(encoding="utf-8"), *declarations,
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
    elif source == "fs_postprocess":
        dimensions = ["sampler2D"]
    elif sampler_layout == 1:
        dimensions = ["sampler2D"] * 4 + ["samplerCube"] * 8 + ["sampler3D"] * 4
    elif sampler_layout == 2:
        dimensions = ["sampler2D"] * 4 + ["samplerCube"] * 4 + ["sampler3D"] * 8
    else:
        dimensions = ["sampler2D"] * 8 + ["samplerCube"] * 4 + ["sampler3D"] * 4 if samplers else []
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


def verify_artifacts(directory, abi, abi_hash):
    manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
    assert (manifest["abi_version"], manifest["interface_hash"]) == (abi, abi_hash), "Shader ABI is stale"
    expected_abi = (f"static constexpr unsigned CKSDL_SHADER_ABI_VERSION = {abi};\n"
                    f"static constexpr unsigned CKSDL_SHADER_INTERFACE_HASH = 0x{abi_hash:08x};\n")
    assert (directory / "abi.h").read_text(encoding="utf-8") == expected_abi, "Compiled shader identity is stale"
    expected = {(name, format_) for name, _, _, _, _ in SHADERS for format_ in ("dxil", "spirv")}
    assert {(s["name"], s["format"]) for s in manifest["shaders"]} == expected
    sources = {name: hashlib.sha256(make_source(name, source, clipping, compare_count,
                                                sampler_layout).encode()).hexdigest()
               for name, source, clipping, compare_count, sampler_layout in SHADERS}
    layouts = {name: sum(buffer[2] for buffer in uniform_layout(source))
               for name, source, _, _, _ in SHADERS}
    source_by_name = {name: source for name, source, _, _, _ in SHADERS}
    for shader in manifest["shaders"]:
        name, format_ = shader["name"], shader["format"]
        assert (shader["uniform_buffers"], shader["samplers"]) == shader_resources(source_by_name[name]), f"{name}: resource count mismatch"
        assert shader["source_sha256"] == sources[name], f"{name}: source changed; regenerate native shaders"
        header = (directory / f"{format_}_{name}.h").read_text(encoding="utf-8")
        payload = bytes(int(b, 16) for b in re.findall(r"0x([0-9a-f]{2})", header))
        assert hashlib.sha256(payload).hexdigest() == shader["sha256"], f"{name}: native payload hash mismatch"
        assert shader["entry"] == "main" and shader["reflected"]
        assert shader["uniform_bytes"] == layouts[name], f"{name}: uniform layout mismatch"
    print("Verified complete native shader families, source hashes and ABI.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", type=Path)
    parser.add_argument("--verify", action="store_true", help="Validate checked-in artifacts without a compiler")
    parser.add_argument("--output-dir", type=Path, default=HERE / "generated")
    parser.add_argument("--dxc", default=shutil.which("dxc"))
    parser.add_argument("--spirv-cross", default=shutil.which("spirv-cross"))
    args = parser.parse_args()
    abi_text = (CKFF_ROOT / "Interface" / "CKBuiltinShaderIdentity.h").read_text(encoding="utf-8")
    abi = int(re.search(r"CKFF_SHADER_ABI_VERSION = (\d+)", abi_text)[1])
    abi_hash = int(re.search(r"CKFF_SHADER_INTERFACE_HASH = (0x[0-9a-fA-F]+)", abi_text)[1], 16)
    # Hash exactly the ordered schema consumed by the rasterizer shader identity.
    for byte in native_layout_schema()[1].encode("ascii"):
        abi_hash = ((abi_hash ^ byte) * 16777619) & 0xffffffff
    if args.verify:
        verify_artifacts(args.output_dir, abi, abi_hash)
        return
    if not args.dxc or not args.spirv_cross or not args.work_dir:
        parser.error("DXC, spirv-cross and --work-dir are required for offline shader generation")
    args.work_dir.mkdir(parents=True, exist_ok=True)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    pending = {}
    manifest = {"abi_version": abi, "interface_hash": abi_hash, "shaders": []}
    for name, source, clipping, compare_count, sampler_layout in SHADERS:
        vertex = source.startswith("vs_")
        uniforms, samplers = shader_resources(source)
        hlsl = args.work_dir / f"{name}.hlsl"
        hlsl.write_text(make_source(name, source, clipping, compare_count,
                                    sampler_layout))
        for format_ in ("dxil", "spirv"):
            output = args.work_dir / f"{format_}_{name}.bin"
            assembly = args.work_dir / f"{format_}_{name}.asm"
            optimization = "-O3"
            command = [args.dxc, "-T", "vs_6_0" if vertex else "ps_6_0", "-E", "main",
                       optimization, "-Fo", str(output), "-Fc", str(assembly), str(hlsl)]
            if format_ == "spirv":
                command += ["-spirv", "-fspv-target-env=vulkan1.0", "-fvk-use-gl-layout"]
            subprocess.run(command, check=True)
            if format_ == "spirv":
                reflection = json.loads(subprocess.check_output([args.spirv_cross, str(output), "--reflect"]))
                validate_spirv(reflection, source, vertex, samplers, uniforms,
                               sampler_layout)
                (args.work_dir / f"{format_}_{name}.json").write_text(json.dumps(reflection, indent=2))
            else:
                validate_dxil(assembly.read_text(encoding="utf-8"), source, vertex, samplers, uniforms)
            code = output.read_bytes()
            assert code[:4] == (b"DXBC" if format_ == "dxil" else b"\x03\x02\x23\x07")
            header = args.work_dir / f"{format_}_{name}.h"
            pending[args.output_dir / header.name] = header
            with header.open("w") as f:
                f.write("// Generated by compile_native_shaders.py. Native payload; never a bgfx container.\n")
                f.write(f"static const unsigned char s_sdl_{format_}_{name}[] = {{\n")
                for i in range(0, len(code), 24):
                    f.write("    " + ",".join(f"0x{b:02x}" for b in code[i:i+24]) + ",\n")
                f.write("};\n")
            manifest["shaders"].append({"name": name, "format": format_, "entry": "main",
                "samplers": samplers, "uniform_buffers": uniforms, "reflected": True,
                "uniform_bytes": sum(buffer[2] for buffer in uniform_layout(source)),
                "source_sha256": hashlib.sha256(make_source(name, source, clipping,
                                                              compare_count, sampler_layout).encode()).hexdigest(),
                "sha256": hashlib.sha256(code).hexdigest()})
    for destination, source in pending.items():
        shutil.copyfile(source, destination)
    (args.output_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (args.output_dir / "abi.h").write_text(
        f"static constexpr unsigned CKSDL_SHADER_ABI_VERSION = {abi};\n"
        f"static constexpr unsigned CKSDL_SHADER_INTERFACE_HASH = 0x{abi_hash:08x};\n")
    print("Generated and reflected both complete native shader families.")


if __name__ == "__main__":
    main()
