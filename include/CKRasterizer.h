#ifndef CKRASTERIZER_H
#define CKRASTERIZER_H

// CKRasterizer v3 contract (spec: docs/spec/2026-09-01-render-engine-
// redesign-v3.md, section 4). The engine talks to a rasterizer plugin
// exclusively through the three classes declared here. Everything that is
// not a fixed-function state, a draw, a resource handle or a target / readback
// operation is an implementation detail of the plugin.

#include "VxDefines.h"
#include "VxMath.h"
#include "CKError.h"
#include "CKRasterizerEnums.h"
#include "CKRasterizerTypes.h"

class CKRasterizerDriver;
class CKRasterizerContext;
class CKRasterizer;

// ===========================================================================
// Plugin entry point (unchanged since v1)
// ===========================================================================
// A rasterizer DLL exports one function, "CKRasterizerGetInfo", that fills
// this structure. The render engine rejects plugins whose InterfaceRevision
// differs from CKRST_INTERFACE_REVISION.

struct CKRasterizerInfo {
    XString DllName;
    XString Desc;
    INSTANCE_HANDLE DllInstance;
    CKRST_STARTFUNCTION StartFct;
    CKRST_CLOSEFUNCTION CloseFct;
    CKDWORD InterfaceRevision;

    CKRasterizerInfo()
        : DllInstance(NULL), StartFct(NULL), CloseFct(NULL), InterfaceRevision(0) {}
};

typedef void (*CKRST_GETINFO)(CKRasterizerInfo *);

// ===========================================================================
// CKRasterizer: one instance per plugin, owns the drivers (spec 4.2)
// ===========================================================================

class CKRasterizer {
public:
    CKRasterizer() : m_MainWindow(NULL) {}
    virtual ~CKRasterizer() {}

    // Enumerates drivers. Returns FALSE when no usable driver exists.
    virtual CKBOOL Start(WIN_HANDLE AppWnd) = 0;
    virtual void Close() = 0;

    virtual int GetDriverCount() { return m_Drivers.Size(); }
    virtual CKRasterizerDriver *GetDriver(CKDWORD Index)
    {
        return Index < (CKDWORD)m_Drivers.Size() ? m_Drivers[Index] : NULL;
    }

public:
    WIN_HANDLE m_MainWindow;
    XArray<CKRasterizerDriver *> m_Drivers;
};

// ===========================================================================
// CKRasterizerDriver: one per adapter / implementation (spec 4.2, 4.9.2)
// ===========================================================================
// m_3DCaps / m_2DCaps MUST follow the caps baseline (spec 4.9.2); only numeric
// fields may be lowered to the real backend limits. m_TextureFormats lists
// the storage formats the driver accepts; any Virtools pixel format is still
// accepted as upload input (spec 4.5).

class CKRasterizerDriver {
public:
    CKRasterizerDriver()
        : m_Hardware(FALSE), m_CapsUpToDate(FALSE), m_Owner(NULL), m_DriverIndex(0)
    {
        memset(&m_3DCaps, 0, sizeof(m_3DCaps));
        memset(&m_2DCaps, 0, sizeof(m_2DCaps));
    }
    virtual ~CKRasterizerDriver() {}

    // A driver MAY support a single context; the second call returns NULL.
    virtual CKRasterizerContext *CreateContext() = 0;
    virtual CKBOOL DestroyContext(CKRasterizerContext *Context) = 0;

public:
    CKBOOL m_Hardware;
    CKBOOL m_CapsUpToDate;
    CKRasterizer *m_Owner;
    CKDWORD m_DriverIndex;
    XArray<VxDisplayMode> m_DisplayModes;
    XClassArray<CKTextureDesc> m_TextureFormats;
    Vx3DCapsDesc m_3DCaps;
    Vx2DCapsDesc m_2DCaps;
    XString m_Desc;
    XArray<CKRasterizerContext *> m_Contexts;
};

