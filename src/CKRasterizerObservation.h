#ifndef CKRASTERIZEROBSERVATION_H
#define CKRASTERIZEROBSERVATION_H

#include "CKRasterizer.h"
#include "RCKRenderContext.h"

// The owner retains one adapter per backend until that backend is destroyed.
// Resource owners still use Identity() for their driver-owned handles.
class CKObservedRasterizerContext : public CKRasterizerContext {
public:
    CKObservedRasterizerContext(RCKRenderContext *owner, CKRasterizerContext *target)
        : CKRasterizerContext(nullptr), m_Next(Registry()), m_Owner(owner), m_Target(target),
          m_ActiveDepth(0), m_Retired(FALSE), m_MaterialKnown(FALSE), m_ViewportKnown(FALSE) {
        Registry() = this;
    }
    ~CKObservedRasterizerContext() override {
        CKObservedRasterizerContext **link = &Registry();
        while (*link && *link != this)
            link = &(*link)->m_Next;
        if (*link)
            *link = m_Next;
    }
    static CKObservedRasterizerContext *Enter(RCKRenderContext *owner, CKRasterizerContext *context) {
        CKRasterizerContext *target = Identity(context);
        CKObservedRasterizerContext *observed = Registry();
        while (observed && (observed->m_Retired || observed->m_Owner != owner || observed->m_Target != target))
            observed = observed->m_Next;
        if (!observed)
            observed = new CKObservedRasterizerContext(owner, target);
        if (observed->m_ActiveDepth++ == 0)
            observed->Invalidate();
        return observed;
    }
    static void Leave(CKRasterizerContext *context) {
        CKObservedRasterizerContext *observed = Find(context);
        if (observed && --observed->m_ActiveDepth == 0 && observed->m_Retired)
            delete observed;
    }
    static void Forget(RCKRenderContext *owner, CKRasterizerContext *context) {
        CKRasterizerContext *target = Identity(context);
        CKObservedRasterizerContext *observed = Registry();
        while (observed) {
            CKObservedRasterizerContext *next = observed->m_Next;
            if (observed->m_Owner == owner && (!context || observed->m_Target == target)) {
                if (observed->m_ActiveDepth)
                    observed->m_Retired = TRUE;
                else
                    delete observed;
            }
            observed = next;
        }
    }
    static void Invalidate(RCKRenderContext *owner, CKRasterizerContext *context) {
        CKRasterizerContext *target = Identity(context);
        for (CKObservedRasterizerContext *observed = Registry(); observed; observed = observed->m_Next) {
            if (observed->m_Owner == owner && observed->m_Target == target)
                observed->Invalidate();
        }
    }

    static CKBOOL IsObserved(CKRasterizerContext *context) {
        return Find(context) != nullptr;
    }
    static CKBOOL IsLive(CKRasterizerContext *context) {
        CKObservedRasterizerContext *observed = Find(context);
        return observed && !observed->m_Retired;
    }
    static CKRasterizerContext *Identity(CKRasterizerContext *context) {
        CKObservedRasterizerContext *observed = Find(context);
        while (observed) {
            context = observed->m_Target;
            observed = Find(context);
        }
        return context;
    }
    static CKBOOL ReadMaterial(CKRasterizerContext *context, CKMaterialData &material) {
        CKObservedRasterizerContext *observed = Find(context);
        while (observed) {
            if (observed->m_MaterialKnown) {
                material = observed->m_Material;
                return TRUE;
            }
            observed = Find(observed->m_Target);
        }
        return FALSE;
    }
    static CKBOOL ReadViewport(CKRasterizerContext *context, CKViewportData &viewport) {
        CKObservedRasterizerContext *observed = Find(context);
        while (observed) {
            if (observed->m_ViewportKnown) {
                viewport = observed->m_Viewport;
                return TRUE;
            }
            observed = Find(observed->m_Target);
        }
        return FALSE;
    }

    CKBOOL Create(WIN_HANDLE Window, int PosX, int PosY, int Width, int Height, int Bpp, CKBOOL Fullscreen, int RefreshRate, int Zbpp, int StencilBpp) override {
        return m_Target->Create(Window, PosX, PosY, Width, Height, Bpp, Fullscreen, RefreshRate, Zbpp, StencilBpp);
    }

    CKBOOL Resize(int PosX, int PosY, int Width, int Height, CKDWORD Flags) override {
        return m_Target->Resize(PosX, PosY, Width, Height, Flags);
    }

    CKBOOL SetOptions(const CKRasterizerOptions *Options) override {
        return m_Target->SetOptions(Options);
    }

