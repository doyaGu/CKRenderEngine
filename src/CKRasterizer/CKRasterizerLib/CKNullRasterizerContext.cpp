#include "CKNullRasterizerInternal.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

enum CKNullFramePhase {
    CKNULL_FRAME_IDLE = 0,
    CKNULL_FRAME_SCENE,
    CKNULL_FRAME_OVERLAY
};

struct CKNullResource {
    CKDWORD Type = 0;
    CKDWORD Handle = 0;
    CKTextureDesc Texture;
    CKVertexBufferDesc VertexBuffer;
    CKIndexBufferDesc IndexBuffer;
    std::vector<CKBYTE> Data;
    CKBOOL Locked = FALSE;
    CKDWORD LockStart = 0;
    CKDWORD LockCount = 0;
    std::string Name;
};

struct CKNullReadback {
    CKReadbackCallback Callback = NULL;
    void *User = NULL;
    CKRECT Rect = {0, 0, 0, 0};
    CKBOOL HasRect = FALSE;
    VXBUFFER_TYPE Buffer = VXBUFFER_BACKBUFFER;
};

static CKDWORD CKNullPrimitiveCount(VXPRIMITIVETYPE type, int elementCount)
{
    switch (type) {
    case VX_POINTLIST:     return (CKDWORD)elementCount;
    case VX_LINELIST:      return (CKDWORD)(elementCount / 2);
    case VX_LINESTRIP:     return (CKDWORD)(elementCount > 1 ? elementCount - 1 : 0);
    case VX_TRIANGLELIST:  return (CKDWORD)(elementCount / 3);
    case VX_TRIANGLESTRIP:
    case VX_TRIANGLEFAN:   return (CKDWORD)(elementCount > 2 ? elementCount - 2 : 0);
    default:               return 0;
    }
}

static CKBOOL CKNullValidPrimitive(VXPRIMITIVETYPE type, int elementCount)
{
    switch (type) {
    case VX_POINTLIST:     return elementCount >= 1 ? TRUE : FALSE;
    case VX_LINELIST:
    case VX_LINESTRIP:     return elementCount >= 2 ? TRUE : FALSE;
    case VX_TRIANGLELIST:
    case VX_TRIANGLESTRIP:
    case VX_TRIANGLEFAN:   return elementCount >= 3 ? TRUE : FALSE;
    default:               return FALSE;
    }
}

static CKBOOL CKNullValidRect(const CKRECT *rect, CKDWORD width, CKDWORD height)
{
    if (!rect)
        return TRUE;
    return rect->left >= 0 && rect->top >= 0 && rect->right > rect->left && rect->bottom > rect->top &&
           (CKDWORD)rect->right <= width && (CKDWORD)rect->bottom <= height;
}

static CKBOOL CKNullCheckedByteSize(CKDWORD count, CKDWORD stride, size_t *size)
{
    if (!size || count == 0 || stride == 0)
        return FALSE;
    if ((size_t)count > (std::numeric_limits<size_t>::max)() / (size_t)stride)
        return FALSE;
    *size = (size_t)count * (size_t)stride;
    return TRUE;
}

class CKNullRasterizerContext final : public CKRasterizerContext {
public:
    explicit CKNullRasterizerContext(CKRasterizerDriver *driver)
        : m_Created(FALSE), m_ShuttingDown(FALSE), m_Phase(CKNULL_FRAME_IDLE),
          m_FrameOpen(FALSE), m_Target(0), m_TargetFace(CKRST_CUBEFACE_XPOS),
          m_NextHandle(1), m_FrameDrawCalls(0), m_FramePrimitives(0),
          m_FramePasses(0), m_FrameClears(0), m_FrameTextureUploads(0),
          m_FrameBufferUploads(0)
    {
        m_Driver = driver;
        std::memset(&m_Stats, 0, sizeof(m_Stats));
        std::memset(m_RenderStates, 0, sizeof(m_RenderStates));
        std::memset(m_StageStates, 0, sizeof(m_StageStates));
        std::memset(m_StageQueryMasks, 0, sizeof(m_StageQueryMasks));
        std::memset(m_Textures, 0, sizeof(m_Textures));
        std::memset(m_Lights, 0, sizeof(m_Lights));
        std::memset(m_LightEnabled, 0, sizeof(m_LightEnabled));
        std::memset(m_ClipPlanes, 0, sizeof(m_ClipPlanes));
        ResetMaterial();
        for (int i = 0; i < CKRST_MATRIX_SLOT_COUNT; ++i)
            Vx3DMatrixIdentity(m_Matrices[i]);
        InitDefaultRenderStatesValue();
    }

    ~CKNullRasterizerContext() override
    {
        BeginShutdown();
    }

    CKBOOL Create(WIN_HANDLE window, int posX, int posY, int width, int height,
                  int bpp, CKBOOL fullscreen, int refreshRate, int zBpp, int stencilBpp) override
    {
        if (m_Created || width <= 0 || height <= 0)
            return FALSE;
        m_Window = window;
        m_PosX = (CKDWORD)posX;
        m_PosY = (CKDWORD)posY;
        m_Width = (CKDWORD)width;
        m_Height = (CKDWORD)height;
        m_Bpp = bpp > 0 ? (CKDWORD)bpp : 32;
        m_ZBpp = zBpp > 0 ? (CKDWORD)zBpp : 24;
        m_StencilBpp = stencilBpp > 0 ? (CKDWORD)stencilBpp : 8;
        m_Fullscreen = fullscreen ? TRUE : FALSE;
        m_RefreshRate = refreshRate > 0 ? (CKDWORD)refreshRate : 0;
        m_Viewport = CKViewportData();
        m_Viewport.ViewWidth = m_Width;
        m_Viewport.ViewHeight = m_Height;
        m_Created = TRUE;
        m_ShuttingDown = FALSE;
        m_Phase = CKNULL_FRAME_IDLE;
        m_FrameOpen = FALSE;
        std::memset(&m_Stats, 0, sizeof(m_Stats));
        m_Stats.Width = m_Width;
        m_Stats.Height = m_Height;
        return TRUE;
    }

