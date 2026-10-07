#include "CKFFContextState.h"

#include "CKFixedFunctionPipeline.h"
#include "CKFFBufferUseTracker.h"
#include "CKFFUniformState.h"
#include "CKRasterizerContextData.h"

#include <math.h>

CKBOOL CKFFValidatePrimitive(VXPRIMITIVETYPE Type, int ElementCount)
{
    int minimum = 0;
    switch (Type) {
    case VX_POINTLIST:     minimum = 1; break;
    case VX_LINELIST:
    case VX_LINESTRIP:     minimum = 2; break;
    case VX_TRIANGLELIST:
    case VX_TRIANGLESTRIP:
    case VX_TRIANGLEFAN:   minimum = 3; break;
    default:               return FALSE;
    }
    return ElementCount >= minimum ? TRUE : FALSE;
}

int CKFFPrimitiveCount(VXPRIMITIVETYPE Type, int ElementCount)
{
    switch (Type) {
    case VX_POINTLIST:     return ElementCount;
    case VX_LINELIST:      return ElementCount / 2;
    case VX_LINESTRIP:     return ElementCount > 1 ? ElementCount - 1 : 0;
    case VX_TRIANGLELIST:  return ElementCount / 3;
    case VX_TRIANGLESTRIP:
    case VX_TRIANGLEFAN:   return ElementCount > 2 ? ElementCount - 2 : 0;
    default:               return 0;
    }
}

CKRECT CKFFMakeRect(int Width, int Height)
{
    CKRECT rect;
    rect.left = 0;
    rect.top = 0;
    rect.right = Width;
    rect.bottom = Height;
    return rect;
}

CKRECT CKFFScaleRect(const CKRECT &Rect, const CKRECT &Logical,
                     const CKRECT &Physical)
{
    if ((Logical.right == Physical.right &&
         Logical.bottom == Physical.bottom) ||
        Logical.right <= 0 || Logical.bottom <= 0)
        return Rect;

    const double sx = (double)Physical.right / (double)Logical.right;
    const double sy = (double)Physical.bottom / (double)Logical.bottom;
    CKRECT scaled;
    scaled.left = (int)floor(Rect.left * sx);
    scaled.top = (int)floor(Rect.top * sy);
    scaled.right = (int)ceil(Rect.right * sx);
    scaled.bottom = (int)ceil(Rect.bottom * sy);
    if (scaled.left < 0) scaled.left = 0;
    if (scaled.top < 0) scaled.top = 0;
    if (scaled.right > Physical.right) scaled.right = Physical.right;
    if (scaled.bottom > Physical.bottom) scaled.bottom = Physical.bottom;
    return scaled;
}

CKDWORD CKFFScaledDimension(CKDWORD Value, float Scale, CKDWORD Maximum)
{
    if (Maximum == 0)
        return 0;
    if (Value == 0)
        Value = 1;
    const float scaled = (float)Value * Scale;
    if (scaled <= 1.0f)
        return 1;
    if (scaled >= (float)Maximum)
        return Maximum;
    return (CKDWORD)(scaled + 0.5f);
}

CKBOOL CKFFCanContinuePass(const CKFFFrameState &Frame,
                           CKDWORD OpenRenderTarget,
                           CKDWORD RenderTarget, const CKRECT &Rect)
{
    const CKRECT &open = Frame.PassRect;
    return Frame.PassOpen && OpenRenderTarget == RenderTarget &&
           open.left == Rect.left && open.top == Rect.top &&
           open.right == Rect.right && open.bottom == Rect.bottom;
}

void CKFFCountDraw(CKDWORD &DrawCalls, CKDWORD &Primitives,
                   VXPRIMITIVETYPE Type, int ElementCount)
{
    ++DrawCalls;
    Primitives += (CKDWORD)CKFFPrimitiveCount(Type, ElementCount);
}

