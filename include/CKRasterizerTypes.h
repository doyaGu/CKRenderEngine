#ifndef CKRASTERIZERTYPES_H
#define CKRASTERIZERTYPES_H

#include "VxDefines.h"
#include "VxColor.h"
#include "XArray.h"
#include "CKTypes.h"
#include "CKRasterizerEnums.h"

class CKRasterizerDriver;
class CKRasterizerContext;
class CKRasterizer;

// ===========================================================================
// Function typedefs for rasterizer DLL entry points
// ===========================================================================

typedef CKRasterizer *(*CKRST_STARTFUNCTION)(WIN_HANDLE);
typedef void (*CKRST_CLOSEFUNCTION)(CKRasterizer *);

// ===========================================================================
// Driver Problems
// ===========================================================================

typedef struct CKDriverProblems
{
    XString m_Vendor;
    XString m_Renderer;
    XString m_DeviceDesc;
    XString m_Version;

    CKBOOL m_VersionMustBeExact;
    XArray<VX_OSINFO> m_ConcernedOS;
    CKBOOL m_OnlyIn16;
    CKBOOL m_OnlyIn32;

    int m_RealMaxTextureWidth;
    int m_RealMaxTextureHeight;
    CKBOOL m_ClampToEdgeBug;
    XArray<VX_PIXELFORMAT> m_TextureFormatsRGBABug;

    CKDriverProblems()
    {
        m_ClampToEdgeBug = FALSE;
        m_VersionMustBeExact = FALSE;
        m_OnlyIn16 = FALSE;
        m_OnlyIn32 = FALSE;
        m_RealMaxTextureWidth = 0;
        m_RealMaxTextureHeight = 0;
    }
} CKDriverProblems;

// ===========================================================================
// Resource Descriptor Base
// ===========================================================================

struct CKRasterizerObjectDesc
{
    virtual ~CKRasterizerObjectDesc() {}
};

// ===========================================================================
// Texture Descriptor
// ===========================================================================

struct CKTextureDesc : public CKRasterizerObjectDesc
{
    CKDWORD Flags;
    VxImageDescEx Format;
    CKDWORD MipMapCount;
    CKDWORD Depth;

    CKTextureDesc() : Flags(0), MipMapCount(0), Depth(1) {}
    virtual ~CKTextureDesc() {}
};

// ===========================================================================
// Vertex Buffer Descriptor
// ===========================================================================

struct CKVertexBufferDesc : CKRasterizerObjectDesc
{
    CKDWORD m_Flags;
    CKDWORD m_VertexFormat;
    CKDWORD m_MaxVertexCount;
    CKDWORD m_VertexSize;
    CKDWORD m_CurrentVCount;

    CKVertexBufferDesc()
    {
        m_Flags = m_VertexFormat = m_MaxVertexCount = m_VertexSize = m_CurrentVCount = 0;
    }
    virtual ~CKVertexBufferDesc() {}

    CKVertexBufferDesc &operator=(const CKVertexBufferDesc &b)
    {
        m_Flags = b.m_Flags;
        m_VertexFormat = b.m_VertexFormat;
        m_MaxVertexCount = b.m_MaxVertexCount;
        m_VertexSize = b.m_VertexSize;
        m_CurrentVCount = b.m_CurrentVCount;
        return *this;
    }
};

// ===========================================================================
// Index Buffer Descriptor
// ===========================================================================

struct CKIndexBufferDesc : CKRasterizerObjectDesc
{
    CKDWORD m_Flags;
    CKDWORD m_MaxIndexCount;
    CKDWORD m_CurrentICount;

    CKIndexBufferDesc()
    {
        m_Flags = m_MaxIndexCount = m_CurrentICount = 0;
    }
    virtual ~CKIndexBufferDesc() {}

    CKIndexBufferDesc &operator=(const CKIndexBufferDesc &b)
    {
        m_Flags = b.m_Flags;
        m_MaxIndexCount = b.m_MaxIndexCount;
        m_CurrentICount = b.m_CurrentICount;
        return *this;
    }
};

// ===========================================================================
// Render State Data (cache)
// ===========================================================================

struct CKRenderStateData
{
    CKDWORD Value;
    CKDWORD Valid;
    CKDWORD Flags;
    CKDWORD DefaultValue;
};

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

struct CKRasterizerCapsDesc {
    CKDWORD Size;
    CKDWORD Version;
    CKRST_CAPS Features;
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

    CKRasterizerCapsDesc()
        : Size(sizeof(CKRasterizerCapsDesc)), Version(1), Features(0),
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

struct CKDepthFormatCaps {
    CKDWORD Size;
    CK_DEPTH_FORMAT Format;
    CKDWORD Caps;

