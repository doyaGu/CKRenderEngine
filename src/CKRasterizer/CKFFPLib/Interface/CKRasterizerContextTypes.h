#ifndef CKRASTERIZERCONTEXTTYPES_H
#define CKRASTERIZERCONTEXTTYPES_H

#include "VxDefines.h"
#include "VxColor.h"
#include "XArray.h"
#include "CKTypes.h"
#include "CKRasterizerContextEnums.h"
// CKTextureDesc is shared with the public rasterizer interface. Concrete
// Context geometry uses explicit layouts and byte buffers rather than
// fixed-function vertex formats.
#include "CKRasterizerResourceTypes.h"

// ===========================================================================
// Shader Descriptor
// ===========================================================================

struct CKRasterizerTargetDesc {
    CK_SHADER_FORMAT ShaderFormat;
    CK_SHADER_PROFILE ShaderProfile;
    CKBOOL HomogeneousDepth;
    CKBOOL OriginBottomLeft;

    CKRasterizerTargetDesc()
        : ShaderFormat(CKRST_SHADER_FORMAT_UNKNOWN),
          ShaderProfile(CKRST_SHADER_PROFILE_UNKNOWN),
          HomogeneousDepth(FALSE),
          OriginBottomLeft(FALSE) {}
};

struct CKReadbackDesc {
    void *Data;
    CKDWORD Capacity;
    CKDWORD RequiredSize;
    CKDWORD RowPitch;
    CKDWORD Width;
    CKDWORD Height;
    VX_PIXELFORMAT Format;
    CKBOOL YFlip;

    CKReadbackDesc()
        : Data(NULL), Capacity(0),
          RequiredSize(0), RowPitch(0), Width(0), Height(0),
          Format(UNKNOWN_PF), YFlip(FALSE) {}
};

struct CKShaderDesc {
    CK_SHADER_STAGE Stage;
    // The payload format is explicit: a shader profile alone does not say
    // whether Code is DXIL or DXBC.
    CK_SHADER_FORMAT Format;
    CK_SHADER_PROFILE Profile;
    const CKBYTE *Code;
    CKDWORD CodeSize;
    const char *EntryPoint;
    CKDWORD SamplerCount;
    CKDWORD StorageTextureCount;
    CKDWORD StorageBufferCount;
    CKDWORD UniformBufferCount;

    CKShaderDesc()
        : Stage(CKRST_SHADER_VERTEX), Format(CKRST_SHADER_FORMAT_UNKNOWN),
          Profile(CKRST_SHADER_PROFILE_UNKNOWN), Code(NULL), CodeSize(0),
          EntryPoint("main"), SamplerCount(0), StorageTextureCount(0),
          StorageBufferCount(0), UniformBufferCount(0) {}
};

// ===========================================================================
// Vertex Layout
// ===========================================================================

struct CKVertexElementDesc {
    CK_VERTEX_ATTRIB Attrib;
    CK_VERTEX_ATTRIB_TYPE Type;
    CKBYTE Count;
    CKBOOL Normalized;
    CKBOOL AsInt;
    CKWORD Offset;
};

struct CKVertexLayoutDesc {
    CKVertexElementDesc *Elements;
    CKDWORD ElementCount;
    CKWORD Stride;
};

// ===========================================================================
// Sampler
// ===========================================================================

struct CKSamplerDesc {
    CK_FILTER_MODE MinFilter;
    CK_FILTER_MODE MagFilter;
    CK_FILTER_MODE MipFilter;
    CK_ADDRESS_MODE AddressU;
    CK_ADDRESS_MODE AddressV;
    CK_ADDRESS_MODE AddressW;
    CKDWORD BorderColor;
    CK_COMPARE_MODE CompareFunc;
    CKDWORD MinMipLevel = 0;
    CKDWORD MaxAnisotropy = 0; // 0 keeps the backend default for direct API callers.
    CKDWORD ShaderAnisotropy = 0; // Fixed-function shader supplies the capped taps.
    float MipLodBias = 0.0f;
};

#endif // CKRASTERIZERCONTEXTTYPES_H
