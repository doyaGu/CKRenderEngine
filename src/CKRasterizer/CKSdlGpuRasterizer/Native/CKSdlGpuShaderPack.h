#ifndef CKSDLGPU_SHADER_PACK_H
#define CKSDLGPU_SHADER_PACK_H

#include "CKRasterizerContextTypes.h"
#include "CKTypes.h"
#include "XArray.h"
#include "shaders/generated/shaders.h"
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_thread.h>
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

// The formats of the embedded packs: SPIR-V, with DirectX DXIL and the
// DXBC vertex shaders that compiled DXBC programs pair with, and with Metal
// MSL.
SDL_GPUShaderFormat CKSdlGpuShaderPackFormats();

// Sets the payload format, profile and entry point of a descriptor of the
// shaders of an SDL format; false for other formats. SPIRV-Cross renames the
// MSL entry point, as main is reserved in Metal.
CKBOOL CKSdlGpuShaderPayload(SDL_GPUShaderFormat Format, CKShaderDesc &Out);
// The SDL format of a payload format and profile, invalid for other ones.
SDL_GPUShaderFormat CKSdlGpuShaderPayloadFormat(CK_SHADER_FORMAT Format, CK_SHADER_PROFILE Profile);

// The embedded packs. The first use of a format decodes its pack once per
// process and is thread safe; a pack that fails to decode leaves its format
// without shaders. Decoded code stays valid until the process exits.
CKBOOL CKSdlGpuLoadShaders(SDL_GPUShaderFormat Format);
CKBOOL CKSdlGpuShaderCode(SDL_GPUShaderFormat Format, CKSdlShader Shader,
                          const CKBYTE *&Code, CKDWORD &Size);

// Decodes the embedded packs of some formats on a thread, so that decoding
// overlaps other work; the destructor waits for the thread. A use of a pack
// meanwhile finds it decoded, waits for it or decodes it itself, so each pack
// still decodes once.
class CKSdlGpuShaderPrefetch {
public:
    explicit CKSdlGpuShaderPrefetch(SDL_GPUShaderFormat Formats);
    ~CKSdlGpuShaderPrefetch();
    CKSdlGpuShaderPrefetch(const CKSdlGpuShaderPrefetch &) = delete;
    CKSdlGpuShaderPrefetch &operator=(const CKSdlGpuShaderPrefetch &) = delete;

private:
    SDL_Thread *m_Thread;
};

#endif
