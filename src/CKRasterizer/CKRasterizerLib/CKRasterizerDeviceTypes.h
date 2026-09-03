#ifndef CKRASTERIZERDEVICETYPES_H
#define CKRASTERIZERDEVICETYPES_H

#include "VxDefines.h"
#include "VxColor.h"
#include "XArray.h"
#include "CKTypes.h"
#include "CKRasterizerDeviceEnums.h"
// CKTextureDesc, CKVertexBufferDesc and CKIndexBufferDesc are the v3 contract
// descriptors; the device interface shares them.
#include "CKRasterizerTypes.h"

// ===========================================================================
// Shader Descriptor
// ===========================================================================

struct CKRasterizerTargetDesc {
    CKDWORD Size;
    CKDWORD Version;
    CK_SHADER_PROFILE ShaderProfile;
    CKBOOL HomogeneousDepth;
    CKBOOL OriginBottomLeft;

    CKRasterizerTargetDesc()
        : Size(sizeof(CKRasterizerTargetDesc)),
          Version(1),
          ShaderProfile(CKRST_SHADER_PROFILE_UNKNOWN),
          HomogeneousDepth(FALSE),
          OriginBottomLeft(FALSE) {}
};

struct CKRasterizerDeviceCapsDesc {
    CKDWORD Size;
    CKDWORD Version;
    CKRST_DEVCAPS Features;
    CKDWORD MaxDrawCalls;
    CKDWORD MaxBlits;
    CKDWORD MaxTextureSize;
    CKDWORD MaxTextureLayers;
    CKDWORD MaxRenderViews;
    CKDWORD MaxFrameBuffers;
    CKDWORD MaxColorAttachments;
    CKDWORD MaxPrograms;
    CKDWORD MaxShaders;
    CKDWORD MaxTextures;
    CKDWORD MaxTextureStages;
    CKDWORD MaxTextureBindings;
    CKDWORD MaxComputeBindings;
    CKDWORD MaxVertexLayouts;
    CKDWORD MaxVertexStreams;
    CKDWORD MaxIndexBuffers;
    CKDWORD MaxVertexBuffers;
    CKDWORD MaxDynamicIndexBuffers;
    CKDWORD MaxDynamicVertexBuffers;
    CKDWORD MaxUniforms;
    CKDWORD MaxOcclusionQueries;
    CKDWORD MaxEncoders;
    CKDWORD MinResourceCommandBufferSize;
    CKDWORD MaxTransientVertexBufferSize;
    CKDWORD MaxTransientIndexBufferSize;
    CKDWORD MinUniformBufferSize;
    CKDWORD MaxTransforms;

    CKRasterizerDeviceCapsDesc()
        : Size(sizeof(CKRasterizerDeviceCapsDesc)), Version(1), Features(0),
          MaxDrawCalls(0), MaxBlits(0), MaxTextureSize(0),
          MaxTextureLayers(0), MaxRenderViews(0), MaxFrameBuffers(0),
          MaxColorAttachments(0), MaxPrograms(0), MaxShaders(0),
          MaxTextures(0), MaxTextureStages(0), MaxTextureBindings(0),
          MaxComputeBindings(0),
          MaxVertexLayouts(0), MaxVertexStreams(0), MaxIndexBuffers(0),
          MaxVertexBuffers(0), MaxDynamicIndexBuffers(0),
          MaxDynamicVertexBuffers(0), MaxUniforms(0),
          MaxOcclusionQueries(0), MaxEncoders(0),
          MinResourceCommandBufferSize(0), MaxTransientVertexBufferSize(0),
          MaxTransientIndexBufferSize(0), MinUniformBufferSize(0),
          MaxTransforms(0) {}
};

struct CKTextureFormatCaps {
    CKDWORD Size;
    VX_PIXELFORMAT Format;
    CKDWORD Caps;

    CKTextureFormatCaps()
        : Size(sizeof(CKTextureFormatCaps)), Format(UNKNOWN_PF), Caps(0) {}
};

struct CKReadbackDesc {
    CKDWORD Size;
    void *Data;
    CKDWORD Capacity;
    CKDWORD RequiredSize;
    CKDWORD RowPitch;
    CKDWORD Width;
    CKDWORD Height;
    VX_PIXELFORMAT Format;
    CKBOOL YFlip;

    CKReadbackDesc()
        : Size(sizeof(CKReadbackDesc)), Data(NULL), Capacity(0),
          RequiredSize(0), RowPitch(0), Width(0), Height(0),
          Format(UNKNOWN_PF), YFlip(FALSE) {}
};

struct CKShaderDesc {
    CK_SHADER_STAGE Stage;
    // Opaque precompiled shader blob for the selected rasterizer backend.
    // Backend-specific payload selection happens above the rasterizer layer.
    CK_SHADER_FORMAT Format;
    CK_SHADER_PROFILE Profile;
    const CKBYTE *Code;
    CKDWORD CodeSize;
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
};

#endif // CKRASTERIZERDEVICETYPES_H