// ===========================================================================
// CKRasterizerContext: the D3D7-shaped device (spec 4.2 - 4.13)
// ===========================================================================
// Error model (spec 4.10): every state setter returns CKBOOL. Any value of a
// valid state type is stored verbatim and returned by the matching getter;
// unsupported values are approximated at draw time and counted in
// CKRenderStats::Diagnostics. Only out-of-range state types, stage indices,
// light indices, clip plane indices and matrix types fail, without changing
// state. Draws fail only for invalid handles, invalid parameters or a lost
// device. All methods are called from the render thread (spec 4.11).

class CKRasterizerContext {
public:
    CKRasterizerContext()
        : m_Driver(NULL), m_PosX(0), m_PosY(0), m_Width(0), m_Height(0), m_Bpp(0), m_ZBpp(0),
          m_StencilBpp(0), m_Fullscreen(FALSE), m_RefreshRate(0), m_Window(NULL) {}
    virtual ~CKRasterizerContext() {}

    // --- Lifecycle (spec 4.2) ---
    // Window is the SDL_Window* the engine received as WIN_HANDLE; native
    // handle extraction is the backend's business.
    virtual CKBOOL Create(WIN_HANDLE Window, int PosX, int PosY, int Width, int Height,
                          int Bpp, CKBOOL Fullscreen, int RefreshRate, int Zbpp, int StencilBpp) = 0;
    virtual CKBOOL Resize(int PosX, int PosY, int Width, int Height, CKDWORD Flags) = 0;
    virtual CKBOOL SetOptions(const CKRasterizerOptions *Options) = 0;
    // Backend capabilities below the translation core: tests and diagnostics
    // only, the engine MUST NOT read them (spec 4.9.1).
    virtual CKBOOL GetCaps(CKRasterizerCapsDesc *Caps) const = 0;
    virtual CKERROR GetDeviceStatus() const = 0;
    virtual CKBOOL BeginShutdown() = 0;
    virtual CKBOOL IsIdle() const = 0;

    // --- Frame (spec 4.3) ---
    // Order: Clear* -> BeginScene -> draws -> EndScene -> BackToFront.
    // Clear MAY also be called inside the scene (immediate clear of the current
    // target; RectCount == 0 clears the current viewport). BeginOverlayPhase
    // marks the start of native-resolution drawing over the presented scene
    // image; it returns FALSE while a texture is the target.
    virtual CKBOOL Clear(CKDWORD Flags, CKDWORD Color, float Z, CKDWORD Stencil,
                         int RectCount, CKRECT *Rects) = 0;
    virtual CKBOOL BeginScene() = 0;
    virtual CKBOOL EndScene() = 0;
    virtual CKBOOL BeginOverlayPhase() = 0;
    virtual CKBOOL BackToFront(CKBOOL VSync) = 0;

    // --- Fixed-function state (spec 4.6) ---
    // Every setter has a getter (D3D7 shape) so the engine can save and
    // restore state around its special draws without rasterizer internals.
    virtual CKBOOL SetRenderState(VXRENDERSTATETYPE State, CKDWORD Value) = 0;
    virtual CKBOOL GetRenderState(VXRENDERSTATETYPE State, CKDWORD *Value) = 0;
    // CKRST_TSS_ADDRESS sets ADDRESSU, ADDRESSV and ADDRESW together. For the
    // nine combine states (OP, ARG1, ARG2, AOP, AARG1, AARG2, COLORARG0,
    // ALPHAARG0, RESULTARG0) and STAGEBLEND the value 0 means "not set": the
    // stage is then derived at draw time from TEXTUREMAPBLEND and the bound
    // texture, and a stage without texture is disabled. Setting
    // TEXTUREMAPBLEND resets the nine combine states to 0; setting a non-zero
    // STAGEBLEND stores the derived combine states.
    virtual CKBOOL SetTextureStageState(int Stage, CKRST_TEXTURESTAGESTATETYPE Tss, CKDWORD Value) = 0;
    virtual CKBOOL GetTextureStageState(int Stage, CKRST_TEXTURESTAGESTATETYPE Tss, CKDWORD *Value) = 0;
    virtual CKBOOL SetTexture(CKDWORD Texture, int Stage) = 0;
    virtual CKBOOL GetTexture(int Stage, CKDWORD *Texture) = 0;
    virtual CKBOOL SetTransformMatrix(VXMATRIX_TYPE Type, const VxMatrix &Mat) = 0;
    virtual CKBOOL GetTransformMatrix(VXMATRIX_TYPE Type, VxMatrix &Mat) = 0;
    virtual CKBOOL SetLight(CKDWORD Index, const CKLightData *Data) = 0;
    virtual CKBOOL EnableLight(CKDWORD Index, CKBOOL Enable) = 0;
    virtual CKBOOL SetMaterial(const CKMaterialData *Data) = 0;
    virtual CKBOOL SetViewport(const CKViewportData *Data) = 0;
    virtual CKBOOL SetUserClipPlane(CKDWORD Index, const VxPlane &Plane) = 0;
    virtual CKBOOL GetUserClipPlane(CKDWORD Index, VxPlane &Plane) = 0;
    // Resets every render state and texture stage state to the v1 defaults
    // (CKRSTDefaultRenderStateValue / CKRSTDefaultTextureStageStateValue).
    virtual void InitDefaultRenderStatesValue() = 0;

