#include "CKRasterizer.h"
#include "CKRasterizerValidation.h"

#include <atomic>
#include <string.h>

CKERROR CKRasterizerEncoder::GetStatus() const
{
    return CK_OK;
}

void CKRasterizerEncoder::SetState(CKDrawState)
{
}

void CKRasterizerEncoder::SetStencilRef(CKDWORD)
{
}

void CKRasterizerEncoder::SetStencilMask(CKDWORD, CKDWORD)
{
}

void CKRasterizerEncoder::SetScissor(const CKRECT *)
{
}

void CKRasterizerEncoder::SetPointSize(float)
{
}

void CKRasterizerEncoder::SetTransform(CKDWORD, CKDWORD)
{
}

void CKRasterizerEncoder::SetVertexBuffer(CKDWORD, CKDWORD, CKDWORD, CKDWORD, CKDWORD)
{
}

void CKRasterizerEncoder::SetIndexBuffer(CKDWORD, CKDWORD, CKDWORD)
{
}

void CKRasterizerEncoder::SetInstanceBuffer(CKDWORD, CKDWORD, CKDWORD, CKDWORD)
{
}

void CKRasterizerEncoder::SetTransientVertexBuffer(CKDWORD, CKTransientVertexBuffer *)
{
}

void CKRasterizerEncoder::SetTransientIndexBuffer(CKTransientIndexBuffer *)
{
}

void CKRasterizerEncoder::SetTransientInstanceBuffer(CKDWORD, CKTransientInstanceBuffer *)
{
}

void CKRasterizerEncoder::SetTexture(CKDWORD, CKDWORD, CKDWORD, CKSamplerDesc *)
{
}

void CKRasterizerEncoder::SetUniform(CKDWORD, const void *, CKDWORD)
{
}

void CKRasterizerEncoder::SetComputeBuffer(CKDWORD, CKDWORD, CK_ACCESS_MODE)
{
}

void CKRasterizerEncoder::SetComputeImage(CKDWORD, CKDWORD, CKDWORD, CK_ACCESS_MODE)
{
}

void CKRasterizerEncoder::SetCondition(CKDWORD, CKBOOL)
{
}

void CKRasterizerEncoder::SetMarker(CKSTRING)
{
}

CKBOOL CKRasterizerEncoder::ConsumeMarker(char *, CKDWORD)
{
    return FALSE;
}

void CKRasterizerEncoder::Submit(CKRenderView, CKDWORD, CKDWORD, CKDWORD)
{
}

void CKRasterizerEncoder::SubmitOcclusionQuery(CKRenderView, CKDWORD, CKDWORD, CKDWORD, CKDWORD)
{
}

void CKRasterizerEncoder::SubmitIndirect(CKRenderView, CKDWORD, CKDWORD, CKDWORD, CKDWORD, CKDWORD, CKDWORD)
{
}

void CKRasterizerEncoder::Dispatch(CKRenderView, CKDWORD, CKDWORD, CKDWORD, CKDWORD, CKDWORD)
{
}

void CKRasterizerEncoder::DispatchIndirect(CKRenderView, CKDWORD, CKDWORD, CKDWORD, CKDWORD, CKDWORD)
{
}

void CKRasterizerEncoder::Touch(CKRenderView)
{
}

void CKRasterizerEncoder::Blit(CKRenderView, CKDWORD, CKDWORD, CKDWORD, CKDWORD, CKDWORD, CKDWORD, const CKRECT *)
{
}

namespace {

struct CKNullResourceRecord {
    CKDWORD Handle;
    CKDWORD Type;
    CKDWORD Capacity;
    CKDWORD Count;
    CKDWORD Kind;
    CKDWORD Attachment;
    XString Name;
};

struct CKNullEncoderSnapshot {
    CKDrawState DrawState;
    CKDWORD StencilRef;
    CKDWORD StencilReadMask;
    CKDWORD StencilWriteMask;
    CKRECT Scissor;
    CKBOOL ScissorEnabled;
    float PointSize;
    CKDWORD Transform;
    CKDWORD TransformCount;
    CKDWORD VertexBuffers[CKRST_MAX_VERTEX_STREAMS];
    CKDWORD VertexLayouts[CKRST_MAX_VERTEX_STREAMS];
    CKDWORD VertexStarts[CKRST_MAX_VERTEX_STREAMS];
    CKDWORD VertexCounts[CKRST_MAX_VERTEX_STREAMS];
    CKDWORD IndexBuffer;
    CKDWORD IndexStart;
    CKDWORD IndexCount;
    CKDWORD Textures[CKRST_MAX_TEXTURE_STAGES];
    CKDWORD TextureUniforms[CKRST_MAX_TEXTURE_STAGES];
};

struct CKNullSubmission {
    CKRenderView View;
    CKDWORD Program;
    CKDWORD Depth;
    CKDWORD Discard;
    CKNullEncoderSnapshot Snapshot;
};

struct CKNullViewRecord {
    XString Name;
    CKRECT Rect;
    CKRECT Scissor;
    CKBOOL ScissorEnabled;
    CKDWORD ClearFlags;
    CKDWORD ClearColor;
    float ClearDepth;
    CKDWORD ClearStencil;
    VxMatrix ViewMatrix;
    VxMatrix ProjectionMatrix;
    CKBOOL HasViewMatrix;
    CKBOOL HasProjectionMatrix;
    CKDWORD FrameBuffer;
    CK_VIEW_MODE Mode;
};

class CKNullRasterizerEncoder;

struct CKNullContextState {
    XUINTPTR ApiThread;
    VxMutex ResourceMutex;
    XArray<CKNullResourceRecord> Resources;
    CKDWORD NextResourceHandle;
    VxMutex TransformMutex;
    XArray<VxMatrix> Transforms;
    VxMutex SubmissionMutex;
    XArray<CKNullSubmission> CurrentSubmissions;
    XArray<CKNullSubmission> LastSubmissions;
    CKNullViewRecord Views[CKRST_MAX_RENDER_VIEWS];
    CKRenderView ViewOrder[CKRST_MAX_RENDER_VIEWS];
    VxMutex EncoderLifecycleMutex;
    CKBOOL FrameInProgress;
    CKBOOL ShuttingDown;
    CKNullRasterizerEncoder *DefaultEncoder;
    CKNullRasterizerEncoder *PoolEncoder;

    CKNullContextState();
    ~CKNullContextState();
};

static CKNullContextState *CKNullState(CKRasterizerContext *Context)
{
    return Context ? static_cast<CKNullContextState *>(Context->m_NullBackendState) : NULL;
}

static const CKNullContextState *CKNullState(const CKRasterizerContext *Context)
{
    return Context ? static_cast<const CKNullContextState *>(Context->m_NullBackendState) : NULL;
}

static CKBOOL CKNullIsApiThread(const CKRasterizerContext *Context)
{
    const CKNullContextState *state = CKNullState(Context);
    return state && state->ApiThread == VxThread::GetCurrentVxThreadId() ? TRUE : FALSE;
}

static CKBOOL CKNullFindResource(CKNullContextState *State, CKDWORD Handle,
                                 CKDWORD Type, CKNullResourceRecord *Record = NULL)
{
    if (!State || Handle == 0)
        return FALSE;
    VxMutexLock lock(State->ResourceMutex);
    for (int i = 0; i < State->Resources.Size(); ++i) {
        const CKNullResourceRecord &resource = State->Resources[i];
        if (resource.Handle == Handle && resource.Type == Type) {
            if (Record)
                *Record = resource;
            return TRUE;
        }
    }
    return FALSE;
}

static CKDWORD CKNullMipCount(CKDWORD Width, CKDWORD Height, CKDWORD Depth = 1)
{
    CKDWORD count = 1;
    CKDWORD size = Width > Height ? Width : Height;
    if (Depth > size)
        size = Depth;
    while (size > 1) {
        size >>= 1;
        ++count;
    }
    return count;
}

static void CKNullResetView(CKNullViewRecord &View)
{
    View.Name.Clear();
    memset(&View.Rect, 0, sizeof(View.Rect));
    memset(&View.Scissor, 0, sizeof(View.Scissor));
    View.ScissorEnabled = FALSE;
    View.ClearFlags = 0;
    View.ClearColor = 0;
    View.ClearDepth = 1.0f;
    View.ClearStencil = 0;
    View.HasViewMatrix = FALSE;
    View.HasProjectionMatrix = FALSE;
    View.FrameBuffer = 0;
    View.Mode = CKRST_VIEWMODE_DEFAULT;
}

class CKNullRasterizerEncoder : public CKRasterizerEncoder {
public:
    CKNullRasterizerEncoder()
        : m_Active(false), m_Context(NULL), m_State(NULL),
          m_Status(CK_OK), m_FrameStatus(CK_OK)
    {
        ResetSnapshot();
    }

