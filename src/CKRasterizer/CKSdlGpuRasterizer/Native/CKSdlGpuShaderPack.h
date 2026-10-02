#ifndef CKSDLGPU_SHADER_PACK_H
#define CKSDLGPU_SHADER_PACK_H

#include "CKTypes.h"
#include "XArray.h"
#include "shaders/generated/shaders.h"
#include <SDL3/SDL_gpu.h>
#include <stddef.h>

// The precompiled shaders of one format. Shader i is
// Code[Offsets[i], Offsets[i + 1]); a shader the format lacks is empty.
struct CKSdlGpuShaderArtifacts {
    XArray<CKBYTE> Code;
    XArray<CKDWORD> Offsets;
};

// Decodes a pack written by shaders/shader_pack.py. False when the pack is
// malformed or a shader fails its checksum.
CKBOOL CKSdlGpuDecodeShaderPack(const CKBYTE *Pack, size_t Size,
                                CKSdlGpuShaderArtifacts &Out);

// The embedded packs. The first use of a format decodes its pack once per
// process and is thread safe; a pack that fails to decode leaves its format
// without shaders. Decoded code stays valid until the process exits.
CKBOOL CKSdlGpuLoadShaders(SDL_GPUShaderFormat Format);
CKBOOL CKSdlGpuShaderCode(SDL_GPUShaderFormat Format, CKSdlShader Shader,
                          const CKBYTE *&Code, CKDWORD &Size);

#endif
