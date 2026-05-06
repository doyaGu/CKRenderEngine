#!/usr/bin/env python3
"""
Compile HLSL shaders with fxc.exe and wrap them in bgfx binary format.
Produces C header files with embedded byte arrays ready for CKFFShaderCache.

bgfx shader binary format (version 11):
  Magic (3 bytes): "VSH" or "FSH"
  Version (1 byte): 0x0b
  Input hash (4 bytes LE): 0
  Output hash (4 bytes LE): computed from DXBC
  Uniform count (2 bytes LE)
  For each uniform:
    Name length (1 byte)
    Name (N bytes, no null terminator)
    Type (2 bytes LE): 0=float1, 1=float2, 2=float3, 3=float4, 4=mat4, 5=sampler
    Num (2 bytes LE): array count (0 = single)
    RegIndex (2 bytes LE): byte offset within constant buffer for non-samplers;
                           texture binding index for samplers
    RegCount (2 bytes LE): number of float4 registers consumed
  End marker (2 bytes): 0x00 0x00  [NOTE: actually part of DXBC size below]
  DXBC size (4 bytes LE)
  DXBC data (N bytes)

bgfx predefined uniform names (auto-detected by bgfx):
  u_viewRect, u_viewTexel, u_view, u_invView, u_proj, u_invProj,
  u_viewProj, u_invViewProj, u_model, u_modelView, u_invModelView,
  u_modelViewProj, u_alphaRef4

Type values: 0=FLOAT1, 1=FLOAT2, 2=FLOAT3, 3=FLOAT4/VEC4, 4=MAT4, 5=SAMPLER
For samplers: type has bit 0x10 set (type=0x10|0=sampler)
"""

import os
import struct
import subprocess
import sys
import hashlib

FXC_PATH = r"C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\fxc.exe"

# bgfx UniformType::Enum values (from bgfx.h)
#   Sampler = 0, End = 1, Vec4 = 2, Mat3 = 3, Mat4 = 4
# In the binary format, type is uint8 with additional flag bits:
#   kUniformFragmentBit = 0x10
#   kUniformSamplerBit  = 0x20
BGFX_UNIFORM_VEC4 = 2
BGFX_UNIFORM_MAT3 = 3
BGFX_UNIFORM_MAT4 = 4
BGFX_UNIFORM_FRAGMENT = 0x10
BGFX_UNIFORM_SAMPLER = 0x20  # Sampler(0) | kUniformSamplerBit(0x20)

# bgfx Attrib IDs (from vertexlayout.cpp s_attribToId)
ATTRIB_POSITION  = 0x0001
ATTRIB_NORMAL    = 0x0002
ATTRIB_COLOR0    = 0x0005
ATTRIB_COLOR1    = 0x0006
ATTRIB_TEXCOORD0 = 0x0010
ATTRIB_TEXCOORD1 = 0x0011