    CKBOOL GetDesc(CKRasterizerContextDesc *Desc) const override {
        return m_Target->GetDesc(Desc);
    }

    CKBOOL GetCaps(CKRasterizerCapsDesc *Caps) const override {
        return m_Target->GetCaps(Caps);
    }

    CKERROR GetDeviceStatus() const override {
        return m_Target->GetDeviceStatus();
    }

    CKBOOL BeginShutdown() override {
        return m_Target->BeginShutdown();
    }

    CKBOOL IsIdle() const override {
        return m_Target->IsIdle();
    }

    CKBOOL Clear(CKDWORD Flags, CKDWORD Color, float Z, CKDWORD Stencil, int RectCount, CKRECT *Rects) override {
        return m_Target->Clear(Flags, Color, Z, Stencil, RectCount, Rects);
    }

    CKBOOL BeginScene() override {
        return m_Target->BeginScene();
    }

    CKBOOL EndScene() override {
        return m_Target->EndScene();
    }

    CKBOOL BeginOverlayPhase() override {
        return m_Target->BeginOverlayPhase();
    }

    CKBOOL BackToFront(CKBOOL VSync) override {
        return m_Target->BackToFront(VSync);
    }

    CKBOOL SetRenderState(VXRENDERSTATETYPE State, CKDWORD Value) override {
        return m_Target->SetRenderState(State, Value);
    }

    CKBOOL GetRenderState(VXRENDERSTATETYPE State, CKDWORD *Value) override {
        return m_Target->GetRenderState(State, Value);
    }

    CKBOOL SetTextureStageState(int Stage, CKRST_TEXTURESTAGESTATETYPE Tss, CKDWORD Value) override {
        return m_Target->SetTextureStageState(Stage, Tss, Value);
    }

    CKBOOL GetTextureStageState(int Stage, CKRST_TEXTURESTAGESTATETYPE Tss, CKDWORD *Value) override {
        return m_Target->GetTextureStageState(Stage, Tss, Value);
    }

    CKBOOL ResetTextureStages(int FirstStage, int StageCount) override {
        return m_Target->ResetTextureStages(FirstStage, StageCount);
    }

    CKBOOL SetTexture(CKDWORD Texture, int Stage) override {
        return m_Target->SetTexture(Texture, Stage);
    }

    CKBOOL GetTexture(int Stage, CKDWORD *Texture) override {
        return m_Target->GetTexture(Stage, Texture);
    }

    CKBOOL SetTransformMatrix(VXMATRIX_TYPE Type, const VxMatrix &Mat) override {
        return m_Target->SetTransformMatrix(Type, Mat);
    }

    CKBOOL GetTransformMatrix(VXMATRIX_TYPE Type, VxMatrix &Mat) override {
        return m_Target->GetTransformMatrix(Type, Mat);
    }

    CKBOOL SetLight(CKDWORD Index, const CKLightData *Data) override {
        return m_Target->SetLight(Index, Data);
    }

    CKBOOL EnableLight(CKDWORD Index, CKBOOL Enable) override {
        return m_Target->EnableLight(Index, Enable);
    }

    CKBOOL SetMaterial(const CKMaterialData *Data) override {
        const CKBOOL result = m_Target->SetMaterial(Data);
        if (result && Data) {
            m_Material = *Data;
            m_MaterialKnown = TRUE;
        }
        return result;
    }

    CKBOOL ApplyMaterial(const CKMaterialRenderState &State) override {
        const CKBOOL result = m_Target->ApplyMaterial(State);
        if (result) {
            m_Material = State.Material;
            m_MaterialKnown = TRUE;
        }
        return result;
    }

    CKBOOL SetViewport(const CKViewportData *Data) override {
        const CKBOOL result = m_Target->SetViewport(Data);
        if (result && Data) {
            m_Viewport = *Data;
            m_ViewportKnown = TRUE;
        }
        return result;
    }

    CKBOOL SetUserClipPlane(CKDWORD Index, const VxPlane &Plane) override {
        return m_Target->SetUserClipPlane(Index, Plane);
    }

    CKBOOL GetUserClipPlane(CKDWORD Index, VxPlane &Plane) override {
        return m_Target->GetUserClipPlane(Index, Plane);
    }

    void InitDefaultRenderStatesValue() override {
        m_Target->InitDefaultRenderStatesValue();
    }

    CKBOOL DrawPrimitive(VXPRIMITIVETYPE Type, CKWORD *Indices, int IndexCount, VxDrawPrimitiveData *Data) override {
        return m_Target->DrawPrimitive(Type, Indices, IndexCount, Data);
    }