    CKERROR GetStatus() const override { return m_Status.load(std::memory_order_acquire); }
    CKERROR GetFrameStatus() const { return m_FrameStatus.load(std::memory_order_acquire); }

    void Reset(CKRasterizerContext *Context, CKNullContextState *State)
    {
        m_Context = Context;
        m_State = State;
        m_Status.store(CK_OK, std::memory_order_release);
        m_FrameStatus.store(CK_OK, std::memory_order_release);
        m_OwnerThread = VxThread::GetCurrentVxThreadId();
        m_Marker.Clear();
        ResetSnapshot();
    }

    void SetError(CKERROR Error)
    {
        CKERROR expected = CK_OK;
        if (Error != CK_OK) {
            m_Status.compare_exchange_strong(expected, Error, std::memory_order_acq_rel);
            expected = CK_OK;
            m_FrameStatus.compare_exchange_strong(expected, Error,
                                                  std::memory_order_acq_rel);
        }
    }

    CKBOOL CanRecord()
    {
        if (!m_Active.load(std::memory_order_acquire) || !m_Context || !m_State ||
            m_OwnerThread != VxThread::GetCurrentVxThreadId()) {
            SetError(CKERR_INVALIDOPERATION);
            return FALSE;
        }
        return GetStatus() == CK_OK ? TRUE : FALSE;
    }

    void SetState(CKDrawState State) override
    {
        if (!CanRecord()) return;
        const CKERROR validation = CKRasterizerValidateDrawState(State);
        if (validation != CK_OK) {
            SetError(validation);
            return;
        }
        m_Snapshot.DrawState = State;
    }

    void SetStencilRef(CKDWORD Ref) override
    {
        if (!CanRecord()) return;
        if (Ref > 0xff) {
            SetError(CKERR_INVALIDPARAMETER);
            return;
        }
        m_Snapshot.StencilRef = Ref;
    }

    void SetStencilMask(CKDWORD ReadMask, CKDWORD WriteMask) override
    {
        if (!CanRecord()) return;
        if (ReadMask > 0xff || WriteMask > 0xff) {
            SetError(CKERR_INVALIDPARAMETER);
            return;
        }
        if (WriteMask != 0 && WriteMask != 0xff) {
            SetError(CKERR_NOTIMPLEMENTED);
            return;
        }
        m_Snapshot.StencilReadMask = ReadMask;
        m_Snapshot.StencilWriteMask = WriteMask;
    }

    void SetScissor(const CKRECT *Rect) override
    {
        if (!CanRecord()) return;
        if (!Rect) {
            m_Snapshot.ScissorEnabled = FALSE;
            return;
        }
        if (Rect->left < 0 || Rect->top < 0 || Rect->right < Rect->left ||
            Rect->bottom < Rect->top || Rect->left > 0xffff || Rect->top > 0xffff ||
            Rect->right - Rect->left > 0xffff || Rect->bottom - Rect->top > 0xffff) {
            SetError(CKERR_INVALIDPARAMETER);
            return;
        }
        m_Snapshot.Scissor = *Rect;
        m_Snapshot.ScissorEnabled = TRUE;
    }

    void SetPointSize(float Size) override
    {
        if (!CanRecord()) return;
        if (!(Size >= 0.0f && Size <= 15.0f)) {
            SetError(CKERR_INVALIDPARAMETER);
            return;
        }
        m_Snapshot.PointSize = Size;
    }

    void SetTransform(CKDWORD TransformIndex, CKDWORD Count) override
    {
        if (!CanRecord()) return;
        VxMutexLock lock(m_State->TransformMutex);
        if (TransformIndex == CKRST_INVALID_TRANSFORM || Count == 0 ||
            TransformIndex > (CKDWORD)m_State->Transforms.Size() ||
            Count > (CKDWORD)m_State->Transforms.Size() - TransformIndex) {
            SetError(CKERR_INVALIDPARAMETER);
            return;
        }
        m_Snapshot.Transform = TransformIndex;
        m_Snapshot.TransformCount = Count;
    }

    void SetVertexBuffer(CKDWORD Stream, CKDWORD Buffer, CKDWORD StartVertex,
                         CKDWORD VertexCount, CKDWORD Layout) override
    {
        if (!CanRecord()) return;
        CKNullResourceRecord buffer;
        if (Stream >= CKRST_MAX_VERTEX_STREAMS || VertexCount == 0 ||
            !CKNullFindResource(m_State, Buffer, CKRST_OBJ_VERTEXBUFFER, &buffer) ||
            !CKNullFindResource(m_State, Layout, CKRST_OBJ_VERTEXLAYOUT) ||
            StartVertex > buffer.Count || VertexCount > buffer.Count - StartVertex) {
            SetError(CKERR_INVALIDPARAMETER);
            return;
        }
        m_Snapshot.VertexBuffers[Stream] = Buffer;
        m_Snapshot.VertexLayouts[Stream] = Layout;
        m_Snapshot.VertexStarts[Stream] = StartVertex;
        m_Snapshot.VertexCounts[Stream] = VertexCount;
    }

    void SetIndexBuffer(CKDWORD Buffer, CKDWORD StartIndex, CKDWORD IndexCount) override
    {
        if (!CanRecord()) return;
        CKNullResourceRecord buffer;
        if (IndexCount == 0 ||
            !CKNullFindResource(m_State, Buffer, CKRST_OBJ_INDEXBUFFER, &buffer) ||
            StartIndex > buffer.Count || IndexCount > buffer.Count - StartIndex) {
            SetError(CKERR_INVALIDPARAMETER);
            return;
        }
        m_Snapshot.IndexBuffer = Buffer;
        m_Snapshot.IndexStart = StartIndex;
        m_Snapshot.IndexCount = IndexCount;
    }

    void SetInstanceBuffer(CKDWORD Stream, CKDWORD Buffer, CKDWORD StartInstance,
                           CKDWORD InstanceCount) override
    {
        (void)Stream;
        (void)Buffer;
        (void)StartInstance;
        (void)InstanceCount;
        if (CanRecord()) SetError(CKERR_NOTIMPLEMENTED);
    }

    void SetTransientVertexBuffer(CKDWORD, CKTransientVertexBuffer *) override
    {
        if (CanRecord()) SetError(CKERR_NOTIMPLEMENTED);
    }

    void SetTransientIndexBuffer(CKTransientIndexBuffer *) override
    {
        if (CanRecord()) SetError(CKERR_NOTIMPLEMENTED);
    }

    void SetTransientInstanceBuffer(CKDWORD, CKTransientInstanceBuffer *) override
    {
        if (CanRecord()) SetError(CKERR_NOTIMPLEMENTED);
    }

    void SetTexture(CKDWORD Stage, CKDWORD Uniform, CKDWORD Texture,
                    CKSamplerDesc *Sampler) override
    {
        if (!CanRecord()) return;
        CKNullResourceRecord uniform;
        if (Stage >= CKRST_MAX_TEXTURE_STAGES ||
            !CKNullFindResource(m_State, Uniform, CKRST_OBJ_UNIFORM, &uniform) ||
            uniform.Kind != CKRST_UNIFORM_SAMPLER ||
            (Texture != 0 && !CKNullFindResource(m_State, Texture, CKRST_OBJ_TEXTURE)) ||
            CKRasterizerValidateSampler(Sampler) != CK_OK) {
            SetError(CKERR_INVALIDPARAMETER);
            return;
        }
        if (Sampler && Sampler->CompareFunc != CKRST_COMPARE_NONE) {
            SetError(CKERR_NOTIMPLEMENTED);
            return;
        }
        m_Snapshot.Textures[Stage] = Texture;
        m_Snapshot.TextureUniforms[Stage] = Uniform;
    }

    void SetUniform(CKDWORD Uniform, const void *Data, CKDWORD Count) override
    {
        if (!CanRecord()) return;
        CKNullResourceRecord uniform;
        if (!Data || Count == 0 ||
            !CKNullFindResource(m_State, Uniform, CKRST_OBJ_UNIFORM, &uniform) ||
            uniform.Kind == CKRST_UNIFORM_SAMPLER || Count > uniform.Count) {
            SetError(CKERR_INVALIDPARAMETER);
            return;
        }
    }

    void Discard(CKDWORD Flags) override
    {
        const CKERROR validation = CKRasterizerValidateDiscard(Flags);
        if (validation != CK_OK) {
            SetError(validation);
            return;
        }
        if (!m_Active.load(std::memory_order_acquire) || !m_Context || !m_State ||
            m_OwnerThread != VxThread::GetCurrentVxThreadId()) {
            SetError(CKERR_INVALIDOPERATION);
            return;
        }
        ApplyDiscard(Flags);
        m_Status.store(CK_OK, std::memory_order_release);
    }