    CKBOOL Resize(int posX, int posY, int width, int height, CKDWORD flags) override
    {
        if (!CanWork() || m_FrameOpen || (flags & ~(VX_RESIZE_NOMOVE | VX_RESIZE_NOSIZE)))
            return FALSE;
        if (flags & VX_RESIZE_NOMOVE) {
            posX = (int)m_PosX;
            posY = (int)m_PosY;
        }
        if (flags & VX_RESIZE_NOSIZE) {
            width = (int)m_Width;
            height = (int)m_Height;
        }
        if (width <= 0 || height <= 0)
            return FALSE;
        const CKBOOL sizeChanged = width != (int)m_Width || height != (int)m_Height;
        m_PosX = (CKDWORD)posX;
        m_PosY = (CKDWORD)posY;
        if (sizeChanged) {
            m_Width = (CKDWORD)width;
            m_Height = (CKDWORD)height;
            m_Viewport.ViewX = 0;
            m_Viewport.ViewY = 0;
            m_Viewport.ViewWidth = m_Width;
            m_Viewport.ViewHeight = m_Height;
            m_Stats.Width = m_Width;
            m_Stats.Height = m_Height;
        }
        return TRUE;
    }

    CKBOOL SetOptions(const CKRasterizerOptions *options) override
    {
        if (!options || options->Size != sizeof(CKRasterizerOptions)) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return FALSE;
        }
        m_Options = *options;
        m_Options.Size = sizeof(CKRasterizerOptions);
        if (m_Options.RenderScale < 0.5f) m_Options.RenderScale = 0.5f;
        if (m_Options.RenderScale > 2.0f) m_Options.RenderScale = 2.0f;
        if (m_Options.Sharpness < 0.0f) m_Options.Sharpness = 0.0f;
        if (m_Options.Sharpness > 1.0f) m_Options.Sharpness = 1.0f;
        if (m_Options.MSAASamples <= 1) m_Options.MSAASamples = 0;
        return TRUE;
    }

    CKBOOL GetCaps(CKRasterizerCapsDesc *caps) const override
    {
        if (!caps || !m_Created)
            return FALSE;
        CKRasterizerCapsDesc result;
        result.Features = 0;
        result.MaxTextureSize = 4096;
        result.MaxTextureStages = CKRST_MAX_TEXTURE_STAGES;
        result.MaxAnisotropy = 1;
        result.MaxUserClipPlanes = CKRST_MAX_USER_CLIP_PLANES;
        result.MaxVertexBlendMatrices = CKRST_MAX_WORLD_MATRICES;
        result.MaxMSAASamples = 1;
        result.MaxPointSize = 1.0f;
        result.MaxLights = CKRST_MAX_LIGHTS;
        *caps = result;
        return TRUE;
    }

    CKERROR GetDeviceStatus() const override
    {
        return CanWork() ? CK_OK : CKERR_INVALIDRENDERCONTEXT;
    }

    CKBOOL BeginShutdown() override
    {
        if (!m_Created || m_ShuttingDown)
            return TRUE;
        m_ShuttingDown = TRUE;
        CancelReadbacks();
        m_Resources.clear();
        std::memset(m_Textures, 0, sizeof(m_Textures));
        m_Target = 0;
        m_Phase = CKNULL_FRAME_IDLE;
        m_FrameOpen = FALSE;
        return TRUE;
    }

    CKBOOL IsIdle() const override
    {
        return m_Phase == CKNULL_FRAME_IDLE && !m_FrameOpen ? TRUE : FALSE;
    }

    CKBOOL Clear(CKDWORD flags, CKDWORD, float, CKDWORD, int rectCount, CKRECT *rects) override
    {
        if (!CanWork() || rectCount < 0 || (rectCount > 0 && !rects))
            return FALSE;
        flags &= CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH | CKRST_CTXCLEAR_STENCIL;
        if (!flags)
            return TRUE;
        const CKRECT target = TargetRect();
        const int count = rectCount > 0 ? rectCount : 1;
        for (int i = 0; i < count; ++i) {
            CKRECT rect = rectCount > 0 ? rects[i] : ViewportRect();
            rect.left = (std::max)(rect.left, 0);
            rect.top = (std::max)(rect.top, 0);
            rect.right = (std::min)(rect.right, target.right);
            rect.bottom = (std::min)(rect.bottom, target.bottom);
            if (rect.right > rect.left && rect.bottom > rect.top) {
                ++m_FrameClears;
                ++m_FramePasses;
            }
        }
        m_FrameOpen = TRUE;
        return TRUE;
    }

    CKBOOL BeginScene() override
    {
        if (!CanWork() || m_Phase != CKNULL_FRAME_IDLE) {
            if (CanWork()) Diag(CKRST_DIAG_REJECT_SCENE_STATE);
            return FALSE;
        }
        DeliverReadbacks(TRUE);
        m_Phase = CKNULL_FRAME_SCENE;
        m_FrameOpen = TRUE;
        ++m_FramePasses;
        return TRUE;
    }

    CKBOOL EndScene() override
    {
        if (!CanWork() || m_Phase != CKNULL_FRAME_SCENE) {
            if (CanWork()) Diag(CKRST_DIAG_REJECT_SCENE_STATE);
            return FALSE;
        }
        m_Phase = CKNULL_FRAME_IDLE;
        return TRUE;
    }

    CKBOOL BeginOverlayPhase() override
    {
        if (!CanWork())
            return FALSE;
        if (m_Target) {
            Diag(CKRST_DIAG_OVERLAY_ON_TARGET);
            return FALSE;
        }
        if (m_Phase != CKNULL_FRAME_IDLE) {
            Diag(CKRST_DIAG_REJECT_SCENE_STATE);
            return FALSE;
        }
        m_Phase = CKNULL_FRAME_OVERLAY;
        m_FrameOpen = TRUE;
        ++m_FramePasses;
        return TRUE;
    }

    CKBOOL BackToFront(CKBOOL) override
    {
        if (!CanWork())
            return FALSE;
        if (m_Phase == CKNULL_FRAME_SCENE) {
            Diag(CKRST_DIAG_REJECT_SCENE_STATE);
            return FALSE;
        }
        m_Phase = CKNULL_FRAME_IDLE;
        m_FrameOpen = FALSE;
        ++m_Stats.FrameNumber;
        m_Stats.DrawCalls = m_FrameDrawCalls;
        m_Stats.Primitives = m_FramePrimitives;
        m_Stats.Passes = m_FramePasses;
        m_Stats.Clears = m_FrameClears;
        m_Stats.TextureUploads = m_FrameTextureUploads;
        m_Stats.BufferUploads = m_FrameBufferUploads;
        m_FrameDrawCalls = m_FramePrimitives = m_FramePasses = m_FrameClears = 0;
        m_FrameTextureUploads = m_FrameBufferUploads = 0;
        DeliverReadbacks(TRUE);
        return TRUE;
    }

    CKBOOL SetRenderState(VXRENDERSTATETYPE state, CKDWORD value) override
    {
        if (!CKRSTIsValidRenderStateType((CKDWORD)state)) {
            Diag(CKRST_DIAG_INVALID_RENDER_STATE);
            return FALSE;
        }
        m_RenderStates[(CKDWORD)state] = value;
        return TRUE;
    }

    CKBOOL GetRenderState(VXRENDERSTATETYPE state, CKDWORD *value) override
    {
        if (!value)
            return FALSE;
        if (!CKRSTIsValidRenderStateType((CKDWORD)state)) {
            Diag(CKRST_DIAG_INVALID_RENDER_STATE);
            return FALSE;
        }
        *value = m_RenderStates[(CKDWORD)state];
        return TRUE;
    }

    CKBOOL SetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE state, CKDWORD value) override
    {
        if (stage < 0 || stage >= CKRST_MAX_TEXTURE_STAGES) {
            Diag(CKRST_DIAG_INVALID_STAGE_INDEX);
            return FALSE;
        }
        if (!CKRSTIsValidTextureStageStateType((CKDWORD)state)) {
            Diag(CKRST_DIAG_INVALID_STAGE_STATE);
            return FALSE;
        }
        const uint64_t bit = UINT64_C(1) << (CKDWORD)state;
        m_StageStates[stage][(CKDWORD)state] = value;
        m_StageQueryMasks[stage] |= bit;
        if (state == CKRST_TSS_ADDRESS) {
            const CKRST_TEXTURESTAGESTATETYPE axes[] = {CKRST_TSS_ADDRESSU, CKRST_TSS_ADDRESSV, CKRST_TSS_ADDRESW};
            for (CKRST_TEXTURESTAGESTATETYPE axis : axes) {
                m_StageStates[stage][(CKDWORD)axis] = value;
                m_StageQueryMasks[stage] |= UINT64_C(1) << (CKDWORD)axis;
            }
        } else if (state == CKRST_TSS_TEXTUREMAPBLEND) {
            const CKRST_TEXTURESTAGESTATETYPE combine[] = {
                CKRST_TSS_OP, CKRST_TSS_ARG1, CKRST_TSS_ARG2,
                CKRST_TSS_AOP, CKRST_TSS_AARG1, CKRST_TSS_AARG2,
                CKRST_TSS_COLORARG0, CKRST_TSS_ALPHAARG0, CKRST_TSS_RESULTARG0,
            };
            for (CKRST_TEXTURESTAGESTATETYPE entry : combine) {
                m_StageStates[stage][(CKDWORD)entry] = 0;
                m_StageQueryMasks[stage] |= UINT64_C(1) << (CKDWORD)entry;
            }
        } else if (state == CKRST_TSS_STAGEBLEND && value != 0) {
            SetStageBlend(stage, value);
        } else if (state == CKRST_TSS_STAGEBLEND && value == 0 && stage > 0) {
            ResetTextureStages(stage, CKRST_MAX_TEXTURE_STAGES - stage);
        }
        return TRUE;
    }

    CKBOOL GetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE state, CKDWORD *value) override
    {
        if (!value)
            return FALSE;
        if (stage < 0 || stage >= CKRST_MAX_TEXTURE_STAGES) {
            Diag(CKRST_DIAG_INVALID_STAGE_INDEX);
            return FALSE;
        }
        if (!CKRSTIsValidTextureStageStateType((CKDWORD)state)) {
            Diag(CKRST_DIAG_INVALID_STAGE_STATE);
            return FALSE;
        }
        const uint64_t bit = UINT64_C(1) << (CKDWORD)state;
        *value = (m_StageQueryMasks[stage] & bit) != 0
            ? m_StageStates[stage][(CKDWORD)state]
            : CKRSTDefaultTextureStageStateValue(stage, state);
        return TRUE;
    }

    CKBOOL ResetTextureStages(int firstStage, int stageCount) override
    {
        if (firstStage < 0 || firstStage > CKRST_MAX_TEXTURE_STAGES || stageCount < 0 ||
            stageCount > CKRST_MAX_TEXTURE_STAGES - firstStage) {
            Diag(CKRST_DIAG_INVALID_STAGE_INDEX);
            return FALSE;
        }
        for (int stage = firstStage; stage < firstStage + stageCount; ++stage) {
            m_Textures[stage] = 0;
            std::memset(m_StageStates[stage], 0, sizeof(m_StageStates[stage]));
            m_StageStates[stage][CKRST_TSS_TEXCOORDINDEX] = (CKDWORD)stage;
            m_StageQueryMasks[stage] = ((UINT64_C(1) << CKRST_TSS_MAXSTATE) - 1) &
                                       ~((UINT64_C(1) << CKRST_TSS_OP) - 1);
            Vx3DMatrixIdentity(m_Matrices[MatrixSlot(VXMATRIX_TEXTURE(stage))]);
        }
        return TRUE;
    }

    CKBOOL SetTexture(CKDWORD texture, int stage) override
    {
        if (stage < 0 || stage >= CKRST_MAX_TEXTURE_STAGES) {
            Diag(CKRST_DIAG_INVALID_STAGE_INDEX);
            return FALSE;
        }
        if (texture && !FindResource(CKRST_OBJ_TEXTURE, texture)) {
            Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
            return FALSE;
        }
        m_Textures[stage] = texture;
        return TRUE;
    }

    CKBOOL GetTexture(int stage, CKDWORD *texture) override
    {
        if (!texture)
            return FALSE;
        if (stage < 0 || stage >= CKRST_MAX_TEXTURE_STAGES) {
            Diag(CKRST_DIAG_INVALID_STAGE_INDEX);
            return FALSE;
        }
        *texture = m_Textures[stage];
        return TRUE;
    }

    CKBOOL SetTransformMatrix(VXMATRIX_TYPE type, const VxMatrix &matrix) override
    {
        const int slot = MatrixSlot(type);
        if (slot < 0) {
            Diag(CKRST_DIAG_INVALID_MATRIX_TYPE);
            return FALSE;
        }
        m_Matrices[slot] = matrix;
        return TRUE;
    }

    CKBOOL GetTransformMatrix(VXMATRIX_TYPE type, VxMatrix &matrix) override
    {
        const int slot = MatrixSlot(type);
        if (slot < 0) {
            Diag(CKRST_DIAG_INVALID_MATRIX_TYPE);
            return FALSE;
        }
        matrix = m_Matrices[slot];
        return TRUE;
    }

    CKBOOL SetLight(CKDWORD index, const CKLightData *data) override
    {
        if (index >= CKRST_MAX_LIGHTS) {
            Diag(CKRST_DIAG_INVALID_LIGHT_INDEX);
            return FALSE;
        }
        if (!data) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return FALSE;
        }
        m_Lights[index] = *data;
        return TRUE;
    }

    CKBOOL EnableLight(CKDWORD index, CKBOOL enable) override
    {
        if (index >= CKRST_MAX_LIGHTS) {
            Diag(CKRST_DIAG_INVALID_LIGHT_INDEX);
            return FALSE;
        }
        m_LightEnabled[index] = enable ? TRUE : FALSE;
        return TRUE;
    }

    CKBOOL SetMaterial(const CKMaterialData *data) override
    {
        if (data)
            m_Material = *data;
        else
            ResetMaterial();
        return TRUE;
    }

    CKBOOL ApplyMaterial(const CKMaterialRenderState &state) override
    {
        m_Material = state.Material;
        SetRenderState(VXRENDERSTATE_CULLMODE, state.CullMode);
        SetRenderState(VXRENDERSTATE_FILLMODE, state.FillMode);
        SetRenderState(VXRENDERSTATE_SHADEMODE, state.ShadeMode);
        SetRenderState(VXRENDERSTATE_ALPHABLENDENABLE, state.AlphaBlend ? TRUE : FALSE);
        if (state.AlphaBlend) {
            SetRenderState(VXRENDERSTATE_SRCBLEND, state.SourceBlend);
            SetRenderState(VXRENDERSTATE_DESTBLEND, state.DestBlend);
        }
        SetRenderState(VXRENDERSTATE_ZENABLE, TRUE);
        SetRenderState(VXRENDERSTATE_ZWRITEENABLE, state.ZWrite ? TRUE : FALSE);
        SetRenderState(VXRENDERSTATE_ZFUNC, state.ZFunc);
        return TRUE;
    }

    CKBOOL SetViewport(const CKViewportData *data) override
    {
        if (!data) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return FALSE;
        }
        m_Viewport = *data;
        return TRUE;
    }

    CKBOOL SetUserClipPlane(CKDWORD index, const VxPlane &plane) override
    {
        if (index >= CKRST_MAX_USER_CLIP_PLANES) {
            Diag(CKRST_DIAG_INVALID_CLIP_PLANE_INDEX);
            return FALSE;
        }
        m_ClipPlanes[index] = plane;
        return TRUE;
    }

    CKBOOL GetUserClipPlane(CKDWORD index, VxPlane &plane) override
    {
        if (index >= CKRST_MAX_USER_CLIP_PLANES) {
            Diag(CKRST_DIAG_INVALID_CLIP_PLANE_INDEX);
            return FALSE;
        }
        plane = m_ClipPlanes[index];
        return TRUE;
    }

    void InitDefaultRenderStatesValue() override
    {
        for (CKDWORD state = 0; state < (CKDWORD)VXRENDERSTATE_MAXSTATE; ++state)
            m_RenderStates[state] = CKRSTDefaultRenderStateValue((VXRENDERSTATETYPE)state);
        std::memset(m_StageStates, 0, sizeof(m_StageStates));
        std::memset(m_StageQueryMasks, 0, sizeof(m_StageQueryMasks));
        std::memset(m_Textures, 0, sizeof(m_Textures));
    }

    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int indexCount,
                         VxDrawPrimitiveData *data) override
    {
        if (!CanDraw() || !data || data->VertexCount <= 0) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return FALSE;
        }
        const int count = indices ? indexCount : data->VertexCount;
        return AcceptDraw(type, count);
    }

    CKBOOL DrawPrimitiveVB(VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD startVertex,
                           CKDWORD vertexCount, CKWORD *indices, int indexCount) override
    {
        CKNullResource *resource = FindResource(CKRST_OBJ_VERTEXBUFFER, vb);
        if (!CanDraw() || !resource) {
            Diag(resource ? CKRST_DIAG_REJECT_INVALID_PARAMETER : CKRST_DIAG_REJECT_INVALID_HANDLE);
            return FALSE;
        }
        if (vertexCount == 0 || startVertex > resource->VertexBuffer.m_MaxVertexCount ||
            vertexCount > resource->VertexBuffer.m_MaxVertexCount - startVertex) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return FALSE;
        }
        return AcceptDraw(type, indices ? indexCount : (int)vertexCount);
    }

    CKBOOL DrawPrimitiveVBIB(VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib,
                             CKDWORD minVertexIndex, CKDWORD vertexCount,
                             CKDWORD startIndex, int indexCount) override
    {
        CKNullResource *vertices = FindResource(CKRST_OBJ_VERTEXBUFFER, vb);
        CKNullResource *indices = FindResource(CKRST_OBJ_INDEXBUFFER, ib);
        if (!CanDraw() || !vertices || !indices) {
            Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
            return FALSE;
        }
        if (vertexCount == 0 || indexCount <= 0 ||
            minVertexIndex > vertices->VertexBuffer.m_MaxVertexCount ||
            vertexCount > vertices->VertexBuffer.m_MaxVertexCount - minVertexIndex ||
            startIndex > indices->IndexBuffer.m_MaxIndexCount ||
            (CKDWORD)indexCount > indices->IndexBuffer.m_MaxIndexCount - startIndex) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return FALSE;
        }
        return AcceptDraw(type, indexCount);
    }

    CKBOOL CreateTexture(const CKTextureDesc *desc, CKDWORD *outHandle) override
    {
        if (outHandle) *outHandle = 0;
        if (!CanWork() || !desc || !outHandle || desc->Format.Width <= 0 || desc->Format.Height <= 0 ||
            ((desc->Flags & CKRST_TEXTURE_CUBEMAP) && desc->Format.Width != desc->Format.Height)) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return FALSE;
        }
        CKNullResource resource;
        resource.Type = CKRST_OBJ_TEXTURE;
        resource.Texture = *desc;
        resource.Texture.Flags |= CKRST_TEXTURE_VALID;
        if (resource.Texture.Depth == 0) resource.Texture.Depth = 1;
        if (resource.Texture.MipMapCount == 0) resource.Texture.MipMapCount = 1;
        return InsertResource(std::move(resource), outHandle);
    }

    CKBOOL LoadTexture(CKDWORD texture, const VxImageDescEx &image, int mipLevel,
                       CKRST_CUBEFACE face, const CKRECT *region) override
    {
        CKNullResource *resource = FindResource(CKRST_OBJ_TEXTURE, texture);
        if (!CanWork() || !resource) {
            Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
            return FALSE;
        }
        if (!image.Image || image.Width <= 0 || image.Height <= 0 || mipLevel < 0 || mipLevel >= 32) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return FALSE;
        }
        const CKBOOL cube = (resource->Texture.Flags & CKRST_TEXTURE_CUBEMAP) != 0;
        const CKBOOL volume = (resource->Texture.Flags & CKRST_TEXTURE_VOLUMEMAP) != 0;
        const CKDWORD layers = cube ? CKRST_CUBEFACE_COUNT :
            (volume ? (std::max<CKDWORD>)(1, resource->Texture.Depth >> mipLevel) : 1);
        const CKDWORD levels = resource->Texture.MipMapCount == CKRST_MIPMAP_GENERATE
            ? 1 : (std::max<CKDWORD>)(1, resource->Texture.MipMapCount);
        const CKDWORD width = (std::max<CKDWORD>)(1, (CKDWORD)resource->Texture.Format.Width >> mipLevel);
        const CKDWORD height = (std::max<CKDWORD>)(1, (CKDWORD)resource->Texture.Format.Height >> mipLevel);
        if ((CKDWORD)face >= layers || (CKDWORD)mipLevel >= levels || !CKNullValidRect(region, width, height)) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return FALSE;
        }
        ++m_FrameTextureUploads;
        return TRUE;
    }

    CKBOOL GetTextureDesc(CKDWORD texture, CKTextureDesc *desc) const override
    {
        if (!desc)
            return FALSE;
        const CKNullResource *resource = FindResource(CKRST_OBJ_TEXTURE, texture);
        if (!resource)
            return FALSE;
        *desc = resource->Texture;
        return TRUE;
    }

    CKBOOL CreateVertexBuffer(const CKVertexBufferDesc *desc, const void *data, CKDWORD *outHandle) override
    {
        if (outHandle) *outHandle = 0;
        if (!CanWork() || !desc || !outHandle || desc->m_MaxVertexCount == 0) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return FALSE;
        }
        CKNullResource resource;
        resource.Type = CKRST_OBJ_VERTEXBUFFER;
        resource.VertexBuffer = *desc;
        CKRSTVertexLayout layout;
        const CKDWORD stride = CKRSTGetVertexLayout(desc->m_VertexFormat, desc->m_TexcoordDims, &layout);
        if (!stride || (desc->m_VertexSize && desc->m_VertexSize != stride)) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return FALSE;
        }
        resource.VertexBuffer.m_VertexSize = stride;
        size_t size = 0;
        if (!CKNullCheckedByteSize(desc->m_MaxVertexCount, stride, &size))
            return FALSE;
        resource.Data.resize(size, 0);
        if (data) {
            std::memcpy(resource.Data.data(), data, size);
            ++m_FrameBufferUploads;
        }
        return InsertResource(std::move(resource), outHandle);
    }

    CKBOOL CreateIndexBuffer(const CKIndexBufferDesc *desc, const void *data, CKDWORD *outHandle) override
    {
        if (outHandle) *outHandle = 0;
        if (!CanWork() || !desc || !outHandle || desc->m_MaxIndexCount == 0) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return FALSE;
        }
        CKNullResource resource;
        resource.Type = CKRST_OBJ_INDEXBUFFER;
        resource.IndexBuffer = *desc;
        size_t size = 0;
        if (!CKNullCheckedByteSize(desc->m_MaxIndexCount, 2, &size))
            return FALSE;
        resource.Data.resize(size, 0);
        if (data) {
            std::memcpy(resource.Data.data(), data, size);
            ++m_FrameBufferUploads;
        }
        return InsertResource(std::move(resource), outHandle);
    }

    void *LockVertexBuffer(CKDWORD vb, CKDWORD startVertex, CKDWORD vertexCount, CKRST_LOCKFLAGS) override
    {
        CKNullResource *resource = FindResource(CKRST_OBJ_VERTEXBUFFER, vb);
        return LockResource(resource, startVertex, vertexCount,
                            resource ? resource->VertexBuffer.m_MaxVertexCount : 0,
                            resource ? resource->VertexBuffer.m_VertexSize : 0);
    }

    CKBOOL UnlockVertexBuffer(CKDWORD vb) override
    {
        return UnlockBuffer(FindResource(CKRST_OBJ_VERTEXBUFFER, vb));
    }

    void *LockIndexBuffer(CKDWORD ib, CKDWORD startIndex, CKDWORD indexCount, CKRST_LOCKFLAGS) override
    {
        CKNullResource *resource = FindResource(CKRST_OBJ_INDEXBUFFER, ib);
        return LockResource(resource, startIndex, indexCount,
                            resource ? resource->IndexBuffer.m_MaxIndexCount : 0, 2);
    }

    CKBOOL UnlockIndexBuffer(CKDWORD ib) override
    {
        return UnlockBuffer(FindResource(CKRST_OBJ_INDEXBUFFER, ib));
    }

    CKBOOL DeleteObject(CKDWORD handle, CKDWORD type) override
    {
        if (type != CKRST_OBJ_TEXTURE && type != CKRST_OBJ_VERTEXBUFFER && type != CKRST_OBJ_INDEXBUFFER) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return FALSE;
        }
        CKNullResource *resource = FindResource(type, handle);
        if (!resource) {
            Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
            return FALSE;
        }
        if (type == CKRST_OBJ_TEXTURE) {
            if (m_Target == handle) m_Target = 0;
            for (CKDWORD &texture : m_Textures)
                if (texture == handle) texture = 0;
        }
        m_Resources.erase(handle);
        return TRUE;
    }

    CKBOOL FlushObjects(CKDWORD typeMask) override
    {
        std::vector<CKDWORD> handles;
        for (const auto &entry : m_Resources)
            if ((entry.second.Type & typeMask) != 0) handles.push_back(entry.first);
        for (CKDWORD handle : handles) {
            auto found = m_Resources.find(handle);
            if (found != m_Resources.end()) DeleteObject(handle, found->second.Type);
        }
        return TRUE;
    }

    void SetResourceName(CKDWORD handle, CKDWORD type, CKSTRING name) override
    {
        CKNullResource *resource = FindResource(type, handle);
        if (resource) resource->Name = name ? name : "";
    }

    CKBOOL SetTargetTexture(CKDWORD texture, int width, int height, CKRST_CUBEFACE face) override
    {
        if (!CanWork())
            return FALSE;
        if (m_Phase != CKNULL_FRAME_IDLE) {
            Diag(CKRST_DIAG_INVALID_TARGET);
            return FALSE;
        }
        if (!texture) {
            m_Target = 0;
            m_TargetFace = CKRST_CUBEFACE_XPOS;
            return TRUE;
        }
        CKNullResource *resource = FindResource(CKRST_OBJ_TEXTURE, texture);
        if (!resource) {
            Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
            return FALSE;
        }
        const CKBOOL cube = (resource->Texture.Flags & CKRST_TEXTURE_CUBEMAP) != 0;
        if ((resource->Texture.Flags & CKRST_TEXTURE_RENDERTARGET) == 0 ||
            (CKDWORD)face >= CKRST_CUBEFACE_COUNT || (!cube && face != CKRST_CUBEFACE_XPOS) ||
            (width > 0 && width != resource->Texture.Format.Width) ||
            (height > 0 && height != resource->Texture.Format.Height)) {
            Diag(CKRST_DIAG_INVALID_TARGET);
            return FALSE;
        }
        m_Target = texture;
        m_TargetFace = face;
        return TRUE;
    }

    CKBOOL CopyToTexture(CKDWORD texture, const VxRect *src, const VxRect *dst, CKRST_CUBEFACE face) override
    {
        CKNullResource *resource = FindResource(CKRST_OBJ_TEXTURE, texture);
        if (!CanWork() || !resource) {
            Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
            return FALSE;
        }
        const CKBOOL cube = (resource->Texture.Flags & CKRST_TEXTURE_CUBEMAP) != 0;
        if ((CKDWORD)face >= CKRST_CUBEFACE_COUNT || (!cube && face != CKRST_CUBEFACE_XPOS)) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return FALSE;
        }
        CKRECT source = TargetRect();
        if (src) source = {(int)src->left, (int)src->top, (int)src->right, (int)src->bottom};
        CKRECT destination = {0, 0, resource->Texture.Format.Width, resource->Texture.Format.Height};
        if (dst) destination = {(int)dst->left, (int)dst->top, (int)dst->right, (int)dst->bottom};
        if (!CKNullValidRect(&source, (CKDWORD)TargetRect().right, (CKDWORD)TargetRect().bottom) ||
            !CKNullValidRect(&destination, (CKDWORD)resource->Texture.Format.Width,
                            (CKDWORD)resource->Texture.Format.Height)) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return FALSE;
        }
        ++m_FrameTextureUploads;
        return TRUE;
    }

    int CopyToMemoryBuffer(const CKRECT *rect, VXBUFFER_TYPE buffer, VxImageDescEx &image) override
    {
        if (!CanWork() || buffer != VXBUFFER_BACKBUFFER || m_FrameOpen) {
            Diag(m_FrameOpen ? CKRST_DIAG_REJECT_SCENE_STATE : CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return 0;
        }
        const CKRECT target = TargetRect();
        if (!CKNullValidRect(rect, (CKDWORD)target.right, (CKDWORD)target.bottom)) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return 0;
        }
        const CKDWORD width = rect ? (CKDWORD)(rect->right - rect->left) : (CKDWORD)target.right;
        const CKDWORD height = rect ? (CKDWORD)(rect->bottom - rect->top) : (CKDWORD)target.bottom;
        if (width == 0 || height == 0 || width > (CKDWORD)(std::numeric_limits<int>::max)() / 4 ||
            height > (CKDWORD)(std::numeric_limits<int>::max)() / (width * 4))
            return 0;
        CKBYTE *destination = image.Image;
        VxPixelFormat2ImageDesc(_32_ARGB8888, image);
        image.Width = (int)width;
        image.Height = (int)height;
        image.BytesPerLine = (int)width * 4;
        const int size = image.BytesPerLine * image.Height;
        image.Image = destination;
        if (destination) std::memset(destination, 0, (size_t)size);
        return size;
    }

    int CopyFromMemoryBuffer(const CKRECT *rect, VXBUFFER_TYPE buffer, const VxImageDescEx &image) override
    {
        if (!CanWork() || buffer != VXBUFFER_BACKBUFFER || !image.Image || image.BitsPerPixel <= 0) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return 0;
        }
        CKRECT destination = rect ? *rect : TargetRect();
        const CKRECT target = TargetRect();
        destination.left = (std::max)(destination.left, 0);
        destination.top = (std::max)(destination.top, 0);
        destination.right = (std::min)(destination.right, target.right);
        destination.bottom = (std::min)(destination.bottom, target.bottom);
        const int width = destination.right - destination.left;
        const int height = destination.bottom - destination.top;
        if (width <= 0 || height <= 0 || image.Width != width || image.Height != height) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return 0;
        }
        m_FrameOpen = TRUE;
        ++m_FrameTextureUploads;
        ++m_FrameDrawCalls;
        ++m_FramePrimitives;
        ++m_FramePasses;
        return width * height * 4;
    }

    CKBOOL RequestReadback(const CKRECT *rect, VXBUFFER_TYPE buffer,
                           CKReadbackCallback callback, void *user) override
    {
        if (!CanWork() || buffer != VXBUFFER_BACKBUFFER || !callback) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return FALSE;
        }
        const CKRECT target = TargetRect();
        if (!CKNullValidRect(rect, (CKDWORD)target.right, (CKDWORD)target.bottom)) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return FALSE;
        }
        CKNullReadback readback;
        readback.Callback = callback;
        readback.User = user;
        readback.Buffer = buffer;
        if (rect) {
            readback.Rect = *rect;
            readback.HasRect = TRUE;
        }
        m_Readbacks.push_back(readback);
        return TRUE;
    }

    void SetDebugMarker(CKSTRING name) override
    {
        m_Marker = name ? name : "";
    }

    const CKRenderStats *GetStats() override
    {
        return &m_Stats;
    }