    // --- Draw (spec 4.7) ---
    // Indices are 16-bit. All six VXPRIMITIVETYPE topologies are accepted.
    virtual CKBOOL DrawPrimitive(VXPRIMITIVETYPE Type, CKWORD *Indices, int IndexCount,
                                 VxDrawPrimitiveData *Data) = 0;
    virtual CKBOOL DrawPrimitiveVB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD StartVertex,
                                   CKDWORD VertexCount, CKWORD *Indices, int IndexCount) = 0;
    virtual CKBOOL DrawPrimitiveVBIB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD IB,
                                     CKDWORD MinVertexIndex, CKDWORD VertexCount,
                                     CKDWORD StartIndex, int IndexCount) = 0;

    // --- Resources (spec 4.5) ---
    // Handles are allocated by the rasterizer; 0 is never a valid handle and a
    // deleted handle MAY be reused.
    virtual CKBOOL CreateTexture(const CKTextureDesc *Desc, CKDWORD *OutHandle) = 0;
    virtual CKBOOL LoadTexture(CKDWORD Texture, const VxImageDescEx &Image, int MipLevel,
                               CKRST_CUBEFACE Face, const CKRECT *Region) = 0;
    virtual CKBOOL GetTextureDesc(CKDWORD Texture, CKTextureDesc *Desc) const = 0;
    virtual CKBOOL CreateVertexBuffer(const CKVertexBufferDesc *Desc, const void *Data, CKDWORD *OutHandle) = 0;
    virtual CKBOOL CreateIndexBuffer(const CKIndexBufferDesc *Desc, const void *Data, CKDWORD *OutHandle) = 0;
    virtual void *LockVertexBuffer(CKDWORD VB, CKDWORD StartVertex, CKDWORD VertexCount, CKRST_LOCKFLAGS Flags) = 0;
    virtual CKBOOL UnlockVertexBuffer(CKDWORD VB) = 0;
    virtual void *LockIndexBuffer(CKDWORD IB, CKDWORD StartIndex, CKDWORD IndexCount, CKRST_LOCKFLAGS Flags) = 0;
    virtual CKBOOL UnlockIndexBuffer(CKDWORD IB) = 0;
    virtual CKBOOL DeleteObject(CKDWORD Handle, CKDWORD Type) = 0;
    virtual CKBOOL FlushObjects(CKDWORD TypeMask) = 0;
    virtual void SetResourceName(CKDWORD Handle, CKDWORD Type, CKSTRING Name) = 0;

    // --- Targets, readback, copies (spec 4.8) ---
    // Texture == 0 selects the virtual backbuffer. Only outside a scene.
    virtual CKBOOL SetTargetTexture(CKDWORD Texture, int Width, int Height, CKRST_CUBEFACE Face) = 0;
    virtual CKBOOL CopyToTexture(CKDWORD Texture, const VxRect *Src, const VxRect *Dst, CKRST_CUBEFACE Face) = 0;
    // Synchronous, outside a scene; returns the number of bytes written and
    // 0 on failure. The image is at native (window) resolution (spec 4.4).
    // Two-call protocol: when Image.Image is NULL the descriptor is filled
    // (size, 32-bit ARGB format) and the required byte count is returned
    // without copying anything.
    virtual int CopyToMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, VxImageDescEx &Image) = 0;
    virtual int CopyFromMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, const VxImageDescEx &Image) = 0;
    virtual CKBOOL RequestReadback(const CKRECT *Rect, VXBUFFER_TYPE Buffer,
                                   CKReadbackCallback Callback, void *User) = 0;

    // --- Diagnostics ---
    virtual void SetDebugMarker(CKSTRING Name) = 0;
    virtual const CKRenderStats *GetStats() = 0;