    void SetComputeBuffer(CKDWORD, CKDWORD, CK_ACCESS_MODE) override
    {
        if (CanRecord()) SetError(CKERR_NOTIMPLEMENTED);
    }

    void SetComputeImage(CKDWORD, CKDWORD, CKDWORD, CK_ACCESS_MODE) override
    {
        if (CanRecord()) SetError(CKERR_NOTIMPLEMENTED);
    }

    void SetCondition(CKDWORD, CKBOOL) override
    {
        if (CanRecord()) SetError(CKERR_NOTIMPLEMENTED);
    }

    void SetMarker(CKSTRING Name) override
    {
        if (!CanRecord()) return;
        m_Marker = Name ? Name : "";
    }

    CKBOOL ConsumeMarker(char *Buffer, CKDWORD BufferSize) override
    {
        if (!CanRecord() || !Buffer || BufferSize == 0)
            return FALSE;
        Buffer[0] = '\0';
        if (m_Marker.IsEmpty())
            return FALSE;
        const CKDWORD count = XMin((CKDWORD)m_Marker.Length(), BufferSize - 1);
        memcpy(Buffer, m_Marker.CStr(), count);
        Buffer[count] = '\0';
        m_Marker.Clear();
        return TRUE;
    }

    void Submit(CKRenderView View, CKDWORD Program, CKDWORD Depth,
                CKDWORD Flags) override
    {
        if (!ValidateSubmit(View, Program, Flags)) return;
        RecordSubmit(View, Program, Depth, Flags);
    }

    void SubmitOcclusionQuery(CKRenderView, CKDWORD, CKDWORD, CKDWORD, CKDWORD) override
    {
        if (CanRecord()) SetError(CKERR_NOTIMPLEMENTED);
    }

    void SubmitIndirect(CKRenderView, CKDWORD, CKDWORD, CKDWORD, CKDWORD,
                        CKDWORD, CKDWORD) override
    {
        if (CanRecord()) SetError(CKERR_NOTIMPLEMENTED);
    }

    void Dispatch(CKRenderView, CKDWORD, CKDWORD, CKDWORD, CKDWORD, CKDWORD) override
    {
        if (CanRecord()) SetError(CKERR_NOTIMPLEMENTED);
    }

    void DispatchIndirect(CKRenderView, CKDWORD, CKDWORD, CKDWORD, CKDWORD,
                          CKDWORD) override
    {
        if (CanRecord()) SetError(CKERR_NOTIMPLEMENTED);
    }

    void Touch(CKRenderView View) override
    {
        if (!CanRecord()) return;
        if (View >= CKRST_MAX_RENDER_VIEWS) {
            SetError(CKERR_INVALIDPARAMETER);
            return;
        }
        RecordSubmit(View, 0, 0, CKRST_DISCARD_NONE);
    }

    void Blit(CKRenderView, CKDWORD, CKDWORD, CKDWORD, CKDWORD, CKDWORD,
              CKDWORD, const CKRECT *) override
    {
        if (CanRecord()) SetError(CKERR_NOTIMPLEMENTED);
    }

    std::atomic<bool> m_Active;
    XUINTPTR m_OwnerThread;

private:
    void ResetSnapshot()
    {
        memset(&m_Snapshot, 0, sizeof(m_Snapshot));
        m_Snapshot.StencilReadMask = 0xff;
        m_Snapshot.StencilWriteMask = 0xff;
    }

    CKBOOL ValidateSubmit(CKRenderView View, CKDWORD Program, CKDWORD Flags)
    {
        if (!CanRecord()) return FALSE;
        if (View >= CKRST_MAX_RENDER_VIEWS ||
            CKRasterizerValidateDiscard(Flags) != CK_OK ||
            !CKNullFindResource(m_State, Program, CKRST_OBJ_PROGRAM)) {
            SetError(CKERR_INVALIDPARAMETER);
            return FALSE;
        }
        return TRUE;
    }

    void RecordSubmit(CKRenderView View, CKDWORD Program, CKDWORD Depth, CKDWORD Flags)
    {
        CKNullSubmission submission;
        submission.View = View;
        submission.Program = Program;
        submission.Depth = Depth;
        submission.Discard = Flags;
        submission.Snapshot = m_Snapshot;
        {
            VxMutexLock lock(m_State->SubmissionMutex);
            m_State->CurrentSubmissions.PushBack(submission);
        }
        ApplyDiscard(Flags);
    }

    void ApplyDiscard(CKDWORD Flags)
    {
        if (Flags & CKRST_DISCARD_BINDINGS) {
            memset(m_Snapshot.Textures, 0, sizeof(m_Snapshot.Textures));
            memset(m_Snapshot.TextureUniforms, 0, sizeof(m_Snapshot.TextureUniforms));
        }
        if (Flags & CKRST_DISCARD_INDEX_BUFFER) {
            m_Snapshot.IndexBuffer = 0;
            m_Snapshot.IndexStart = 0;
            m_Snapshot.IndexCount = 0;
        }
        if (Flags & CKRST_DISCARD_STATE) {
            memset(&m_Snapshot.DrawState, 0, sizeof(m_Snapshot.DrawState));
            m_Snapshot.StencilRef = 0;
            m_Snapshot.StencilReadMask = 0xff;
            m_Snapshot.StencilWriteMask = 0xff;
            m_Snapshot.ScissorEnabled = FALSE;
            m_Snapshot.PointSize = 0.0f;
        }
        if (Flags & CKRST_DISCARD_TRANSFORM) {
            m_Snapshot.Transform = 0;
            m_Snapshot.TransformCount = 0;
        }
        if (Flags & CKRST_DISCARD_VERTEX_STREAMS) {
            memset(m_Snapshot.VertexBuffers, 0, sizeof(m_Snapshot.VertexBuffers));
            memset(m_Snapshot.VertexLayouts, 0, sizeof(m_Snapshot.VertexLayouts));
            memset(m_Snapshot.VertexStarts, 0, sizeof(m_Snapshot.VertexStarts));
            memset(m_Snapshot.VertexCounts, 0, sizeof(m_Snapshot.VertexCounts));
        }
    }

    CKRasterizerContext *m_Context;
    CKNullContextState *m_State;
    std::atomic<CKERROR> m_Status;
    std::atomic<CKERROR> m_FrameStatus;
    CKNullEncoderSnapshot m_Snapshot;
    XString m_Marker;
};

CKNullContextState::CKNullContextState()
    : NextResourceHandle(1),
      FrameInProgress(FALSE),
      ShuttingDown(FALSE),
      DefaultEncoder(new CKNullRasterizerEncoder()),
      PoolEncoder(new CKNullRasterizerEncoder())
{
    for (CKRenderView view = 0; view < CKRST_MAX_RENDER_VIEWS; ++view) {
        CKNullResetView(Views[view]);
        ViewOrder[view] = view;
    }
}

CKNullContextState::~CKNullContextState()
{
    delete DefaultEncoder;
    delete PoolEncoder;
}

} // namespace

CKRasterizerContext::CKRasterizerContext()
    : m_Driver(NULL),
      m_PosX(0),
      m_PosY(0),
      m_Width(0),
      m_Height(0),
      m_Bpp(0),
      m_ZBpp(0),
      m_StencilBpp(0),
      m_Fullscreen(FALSE),
      m_RefreshRate(0),
      m_Window(NULL),
      m_Created(FALSE),
      m_NullFrameNumber(0),
      m_NullBackendState(new CKNullContextState())
{
}

CKRasterizerContext::~CKRasterizerContext()
{
    delete CKNullState(this);
    m_NullBackendState = NULL;
}

CKERROR CKRasterizerContext::Create(WIN_HANDLE Window, int PosX, int PosY,
                                    int Width, int Height, int Bpp,
                                    CKBOOL Fullscreen, int RefreshRate,
                                    int Zbpp, int StencilBpp)
{
    if (m_Created)
        return CKERR_INVALIDOPERATION;
    if (Width <= 0 || Height <= 0)
        return CKERR_INVALIDPARAMETER;
    CKNullContextState *state = CKNullState(this);
    state->ApiThread = VxThread::GetCurrentVxThreadId();
    state->ShuttingDown = FALSE;
    m_Window = Window;
    m_PosX = PosX;
    m_PosY = PosY;
    m_Width = Width;
    m_Height = Height;
    m_Bpp = Bpp;
    m_Fullscreen = Fullscreen;
    m_RefreshRate = RefreshRate;
    m_ZBpp = Zbpp;
    m_StencilBpp = StencilBpp;
    m_Created = TRUE;
    return CK_OK;
}