    CKDepthFormatCaps()
        : Size(sizeof(CKDepthFormatCaps)), Format(CKRST_DEPTHFMT_D24S8), Caps(0) {}
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
// Program Descriptor
// ===========================================================================

struct CKProgramDesc {
    CKDWORD VertexShader;
    CKDWORD PixelShader;
    CKBOOL ConsumeShaders;
};

// ===========================================================================
// Uniform Descriptor
// ===========================================================================

struct CKUniformDesc {
    CKSTRING Name;
    CK_UNIFORM_TYPE Type;
    CKDWORD Count;
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
// Frame Buffer
// ===========================================================================

struct CKFrameBufferAttachmentDesc {
    CKDWORD Texture;
    CKDWORD Mip;
    CKDWORD Layer;
};

struct CKFrameBufferDesc {
    CKFrameBufferAttachmentDesc *Color;
    CKDWORD ColorCount;
    CKFrameBufferAttachmentDesc DepthStencil;
};

// ===========================================================================
// Depth Texture
// ===========================================================================

struct CKDepthTextureDesc {
    CKDWORD Flags;
    CKDWORD Width;
    CKDWORD Height;
    CK_DEPTH_FORMAT DepthFormat;
    CKDWORD MipMapCount;
};

// ===========================================================================
// Occlusion Query
// ===========================================================================

struct CKOcclusionQueryDesc {
};

// ===========================================================================
// Indirect Buffer
// ===========================================================================

struct CKIndirectBufferDesc {
    CKDWORD MaxCommands;
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

// ===========================================================================
// Transient Buffers
// ===========================================================================

struct CKTransientVertexBuffer {
    void *Data;
    CKDWORD Size;
    CKDWORD StartVertex;
    CKDWORD VertexCount;
    CKDWORD Stride;
    CKDWORD Layout;
};

struct CKTransientIndexBuffer {
    void *Data;
    CKDWORD Size;
    CKDWORD StartIndex;
    CKDWORD IndexCount;
    CKBOOL Index32;
};

struct CKTransientInstanceBuffer {
    void *Data;
    CKDWORD Size;
    CKDWORD StartInstance;
    CKDWORD InstanceCount;
    CKDWORD Stride;
    CKDWORD Layout;
};

// ===========================================================================
// Uniform Info (Shader Reflection)
// ===========================================================================

struct CKUniformInfo {
    char Name[256];
    CK_UNIFORM_TYPE Type;
    CKDWORD Count;
};

// ===========================================================================
// Render Statistics
// ===========================================================================

struct CKRenderViewStats {
    CKSTRING Name;
    CKRenderView View;
    CKDWORD DrawCalls;
    int64_t CpuTimeBegin;
    int64_t CpuTimeEnd;
    int64_t GpuTimeBegin;
    int64_t GpuTimeEnd;
};

struct CKRenderStats {
    int64_t CpuTimeFrame;
    int64_t CpuTimerFreq;
    int64_t GpuTimeBegin;
    int64_t GpuTimeEnd;
    int64_t GpuTimerFreq;
    int64_t WaitRender;
    int64_t WaitSubmit;
    CKDWORD DrawCalls;
    CKDWORD BlitCalls;
    CKDWORD ComputeCalls;
    CKDWORD MaxGpuLatency;
    CKDWORD NumUpdatedVertexBuffers;
    CKDWORD NumUpdatedIndexBuffers;
    CKDWORD NumTransientVertexBuffers;
    CKDWORD NumTransientIndexBuffers;
    CKDWORD NumTransientInstanceBuffers;
    CKWORD NumViews;
    CKRenderViewStats *ViewStats;
    CKDWORD GpuMemoryMax;
    CKDWORD GpuMemoryUsed;
    CKDWORD Width;
    CKDWORD Height;
    CKDWORD TextWidth;
    CKDWORD TextHeight;
};

// ===========================================================================
// Texture Info (Size Calculation)
// ===========================================================================

struct CKTextureInfo {
    CKDWORD Format;
    CKDWORD StorageSize;
    CKWORD Width;
    CKWORD Height;
    CKWORD Depth;
    CKDWORD NumMips;
    CKDWORD BitsPerPixel;
    CKBOOL CubeMap;
};

// ===========================================================================
// Screenshot Callback
// ===========================================================================

typedef void (*CKScreenShotCallback)(void *UserData, CKDWORD FrameBuffer,
                                      CKDWORD Width, CKDWORD Height,
                                      CKDWORD Pitch, VX_PIXELFORMAT Format,
                                      const void *Data, CKDWORD Size,
                                      CKBOOL YFlip);

#endif // CKRASTERIZERTYPES_H