public:
    CKRasterizerDriver *m_Driver;
    CKDWORD m_PosX;
    CKDWORD m_PosY;
    CKDWORD m_Width;
    CKDWORD m_Height;
    CKDWORD m_Bpp;
    CKDWORD m_ZBpp;
    CKDWORD m_StencilBpp;
    CKDWORD m_Fullscreen;
    CKDWORD m_RefreshRate;
    WIN_HANDLE m_Window;
};

// ===========================================================================
// Inline helpers shared by the engine and the rasterizer
// ===========================================================================

// ---------------------------------------------------------------------------
// State type validation
// ---------------------------------------------------------------------------

inline CKBOOL CKRSTIsValidRenderStateType(CKDWORD State)
{
    return State < (CKDWORD)VXRENDERSTATE_MAXSTATE ? TRUE : FALSE;
}

inline CKBOOL CKRSTIsValidTextureStageStateType(CKDWORD Tss)
{
    return Tss >= (CKDWORD)CKRST_TSS_OP && Tss < (CKDWORD)CKRST_TSS_MAXSTATE ? TRUE : FALSE;
}

// Maps a VXMATRIX_TYPE to a dense storage slot in 0..CKRST_MATRIX_SLOT_COUNT-1
// (world matrices first so WORLD and WORLDMATRIX(0) share slot 0), or -1.
inline int CKRSTMatrixSlot(VXMATRIX_TYPE Type)
{
    const CKDWORD t = (CKDWORD)Type;
    if (t == (CKDWORD)VXMATRIX_WORLD)
        return 0;
    if (t >= (CKDWORD)VXMATRIX_WMAT && t < (CKDWORD)VXMATRIX_WMAT + CKRST_MAX_WORLD_MATRICES)
        return (int)(t - (CKDWORD)VXMATRIX_WMAT);
    if (t == (CKDWORD)VXMATRIX_VIEW)
        return CKRST_MAX_WORLD_MATRICES;
    if (t == (CKDWORD)VXMATRIX_PROJECTION)
        return CKRST_MAX_WORLD_MATRICES + 1;
    if (t >= (CKDWORD)VXMATRIX_TEXTURE0 && t < (CKDWORD)VXMATRIX_TEXTURE0 + CKRST_MAX_TEXTURE_STAGES)
        return CKRST_MAX_WORLD_MATRICES + 2 + (int)(t - (CKDWORD)VXMATRIX_TEXTURE0);
    return -1;
}

// ---------------------------------------------------------------------------
// Default state values (spec 4.6): original CKRasterizerContext::
// InitDefaultRenderStatesValue. States it does not list default to 0, except
// the v3 addition COLORWRITEENABLE which defaults to all channels.
// ---------------------------------------------------------------------------