CKERROR CKRasterizerContext::Resize(int PosX, int PosY, int Width, int Height,
                                    CKDWORD Flags)
{
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    if (Width <= 0 || Height <= 0 || Flags != 0)
        return CKERR_INVALIDPARAMETER;
    CKNullContextState *state = CKNullState(this);
    VxMutexLock lifecycleLock(state->EncoderLifecycleMutex);
    if (state->FrameInProgress ||
        state->DefaultEncoder->m_Active.load(std::memory_order_acquire) ||
        state->PoolEncoder->m_Active.load(std::memory_order_acquire))
        return CKERR_INVALIDOPERATION;
    m_PosX = PosX;
    m_PosY = PosY;
    m_Width = Width;
    m_Height = Height;
    return CK_OK;
}

CKERROR CKRasterizerContext::SetAntialias(CKDWORD Samples)
{
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    if (Samples != 0 && Samples != 2 && Samples != 4 &&
        Samples != 8 && Samples != 16)
        return CKERR_INVALIDPARAMETER;
    CKNullContextState *state = CKNullState(this);
    VxMutexLock lifecycleLock(state->EncoderLifecycleMutex);
    return state->FrameInProgress ||
           state->DefaultEncoder->m_Active.load(std::memory_order_acquire) ||
           state->PoolEncoder->m_Active.load(std::memory_order_acquire)
        ? CKERR_INVALIDOPERATION : CK_OK;
}

CKBOOL CKRasterizerContext::IsIdle() const
{
    const CKNullContextState *constState = CKNullState(this);
    if (!constState)
        return TRUE;
    CKNullContextState *state = const_cast<CKNullContextState *>(constState);
    VxMutexLock lifecycleLock(state->EncoderLifecycleMutex);
    return !state->FrameInProgress &&
           !state->DefaultEncoder->m_Active.load(std::memory_order_acquire) &&
           !state->PoolEncoder->m_Active.load(std::memory_order_acquire)
        ? TRUE : FALSE;
}

CKERROR CKRasterizerContext::GetDeviceStatus() const
{
    if (!m_Created)
        return CKERR_INVALIDRENDERCONTEXT;
    CKNullContextState *state = const_cast<CKNullContextState *>(CKNullState(this));
    if (!state)
        return CKERR_INVALIDRENDERCONTEXT;
    VxMutexLock lifecycleLock(state->EncoderLifecycleMutex);
    return state->ShuttingDown ? CKERR_INVALIDOPERATION : CK_OK;
}

CKERROR CKRasterizerContext::BeginShutdown()
{
    CKNullContextState *state = CKNullState(this);
    if (!m_Created || !state)
        return CKERR_INVALIDRENDERCONTEXT;
    VxMutexLock lifecycleLock(state->EncoderLifecycleMutex);
    if (state->FrameInProgress ||
        state->DefaultEncoder->m_Active.load(std::memory_order_acquire) ||
        state->PoolEncoder->m_Active.load(std::memory_order_acquire))
        return CKERR_INVALIDOPERATION;
    state->ShuttingDown = TRUE;
    return CK_OK;
}

CKERROR CKRasterizerContext::GetTargetDesc(CKRasterizerTargetDesc *Target) const
{
    if (!Target || Target->Size < sizeof(CKRasterizerTargetDesc))
        return CKERR_INVALIDPARAMETER;
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    *Target = CKRasterizerTargetDesc();
    return CK_OK;
}

CKERROR CKRasterizerContext::GetCaps(CKRasterizerCapsDesc *Caps) const
{
    if (!Caps || Caps->Size < sizeof(CKRasterizerCapsDesc))
        return CKERR_INVALIDPARAMETER;
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    *Caps = CKRasterizerCapsDesc();
    Caps->Features = CKRST_CAPS_RENDER_VIEWS |
                     CKRST_CAPS_FRAMEBUFFER |
                     CKRST_CAPS_SCISSOR |
                     CKRST_CAPS_BUFFER_UPDATE |
                     CKRST_CAPS_TEXTURE_UPDATE |
                     CKRST_CAPS_DEPTH_TEXTURE |
                     CKRST_CAPS_TRANSFORM_CACHE;
    Caps->MaxDrawCalls = 1024;
    Caps->MaxTextureSize = 4096;
    Caps->MaxTextureLayers = 1;
    Caps->MaxRenderViews = CKRST_MAX_RENDER_VIEWS;
    Caps->MaxFrameBuffers = 64;
    Caps->MaxColorAttachments = 1;
    Caps->MaxPrograms = 0;
    Caps->MaxShaders = 0;
    Caps->MaxTextures = 64;
    Caps->MaxTextureStages = CKRST_MAX_TEXTURE_STAGES;
    Caps->MaxVertexLayouts = 64;
    Caps->MaxVertexStreams = CKRST_MAX_VERTEX_STREAMS;
    Caps->MaxIndexBuffers = 64;
    Caps->MaxVertexBuffers = 64;
    Caps->MaxDynamicIndexBuffers = 64;
    Caps->MaxDynamicVertexBuffers = 64;
    Caps->MaxUniforms = 128;
    Caps->MaxEncoders = 1;
    Caps->MaxTransforms = CKRST_MAX_TRANSFORMS;
    return CK_OK;
}

CKERROR CKRasterizerContext::GetTextureFormatCaps(VX_PIXELFORMAT Format,
                                                   CKTextureFormatCaps *Caps) const
{
    if (!Caps || Caps->Size < sizeof(CKTextureFormatCaps))
        return CKERR_INVALIDPARAMETER;
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    *Caps = CKTextureFormatCaps();
    Caps->Format = Format;
    if (Format == _32_ARGB8888)
        Caps->Caps = CKRST_FORMAT_CAPS_TEXTURE_2D |
                     CKRST_FORMAT_CAPS_FRAMEBUFFER;
    return CK_OK;
}

CKERROR CKRasterizerContext::GetDepthFormatCaps(CK_DEPTH_FORMAT Format,
                                                 CKDepthFormatCaps *Caps) const
{
    if (!Caps || Caps->Size < sizeof(CKDepthFormatCaps))
        return CKERR_INVALIDPARAMETER;
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    switch (Format) {
    case CKRST_DEPTHFMT_D16:
    case CKRST_DEPTHFMT_D24:
    case CKRST_DEPTHFMT_D24S8:
    case CKRST_DEPTHFMT_D32F:
        break;
    default:
        return CKERR_INVALIDPARAMETER;
    }
    *Caps = CKDepthFormatCaps();
    Caps->Format = Format;
    if (Format == CKRST_DEPTHFMT_D16)
        Caps->Caps = CKRST_FORMAT_CAPS_TEXTURE_2D |
                     CKRST_FORMAT_CAPS_FRAMEBUFFER;
    return CK_OK;
}

static CKERROR CKNullCreateObject(CKRasterizerContext *Context, CKDWORD Type,
                                  CKDWORD *OutObject, CKDWORD Capacity = 0,
                                  CKDWORD Count = 0, CKDWORD Kind = 0,
                                  CKDWORD Attachment = 0,
                                  CKSTRING Name = NULL)
{
    if (!Context || !OutObject)
        return CKERR_INVALIDPARAMETER;
    *OutObject = 0;
    if (!Context->m_Created || !CKNullIsApiThread(Context))
        return CKERR_INVALIDOPERATION;
    CKNullContextState *state = CKNullState(Context);
    CKDWORD limit = 64;
    if (Type == CKRST_OBJ_SHADER || Type == CKRST_OBJ_UNIFORM)
        limit = 128;
    VxMutexLock lock(state->ResourceMutex);
    CKDWORD liveCount = 0;
    for (int i = 0; i < state->Resources.Size(); ++i) {
        if (state->Resources[i].Type == Type)
            ++liveCount;
    }
    if (liveCount >= limit)
        return CKERR_OUTOFMEMORY;
    CKDWORD handle = 0;
    do {
        handle = state->NextResourceHandle++;
        if (handle == 0)
            continue;

        CKBOOL collision = FALSE;
        for (int i = 0; i < state->Resources.Size(); ++i) {
            if (state->Resources[i].Handle == handle) {
                collision = TRUE;
                break;
            }
        }
        if (!collision)
            break;
        handle = 0;
    } while (TRUE);
    CKNullResourceRecord resource;
    resource.Handle = handle;
    resource.Type = Type;
    resource.Capacity = Capacity;
    resource.Count = Count;
    resource.Kind = Kind;
    resource.Attachment = Attachment;
    resource.Name = Name ? Name : "";
    state->Resources.PushBack(resource);
    *OutObject = handle;
    return CK_OK;
}