    CKBOOL DrawPrimitiveVB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD StartVertex, CKDWORD VertexCount, CKWORD *Indices, int IndexCount) override {
        return m_Target->DrawPrimitiveVB(Type, VB, StartVertex, VertexCount, Indices, IndexCount);
    }

    CKBOOL DrawPrimitiveVBIB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD IB, CKDWORD MinVertexIndex, CKDWORD VertexCount, CKDWORD StartIndex, int IndexCount) override {
        return m_Target->DrawPrimitiveVBIB(Type, VB, IB, MinVertexIndex, VertexCount, StartIndex, IndexCount);
    }

    CKBOOL CreateTexture(const CKTextureDesc *Desc, CKDWORD *OutHandle) override {
        return m_Target->CreateTexture(Desc, OutHandle);
    }

    CKBOOL ImportTexture(const CKTextureDesc *Desc, const CKTextureImportDesc *Import,
                         CKDWORD *OutTexture) override {
        return m_Target->ImportTexture(Desc, Import, OutTexture);
    }

    CKBOOL LoadTexture(CKDWORD Texture, const VxImageDescEx &Image, int MipLevel, CKRST_CUBEFACE Face, const CKRECT *Region) override {
        return m_Target->LoadTexture(Texture, Image, MipLevel, Face, Region);
    }

    CKBOOL GetTextureDesc(CKDWORD Texture, CKTextureDesc *Desc) const override {
        return m_Target->GetTextureDesc(Texture, Desc);
    }

    CKBOOL CreateVertexBuffer(const CKVertexBufferDesc *Desc, const void *Data, CKDWORD *OutHandle) override {
        return m_Target->CreateVertexBuffer(Desc, Data, OutHandle);
    }

    CKBOOL GetVertexBufferDesc(CKDWORD VB, CKVertexBufferDesc *Desc) const override {
        return m_Target->GetVertexBufferDesc(VB, Desc);
    }

    CKBOOL CreateIndexBuffer(const CKIndexBufferDesc *Desc, const void *Data, CKDWORD *OutHandle) override {
        return m_Target->CreateIndexBuffer(Desc, Data, OutHandle);
    }

    CKBOOL GetIndexBufferDesc(CKDWORD IB, CKIndexBufferDesc *Desc) const override {
        return m_Target->GetIndexBufferDesc(IB, Desc);
    }

    void * LockVertexBuffer(CKDWORD VB, CKDWORD StartVertex, CKDWORD VertexCount, CKRST_LOCKFLAGS Flags) override {
        return m_Target->LockVertexBuffer(VB, StartVertex, VertexCount, Flags);
    }

    CKBOOL UnlockVertexBuffer(CKDWORD VB) override {
        return m_Target->UnlockVertexBuffer(VB);
    }

    void * LockIndexBuffer(CKDWORD IB, CKDWORD StartIndex, CKDWORD IndexCount, CKRST_LOCKFLAGS Flags) override {
        return m_Target->LockIndexBuffer(IB, StartIndex, IndexCount, Flags);
    }

    CKBOOL UnlockIndexBuffer(CKDWORD IB) override {
        return m_Target->UnlockIndexBuffer(IB);
    }

    CKBOOL DeleteObject(CKRST_HANDLE Handle, CKRST_OBJECTTYPE Type) override {
        return m_Target->DeleteObject(Handle, Type);
    }

    CKBOOL FlushObjects(CKRST_OBJECTMASK TypeMask) override {
        return m_Target->FlushObjects(TypeMask);
    }

    CKBOOL SetResourceName(CKRST_HANDLE Handle, CKRST_OBJECTTYPE Type, CKSTRING Name) override {
        return m_Target->SetResourceName(Handle, Type, Name);
    }

    CKBOOL SetTargetTexture(CKDWORD Texture, int Width, int Height, CKRST_CUBEFACE Face) override {
        return m_Target->SetTargetTexture(Texture, Width, Height, Face);
    }

    CKBOOL SetBackBufferTexture(CKDWORD Texture) override {
        return m_Target->SetBackBufferTexture(Texture);
    }

    CKBOOL CopyToTexture(CKDWORD Texture, const VxRect *Src, const VxRect *Dst, CKRST_CUBEFACE Face) override {
        return m_Target->CopyToTexture(Texture, Src, Dst, Face);
    }

    int CopyToMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, VxImageDescEx &Image) override {
        return m_Target->CopyToMemoryBuffer(Rect, Buffer, Image);
    }

    int CopyFromMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, const VxImageDescEx &Image) override {
        return m_Target->CopyFromMemoryBuffer(Rect, Buffer, Image);
    }

    CKBOOL RequestReadback(const CKRECT *Rect, VXBUFFER_TYPE Buffer, CKReadbackCallback Callback, void *User) override {
        return m_Target->RequestReadback(Rect, Buffer, Callback, User);
    }

    void SetDebugMarker(CKSTRING Name) override {
        m_Target->SetDebugMarker(Name);
    }

    void GetStats(CKRenderStats &Stats) const override {
        m_Target->GetStats(Stats);
    }