CKRST_DIAGNOSTIC CKFFDrawRejectDiagnostic(
    const CKFixedFunctionPipeline &Pipeline)
{
    switch (Pipeline.GetLastDrawRejectReason()) {
    case CKFF_DRAW_REJECT_INVALID_INPUT:
    case CKFF_DRAW_REJECT_TEXTURE_OP:
    case CKFF_DRAW_REJECT_STATE_VALUE:
        return CKRST_DIAG_REJECT_INVALID_PARAMETER;
    default:
        return CKRST_DIAG_REJECT_UNSUPPORTED_STATE;
    }
}

void CKFFRecordDrawApproximations(CKRenderStats &Stats, uint64_t Mask)
{
    for (CKDWORD code = 0;
         Mask != 0 && code < CKRST_DIAG_COUNT;
         ++code, Mask >>= 1) {
        if ((Mask & 1ull) != 0)
            ++Stats.Diagnostics[code];
    }
}

CKBOOL CKFFIsPublicResourceType(CKRST_OBJECTTYPE Type)
{
    return Type == CKRST_OBJ_TEXTURE || Type == CKRST_OBJ_VERTEXBUFFER ||
           Type == CKRST_OBJ_INDEXBUFFER;
}

CKBOOL CKFFIsPublicResourceMask(CKRST_OBJECTMASK TypeMask)
{
    const CKDWORD supported = CKRST_OBJ_TEXTURE | CKRST_OBJ_VERTEXBUFFER |
                              CKRST_OBJ_INDEXBUFFER;
    return TypeMask == CKRST_OBJ_ALL || (((CKDWORD)TypeMask & ~supported) == 0) ? TRUE : FALSE;
}

CKBOOL CKFFDriverHasCapability(const CKRasterizerDriver *Driver, CKRST_CAPS Capability)
{
    CKRasterizerNativeCapsDesc caps;
    return Driver && Driver->GetNativeCaps(&caps) && (caps.Features & Capability) == Capability ? TRUE : FALSE;
}

void CKFFUpdateDriverCaps(const CKRasterizerDeviceCaps &DeviceCaps,
                          CKRasterizerNativeCapsDesc &DriverCaps)
{
    DriverCaps.Features = 0;
    if (DeviceCaps.Features & CKRST_DEVCAPS_TEXTURE_READBACK)
        DriverCaps.Features |= CKRST_CAPS_SYNC_READBACK;
    if (DeviceCaps.Features & CKRST_DEVCAPS_STENCIL_WRITE_MASK)
        DriverCaps.Features |= CKRST_CAPS_STENCIL_WRITE_MASK;
    if (DeviceCaps.Features & CKRST_DEVCAPS_TEXTURE_CUBE)
        DriverCaps.Features |= CKRST_CAPS_TEXTURE_CUBE;
    if (DeviceCaps.Features & CKRST_DEVCAPS_TEXTURE_3D)
        DriverCaps.Features |= CKRST_CAPS_TEXTURE_VOLUME;
    if (DeviceCaps.Features & CKRST_DEVCAPS_BLEND_EQUATION)
        DriverCaps.Features |= CKRST_CAPS_SEPARATE_ALPHA_BLEND;
    DriverCaps.MaxTextureSize = DeviceCaps.MaxTextureSize;
    DriverCaps.MaxTextureStages = XMin(DeviceCaps.MaxTextureBindings, (CKDWORD)CKRST_MAX_TEXTURE_STAGES);
    DriverCaps.MaxMSAASamples = DeviceCaps.MaxMSAASamples > 1 ? DeviceCaps.MaxMSAASamples : 1;
}