CKERROR CKRasterizerContext::CreateVertexBuffer(const CKVertexBufferDesc *Desc,
                                                 const void *, CKDWORD *OutBuffer)
{
    if (!OutBuffer)
        return CKERR_INVALIDPARAMETER;
    *OutBuffer = 0;
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    if (!Desc || Desc->m_MaxVertexCount == 0 || Desc->m_VertexSize == 0)
        return CKERR_INVALIDPARAMETER;
    if (Desc->m_Flags & (CKRST_VB_COMPUTE_READ | CKRST_VB_COMPUTE_WRITE))
        return CKERR_NOTIMPLEMENTED;
    if (Desc->m_MaxVertexCount > 0xffffffffu / Desc->m_VertexSize)
        return CKERR_INVALIDPARAMETER;
    return CKNullCreateObject(this, CKRST_OBJ_VERTEXBUFFER, OutBuffer,
                              Desc->m_MaxVertexCount * Desc->m_VertexSize,
                              Desc->m_MaxVertexCount);
}

CKERROR CKRasterizerContext::CreateIndexBuffer(const CKIndexBufferDesc *Desc,
                                                CKBOOL Index32, const void *, CKDWORD *OutBuffer)
{
    if (!OutBuffer)
        return CKERR_INVALIDPARAMETER;
    *OutBuffer = 0;
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    if (!Desc || Desc->m_MaxIndexCount == 0)
        return CKERR_INVALIDPARAMETER;
    if (Desc->m_MaxIndexCount > 0x7fffffffu)
        return CKERR_INVALIDPARAMETER;
    if (Index32)
        return CKERR_NOTIMPLEMENTED;
    return CKNullCreateObject(this, CKRST_OBJ_INDEXBUFFER, OutBuffer,
                              Desc->m_MaxIndexCount * 2,
                              Desc->m_MaxIndexCount);
}

CKERROR CKRasterizerContext::CreateTexture(const CKTextureDesc *Desc,
                                            const VxImageDescEx *, CKDWORD *OutTexture)
{
    if (!OutTexture)
        return CKERR_INVALIDPARAMETER;
    *OutTexture = 0;
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    if (!Desc || Desc->Format.Width <= 0 || Desc->Format.Height <= 0 ||
        Desc->Format.Width > 4096 || Desc->Format.Height > 4096)
        return CKERR_INVALIDPARAMETER;
    if (VxImageDesc2PixelFormat(Desc->Format) != _32_ARGB8888)
        return CKERR_NOTIMPLEMENTED;
    if (Desc->Flags & CKRST_TEXTURE_DEPTHSTENCIL)
        return CKERR_INVALIDPARAMETER;
    if (Desc->Flags & (CKRST_TEXTURE_CUBEMAP | CKRST_TEXTURE_VOLUMEMAP |
                       CKRST_TEXTURE_BLIT_DST | CKRST_TEXTURE_COMPUTE_WRITE |
                       CKRST_TEXTURE_READBACK))
        return CKERR_NOTIMPLEMENTED;
    if (Desc->Depth != 1)
        return CKERR_INVALIDPARAMETER;
    const CKDWORD fullMipCount = CKNullMipCount(
        Desc->Format.Width, Desc->Format.Height);
    if (Desc->MipMapCount == static_cast<CKDWORD>(-1) ||
        Desc->MipMapCount > fullMipCount)
        return CKERR_NOTIMPLEMENTED;
    const CKDWORD mipCount = Desc->MipMapCount > 1 ? fullMipCount : 1;
    const CKDWORD dimensions = static_cast<CKDWORD>(Desc->Format.Width) |
                               (static_cast<CKDWORD>(Desc->Format.Height) << 16);
    return CKNullCreateObject(this, CKRST_OBJ_TEXTURE, OutTexture, dimensions,
                              mipCount,
                              Desc->Flags);
}

CKERROR CKRasterizerContext::CreateShader(const CKShaderDesc *Desc, CKDWORD *OutShader)
{
    if (!OutShader)
        return CKERR_INVALIDPARAMETER;
    *OutShader = 0;
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    if (!Desc || !Desc->Code || Desc->CodeSize == 0 ||
        (Desc->Stage != CKRST_SHADER_VERTEX && Desc->Stage != CKRST_SHADER_PIXEL))
        return CKERR_INVALIDPARAMETER;
    return CKERR_NOTIMPLEMENTED;
}

CKERROR CKRasterizerContext::CreateProgram(const CKProgramDesc *Desc, CKDWORD *OutProgram)
{
    if (!OutProgram)
        return CKERR_INVALIDPARAMETER;
    *OutProgram = 0;
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    if (!Desc)
        return CKERR_INVALIDPARAMETER;
    return CKERR_NOTIMPLEMENTED;
}

CKERROR CKRasterizerContext::CreateUniform(const CKUniformDesc *Desc, CKDWORD *OutUniform)
{
    if (!OutUniform)
        return CKERR_INVALIDPARAMETER;
    *OutUniform = 0;
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    if (!Desc || !Desc->Name || Desc->Count == 0 ||
        (Desc->Type != CKRST_UNIFORM_SAMPLER && Desc->Type != CKRST_UNIFORM_VEC4 &&
         Desc->Type != CKRST_UNIFORM_MAT3 && Desc->Type != CKRST_UNIFORM_MAT4))
        return CKERR_INVALIDPARAMETER;
    return CKNullCreateObject(this, CKRST_OBJ_UNIFORM, OutUniform, 0,
                              Desc->Count, Desc->Type, 0, Desc->Name);
}

CKERROR CKRasterizerContext::CreateVertexLayout(const CKVertexLayoutDesc *Desc,
                                                 CKDWORD *OutLayout)
{
    if (!OutLayout)
        return CKERR_INVALIDPARAMETER;
    *OutLayout = 0;
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    if (CKRasterizerValidateVertexLayout(Desc) != CK_OK)
        return CKERR_INVALIDPARAMETER;
    return CKNullCreateObject(this, CKRST_OBJ_VERTEXLAYOUT, OutLayout,
                              Desc->Stride, Desc->ElementCount);
}

CKERROR CKRasterizerContext::CreateFrameBuffer(const CKFrameBufferDesc *Desc,
                                                CKDWORD *OutFrameBuffer)
{
    if (!OutFrameBuffer)
        return CKERR_INVALIDPARAMETER;
    *OutFrameBuffer = 0;
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    CKNullContextState *state = CKNullState(this);
    CKNullResourceRecord color;
    CKNullResourceRecord depth;
    if (!Desc || Desc->ColorCount > 1 ||
        (Desc->ColorCount != 0 &&
          (!Desc->Color ||
           !CKNullFindResource(state, Desc->Color[0].Texture, CKRST_OBJ_TEXTURE, &color) ||
           (color.Kind & CKRST_TEXTURE_DEPTHSTENCIL) != 0 ||
           (color.Kind & CKRST_TEXTURE_RENDERTARGET) == 0 ||
           Desc->Color[0].Mip >= color.Count || Desc->Color[0].Layer != 0)) ||
        (Desc->DepthStencil.Texture != 0 &&
         (!CKNullFindResource(state, Desc->DepthStencil.Texture, CKRST_OBJ_TEXTURE, &depth) ||
          (depth.Kind & CKRST_TEXTURE_DEPTHSTENCIL) == 0 ||
          Desc->DepthStencil.Mip >= depth.Count || Desc->DepthStencil.Layer != 0)) ||
        (Desc->ColorCount == 0 && Desc->DepthStencil.Texture == 0))
        return CKERR_INVALIDPARAMETER;
    if (Desc->ColorCount != 0 && Desc->DepthStencil.Texture != 0) {
        const CKDWORD colorWidth = XMax((CKDWORD)1,
            (color.Capacity & 0xffff) >> Desc->Color[0].Mip);
        const CKDWORD colorHeight = XMax((CKDWORD)1,
            (color.Capacity >> 16) >> Desc->Color[0].Mip);
        const CKDWORD depthWidth = XMax((CKDWORD)1,
            (depth.Capacity & 0xffff) >> Desc->DepthStencil.Mip);
        const CKDWORD depthHeight = XMax((CKDWORD)1,
            (depth.Capacity >> 16) >> Desc->DepthStencil.Mip);
        if (colorWidth != depthWidth || colorHeight != depthHeight)
            return CKERR_NOTIMPLEMENTED;
    }
    return CKNullCreateObject(this, CKRST_OBJ_FRAMEBUFFER, OutFrameBuffer,
                              0, Desc->ColorCount, 0,
                              Desc->ColorCount ? Desc->Color[0].Texture : 0);
}

