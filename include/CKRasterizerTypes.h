#ifndef CKRASTERIZERTYPES_H
#define CKRASTERIZERTYPES_H

// CKRasterizer interface: descriptor and data structures.

#include <stdint.h>

#include "VxDefines.h"
#include "VxMath.h"
#include "CKTypes.h"
#include "CKRasterizerEnums.h"
#include "CKRasterizerResourceTypes.h"

class CKRasterizerDriver;
class CKRasterizerContext;
class CKRasterizer;

struct CKRasterizerDriverDesc {
    CKDWORD DriverIndex;
    CKBOOL Hardware;
    CKBOOL CapsFinal;
    XString Description;
};

struct CKRasterizerContextDesc {
    int PosX;
    int PosY;
    int Width;
    int Height;
    int Bpp;
    int ZBpp;
    int StencilBpp;
    CKBOOL Fullscreen;
    int RefreshRate;
    WIN_HANDLE Window;
};

struct CKRasterizerNativeCapsDesc {
    uint64_t Features;
    CKDWORD MaxTextureSize;
    CKDWORD MaxTextureStages;
    CKDWORD MaxAnisotropy;
    CKDWORD MaxUserClipPlanes;
    CKDWORD MaxVertexBlendMatrices;
    CKDWORD MaxMSAASamples;
    float MaxPointSize;
    CKDWORD MaxLights;

    CKRasterizerNativeCapsDesc()
        : Features(0), MaxTextureSize(0), MaxTextureStages(0), MaxAnisotropy(0),
          MaxUserClipPlanes(0), MaxVertexBlendMatrices(0),
          MaxMSAASamples(0), MaxPointSize(0.0f), MaxLights(0) {}
};

// ===========================================================================
// DLL entry points
// ===========================================================================

typedef CKRasterizer *(*CKRST_STARTFUNCTION)(WIN_HANDLE);
typedef void (*CKRST_CLOSEFUNCTION)(CKRasterizer *);

// ===========================================================================
// Vertex buffer descriptor
// ===========================================================================
// m_VertexFormat is the vertex-data subset of CKRST_DPFLAGS (CKRST_VF_MASK).
// m_TexcoordDims[i] gives the component count (1..4) of texture coordinate
// set i; 0 means the default of 2. m_VertexSize is filled by the rasterizer
// from CKRSTGetVertexLayout() on creation.

struct CKVertexBufferDesc {
    CKDWORD m_Flags;          // CKRST_VBFLAGS
    CKDWORD m_VertexFormat;
    CKDWORD m_MaxVertexCount;
    CKDWORD m_VertexSize;
    CKDWORD m_CurrentVCount;
    CKBYTE m_TexcoordDims[CKRST_MAX_TEXTURE_STAGES];

    CKVertexBufferDesc()
        : m_Flags(0), m_VertexFormat(0), m_MaxVertexCount(0), m_VertexSize(0), m_CurrentVCount(0)
    {
        for (int i = 0; i < CKRST_MAX_TEXTURE_STAGES; ++i)
            m_TexcoordDims[i] = 0;
    }
};

// ===========================================================================
// Index buffer descriptor: 16-bit indices
// ===========================================================================

struct CKIndexBufferDesc {
    CKDWORD m_Flags;          // CKRST_VBFLAGS
    CKDWORD m_MaxIndexCount;
    CKDWORD m_CurrentICount;

    CKIndexBufferDesc() : m_Flags(0), m_MaxIndexCount(0), m_CurrentICount(0) {}
};

// ===========================================================================
// Canonical interleaved vertex layout
// ===========================================================================
// Byte offsets of each component inside one vertex, -1 when absent. Derived
// from a vertex format by CKRSTGetVertexLayout() in CKRasterizer.h. Both the
// engine (writing Lock memory) and the rasterizer (declaring the backend
// vertex layout) use it.

struct CKRSTVertexLayout {
    CKDWORD Stride;
    int PositionOffset;      // 3 floats (x, y, z) or 4 floats (x, y, z, rhw)
    int PositionComponents;  // 3 or 4
    int WeightOffset;        // WeightCount floats
    int WeightCount;         // 0..5
    int BlendIndexOffset;    // 4 x uint8 matrix palette indices
    int NormalOffset;        // 3 floats
    int TweenPositionOffset; // 3 floats
    int TweenNormalOffset;   // 3 floats
    int PointSizeOffset;     // 1 float
    int DiffuseOffset;       // ARGB dword
    int SpecularOffset;      // ARGB dword
    int TexcoordCount;       // 0..8
    int TexcoordOffset[CKRST_MAX_TEXTURE_STAGES];
    int TexcoordDims[CKRST_MAX_TEXTURE_STAGES];
};

// ===========================================================================
// Fixed-function data
// ===========================================================================

struct CKViewportData {
    CKDWORD ViewX;
    CKDWORD ViewY;
    CKDWORD ViewWidth;
    CKDWORD ViewHeight;
    float ViewZMin;
    float ViewZMax;