SHADERS = [
    {
        "source": "vs_ff_3d.hlsl",
        "output": "vs_ff_3d.bin.h",
        "var_name": "s_vs_ff_3d",
        "stage": "vertex",
        "profile": "vs_5_0",
        "entry": "main",
        "uniforms": [
            ("u_ckModelViewProj", BGFX_UNIFORM_MAT4, 1, 0, 4),
            ("u_ckModelView", BGFX_UNIFORM_MAT4, 1, 4, 4),
            ("u_lightParams", BGFX_UNIFORM_VEC4, 1, 8, 1),
            ("u_material", BGFX_UNIFORM_VEC4, 5, 9, 5),
            ("u_fogParams", BGFX_UNIFORM_VEC4, 1, 14, 1),
            ("u_viewport", BGFX_UNIFORM_VEC4, 1, 15, 1),
            ("u_ffParams", BGFX_UNIFORM_VEC4, 1, 16, 1),
            ("u_lights", BGFX_UNIFORM_VEC4, 56, 17, 56),
        ],
        "attrs": [ATTRIB_POSITION, ATTRIB_NORMAL, ATTRIB_TEXCOORD0, ATTRIB_COLOR0, ATTRIB_COLOR1],
        "cb_size": (17 + 56) * 16,
    },
    {
        "source": "vs_ff_positiont.hlsl",
        "output": "vs_ff_positiont.bin.h",
        "var_name": "s_vs_ff_positiont",
        "stage": "vertex",
        "profile": "vs_5_0",
        "entry": "main",
        "uniforms": [
            ("u_fogParams", BGFX_UNIFORM_VEC4, 1, 0, 1),
            ("u_viewport", BGFX_UNIFORM_VEC4, 1, 1, 1),
        ],
        "attrs": [ATTRIB_POSITION, ATTRIB_TEXCOORD0, ATTRIB_COLOR0, ATTRIB_COLOR1],
        "cb_size": 2 * 16,
    },
    {
        "source": "fs_ff_stage.hlsl",
        "output": "fs_ff_stage.bin.h",
        "var_name": "s_fs_ff_stage",
        "stage": "pixel",
        "profile": "ps_5_0",
        "entry": "main",
        "uniforms": [
            ("u_fogColor", BGFX_UNIFORM_VEC4, 1, 0, 1),
            ("u_alphaParams", BGFX_UNIFORM_VEC4, 1, 1, 1),
            ("u_texFactor", BGFX_UNIFORM_VEC4, 1, 2, 1),
            ("u_stageParams", BGFX_UNIFORM_VEC4, 6, 3, 6),
            ("s_texture0", BGFX_UNIFORM_SAMPLER, 0, 0, 1),
            ("s_texture1", BGFX_UNIFORM_SAMPLER, 0, 1, 1),
            ("s_texture2", BGFX_UNIFORM_SAMPLER, 0, 2, 1),
        ],
        "attrs": [],
        "cb_size": 9 * 16,
    },
    {
        "source": "vs_ff_lit.hlsl",
        "output": "vs_ff_lit.bin.h",
        "var_name": "s_vs_ff_lit",
        "stage": "vertex",
        "profile": "vs_5_0",
        "entry": "main",
        "uniforms": [
            # (name, type, num, regIndex, regCount)
            # Predefined uniforms (bgfx auto-fills these from SetTransform/SetViewTransform)
            ("u_ckModelViewProj", BGFX_UNIFORM_MAT4, 1, 0, 4),
            ("u_ckModelView", BGFX_UNIFORM_MAT4, 1, 4, 4),
            # User uniforms
            ("u_lightParams", BGFX_UNIFORM_VEC4, 1, 8, 1),
            ("u_material", BGFX_UNIFORM_VEC4, 5, 9, 5),
            ("u_fogParams", BGFX_UNIFORM_VEC4, 1, 14, 1),
            ("u_lights", BGFX_UNIFORM_VEC4, 56, 15, 56),
        ],
        # Vertex inputs used by this shader
        "attrs": [ATTRIB_POSITION, ATTRIB_NORMAL, ATTRIB_COLOR0, ATTRIB_TEXCOORD0],
        # Constant buffer size = max(regIndex + regCount) * 16 bytes per float4
        # = (23 + 56) * 16 = 79 * 16 = 1264 bytes
        "cb_size": (15 + 56) * 16,
    },
    {
        "source": "fs_ff_lit.hlsl",
        "output": "fs_ff_lit.bin.h",
        "var_name": "s_fs_ff_lit",
        "stage": "pixel",
        "profile": "ps_5_0",
        "entry": "main",
        "uniforms": [
            ("u_fogColor", BGFX_UNIFORM_VEC4, 1, 0, 1),
            ("u_alphaParams", BGFX_UNIFORM_VEC4, 1, 1, 1),
            ("u_texFactor", BGFX_UNIFORM_VEC4, 1, 2, 1),
            ("s_texture0", BGFX_UNIFORM_SAMPLER, 0, 0, 1),
        ],
        "attrs": [],
        # CB = 3 vec4 = 48 bytes
        "cb_size": 3 * 16,
    },
    {
        "source": "vs_ff_unlit.hlsl",
        "output": "vs_ff_unlit.bin.h",
        "var_name": "s_vs_ff_unlit",
        "stage": "vertex",
        "profile": "vs_5_0",
        "entry": "main",
        "uniforms": [
            ("u_ckModelViewProj", BGFX_UNIFORM_MAT4, 1, 0, 4),
            ("u_ckModelView", BGFX_UNIFORM_MAT4, 1, 4, 4),
            ("u_fogParams", BGFX_UNIFORM_VEC4, 1, 8, 1),
        ],
        "attrs": [ATTRIB_POSITION, ATTRIB_COLOR0, ATTRIB_TEXCOORD0],
        # CB = (12 + 1) * 16 = 208 bytes
        "cb_size": (8 + 1) * 16,
    },
    {
        "source": "fs_ff_unlit.hlsl",
        "output": "fs_ff_unlit.bin.h",
        "var_name": "s_fs_ff_unlit",
        "stage": "pixel",
        "profile": "ps_5_0",
        "entry": "main",
        "uniforms": [
            ("u_fogColor", BGFX_UNIFORM_VEC4, 1, 0, 1),
        ],
        "attrs": [],
        # CB = 1 vec4 = 16 bytes
        "cb_size": 1 * 16,
    },
]


def compile_hlsl(source_path, profile, entry, output_path):
    """Compile HLSL to DXBC using fxc.exe."""
    cmd = [
        FXC_PATH,
        "/T", profile,
        "/E", entry,
        "/O2",
        "/Fo", output_path,
        source_path,
    ]
    print(f"  Compiling: {os.path.basename(source_path)} ({profile})")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"ERROR compiling {source_path}:")
        print(result.stderr)
        return False
    if result.stderr:
        # Print warnings but don't fail
        for line in result.stderr.strip().split('\n'):
            if line.strip():
                print(f"  WARNING: {line.strip()}")
    return True