inline CKDWORD CKRSTDefaultRenderStateValue(VXRENDERSTATETYPE State)
{
    switch ((CKDWORD)State) {
    case VXRENDERSTATE_SHADEMODE:        return VXSHADE_GOURAUD;    // 2
    case VXRENDERSTATE_SRCBLEND:         return VXBLEND_ONE;        // 2
    case VXRENDERSTATE_ALPHAFUNC:        return VXCMP_ALWAYS;       // 8
    case VXRENDERSTATE_STENCILFUNC:      return VXCMP_ALWAYS;       // 8
    case VXRENDERSTATE_STENCILMASK:      return 0xFFFFFFFFu;
    case VXRENDERSTATE_STENCILWRITEMASK: return 0xFFFFFFFFu;
    case VXRENDERSTATE_ZENABLE:          return 1;
    case VXRENDERSTATE_FILLMODE:         return VXFILL_SOLID;       // 3
    case VXRENDERSTATE_ZWRITEENABLE:     return 1;
    case VXRENDERSTATE_DESTBLEND:        return VXBLEND_ZERO;       // 1
    case VXRENDERSTATE_CULLMODE:         return VXCULL_CCW;         // 3
    case VXRENDERSTATE_ZFUNC:            return VXCMP_LESSEQUAL;    // 4
    case VXRENDERSTATE_STENCILFAIL:      return VXSTENCILOP_KEEP;   // 1
    case VXRENDERSTATE_STENCILZFAIL:     return VXSTENCILOP_KEEP;   // 1
    case VXRENDERSTATE_STENCILPASS:      return VXSTENCILOP_KEEP;   // 1
    case VXRENDERSTATE_TEXTUREFACTOR:    return 0xFF000000u;        // A_MASK
    case VXRENDERSTATE_CLIPPING:         return 1;
    case VXRENDERSTATE_LIGHTING:         return 1;
    case VXRENDERSTATE_LOCALVIEWER:      return 1;
    case VXRENDERSTATE_NORMALIZENORMALS: return 1;
    case 168 /* VXRENDERSTATE_COLORWRITEENABLE */: return CKRST_COLORWRITE_ALL;
    default:                             return 0;
    }
}

// D3D8 device defaults expressed with Virtools enumerations. Stage 0
// modulates texture and diffuse; every other stage is disabled.
inline CKDWORD CKRSTDefaultTextureStageStateValue(int Stage, CKRST_TEXTURESTAGESTATETYPE Tss)
{
    switch ((CKDWORD)Tss) {
    case CKRST_TSS_OP:            return Stage == 0 ? CKRST_TOP_MODULATE : CKRST_TOP_DISABLE;
    case CKRST_TSS_ARG1:          return CKRST_TA_TEXTURE;
    case CKRST_TSS_ARG2:          return CKRST_TA_CURRENT;
    case CKRST_TSS_AOP:           return Stage == 0 ? CKRST_TOP_SELECTARG1 : CKRST_TOP_DISABLE;
    case CKRST_TSS_AARG1:         return CKRST_TA_TEXTURE;
    case CKRST_TSS_AARG2:         return CKRST_TA_CURRENT;
    case CKRST_TSS_TEXCOORDINDEX: return (CKDWORD)Stage;
    case CKRST_TSS_ADDRESS:       return VXTEXTURE_ADDRESSWRAP;
    case CKRST_TSS_ADDRESSU:      return VXTEXTURE_ADDRESSWRAP;
    case CKRST_TSS_ADDRESSV:      return VXTEXTURE_ADDRESSWRAP;
    case CKRST_TSS_ADDRESW:       return VXTEXTURE_ADDRESSWRAP;
    case CKRST_TSS_MAGFILTER:     return VXTEXTUREFILTER_NEAREST;
    case CKRST_TSS_MINFILTER:     return VXTEXTUREFILTER_NEAREST;
    case CKRST_TSS_MAXANISOTROPY: return 1;
    case CKRST_TSS_COLORARG0:     return CKRST_TA_CURRENT;
    case CKRST_TSS_ALPHAARG0:     return CKRST_TA_CURRENT;
    case CKRST_TSS_RESULTARG0:    return CKRST_TA_CURRENT;
    default:                      return 0;
    }
}

// ---------------------------------------------------------------------------
// Canonical interleaved vertex layout (spec 4.5)
// ---------------------------------------------------------------------------
// Component order follows the D3D FVF memory layout, with the tween set
// inserted after the normal:
//   position (xyz | xyzw)  weights  blend indices  normal  tween position
//   tween normal  point size  diffuse  specular  texcoord0..n
// Rules:
//   - CKRST_DP_TRANSFORM missing => pre-transformed xyzw position; weights,
//     blend indices, normal and tween data are not part of the layout.
//   - Weight count = index of the highest CKRST_DP_WEIGHTS* bit + 1 (0..5).
//   - CKRST_DP_MATRIXPAL adds one dword of four uint8 indices after weights.
//   - CKRST_DP_LIGHT => normal present. CKRST_DP_TWEEN => tween position,
//     plus tween normal when a normal is present.
//   - Texcoord set count = index of the highest CKRST_DP_STAGES* bit + 1.
//     Dimensions come from TexcoordDims (NULL or 0 entries => 2).
// Returns the stride in bytes. Layout may be NULL.