private:
    CKBOOL CanWork() const { return m_Created && !m_ShuttingDown ? TRUE : FALSE; }
    CKBOOL CanDraw() const { return CanWork(); }

    static int MatrixSlot(VXMATRIX_TYPE type)
    {
        const CKDWORD value = (CKDWORD)type;
        if (value == (CKDWORD)VXMATRIX_WORLD || value == (CKDWORD)VXMATRIX_WMAT) return 0;
        if (value > (CKDWORD)VXMATRIX_WMAT && value < (CKDWORD)VXMATRIX_WMAT + CKRST_MAX_WORLD_MATRICES)
            return (int)(value - (CKDWORD)VXMATRIX_WMAT);
        if (value == (CKDWORD)VXMATRIX_VIEW) return CKRST_MAX_WORLD_MATRICES;
        if (value == (CKDWORD)VXMATRIX_PROJECTION) return CKRST_MAX_WORLD_MATRICES + 1;
        if (value >= (CKDWORD)VXMATRIX_TEXTURE0 && value < (CKDWORD)VXMATRIX_TEXTURE0 + CKRST_MAX_TEXTURE_STAGES)
            return CKRST_MAX_WORLD_MATRICES + 2 + (int)(value - (CKDWORD)VXMATRIX_TEXTURE0);
        return -1;
    }

    void Diag(CKRST_DIAGNOSTIC diagnostic)
    {
        if ((CKDWORD)diagnostic < CKRST_DIAG_COUNT)
            ++m_Stats.Diagnostics[diagnostic];
    }

    void ResetMaterial()
    {
        m_Material = CKMaterialData();
        m_Material.Diffuse = VxColor(1.0f, 1.0f, 1.0f, 1.0f);
        m_Material.Ambient = VxColor(1.0f, 1.0f, 1.0f, 1.0f);
    }

    CKRECT TargetRect() const
    {
        CKRECT rect = {0, 0, (int)m_Width, (int)m_Height};
        const CKNullResource *target = FindResource(CKRST_OBJ_TEXTURE, m_Target);
        if (target) {
            rect.right = target->Texture.Format.Width;
            rect.bottom = target->Texture.Format.Height;
        }
        return rect;
    }

    CKRECT ViewportRect() const
    {
        CKRECT rect;
        rect.left = (int)m_Viewport.ViewX;
        rect.top = (int)m_Viewport.ViewY;
        rect.right = (int)(m_Viewport.ViewX + m_Viewport.ViewWidth);
        rect.bottom = (int)(m_Viewport.ViewY + m_Viewport.ViewHeight);
        return rect;
    }

    CKBOOL AcceptDraw(VXPRIMITIVETYPE type, int elementCount)
    {
        if (!CKNullValidPrimitive(type, elementCount)) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return FALSE;
        }
        m_FrameOpen = TRUE;
        ++m_FrameDrawCalls;
        m_FramePrimitives += CKNullPrimitiveCount(type, elementCount);
        if (m_FramePasses == 0) ++m_FramePasses;
        m_Marker.clear();
        return TRUE;
    }

    CKBOOL InsertResource(CKNullResource &&resource, CKDWORD *outHandle)
    {
        if (m_NextHandle == 0)
            return FALSE;
        resource.Handle = m_NextHandle++;
        const CKDWORD handle = resource.Handle;
        m_Resources.emplace(handle, std::move(resource));
        *outHandle = handle;
        return TRUE;
    }

    CKNullResource *FindResource(CKDWORD type, CKDWORD handle)
    {
        if (!handle) return NULL;
        auto found = m_Resources.find(handle);
        return found != m_Resources.end() && found->second.Type == type ? &found->second : NULL;
    }

    const CKNullResource *FindResource(CKDWORD type, CKDWORD handle) const
    {
        if (!handle) return NULL;
        auto found = m_Resources.find(handle);
        return found != m_Resources.end() && found->second.Type == type ? &found->second : NULL;
    }

    void *LockResource(CKNullResource *resource, CKDWORD start, CKDWORD count,
                       CKDWORD maximum, CKDWORD stride)
    {
        if (!resource) {
            Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
            return NULL;
        }
        if (count == 0) count = start < maximum ? maximum - start : 0;
        if (resource->Locked || start >= maximum || count == 0 || count > maximum - start) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return NULL;
        }
        resource->Locked = TRUE;
        resource->LockStart = start;
        resource->LockCount = count;
        return resource->Data.data() + (size_t)start * stride;
    }

    CKBOOL UnlockBuffer(CKNullResource *resource)
    {
        if (!resource) {
            Diag(CKRST_DIAG_REJECT_INVALID_HANDLE);
            return FALSE;
        }
        if (!resource->Locked) {
            Diag(CKRST_DIAG_REJECT_INVALID_PARAMETER);
            return FALSE;
        }
        resource->Locked = FALSE;
        ++m_FrameBufferUploads;
        return TRUE;
    }

    void SetStageBlend(int stage, CKDWORD stageBlend)
    {
        const CKDWORD src = (stageBlend >> 4) & 0xF;
        const CKDWORD dst = stageBlend & 0xF;
        CKDWORD colorOp = CKRST_TOP_MODULATE;
        CKDWORD colorArg1 = CKRST_TA_TEXTURE;
        CKDWORD colorArg2 = CKRST_TA_CURRENT;
        if ((src == VXBLEND_ZERO && dst == VXBLEND_SRCCOLOR) ||
            (src == VXBLEND_DESTCOLOR && dst == VXBLEND_ZERO)) colorOp = CKRST_TOP_MODULATE;
        else if (src == VXBLEND_ONE && dst == VXBLEND_ONE) colorOp = CKRST_TOP_ADD;
        else if (src == VXBLEND_ONE && dst == VXBLEND_ZERO) colorOp = CKRST_TOP_SELECTARG1;
        else if (src == VXBLEND_ZERO && dst == VXBLEND_ONE) colorOp = CKRST_TOP_SELECTARG2;
        else if (src == VXBLEND_SRCALPHA && dst == VXBLEND_INVSRCALPHA) colorOp = CKRST_TOP_BLENDTEXTUREALPHA;
        else if (src == VXBLEND_SRCALPHA && dst == VXBLEND_ONE) colorOp = CKRST_TOP_MODULATEALPHA_ADDCOLOR;
        else if (src == VXBLEND_ONE && dst == VXBLEND_INVSRCALPHA) colorOp = CKRST_TOP_BLENDTEXTUREALPHAPM;
        else if (src == VXBLEND_DESTCOLOR && dst == VXBLEND_SRCCOLOR) colorOp = CKRST_TOP_MODULATE2X;
        else if (src == VXBLEND_INVSRCALPHA && dst == VXBLEND_SRCALPHA) {
            colorOp = CKRST_TOP_BLENDTEXTUREALPHA;
            colorArg1 = CKRST_TA_CURRENT;
            colorArg2 = CKRST_TA_TEXTURE;
        }
        const CKRST_TEXTURESTAGESTATETYPE keys[] = {
            CKRST_TSS_OP, CKRST_TSS_ARG1, CKRST_TSS_ARG2,
            CKRST_TSS_AOP, CKRST_TSS_AARG1, CKRST_TSS_AARG2,
        };
        const CKDWORD values[] = {
            colorOp, colorArg1, colorArg2,
            CKRST_TOP_SELECTARG2, CKRST_TA_TEXTURE, CKRST_TA_CURRENT,
        };
        for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
            m_StageStates[stage][(CKDWORD)keys[i]] = values[i];
            m_StageQueryMasks[stage] |= UINT64_C(1) << (CKDWORD)keys[i];
        }
    }

    void DeliverReadbacks(CKBOOL success)
    {
        std::vector<CKNullReadback> ready;
        ready.swap(m_Readbacks);
        for (CKNullReadback &readback : ready) {
            if (!success) {
                readback.Callback(readback.User, readback.HasRect ? &readback.Rect : NULL,
                                  readback.Buffer, NULL, FALSE);
                continue;
            }
            const CKRECT target = TargetRect();
            const int width = readback.HasRect ? readback.Rect.right - readback.Rect.left : target.right;
            const int height = readback.HasRect ? readback.Rect.bottom - readback.Rect.top : target.bottom;
            std::vector<CKBYTE> pixels((size_t)width * (size_t)height * 4, 0);
            VxImageDescEx image;
            VxPixelFormat2ImageDesc(_32_ARGB8888, image);
            image.Width = width;
            image.Height = height;
            image.BytesPerLine = width * 4;
            image.Image = pixels.data();
            readback.Callback(readback.User, readback.HasRect ? &readback.Rect : NULL,
                              readback.Buffer, &image, TRUE);
        }
    }

    void CancelReadbacks()
    {
        DeliverReadbacks(FALSE);
    }

    CKBOOL m_Created;
    CKBOOL m_ShuttingDown;
    CKNullFramePhase m_Phase;
    CKBOOL m_FrameOpen;
    CKDWORD m_Target;
    CKRST_CUBEFACE m_TargetFace;
    CKDWORD m_NextHandle;
    std::unordered_map<CKDWORD, CKNullResource> m_Resources;
    CKRasterizerOptions m_Options;
    CKDWORD m_RenderStates[VXRENDERSTATE_MAXSTATE];
    CKDWORD m_StageStates[CKRST_MAX_TEXTURE_STAGES][CKRST_TSS_MAXSTATE];
    uint64_t m_StageQueryMasks[CKRST_MAX_TEXTURE_STAGES];
    CKDWORD m_Textures[CKRST_MAX_TEXTURE_STAGES];
    VxMatrix m_Matrices[CKRST_MATRIX_SLOT_COUNT];
    CKLightData m_Lights[CKRST_MAX_LIGHTS];
    CKBOOL m_LightEnabled[CKRST_MAX_LIGHTS];
    CKMaterialData m_Material;
    CKViewportData m_Viewport;
    VxPlane m_ClipPlanes[CKRST_MAX_USER_CLIP_PLANES];
    std::vector<CKNullReadback> m_Readbacks;
    std::string m_Marker;
    CKRenderStats m_Stats;
    CKDWORD m_FrameDrawCalls;
    CKDWORD m_FramePrimitives;
    CKDWORD m_FramePasses;
    CKDWORD m_FrameClears;
    CKDWORD m_FrameTextureUploads;
    CKDWORD m_FrameBufferUploads;
};

} // namespace

CKRasterizerContext *CKNullCreateRasterizerContext(CKRasterizerDriver *driver)
{
    return new (std::nothrow) CKNullRasterizerContext(driver);
}

CKBOOL CKNullDestroyRasterizerContext(CKRasterizerContext *context)
{
    if (!context)
        return FALSE;
    CKNullRasterizerContext *nullContext =
        static_cast<CKNullRasterizerContext *>(context);
    if (!nullContext->BeginShutdown())
        return FALSE;
    delete nullContext;
    return TRUE;
}