CKERROR CKRasterizerContext::CreateDepthTexture(const CKDepthTextureDesc *Desc,
                                                 CKDWORD *OutTexture)
{
    if (!OutTexture)
        return CKERR_INVALIDPARAMETER;
    *OutTexture = 0;
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    if (!Desc || Desc->Width == 0 || Desc->Height == 0 ||
        Desc->Width > 4096 || Desc->Height > 4096)
        return CKERR_INVALIDPARAMETER;
    if (Desc->DepthFormat != CKRST_DEPTHFMT_D16)
        return CKERR_NOTIMPLEMENTED;
    if (Desc->MipMapCount > CKNullMipCount(Desc->Width, Desc->Height))
        return CKERR_INVALIDPARAMETER;
    const CKDWORD dimensions = Desc->Width | (Desc->Height << 16);
    return CKNullCreateObject(this, CKRST_OBJ_TEXTURE, OutTexture, dimensions,
                              Desc->MipMapCount ? Desc->MipMapCount : 1,
                              CKRST_TEXTURE_DEPTHSTENCIL);
}

CKERROR CKRasterizerContext::CreateOcclusionQuery(const CKOcclusionQueryDesc *Desc,
                                                   CKDWORD *OutQuery)
{
    if (!OutQuery)
        return CKERR_INVALIDPARAMETER;
    *OutQuery = 0;
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    return Desc ? CKERR_NOTIMPLEMENTED : CKERR_INVALIDPARAMETER;
}

CKERROR CKRasterizerContext::CreateIndirectBuffer(const CKIndirectBufferDesc *Desc,
                                                   CKDWORD *OutBuffer)
{
    if (!OutBuffer)
        return CKERR_INVALIDPARAMETER;
    *OutBuffer = 0;
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    return Desc ? CKERR_NOTIMPLEMENTED : CKERR_INVALIDPARAMETER;
}

CKBOOL CKRasterizerContext::IsObjectAlive(CKDWORD Object, CKDWORD Type) const
{
    if (!m_Created || !CKNullIsApiThread(this))
        return FALSE;
    return CKNullFindResource(const_cast<CKNullContextState *>(CKNullState(this)),
                              Object, Type);
}

CKERROR CKRasterizerContext::DeleteObject(CKDWORD Object, CKDWORD Type)
{
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    CKNullContextState *state = CKNullState(this);
    VxMutexLock lifecycleLock(state->EncoderLifecycleMutex);
    if (state->FrameInProgress ||
        state->DefaultEncoder->m_Active.load(std::memory_order_acquire) ||
        state->PoolEncoder->m_Active.load(std::memory_order_acquire))
        return CKERR_INVALIDOPERATION;
    VxMutexLock lock(state->ResourceMutex);
    for (int i = 0; i < state->Resources.Size(); ++i) {
        if (state->Resources[i].Handle == Object && state->Resources[i].Type == Type) {
            state->Resources.RemoveAt(i);
            return CK_OK;
        }
    }
    return CKERR_INVALIDPARAMETER;
}

CKERROR CKRasterizerContext::FlushObjects(CKDWORD TypeMask)
{
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    if (CKRasterizerValidateObjectMask(TypeMask) != CK_OK)
        return CKERR_INVALIDPARAMETER;
    CKNullContextState *state = CKNullState(this);
    VxMutexLock lifecycleLock(state->EncoderLifecycleMutex);
    if (state->FrameInProgress ||
        state->DefaultEncoder->m_Active.load(std::memory_order_acquire) ||
        state->PoolEncoder->m_Active.load(std::memory_order_acquire))
        return CKERR_INVALIDOPERATION;
    VxMutexLock lock(state->ResourceMutex);
    for (int i = state->Resources.Size(); i > 0; --i) {
        if (state->Resources[i - 1].Type & TypeMask)
            state->Resources.RemoveAt(i - 1);
    }
    return CK_OK;
}

CKERROR CKRasterizerContext::UpdateVertexBuffer(CKDWORD Buffer, CKDWORD Offset,
                                                 CKDWORD Size, const void *Data)
{
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    CKNullResourceRecord buffer;
    return Data && Size > 0 &&
           CKNullFindResource(CKNullState(this), Buffer, CKRST_OBJ_VERTEXBUFFER, &buffer) &&
           Offset <= buffer.Capacity && Size <= buffer.Capacity - Offset
        ? CK_OK : CKERR_INVALIDPARAMETER;
}

CKERROR CKRasterizerContext::UpdateIndexBuffer(CKDWORD Buffer, CKDWORD Offset,
                                                CKDWORD Size, const void *Data)
{
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    CKNullResourceRecord buffer;
    return Data && Size > 0 &&
           CKNullFindResource(CKNullState(this), Buffer, CKRST_OBJ_INDEXBUFFER, &buffer) &&
           Offset <= buffer.Capacity && Size <= buffer.Capacity - Offset
        ? CK_OK : CKERR_INVALIDPARAMETER;
}

CKERROR CKRasterizerContext::UpdateTexture(CKDWORD Texture, CKDWORD Mip,
                                            CKDWORD Face, const CKRECT *Region,
                                            const VxImageDescEx *Data)
{
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    CKNullResourceRecord texture;
    if (!Data || !Data->Image || Face != 0 ||
        !CKNullFindResource(CKNullState(this), Texture, CKRST_OBJ_TEXTURE, &texture) ||
        (texture.Kind & CKRST_TEXTURE_DEPTHSTENCIL) != 0 || Mip >= texture.Count)
        return CKERR_INVALIDPARAMETER;
    const CKDWORD width = XMax((CKDWORD)1, (texture.Capacity & 0xffff) >> Mip);
    const CKDWORD height = XMax((CKDWORD)1, (texture.Capacity >> 16) >> Mip);
    if (Region && (Region->left < 0 || Region->top < 0 ||
                   Region->right <= Region->left || Region->bottom <= Region->top ||
                   static_cast<CKDWORD>(Region->right) > width ||
                   static_cast<CKDWORD>(Region->bottom) > height))
        return CKERR_INVALIDPARAMETER;
    const CKDWORD updateWidth = Region
        ? static_cast<CKDWORD>(Region->right - Region->left) : width;
    const CKDWORD updateHeight = Region
        ? static_cast<CKDWORD>(Region->bottom - Region->top) : height;
    return static_cast<CKDWORD>(Data->Width) == updateWidth &&
           static_cast<CKDWORD>(Data->Height) == updateHeight
        ? CK_OK : CKERR_INVALIDPARAMETER;
}

CKERROR CKRasterizerContext::ReadTexture(CKDWORD Texture, CKDWORD Mip,
                                         CKReadbackDesc *Readback,
                                         CKDWORD *AvailableFrame)
{
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    CKNullResourceRecord texture;
    if (!Readback || Readback->Size < sizeof(CKReadbackDesc) ||
        !CKNullFindResource(CKNullState(this), Texture, CKRST_OBJ_TEXTURE,
                            &texture) || Mip >= texture.Count)
        return CKERR_INVALIDPARAMETER;
    if (Readback->Data && !AvailableFrame)
        return CKERR_INVALIDPARAMETER;
    return CKERR_NOTIMPLEMENTED;
}

CK_OCCLUSION_RESULT CKRasterizerContext::GetOcclusionResult(CKDWORD, CKDWORD *)
{
    return CKRST_OCCLUSION_NORESULT;
}

CKERROR CKRasterizerContext::SetPaletteColor(CKDWORD Index, CKDWORD)
{
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    return Index < 16 ? CK_OK : CKERR_INVALIDPARAMETER;
}

void CKRasterizerContext::DbgTextClear(CKDWORD, CKBOOL)
{
}

void CKRasterizerContext::DbgTextPrintf(CKWORD, CKWORD, CKDWORD, CKSTRING, ...)
{
}

void CKRasterizerContext::DbgTextImage(CKWORD, CKWORD, CKWORD, CKWORD, const void *, CKWORD)
{
}

void CKRasterizerContext::SetDebug(CKDWORD)
{
}

const CKRenderStats *CKRasterizerContext::GetStats()
{
    static CKRenderStats stats = {};
    return &stats;
}

void CKRasterizerContext::SetResourceName(CKDWORD Handle, CKDWORD Type, CKSTRING Name)
{
    if (!m_Created || !CKNullIsApiThread(this) || !Name)
        return;
    CKNullContextState *state = CKNullState(this);
    VxMutexLock lock(state->ResourceMutex);
    for (int i = 0; i < state->Resources.Size(); ++i) {
        if (state->Resources[i].Handle == Handle && state->Resources[i].Type == Type) {
            state->Resources[i].Name = Name;
            return;
        }
    }
}

CKDWORD CKRasterizerContext::GetShaderUniforms(CKDWORD, CKDWORD *, CKDWORD)
{
    return 0;
}