inline CKDWORD CKRSTGetVertexLayout(CKDWORD VertexFormat, const CKBYTE *TexcoordDims,
                                    CKRSTVertexLayout *Layout)
{
    CKRSTVertexLayout local;
    CKRSTVertexLayout &l = Layout ? *Layout : local;
    l.PositionOffset = l.WeightOffset = l.BlendIndexOffset = l.NormalOffset = -1;
    l.TweenPositionOffset = l.TweenNormalOffset = l.PointSizeOffset = -1;
    l.DiffuseOffset = l.SpecularOffset = -1;
    l.WeightCount = 0;
    l.TexcoordCount = 0;
    for (int i = 0; i < CKRST_MAX_TEXTURE_STAGES; ++i) {
        l.TexcoordOffset[i] = -1;
        l.TexcoordDims[i] = 0;
    }

    const CKBOOL transformed = (VertexFormat & CKRST_DP_TRANSFORM) != 0;
    CKDWORD offset = 0;

    l.PositionOffset = 0;
    l.PositionComponents = transformed ? 3 : 4;
    offset += (CKDWORD)l.PositionComponents * 4;

    if (transformed) {
        CKDWORD weights = 0;
        for (CKDWORD bit = VertexFormat & CKRST_DP_WEIGHTMASK; bit != 0; bit >>= 1)
            ++weights;
        // WEIGHTS1 is bit 20: shift the count back to 1..5.
        if (weights > 20)
            weights -= 20;
        else
            weights = 0;
        if (weights > 5)
            weights = 5;
        if (weights > 0) {
            l.WeightOffset = (int)offset;
            l.WeightCount = (int)weights;
            offset += weights * 4;
            if (VertexFormat & CKRST_DP_MATRIXPAL) {
                l.BlendIndexOffset = (int)offset;
                offset += 4;
            }
        }
        if (VertexFormat & CKRST_DP_LIGHT) {
            l.NormalOffset = (int)offset;
            offset += 12;
        }
        if (VertexFormat & CKRST_DP_TWEEN) {
            l.TweenPositionOffset = (int)offset;
            offset += 12;
            if (VertexFormat & CKRST_DP_LIGHT) {
                l.TweenNormalOffset = (int)offset;
                offset += 12;
            }
        }
    }

    if (VertexFormat & CKRST_DP_PSIZE) {
        l.PointSizeOffset = (int)offset;
        offset += 4;
    }
    if (VertexFormat & CKRST_DP_DIFFUSE) {
        l.DiffuseOffset = (int)offset;
        offset += 4;
    }
    if (VertexFormat & CKRST_DP_SPECULAR) {
        l.SpecularOffset = (int)offset;
        offset += 4;
    }

    int texcoordCount = 0;
    for (CKDWORD bit = CKRST_DP_STAGEFLAGS(VertexFormat); bit != 0; bit >>= 1)
        ++texcoordCount;
    if (texcoordCount > CKRST_MAX_TEXTURE_STAGES)
        texcoordCount = CKRST_MAX_TEXTURE_STAGES;
    l.TexcoordCount = texcoordCount;
    for (int i = 0; i < texcoordCount; ++i) {
        int dims = TexcoordDims ? (int)TexcoordDims[i] : 0;
        if (dims < 1 || dims > CKRST_MAX_TEXCOORD_DIMS)
            dims = 2;
        l.TexcoordOffset[i] = (int)offset;
        l.TexcoordDims[i] = dims;
        offset += (CKDWORD)dims * 4;
    }

    l.Stride = offset;
    return offset;
}

inline CKDWORD CKRSTGetVertexSize(CKDWORD VertexFormat, const CKBYTE *TexcoordDims)
{
    return CKRSTGetVertexLayout(VertexFormat, TexcoordDims, NULL);
}

#endif // CKRASTERIZER_H