CKBOOL CKFFGetContextCaps(const CKRasterizerDeviceCaps &DeviceCaps,
                          const CKRasterizerDriver *Driver,
                          CKRasterizerCapsDesc *Caps)
{
    if (!Caps)
        return FALSE;

    CKRasterizerCapsDesc Result;
    CKRST_CAPS Features = CKRST_CAPS_POINT_SIZE | CKRST_CAPS_MSAA | CKRST_CAPS_BORDER_COLOR;
    if (DeviceCaps.Features & CKRST_DEVCAPS_TEXTURE_READBACK)
        Features |= CKRST_CAPS_SYNC_READBACK;
    if (DeviceCaps.Features & CKRST_DEVCAPS_TEXTURE_CUBE)
        Features |= CKRST_CAPS_TEXTURE_CUBE;
    if (DeviceCaps.Features & CKRST_DEVCAPS_TEXTURE_3D)
        Features |= CKRST_CAPS_TEXTURE_VOLUME;
    if (DeviceCaps.Features & CKRST_DEVCAPS_BLEND_EQUATION)
        Features |= CKRST_CAPS_SEPARATE_ALPHA_BLEND;
    if (CKFFDriverHasCapability(Driver, CKRST_CAPS_TEXTURE_DXT))
        Features |= CKRST_CAPS_TEXTURE_DXT;

    Result.Features = Features;
    Result.MaxTextureSize = DeviceCaps.MaxTextureSize;
    Result.MaxTextureStages = CKRST_MAX_TEXTURE_STAGES;
    Result.MaxAnisotropy = 16;
    Result.MaxUserClipPlanes = CKRST_MAX_USER_CLIP_PLANES;
    Result.MaxVertexBlendMatrices = CKRST_MAX_WORLD_MATRICES;
    Result.MaxMSAASamples = DeviceCaps.MaxMSAASamples > 1 ? DeviceCaps.MaxMSAASamples : 1;
    // Points larger than a pixel are expanded into quads, so only
    // POINTSIZE_MAX bounds them; report the common D3D9 device limit.
    Result.MaxPointSize = 8192.0f;
    Result.MaxLights = CKRST_MAX_LIGHTS;
    *Caps = Result;
    return TRUE;
}

void CKFFDetachDeletedResource(CKFixedFunctionPipeline &Pipeline,
                               CKFFBufferUseTracker &BufferUses,
                               CKRST_HANDLE Handle,
                               CKRST_OBJECTTYPE Type)
{
    if (Type == CKRST_OBJ_TEXTURE) {
        for (int Stage = 0; Stage < CKRST_MAX_TEXTURE_STAGES; ++Stage) {
            if (Pipeline.GetTexture(Stage) == Handle)
                Pipeline.SetTexture(Stage, 0, 0);
        }
    } else if (Type == CKRST_OBJ_VERTEXBUFFER) {
        BufferUses.Remove(CKRST_BUFFER_VERTEX, Handle);
    } else if (Type == CKRST_OBJ_INDEXBUFFER) {
        BufferUses.Remove(CKRST_BUFFER_INDEX, Handle);
    }
}

CKFFRenderTargetState::CKFFRenderTargetState()
{
    Reset();
}

CKBOOL CKFFRenderTargetState::Set(
    CKDWORD TextureHandle, const CKTextureDesc &Desc,
    int RequestedWidth, int RequestedHeight,
    CKRST_CUBEFACE RequestedFace)
{
    const CKBOOL cube = (Desc.Flags & CKRST_TEXTURE_CUBEMAP) != 0;
    const int width = Desc.Format.Width;
    const int height = Desc.Format.Height;
    if (!TextureHandle || (Desc.Flags & CKRST_TEXTURE_RENDERTARGET) == 0 ||
        width <= 0 || height <= 0 ||
        (CKDWORD)RequestedFace >= CKRST_CUBEFACE_COUNT ||
        (!cube && RequestedFace != CKRST_CUBEFACE_XPOS) ||
        (cube && width != height) ||
        (RequestedWidth > 0 && RequestedWidth != width) ||
        (RequestedHeight > 0 && RequestedHeight != height))
        return FALSE;

    Texture = TextureHandle;
    Face = RequestedFace;
    Width = (CKDWORD)width;
    Height = (CKDWORD)height;
    return TRUE;
}

void CKFFRenderTargetState::Reset()
{
    Texture = 0;
    Face = CKRST_CUBEFACE_XPOS;
    Width = 0;
    Height = 0;
    DepthFormat = CKRST_DEPTHFMT_D24S8;
}