void CKRasterizerContext::GetUniformInfo(CKDWORD Uniform, CKUniformInfo *Info)
{
    if (!Info)
        return;
    Info->Name[0] = '\0';
    Info->Type = CKRST_UNIFORM_VEC4;
    Info->Count = 0;
    CKNullResourceRecord uniform;
    if (!CKNullFindResource(CKNullState(this), Uniform, CKRST_OBJ_UNIFORM, &uniform))
        return;
    const CKDWORD count = XMin((CKDWORD)uniform.Name.Length(),
                               (CKDWORD)sizeof(Info->Name) - 1);
    memcpy(Info->Name, uniform.Name.CStr(), count);
    Info->Name[count] = '\0';
    Info->Type = static_cast<CK_UNIFORM_TYPE>(uniform.Kind);
    Info->Count = uniform.Count;
}

CKDWORD CKRasterizerContext::GetFrameBufferTexture(CKDWORD FrameBuffer,
                                                    CKDWORD Attachment)
{
    if (!m_Created || !CKNullIsApiThread(this) || Attachment != 0)
        return 0;
    CKNullResourceRecord frameBuffer;
    return CKNullFindResource(CKNullState(this), FrameBuffer,
                              CKRST_OBJ_FRAMEBUFFER, &frameBuffer)
        ? frameBuffer.Attachment : 0;
}

CKBOOL CKRasterizerContext::IsTextureValid(CKDWORD Depth, CKBOOL CubeMap,
                                            CKWORD NumLayers, CKDWORD Format,
                                            CKDWORD Flags)
{
    if (!m_Created || !CKNullIsApiThread(this))
        return FALSE;
    return Depth == 1 && !CubeMap && NumLayers == 1 && Format == _32_ARGB8888 &&
           (Flags & (CKRST_TEXTURE_CUBEMAP | CKRST_TEXTURE_VOLUMEMAP |
                     CKRST_TEXTURE_BLIT_DST | CKRST_TEXTURE_COMPUTE_WRITE |
                     CKRST_TEXTURE_READBACK | CKRST_TEXTURE_DEPTHSTENCIL)) == 0;
}

CKBOOL CKRasterizerContext::IsFrameBufferValid(CKDWORD ColorCount,
                                               const CKFrameBufferAttachmentDesc *Color,
                                               const CKFrameBufferAttachmentDesc *DepthStencil)
{
    if (!m_Created || !CKNullIsApiThread(this) || ColorCount > 1 ||
        (ColorCount != 0 && !Color))
        return FALSE;
    CKNullContextState *state = CKNullState(this);
    CKNullResourceRecord color;
    CKNullResourceRecord depth;
    if (ColorCount != 0 &&
        (!CKNullFindResource(state, Color[0].Texture, CKRST_OBJ_TEXTURE, &color) ||
         (color.Kind & CKRST_TEXTURE_DEPTHSTENCIL) != 0 ||
         (color.Kind & CKRST_TEXTURE_RENDERTARGET) == 0 ||
         Color[0].Mip >= color.Count || Color[0].Layer != 0))
        return FALSE;
    if (DepthStencil && DepthStencil->Texture != 0 &&
        (!CKNullFindResource(state, DepthStencil->Texture, CKRST_OBJ_TEXTURE, &depth) ||
         (depth.Kind & CKRST_TEXTURE_DEPTHSTENCIL) == 0 ||
         DepthStencil->Mip >= depth.Count || DepthStencil->Layer != 0))
        return FALSE;
    if (ColorCount != 0 && DepthStencil && DepthStencil->Texture != 0) {
        const CKDWORD colorWidth = XMax((CKDWORD)1,
            (color.Capacity & 0xffff) >> Color[0].Mip);
        const CKDWORD colorHeight = XMax((CKDWORD)1,
            (color.Capacity >> 16) >> Color[0].Mip);
        const CKDWORD depthWidth = XMax((CKDWORD)1,
            (depth.Capacity & 0xffff) >> DepthStencil->Mip);
        const CKDWORD depthHeight = XMax((CKDWORD)1,
            (depth.Capacity >> 16) >> DepthStencil->Mip);
        if (colorWidth != depthWidth || colorHeight != depthHeight)
            return FALSE;
    }
    return ColorCount != 0 || (DepthStencil && DepthStencil->Texture != 0);
}

void CKRasterizerContext::CalcTextureSize(CKTextureInfo *Info, CKWORD Width,
                                          CKWORD Height, CKWORD Depth,
                                          CKBOOL CubeMap, CKBOOL HasMips,
                                          CKWORD NumLayers, CKDWORD Format)
{
    if (!Info)
        return;
    memset(Info, 0, sizeof(*Info));
    if (!m_Created || !CKNullIsApiThread(this) || Width == 0 || Height == 0 ||
        Depth == 0 || NumLayers == 0 || Format != _32_ARGB8888)
        return;
    CKDWORD mipWidth = Width;
    CKDWORD mipHeight = Height;
    CKDWORD mipDepth = Depth;
    uint64_t texelCount = 0;
    CKDWORD mipCount = 0;
    do {
        texelCount += static_cast<uint64_t>(mipWidth) * mipHeight * mipDepth;
        ++mipCount;
        mipWidth = XMax((CKDWORD)1, mipWidth >> 1);
        mipHeight = XMax((CKDWORD)1, mipHeight >> 1);
        mipDepth = XMax((CKDWORD)1, mipDepth >> 1);
    } while (HasMips && (mipWidth != 1 || mipHeight != 1 || mipDepth != 1));
    if (HasMips && (Width != 1 || Height != 1 || Depth != 1)) {
        texelCount += 1;
        ++mipCount;
    }
    const uint64_t storage = texelCount * 4 * NumLayers * (CubeMap ? 6 : 1);
    if (storage > 0xffffffffu)
        return;
    Info->Format = Format;
    Info->StorageSize = static_cast<CKDWORD>(storage);
    Info->Width = Width;
    Info->Height = Height;
    Info->Depth = Depth;
    Info->NumMips = mipCount;
    Info->BitsPerPixel = 32;
    Info->CubeMap = CubeMap;
}

CKERROR CKRasterizerContext::RequestScreenShot(CKDWORD FrameBuffer,
                                                CKScreenShotCallback Callback,
                                                void *UserData)
{
    (void)UserData;
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    if (!Callback)
        return CKERR_INVALIDPARAMETER;
    if (FrameBuffer != 0 &&
        !CKNullFindResource(CKNullState(this), FrameBuffer,
                            CKRST_OBJ_FRAMEBUFFER))
        return CKERR_INVALIDPARAMETER;
    return CKERR_NOTIMPLEMENTED;
}

CKERROR CKRasterizerContext::CancelScreenShots(void *UserData)
{
    (void)UserData;
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    return CKERR_NOTFOUND;
}

CKERROR CKRasterizerContext::SetViewName(CKRenderView View, CKSTRING Name)
{
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    if (View >= CKRST_MAX_RENDER_VIEWS || !Name)
        return CKERR_INVALIDPARAMETER;
    CKNullState(this)->Views[View].Name = Name;
    return CK_OK;
}

CKERROR CKRasterizerContext::SetViewRect(CKRenderView View, const CKRECT &Rect)
{
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    if (View >= CKRST_MAX_RENDER_VIEWS || Rect.left < 0 || Rect.top < 0 ||
        Rect.right <= Rect.left || Rect.bottom <= Rect.top ||
        Rect.right > 0xffff || Rect.bottom > 0xffff)
        return CKERR_INVALIDPARAMETER;
    CKNullState(this)->Views[View].Rect = Rect;
    return CK_OK;
}

CKERROR CKRasterizerContext::SetViewScissor(CKRenderView View, const CKRECT *Rect)
{
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    if (View >= CKRST_MAX_RENDER_VIEWS)
        return CKERR_INVALIDPARAMETER;
    if (Rect && (Rect->left < 0 || Rect->top < 0 ||
                 Rect->right <= Rect->left || Rect->bottom <= Rect->top ||
                 Rect->right > 0xffff || Rect->bottom > 0xffff))
        return CKERR_INVALIDPARAMETER;
    CKNullViewRecord &view = CKNullState(this)->Views[View];
    view.ScissorEnabled = Rect ? TRUE : FALSE;
    if (Rect)
        view.Scissor = *Rect;
    return CK_OK;
}

CKERROR CKRasterizerContext::SetViewClear(CKRenderView View, CKDWORD Flags,
                                           CKDWORD Color, float Z, CKDWORD Stencil)
{
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    if (View >= CKRST_MAX_RENDER_VIEWS || Stencil > 0xff ||
        (Flags & ~(CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH |
                   CKRST_CTXCLEAR_STENCIL)) != 0 || !(Z >= 0.0f && Z <= 1.0f))
        return CKERR_INVALIDPARAMETER;
    CKNullViewRecord &view = CKNullState(this)->Views[View];
    view.ClearFlags = Flags;
    view.ClearColor = Color;
    view.ClearDepth = Z;
    view.ClearStencil = Stencil;
    return CK_OK;
}