private:
    CKObservedRasterizerContext(const CKObservedRasterizerContext &) = delete;
    CKObservedRasterizerContext &operator=(const CKObservedRasterizerContext &) = delete;
    void Invalidate() {
        m_MaterialKnown = FALSE;
        m_ViewportKnown = FALSE;
    }
    static CKObservedRasterizerContext *&Registry() {
        static thread_local CKObservedRasterizerContext *registry = nullptr;
        return registry;
    }
    static CKObservedRasterizerContext *Find(CKRasterizerContext *context) {
        for (CKObservedRasterizerContext *observed = Registry(); observed;
             observed = observed->m_Next) {
            if (observed == context)
                return observed;
        }
        return nullptr;
    }
    CKObservedRasterizerContext *m_Next;
    RCKRenderContext *m_Owner;
    CKRasterizerContext *m_Target;
    CKDWORD m_ActiveDepth;
    CKBOOL m_Retired;
    CKMaterialData m_Material;
    CKViewportData m_Viewport;
    CKBOOL m_MaterialKnown;
    CKBOOL m_ViewportKnown;
};

class CKRasterizerObservation {
public:
    explicit CKRasterizerObservation(RCKRenderContext *context)
        : m_Context(context), m_Previous(context ? context->m_RasterizerContext : nullptr),
          m_Observed(nullptr) {
        if (m_Previous) {
            m_Observed = CKObservedRasterizerContext::Enter(context, m_Previous);
            m_Context->m_RasterizerContext = m_Observed;
        }
    }
    ~CKRasterizerObservation() {
        if (m_Observed) {
            if (m_Context->m_RasterizerContext == m_Observed &&
                CKObservedRasterizerContext::IsLive(m_Observed))
                m_Context->m_RasterizerContext = m_Previous;
            CKObservedRasterizerContext::Leave(m_Observed);
        }
    }
    static void Forget(RCKRenderContext *owner, CKRasterizerContext *context) {
        CKObservedRasterizerContext::Forget(owner, context);
    }
    static void ForgetOwner(RCKRenderContext *owner) {
        CKObservedRasterizerContext::Forget(owner, nullptr);
    }
    static void Invalidate(RCKRenderContext *owner, CKRasterizerContext *context) {
        CKObservedRasterizerContext::Invalidate(owner, context);
    }
    static CKRasterizerContext *Identity(CKRasterizerContext *context) {
        return CKObservedRasterizerContext::Identity(context);
    }
    static CKBOOL ReadMaterial(CKRasterizerContext *context, CKMaterialData &material) {
        return CKObservedRasterizerContext::ReadMaterial(context, material);
    }
    static CKBOOL ReadViewport(CKRasterizerContext *context, CKViewportData &viewport) {
        return CKObservedRasterizerContext::ReadViewport(context, viewport);
    }
private:
    CKRasterizerObservation(const CKRasterizerObservation &) = delete;
    CKRasterizerObservation &operator=(const CKRasterizerObservation &) = delete;
    RCKRenderContext *m_Context;
    CKRasterizerContext *m_Previous;
    CKObservedRasterizerContext *m_Observed;
};

// Also marks standalone material and mesh callbacks as independent entries for
// nested context restoration, even before a DefaultRender scope exists.
class CKRasterizerCallbackScope {
public:
    explicit CKRasterizerCallbackScope(RCKRenderContext *context)
        : m_Observation(context), m_Previous(Current()), m_Context(context) {
        Current() = this;
    }
    ~CKRasterizerCallbackScope() { Current() = m_Previous; }
    static CKBOOL InCallback(RCKRenderContext *context) {
        for (CKRasterizerCallbackScope *scope = Current(); scope;
             scope = scope->m_Previous) {
            if (scope->m_Context == context)
                return TRUE;
        }
        return FALSE;
    }
private:
    CKRasterizerCallbackScope(const CKRasterizerCallbackScope &) = delete;
    CKRasterizerCallbackScope &operator=(const CKRasterizerCallbackScope &) = delete;
    static CKRasterizerCallbackScope *&Current() {
        static thread_local CKRasterizerCallbackScope *current = nullptr;
        return current;
    }
    CKRasterizerObservation m_Observation;
    CKRasterizerCallbackScope *m_Previous;
    RCKRenderContext *m_Context;
};

#endif