    CKViewportData()
        : ViewX(0), ViewY(0), ViewWidth(0), ViewHeight(0), ViewZMin(0.0f), ViewZMax(1.0f) {}
};

struct CKMaterialData {
    VxColor Diffuse;
    VxColor Ambient;
    VxColor Specular;
    VxColor Emissive;
    float SpecularPower;
};

// Base material operation. Texture residency/callbacks and alpha-test ownership
// remain with CK_3D; disabled blending preserves the existing blend factors.
struct CKMaterialRenderState {
    CKMaterialData Material;
    CKDWORD CullMode, FillMode, ShadeMode;
    CKBOOL AlphaBlend;
    CKDWORD SourceBlend, DestBlend;
    CKBOOL ZWrite;
    CKDWORD ZFunc;
};

struct CKLightData {
    VXLIGHT_TYPE Type;
    VxColor Diffuse;
    VxColor Specular;
    VxColor Ambient;
    VxVector Position;
    VxVector Direction;
    float Range;
    float Falloff;
    // Legacy Virtools/DX5 intensity coefficients. The fixed-function layer
    // converts these using Range before evaluating distance attenuation.
    float Attenuation0;
    float Attenuation1;
    float Attenuation2;
    float InnerSpotCone;
    float OuterSpotCone;
};

// ===========================================================================
// Rasterizer options
// ===========================================================================
// Merges the presentation and sampler options that CK2_3D.ini exposes.
// SetOptions() may be called at any frame boundary.

struct CKRasterizerOptions {
    CKDWORD MSAASamples;               // Antialias (0 / 1 = off)
    float RenderScale;                 // RenderScale, clamped to 0.5..2.0
    CKBOOL FXAA;                       // FXAA
    float Sharpness;                   // Sharpness, clamped to 0..1
    CKBOOL DisableTextureFiltering;    // DisableFilter
    CKBOOL DisableMipmaps;             // DisableMipmap
    CKBOOL ForceAnisotropicFiltering;  // ForceAnisotropicFiltering
    CKDWORD DebugFlags;                // CKRST_DEBUG_*

    CKRasterizerOptions()
        : MSAASamples(0), RenderScale(1.0f), FXAA(FALSE),
          Sharpness(0.0f), DisableTextureFiltering(FALSE), DisableMipmaps(FALSE),
          ForceAnisotropicFiltering(FALSE), DebugFlags(CKRST_DEBUG_NONE) {}
};

// ===========================================================================
// Concrete rasterizer capabilities - tests and diagnostics only
// ===========================================================================

struct CKRasterizerCapsDesc {
    CKRST_CAPS Features;
    CKDWORD MaxTextureSize;
    CKDWORD MaxTextureStages;
    CKDWORD MaxAnisotropy;
    CKDWORD MaxUserClipPlanes;
    CKDWORD MaxVertexBlendMatrices;
    CKDWORD MaxMSAASamples;
    float MaxPointSize;
    CKDWORD MaxLights;

    CKRasterizerCapsDesc()
        : Features(0), MaxTextureSize(0),
          MaxTextureStages(0), MaxAnisotropy(0), MaxUserClipPlanes(0),
          MaxVertexBlendMatrices(0), MaxMSAASamples(0), MaxPointSize(0.0f),
          MaxLights(0) {}
};

// ===========================================================================
// Asynchronous readback
// ===========================================================================
// Image is valid only for the duration of the callback. Success is FALSE
// when the readback could not be completed (device lost, shutdown).

typedef void (*CKReadbackCallback)(void *User, const CKRECT *Rect, VXBUFFER_TYPE Buffer,
                                   const VxImageDescEx *Image, CKBOOL Success);

// ===========================================================================
// Statistics returned by GetStats
// ===========================================================================

struct CKRenderStats {
    CKDWORD FrameNumber;       // Frames presented since creation
    int64_t CpuTimeFrame;      // Last frame CPU time in CpuTimerFreq units
    int64_t CpuTimerFreq;
    int64_t GpuTimeFrame;      // 0 when the backend has no GPU timer
    int64_t GpuTimerFreq;
    CKDWORD DrawCalls;         // Last frame
    CKDWORD Primitives;        // Last frame
    CKDWORD Passes;            // Last frame (scene passes incl. mid-frame clears, present)
    CKDWORD Clears;            // Last frame
    CKDWORD TextureUploads;    // Last frame
    CKDWORD BufferUploads;     // Last frame
    CKDWORD GpuMemoryMax;
    CKDWORD GpuMemoryUsed;
    CKDWORD Width;             // Virtual backbuffer size
    CKDWORD Height;
    CKDWORD Diagnostics[CKRST_DIAG_COUNT]; // Cumulative, see CKRST_DIAGNOSTIC
};

#endif // CKRASTERIZERTYPES_H