CKDWORD CKFFTargetAlphaTestPrecision(const CKTextureDesc *TargetTexture,
                                     int BackBufferBpp)
{
    if (TargetTexture)
        return CKFFAlphaTestPrecisionForFormat(TargetTexture->Format);

    VxImageDescEx backBuffer;
    VxPixelFormat2ImageDesc(
        BackBufferBpp == 16 ? _16_RGB565 : _32_ARGB8888, backBuffer);
    return CKFFAlphaTestPrecisionForFormat(backBuffer);
}

static CKDWORD CKFFMaskBitCount(CKDWORD mask)
{
    CKDWORD count = 0;
    while (mask != 0) {
        count += mask & 1u;
        mask >>= 1;
    }
    return count;
}

CKFFColorTargetFormat CKFFTargetColorFormat(
    const CKTextureDesc *TargetTexture, int BackBufferBpp)
{
    VxImageDescEx backBuffer;
    const VxImageDescEx *format = TargetTexture ? &TargetTexture->Format : &backBuffer;
    if (!TargetTexture) {
        VxPixelFormat2ImageDesc(
            BackBufferBpp == 16 ? _16_RGB565 : _32_ARGB8888, backBuffer);
    }

    const CKDWORD red = CKFFMaskBitCount(format->RedMask);
    const CKDWORD green = CKFFMaskBitCount(format->GreenMask);
    const CKDWORD blue = CKFFMaskBitCount(format->BlueMask);
    const CKDWORD alpha = CKFFMaskBitCount(format->AlphaMask);
    if (red == 5 && green == 6 && blue == 5 && alpha == 0)
        return CKFF_COLOR_TARGET_RGB565;
    if (red == 5 && green == 5 && blue == 5 && alpha == 1)
        return CKFF_COLOR_TARGET_RGB5A1;
    if (red == 4 && green == 4 && blue == 4 && alpha == 4)
        return CKFF_COLOR_TARGET_RGBA4;
    return CKFF_COLOR_TARGET_RGBA8;
}

static void CKFFRecordDiagnostic(CKRenderStats &Stats,
                                 CKRST_DIAGNOSTIC Diagnostic)
{
    ++Stats.Diagnostics[Diagnostic];
}

CKBOOL CKFFSetRenderState(CKFixedFunctionPipeline &Pipeline,
                          CKRenderStats &Stats,
                          VXRENDERSTATETYPE State, CKDWORD Value)
{
    if (!CKRSTIsValidRenderStateType((CKDWORD)State)) {
        CKFFRecordDiagnostic(Stats, CKRST_DIAG_INVALID_RENDER_STATE);
        return FALSE;
    }
    Pipeline.SetRenderState(State, Value);
    return TRUE;
}

CKBOOL CKFFGetRenderState(CKFixedFunctionPipeline &Pipeline,
                          CKRenderStats &Stats,
                          VXRENDERSTATETYPE State, CKDWORD *Value)
{
    if (!Value)
        return FALSE;
    if (!CKRSTIsValidRenderStateType((CKDWORD)State)) {
        CKFFRecordDiagnostic(Stats, CKRST_DIAG_INVALID_RENDER_STATE);
        return FALSE;
    }
    *Value = Pipeline.QueryRenderState(State);
    return TRUE;
}

CKBOOL CKFFSetTextureStageState(CKFixedFunctionPipeline &Pipeline,
                                CKRenderStats &Stats, int Stage,
                                CKRST_TEXTURESTAGESTATETYPE State,
                                CKDWORD Value)
{
    if (Stage < 0 || Stage >= CKRST_MAX_TEXTURE_STAGES) {
        CKFFRecordDiagnostic(Stats, CKRST_DIAG_INVALID_STAGE_INDEX);
        return FALSE;
    }
    if (!CKRSTIsValidTextureStageStateType((CKDWORD)State)) {
        CKFFRecordDiagnostic(Stats, CKRST_DIAG_INVALID_STAGE_STATE);
        return FALSE;
    }

    switch (State) {
    case CKRST_TSS_OP:
    case CKRST_TSS_ARG1:
    case CKRST_TSS_ARG2:
    case CKRST_TSS_AOP:
    case CKRST_TSS_AARG1:
    case CKRST_TSS_AARG2:
    case CKRST_TSS_COLORARG0:
    case CKRST_TSS_ALPHAARG0:
    case CKRST_TSS_RESULTARG0:
    case CKRST_TSS_STAGEBLEND:
        if (Value == 0) {
            Pipeline.ClearTextureStageState(Stage, State);
            return TRUE;
        }
        break;
    default:
        break;
    }

    Pipeline.SetTextureStageState(Stage, State, Value);
    return TRUE;
}