def make_bgfx_binary(stage, dxbc_data, uniforms, attrs=None, cb_size=0):
    """Wrap DXBC in bgfx shader binary format (version 11).

    Full format (from bgfx renderer_d3d11.cpp ShaderD3D11::create):
      Header:
        magic:    4 bytes ("VSH\x0b" or "FSH\x0b")
        hashIn:   uint32_t
        hashOut:  uint32_t (version >= 6)
        count:    uint16_t (number of uniforms)

      Per-uniform:
        nameSize:  uint8_t
        name:      nameSize bytes (no null terminator)
        type:      uint8_t  (UniformType::Enum | flags)
        num:       uint8_t  (array count)
        regIndex:  uint16_t (byte offset in constant buffer for non-samplers,
                             texture binding index for samplers)
        regCount:  uint16_t (number of float4 registers consumed)
        texInfo:   uint16_t (version >= 8)
        texFormat: uint16_t (version >= 10)

      Shader code:
        shaderSize: uint32_t
        code:       shaderSize bytes (DXBC)
        nul:        1 byte (0x00)

      Attributes (vertex inputs):
        numAttrs:   uint8_t
        attrs:      numAttrs × uint16_t (bgfx Attrib::Enum IDs)

      Constant buffer:
        cbSize:     uint16_t (total size in bytes, 0 if no CB)
    """
    buf = bytearray()

    # Magic + version
    magic = b"VSH" if stage == "vertex" else b"FSH"
    buf += magic + b"\x0b"

    # Input hash (4 bytes) — must match paired shader's output hash
    # We use 0 for both VS hashOut and FS hashIn so they always match
    buf += struct.pack("<I", 0)

    # Output hash (4 bytes) — VS hashOut must == paired FS hashIn
    # Set to 0 so any VS can pair with any FS in our set
    buf += struct.pack("<I", 0)

    # Uniform count (uint16_t)
    buf += struct.pack("<H", len(uniforms))

    # Uniform entries
    for name, utype, num, reg_idx, reg_count in uniforms:
        name_bytes = name.encode("ascii")
        buf += struct.pack("B", len(name_bytes))   # nameSize (uint8)
        buf += name_bytes                           # name (no null)
        if stage == "pixel":
            utype |= BGFX_UNIFORM_FRAGMENT
        reg_index = reg_idx if (utype & BGFX_UNIFORM_SAMPLER) else reg_idx * 16
        buf += struct.pack("B", utype)             # type (uint8)
        buf += struct.pack("B", num)               # num (uint8)
        buf += struct.pack("<H", reg_index)        # regIndex (uint16)
        buf += struct.pack("<H", reg_count)        # regCount (uint16)
        buf += struct.pack("<H", 0)                # texInfo (uint16, version>=8)
        buf += struct.pack("<H", 0)                # texFormat (uint16, version>=10)

    # DXBC size + data + null terminator
    buf += struct.pack("<I", len(dxbc_data))
    buf += dxbc_data
    buf += b"\x00"  # null terminator (bgfx skips shaderSize+1)

    # Attributes
    if attrs is None:
        attrs = []
    buf += struct.pack("B", len(attrs))            # numAttrs (uint8)
    for attr_id in attrs:
        buf += struct.pack("<H", attr_id)          # attr ID (uint16)

    # Constant buffer size (total bytes for all non-sampler uniforms)
    buf += struct.pack("<H", cb_size)              # cbSize (uint16)

    return bytes(buf)


def write_c_header(output_path, var_name, data):
    """Write binary data as a C byte array header."""
    with open(output_path, "w") as f:
        f.write(f"// Auto-generated by compile_shaders.py - DO NOT EDIT\n")
        f.write(f"// Source: {var_name}\n\n")
        f.write(f"static const unsigned char {var_name}[] = {{\n")
        for i in range(0, len(data), 12):
            chunk = data[i:i+12]
            hex_str = ", ".join(f"0x{b:02x}" for b in chunk)
            f.write(f"    {hex_str},\n")
        f.write(f"}};\n")
    print(f"  Generated: {os.path.basename(output_path)} ({len(data)} bytes)")


def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    generated_dir = os.path.join(script_dir, "generated")
    os.makedirs(generated_dir, exist_ok=True)

    temp_dir = os.path.join(script_dir, "temp")
    os.makedirs(temp_dir, exist_ok=True)

    success = True
    for shader in SHADERS:
        source_path = os.path.join(script_dir, shader["source"])
        dxbc_path = os.path.join(temp_dir, shader["source"].replace(".hlsl", ".dxbc"))
        output_path = os.path.join(generated_dir, shader["output"])

        if not compile_hlsl(source_path, shader["profile"], shader["entry"], dxbc_path):
            success = False
            continue

        with open(dxbc_path, "rb") as f:
            dxbc_data = f.read()

        bgfx_data = make_bgfx_binary(
            shader["stage"], dxbc_data, shader["uniforms"],
            attrs=shader.get("attrs", []),
            cb_size=shader.get("cb_size", 0))
        write_c_header(output_path, shader["var_name"], bgfx_data)

    # Cleanup temp
    import shutil
    shutil.rmtree(temp_dir, ignore_errors=True)

    if success:
        print("\nAll shaders compiled successfully!")
    else:
        print("\nSome shaders failed to compile!")
        sys.exit(1)


if __name__ == "__main__":
    main()