CKERROR CKRasterizerContext::SetViewTransform(CKRenderView View,
                                               const VxMatrix *ViewMatrix,
                                               const VxMatrix *ProjMatrix)
{
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    if (View >= CKRST_MAX_RENDER_VIEWS)
        return CKERR_INVALIDPARAMETER;
    CKNullViewRecord &view = CKNullState(this)->Views[View];
    view.HasViewMatrix = ViewMatrix ? TRUE : FALSE;
    view.HasProjectionMatrix = ProjMatrix ? TRUE : FALSE;
    if (ViewMatrix)
        view.ViewMatrix = *ViewMatrix;
    if (ProjMatrix)
        view.ProjectionMatrix = *ProjMatrix;
    return CK_OK;
}

CKERROR CKRasterizerContext::SetViewFrameBuffer(CKRenderView View, CKDWORD FrameBuffer)
{
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    if (View >= CKRST_MAX_RENDER_VIEWS ||
        (FrameBuffer != 0 &&
         !CKNullFindResource(CKNullState(this), FrameBuffer, CKRST_OBJ_FRAMEBUFFER)))
        return CKERR_INVALIDPARAMETER;
    CKNullState(this)->Views[View].FrameBuffer = FrameBuffer;
    return CK_OK;
}

CKERROR CKRasterizerContext::SetViewMode(CKRenderView View, CK_VIEW_MODE Mode)
{
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    if (View >= CKRST_MAX_RENDER_VIEWS || Mode < CKRST_VIEWMODE_DEFAULT ||
        Mode > CKRST_VIEWMODE_DEPTH_DESC)
        return CKERR_INVALIDPARAMETER;
    CKNullState(this)->Views[View].Mode = Mode;
    return CK_OK;
}

CKERROR CKRasterizerContext::SetViewOrder(CKRenderView Start, CKWORD Count,
                                           const CKRenderView *Order)
{
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    if (Count == 0 || Start >= CKRST_MAX_RENDER_VIEWS ||
        (CKDWORD)Start + Count > CKRST_MAX_RENDER_VIEWS)
        return CKERR_INVALIDPARAMETER;
    if (Order) {
        for (CKWORD i = 0; i < Count; ++i) {
            if (Order[i] >= CKRST_MAX_RENDER_VIEWS)
                return CKERR_INVALIDPARAMETER;
        }
    }
    CKNullContextState *state = CKNullState(this);
    for (CKWORD i = 0; i < Count; ++i)
        state->ViewOrder[Start + i] = Order ? Order[i] : static_cast<CKRenderView>(Start + i);
    return CK_OK;
}

CKERROR CKRasterizerContext::ResetView(CKRenderView View)
{
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    if (View >= CKRST_MAX_RENDER_VIEWS)
        return CKERR_INVALIDPARAMETER;
    CKNullResetView(CKNullState(this)->Views[View]);
    return CK_OK;
}

CKERROR CKRasterizerContext::TouchView(CKRenderView View)
{
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    return View < CKRST_MAX_RENDER_VIEWS ? CK_OK : CKERR_INVALIDPARAMETER;
}

CKDWORD CKRasterizerContext::AllocTransform(VxMatrix *Transform, CKDWORD Count)
{
    if (!m_Created || !Transform || Count == 0 || Count > CKRST_MAX_TRANSFORMS)
        return CKRST_INVALID_TRANSFORM;
    CKNullContextState *state = CKNullState(this);
    VxMutexLock lock(state->TransformMutex);
    if ((CKDWORD)state->Transforms.Size() > CKRST_MAX_TRANSFORMS - Count)
        return CKRST_INVALID_TRANSFORM;
    const CKDWORD first = (CKDWORD)state->Transforms.Size();
    state->Transforms.Resize(state->Transforms.Size() + (int)Count);
    memcpy(&state->Transforms[(int)first], Transform, sizeof(VxMatrix) * Count);
    return first;
}

CKBOOL CKRasterizerContext::AllocTransientVertexBuffer(CKTransientVertexBuffer *, CKDWORD, CKDWORD)
{
    return FALSE;
}

CKBOOL CKRasterizerContext::AllocTransientIndexBuffer(CKTransientIndexBuffer *, CKDWORD, CKBOOL)
{
    return FALSE;
}

CKBOOL CKRasterizerContext::AllocTransientInstanceBuffer(CKTransientInstanceBuffer *, CKDWORD, CKDWORD)
{
    return FALSE;
}

CKDWORD CKRasterizerContext::GetAvailTransientVertexBuffer(CKDWORD, CKDWORD)
{
    return 0;
}

CKDWORD CKRasterizerContext::GetAvailTransientIndexBuffer(CKDWORD, CKBOOL)
{
    return 0;
}

CKDWORD CKRasterizerContext::GetAvailTransientInstanceBuffer(CKDWORD, CKDWORD)
{
    return 0;
}

CKRasterizerEncoder *CKRasterizerContext::BeginEncoder(CKBOOL ForceNewEncoder)
{
    if (!m_Created)
        return NULL;
    CKNullContextState *state = CKNullState(this);
    VxMutexLock lifecycleLock(state->EncoderLifecycleMutex);
    if (state->FrameInProgress || state->ShuttingDown)
        return NULL;
    const CKBOOL useDefault = !ForceNewEncoder && CKNullIsApiThread(this);
    CKNullRasterizerEncoder *encoder = useDefault
        ? state->DefaultEncoder : state->PoolEncoder;
    bool expected = false;
    if (!encoder->m_Active.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel, std::memory_order_relaxed))
        return NULL;
    encoder->Reset(this, state);
    return encoder;
}

CKERROR CKRasterizerContext::EndEncoder(CKRasterizerEncoder *Encoder)
{
    if (!Encoder)
        return CKERR_INVALIDPARAMETER;
    CKNullContextState *state = CKNullState(this);
    VxMutexLock lifecycleLock(state->EncoderLifecycleMutex);
    CKNullRasterizerEncoder *encoder = NULL;
    if (Encoder == state->DefaultEncoder)
        encoder = state->DefaultEncoder;
    else if (Encoder == state->PoolEncoder)
        encoder = state->PoolEncoder;
    if (!encoder || !encoder->m_Active.load(std::memory_order_acquire))
        return CKERR_INVALIDPARAMETER;
    if (encoder->m_OwnerThread != VxThread::GetCurrentVxThreadId()) {
        encoder->SetError(CKERR_INVALIDOPERATION);
        return encoder->GetStatus();
    }
    const CKERROR status = encoder->GetFrameStatus();
    encoder->m_Active.store(false, std::memory_order_release);
    return status;
}

CKERROR CKRasterizerContext::Frame(CKRST_FRAME_SYNC_MODE SyncMode,
                                   CKDWORD Flags, CKDWORD *FrameNumber)
{
    if (!m_Created || !CKNullIsApiThread(this))
        return CKERR_INVALIDOPERATION;
    if (SyncMode != CKRST_FRAME_SYNC_IMMEDIATE &&
        SyncMode != CKRST_FRAME_SYNC_VSYNC &&
        SyncMode != CKRST_FRAME_SYNC_PRESERVE_PRESENT)
        return CKERR_INVALIDPARAMETER;
    if ((Flags & ~(CKRST_FRAME_CAPTURE | CKRST_FRAME_DISCARD | CKRST_FRAME_FLUSH)) != 0)
        return CKERR_INVALIDPARAMETER;
    CKNullContextState *state = CKNullState(this);
    {
        VxMutexLock lifecycleLock(state->EncoderLifecycleMutex);
        if (state->FrameInProgress || state->ShuttingDown ||
            state->DefaultEncoder->m_Active.load(std::memory_order_acquire) ||
            state->PoolEncoder->m_Active.load(std::memory_order_acquire))
            return CKERR_INVALIDOPERATION;
        state->FrameInProgress = TRUE;
    }
    {
        VxMutexLock lock(state->SubmissionMutex);
        state->LastSubmissions.Swap(state->CurrentSubmissions);
        state->CurrentSubmissions.Resize(0);
    }
    {
        VxMutexLock lock(state->TransformMutex);
        state->Transforms.Resize(0);
    }
    ++m_NullFrameNumber;
    if (FrameNumber)
        *FrameNumber = m_NullFrameNumber;
    {
        VxMutexLock lifecycleLock(state->EncoderLifecycleMutex);
        state->FrameInProgress = FALSE;
    }
    return CK_OK;
}