CKBOOL CKFFGetTextureStageState(CKFixedFunctionPipeline &Pipeline,
                                CKRenderStats &Stats, int Stage,
                                CKRST_TEXTURESTAGESTATETYPE State,
                                CKDWORD *Value)
{
    if (!Value)
        return FALSE;
    if (Stage < 0 || Stage >= CKRST_MAX_TEXTURE_STAGES) {
        CKFFRecordDiagnostic(Stats, CKRST_DIAG_INVALID_STAGE_INDEX);
        return FALSE;
    }
    if (!CKRSTIsValidTextureStageStateType((CKDWORD)State)) {
        CKFFRecordDiagnostic(Stats, CKRST_DIAG_INVALID_STAGE_STATE);
        return FALSE;
    }
    *Value = Pipeline.QueryTextureStageState(Stage, State);
    return TRUE;
}

CKBOOL CKFFResetTextureStages(CKFixedFunctionPipeline &Pipeline,
                              CKRenderStats &Stats, int FirstStage,
                              int StageCount)
{
    if (FirstStage < 0 || FirstStage > CKRST_MAX_TEXTURE_STAGES ||
        StageCount < 0 || StageCount > CKRST_MAX_TEXTURE_STAGES - FirstStage) {
        CKFFRecordDiagnostic(Stats, CKRST_DIAG_INVALID_STAGE_INDEX);
        return FALSE;
    }
    Pipeline.ResetTextureStages(FirstStage, StageCount);
    return TRUE;
}

CKBOOL CKFFSetTextureBinding(CKFixedFunctionPipeline &Pipeline,
                             CKRenderStats &Stats, CKDWORD Texture,
                             const CKTextureDesc *TextureDesc, int Stage)
{
    if (Stage < 0 || Stage >= CKRST_MAX_TEXTURE_STAGES) {
        CKFFRecordDiagnostic(Stats, CKRST_DIAG_INVALID_STAGE_INDEX);
        return FALSE;
    }
    if (Texture && !TextureDesc) {
        CKFFRecordDiagnostic(Stats, CKRST_DIAG_REJECT_INVALID_HANDLE);
        return FALSE;
    }
    const CKDWORD flags = TextureDesc
        ? TextureDesc->Flags | CKRST_TEXTURE_VALID : 0;
    Pipeline.SetTexture(Stage, Texture, flags);
    return TRUE;
}

CKBOOL CKFFGetTextureBinding(CKFixedFunctionPipeline &Pipeline,
                             CKRenderStats &Stats, int Stage,
                             CKDWORD *Texture)
{
    if (!Texture)
        return FALSE;
    if (Stage < 0 || Stage >= CKRST_MAX_TEXTURE_STAGES) {
        CKFFRecordDiagnostic(Stats, CKRST_DIAG_INVALID_STAGE_INDEX);
        return FALSE;
    }
    *Texture = Pipeline.GetTexture(Stage);
    return TRUE;
}

CKBOOL CKFFSetTransform(CKFixedFunctionPipeline &Pipeline,
                        CKRenderStats &Stats, VXMATRIX_TYPE Type,
                        const VxMatrix &Matrix)
{
    const int slot = CKRSTMatrixSlot(Type);
    if (slot < 0) {
        CKFFRecordDiagnostic(Stats, CKRST_DIAG_INVALID_MATRIX_TYPE);
        return FALSE;
    }

    const CKDWORD type = (CKDWORD)Type;
    if (slot == 0) {
        Pipeline.SetTransform(VXMATRIX_WORLD, Matrix);
        Pipeline.SetVertexBlendMatrix(0, Matrix);
    } else if (slot < CKRST_MAX_WORLD_MATRICES) {
        Pipeline.SetVertexBlendMatrix((CKDWORD)slot, Matrix);
    } else if (type == (CKDWORD)VXMATRIX_VIEW ||
               type == (CKDWORD)VXMATRIX_PROJECTION) {
        Pipeline.SetTransform(Type, Matrix);
    } else {
        Pipeline.SetTransform(Type, Matrix);
    }
    return TRUE;
}

CKBOOL CKFFGetTransform(CKFixedFunctionPipeline &Pipeline,
                        CKRenderStats &Stats, VXMATRIX_TYPE Type,
                        VxMatrix &Matrix)
{
    if (CKRSTMatrixSlot(Type) < 0) {
        CKFFRecordDiagnostic(Stats, CKRST_DIAG_INVALID_MATRIX_TYPE);
        return FALSE;
    }
    return Pipeline.GetTransform(Type, Matrix);
}

CKBOOL CKFFSetLight(CKFixedFunctionPipeline &Pipeline,
                    CKRenderStats &Stats, CKDWORD Index,
                    const CKLightData *Data)
{
    if (Index >= CKRST_MAX_LIGHTS) {
        CKFFRecordDiagnostic(Stats, CKRST_DIAG_INVALID_LIGHT_INDEX);
        return FALSE;
    }
    if (!Data) {
        CKFFRecordDiagnostic(Stats, CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    Pipeline.SetLight((int)Index, Data);
    return TRUE;
}

CKBOOL CKFFEnableLight(CKFixedFunctionPipeline &Pipeline,
                       CKRenderStats &Stats, CKDWORD Index,
                       CKBOOL Enable)
{
    if (Index >= CKRST_MAX_LIGHTS) {
        CKFFRecordDiagnostic(Stats, CKRST_DIAG_INVALID_LIGHT_INDEX);
        return FALSE;
    }
    Pipeline.EnableLight((int)Index, Enable);
    return TRUE;
}

CKBOOL CKFFSetMaterial(CKFixedFunctionPipeline &Pipeline,
                       const CKMaterialData *Data)
{
    if (Data)
        Pipeline.SetMaterial(Data);
    else
        Pipeline.ResetMaterial();
    return TRUE;
}

CKBOOL CKFFApplyMaterial(CKFixedFunctionPipeline &Pipeline,
                         const CKMaterialRenderState &State)
{
    Pipeline.ApplyMaterial(State);
    return TRUE;
}

CKBOOL CKFFSetViewport(CKFixedFunctionPipeline &Pipeline,
                       CKRenderStats &Stats, const CKViewportData *Data)
{
    if (!Data) {
        CKFFRecordDiagnostic(Stats, CKRST_DIAG_REJECT_INVALID_PARAMETER);
        return FALSE;
    }
    Pipeline.SetViewport(*Data);
    return TRUE;
}

CKBOOL CKFFSetUserClipPlane(CKFixedFunctionPipeline &Pipeline,
                            CKRenderStats &Stats, CKDWORD Index,
                            const VxPlane &Plane)
{
    if (Index >= CKRST_MAX_USER_CLIP_PLANES) {
        CKFFRecordDiagnostic(Stats, CKRST_DIAG_INVALID_CLIP_PLANE_INDEX);
        return FALSE;
    }
    Pipeline.SetUserClipPlane((int)Index, Plane);
    return TRUE;
}

CKBOOL CKFFGetUserClipPlane(CKFixedFunctionPipeline &Pipeline,
                            CKRenderStats &Stats, CKDWORD Index,
                            VxPlane &Plane)
{
    if (Index >= CKRST_MAX_USER_CLIP_PLANES) {
        CKFFRecordDiagnostic(Stats, CKRST_DIAG_INVALID_CLIP_PLANE_INDEX);
        return FALSE;
    }
    Plane = Pipeline.GetUserClipPlane((int)Index);
    return TRUE;
}
