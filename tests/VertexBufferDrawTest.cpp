#include "TextureTestObserver.h"
#include "RCKVertexBuffer.h"

namespace {
// Records the engine's actual public rasterizer submission and consumes the
// addressed vertices. No renderer or fixed-function implementation runs here.
class VertexDrawObserver : public TextureTestObserver {
public:
    CKVertexBufferDesc Desc;
    CKBYTE Bytes[8192] = {};
    CKDWORD Handle = 0;
    int Creates = 0, Locks = 0, HardwareDraws = 0, SoftwareDraws = 0;
    CKBOOL FailUpload = FALSE, DrawResult = TRUE;
    CKDWORD Start = 0, Count = 0, Flags = 0, Lighting = 0;
    int IndexCount = 0;
    VXPRIMITIVETYPE Type = VX_POINTLIST;
    CKViewportData DrawViewport = {};
    VxVector Position[12], Normal[12], Tween[12], TweenNormal[12];
    float W[12][5] = {};
    CKDWORD Palette[12] = {}, Diffuse[12] = {}, Specular[12] = {};
    Vx2DVector UV[12][CKRST_MAX_STAGES];
    float Rhw[12] = {};

    CKBOOL CreateVertexBuffer(const CKVertexBufferDesc *desc, const void *, CKDWORD *handle) override {
        ++Creates; Desc = *desc; Handle = 301; *handle = Handle; return TRUE;
    }
    CKBOOL GetVertexBufferDesc(CKDWORD handle, CKVertexBufferDesc *desc) const override {
        if (handle != Handle || !handle) return FALSE;
        *desc = Desc; return TRUE;
    }
    void *LockVertexBuffer(CKDWORD handle, CKDWORD start, CKDWORD count, CKRST_LOCKFLAGS) override {
        ++Locks;
        if (FailUpload) return nullptr;
        EXPECT_EQ(Handle, handle);
        EXPECT_LE((start + count) * Desc.m_VertexSize, sizeof(Bytes));
        if (handle != Handle || (start + count) * Desc.m_VertexSize > sizeof(Bytes)) return nullptr;
        return Bytes + start * Desc.m_VertexSize;
    }
    CKBOOL UnlockVertexBuffer(CKDWORD handle) override { return handle == Handle; }
    CKBOOL DeleteObject(CKRST_HANDLE handle, CKRST_OBJECTTYPE type) override {
        EXPECT_EQ(CKRST_OBJ_VERTEXBUFFER, type); EXPECT_EQ(Handle, handle);
        Handle = 0; return TRUE;
    }
    void Begin(VXPRIMITIVETYPE type, CKDWORD start, CKDWORD vertices, int indices, CKDWORD flags) {
        Type = type; Start = start; Count = vertices; IndexCount = indices; Flags = flags;
        Lighting = States[VXRENDERSTATE_LIGHTING]; DrawViewport = Viewport;
    }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        ++SoftwareDraws;
        if (!data) return FALSE;
        Begin(type, 0, data->VertexCount, count, data->Flags);
        EXPECT_LE(count, 12);
        if (count < 0 || count > 12) return FALSE;
        for (int i = 0; i < count; ++i) {
            const int vertex = indices ? indices[i] : i;
            EXPECT_LT(vertex, data->VertexCount);
            if (vertex >= data->VertexCount) return FALSE;
            const CKBYTE *p = static_cast<CKBYTE *>(data->PositionPtr) + vertex * data->PositionStride;
            memcpy(&Position[i], p, 12);
            if (!(data->Flags & CKRST_DP_TRANSFORM)) memcpy(&Rhw[i], p + 12, 4);
            if (data->NormalPtr) memcpy(&Normal[i], static_cast<CKBYTE *>(data->NormalPtr) + vertex * data->NormalStride, 12);
            if (data->TweenPositionPtr) memcpy(&Tween[i], static_cast<CKBYTE *>(data->TweenPositionPtr) + vertex * data->TweenPositionStride, 12);
            if (data->TweenNormalPtr) memcpy(&TweenNormal[i], static_cast<CKBYTE *>(data->TweenNormalPtr) + vertex * data->TweenNormalStride, 12);
            if (data->ColorPtr) memcpy(&Diffuse[i], static_cast<CKBYTE *>(data->ColorPtr) + vertex * data->ColorStride, 4);
            if (data->SpecularColorPtr) memcpy(&Specular[i], static_cast<CKBYTE *>(data->SpecularColorPtr) + vertex * data->SpecularColorStride, 4);
            for (int stage = 0; stage < CKRST_MAX_STAGES; ++stage) {
                void *uv = stage == 0 ? data->TexCoordPtr : data->TexCoordPtrs[stage - 1];
                const CKDWORD stride = stage == 0 ? data->TexCoordStride : data->TexCoordStrides[stage - 1];
                if (uv) memcpy(&UV[i][stage], static_cast<CKBYTE *>(uv) + vertex * stride, 8);
            }
        }
        return DrawResult;
    }
    CKBOOL DrawPrimitiveVB(VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD start, CKDWORD vertices, CKWORD *indices, int count) override {
        ++HardwareDraws; EXPECT_EQ(Handle, vb);
        Begin(type, start, vertices, count, Desc.m_VertexFormat);
        CKRSTVertexLayout layout; CKRSTGetVertexLayout(Desc.m_VertexFormat, nullptr, &layout);
        const int consumed = indices ? count : static_cast<int>(vertices);
        EXPECT_LE(consumed, 12);
        if (consumed < 0 || consumed > 12) return FALSE;
        for (int i = 0; i < consumed; ++i) {
            const CKDWORD vertex = start + (indices ? indices[i] : i);
            EXPECT_LT(vertex, Desc.m_MaxVertexCount);
            if (vertex >= Desc.m_MaxVertexCount) return FALSE;
            const CKBYTE *p = Bytes + vertex * layout.Stride;
            memcpy(&Position[i], p + layout.PositionOffset, 12);
            if (layout.PositionComponents == 4) memcpy(&Rhw[i], p + 12, 4);
            if (layout.NormalOffset >= 0) memcpy(&Normal[i], p + layout.NormalOffset, 12);
            if (layout.TweenPositionOffset >= 0) memcpy(&Tween[i], p + layout.TweenPositionOffset, 12);
            if (layout.TweenNormalOffset >= 0) memcpy(&TweenNormal[i], p + layout.TweenNormalOffset, 12);
            if (layout.WeightOffset >= 0) memcpy(W[i], p + layout.WeightOffset, layout.WeightCount * 4);
            if (layout.BlendIndexOffset >= 0) memcpy(&Palette[i], p + layout.BlendIndexOffset, 4);
            if (layout.DiffuseOffset >= 0) memcpy(&Diffuse[i], p + layout.DiffuseOffset, 4);
            if (layout.SpecularOffset >= 0) memcpy(&Specular[i], p + layout.SpecularOffset, 4);
            for (int stage = 0; stage < layout.TexcoordCount; ++stage) memcpy(&UV[i][stage], p + layout.TexcoordOffset[stage], 8);
        }
        return DrawResult;
    }
};

class VertexBufferDrawTest : public TextureTestWorld {
protected:
    VertexDrawObserver Target;
    void SetUp() override {
        TextureTestWorld::SetUp();
        Device->DetachAll(); Device->m_RasterizerContext = &Target;
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&Target, 0));
    }
    void Fill(RCKVertexBuffer &buffer, CKRST_DPFLAGS flags, int count = 6) {
        ASSERT_EQ(CK_VB_LOST, buffer.Check(Device, count, flags, FALSE));
        VxDrawPrimitiveData *data = buffer.Lock(Device, 0, count, CK_LOCK_DEFAULT);
        ASSERT_NE(nullptr, data);
        for (int i = 0; i < count; ++i) {
            VxVector p(10.0f + i, 20.0f + i, 30.0f + i);
            memcpy(static_cast<CKBYTE *>(data->PositionPtr) + i * data->PositionStride, &p, 12);
            if (!(flags & CKRST_DP_TRANSFORM)) {
                float rhw = .5f + i; memcpy(static_cast<CKBYTE *>(data->PositionPtr) + i * data->PositionStride + 12, &rhw, 4);
            }
            if (flags & CKRST_DP_WEIGHTMASK) {
                const float weights[3] = {.125f + i, .25f + i, .5f + i};
                memcpy(static_cast<CKBYTE *>(data->PositionPtr) + i * data->PositionStride + 12, weights, 12);
                if (flags & CKRST_DP_MATRIXPAL) {
                    const CKDWORD palette = 0x03020100u + i;
                    memcpy(static_cast<CKBYTE *>(data->PositionPtr) + i * data->PositionStride + 24, &palette, 4);
                }
            }
            if (data->NormalPtr) { VxVector n(1.0f + i, 2.0f, 3.0f); memcpy(static_cast<CKBYTE *>(data->NormalPtr) + i * data->NormalStride, &n, 12); }
            if (data->TweenPositionPtr) { VxVector t(40.0f + i, 50.0f, 60.0f); memcpy(static_cast<CKBYTE *>(data->TweenPositionPtr) + i * data->TweenPositionStride, &t, 12); }
            if (data->TweenNormalPtr) { VxVector n(4.0f + i, 5.0f, 6.0f); memcpy(static_cast<CKBYTE *>(data->TweenNormalPtr) + i * data->TweenNormalStride, &n, 12); }
            if (data->ColorPtr) { CKDWORD c = 0xFF123400u + i; memcpy(static_cast<CKBYTE *>(data->ColorPtr) + i * data->ColorStride, &c, 4); }
            if (data->SpecularColorPtr) { CKDWORD c = 0xFF567800u + i; memcpy(static_cast<CKBYTE *>(data->SpecularColorPtr) + i * data->SpecularColorStride, &c, 4); }
            for (int stage = 0; stage < CKRST_MAX_STAGES; ++stage) {
                void *uv = stage == 0 ? data->TexCoordPtr : data->TexCoordPtrs[stage - 1];
                const CKDWORD stride = stage == 0 ? data->TexCoordStride : data->TexCoordStrides[stage - 1];
                if (uv) { Vx2DVector t(.125f * i, .0625f * stage); memcpy(static_cast<CKBYTE *>(uv) + i * stride, &t, 8); }
            }
        }
        buffer.Unlock(Device);
    }
    void Points(int a, int b, int c) {
        EXPECT_FLOAT_EQ(10.0f + a, Target.Position[0].x);
        EXPECT_FLOAT_EQ(20.0f + b, Target.Position[1].y);
        EXPECT_FLOAT_EQ(30.0f + c, Target.Position[2].z);
    }
};
}

TEST_F(VertexBufferDrawTest, NonindexedSubrangeConsumesResidentVerticesWithoutAnotherUpload) {
    RCKVertexBuffer buffer(Context); Fill(buffer, CKRST_DP_TR_V);
    ASSERT_TRUE(buffer.Draw(Device, VX_TRIANGLELIST, nullptr, 999, 2, 3));
    EXPECT_EQ(1, Target.HardwareDraws); EXPECT_EQ(0, Target.SoftwareDraws);
    EXPECT_EQ(2u, Target.Start); EXPECT_EQ(3u, Target.Count); EXPECT_EQ(1, Target.Locks); Points(2, 3, 4);
}

TEST_F(VertexBufferDrawTest, IndexedVerticesAreRelativeToStartAcrossAllStreams) {
    RCKVertexBuffer buffer(Context);
    Fill(buffer, static_cast<CKRST_DPFLAGS>(CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_TWEEN | CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR | CKRST_DP_STAGES7));
    CKWORD order[] = {2, 0, 1};
    ASSERT_TRUE(buffer.Draw(Device, VX_TRIANGLELIST, order, 3, 2, 3));
    EXPECT_EQ(1, Target.SoftwareDraws); EXPECT_EQ(0, Target.HardwareDraws); Points(4, 2, 3);
    EXPECT_FLOAT_EQ(5.0f, Target.Normal[0].x); EXPECT_FLOAT_EQ(44.0f, Target.Tween[0].x);
    EXPECT_FLOAT_EQ(8.0f, Target.TweenNormal[0].x);
    EXPECT_EQ(0xFF123404u, Target.Diffuse[0]); EXPECT_EQ(0xFF567802u, Target.Specular[1]);
    EXPECT_FLOAT_EQ(.5f, Target.UV[0][0].x); EXPECT_FLOAT_EQ(.4375f, Target.UV[2][7].y);
}

TEST_F(VertexBufferDrawTest, OverlongNonindexedSubrangeClipsToWrittenVertexCount) {
    RCKVertexBuffer buffer(Context); Fill(buffer, CKRST_DP_TR_V);
    ASSERT_TRUE(buffer.Draw(Device, VX_LINELIST, nullptr, 0, 4, 0xFFFFFFFFu));
    EXPECT_EQ(2u, Target.Count); EXPECT_FLOAT_EQ(14.0f, Target.Position[0].x); EXPECT_FLOAT_EQ(15.0f, Target.Position[1].x);
}

TEST_F(VertexBufferDrawTest, PointListUsesSoftwareSubmissionAndUpdatesContextStatistics) {
    RCKVertexBuffer buffer(Context); Fill(buffer, CKRST_DP_TR_V);
    ASSERT_TRUE(buffer.Draw(Device, VX_POINTLIST, nullptr, 0, 1, 3));
    EXPECT_EQ(1, Target.SoftwareDraws); EXPECT_EQ(0, Target.HardwareDraws); Points(1, 2, 3);
    EXPECT_EQ(3, Device->m_Stats.NbPointsDrawn); EXPECT_EQ(3, Device->m_Stats.NbVerticesProcessed);
}

TEST_F(VertexBufferDrawTest, WrapOnEveryTextureStageUsesSoftwareWithoutChangingCoordinates) {
    RCKVertexBuffer buffer(Context); Fill(buffer, static_cast<CKRST_DPFLAGS>(CKRST_DP_TRANSFORM | CKRST_DP_STAGES7));
    for (int stage = 0; stage < CKRST_MAX_STAGES; ++stage) {
        Target.States[VXRENDERSTATE_WRAP0 + stage] = VXWRAP_U;
        ASSERT_TRUE(buffer.Draw(Device, VX_TRIANGLELIST, nullptr, 0, 1, 3));
        Points(1, 2, 3); EXPECT_FLOAT_EQ(.125f, Target.UV[0][stage].x);
        Target.States[VXRENDERSTATE_WRAP0 + stage] = 0;
    }
    EXPECT_EQ(8, Target.SoftwareDraws); EXPECT_EQ(0, Target.HardwareDraws);
}

TEST_F(VertexBufferDrawTest, WrapBitsOutsideCoordinateMaskDoNotDisableHardwareSubmission) {
    RCKVertexBuffer buffer(Context); Fill(buffer, CKRST_DP_TR_V);
    Target.States[VXRENDERSTATE_WRAP0 + 7] = 0x80000000u;
    ASSERT_TRUE(buffer.Draw(Device, VX_TRIANGLELIST, nullptr, 0, 0, 3));
    EXPECT_EQ(1, Target.HardwareDraws); EXPECT_EQ(0, Target.SoftwareDraws); Points(0, 1, 2);
}

TEST_F(VertexBufferDrawTest, IndexedBlendStateChoosesSoftwareAndPreservesCallerState) {
    RCKVertexBuffer buffer(Context); Fill(buffer, CKRST_DP_TR_V);
    Target.States[VXRENDERSTATE_INDEXVBLENDENABLE] = 1;
    ASSERT_TRUE(buffer.Draw(Device, VX_TRIANGLELIST, nullptr, 0, 1, 3));
    EXPECT_EQ(1, Target.SoftwareDraws); EXPECT_EQ(1u, Target.States[VXRENDERSTATE_INDEXVBLENDENABLE]); Points(1, 2, 3);
}

TEST_F(VertexBufferDrawTest, HardwarePayloadPreservesWeightedPaletteAndTweenStreams) {
    RCKVertexBuffer buffer(Context);
    Fill(buffer, static_cast<CKRST_DPFLAGS>(CKRST_DP_TRANSFORM | CKRST_DP_WEIGHTS3 | CKRST_DP_MATRIXPAL | CKRST_DP_LIGHT | CKRST_DP_TWEEN | CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR | CKRST_DP_STAGES1));
    ASSERT_TRUE(buffer.Draw(Device, VX_TRIANGLELIST, nullptr, 0, 2, 3));
    EXPECT_EQ(1, Target.HardwareDraws); Points(2, 3, 4);
    EXPECT_FLOAT_EQ(2.125f, Target.W[0][0]); EXPECT_FLOAT_EQ(3.5f, Target.W[1][2]);
    EXPECT_EQ(0x03020104u, Target.Palette[2]); EXPECT_FLOAT_EQ(3.0f, Target.Normal[0].x);
    EXPECT_FLOAT_EQ(44.0f, Target.Tween[2].x); EXPECT_FLOAT_EQ(7.0f, Target.TweenNormal[1].x);
    EXPECT_EQ(0xFF123402u, Target.Diffuse[0]); EXPECT_EQ(0xFF567804u, Target.Specular[2]);
    EXPECT_FLOAT_EQ(.375f, Target.UV[1][1].x);
}

TEST_F(VertexBufferDrawTest, HardwarePretransformedPositionsPreserveRhw) {
    RCKVertexBuffer buffer(Context); Fill(buffer, CKRST_DP_VC);
    ASSERT_TRUE(buffer.Draw(Device, VX_TRIANGLELIST, nullptr, 0, 2, 3));
    EXPECT_EQ(1, Target.HardwareDraws); Points(2, 3, 4); EXPECT_FLOAT_EQ(2.5f, Target.Rhw[0]);
}

TEST_F(VertexBufferDrawTest, FailedUploadFallsBackToCurrentCpuPayloadAndPropagatesDrawFailure) {
    RCKVertexBuffer buffer(Context); Target.FailUpload = TRUE; Fill(buffer, CKRST_DP_TR_V);
    ASSERT_TRUE(buffer.Draw(Device, VX_TRIANGLELIST, nullptr, 0, 2, 3));
    EXPECT_EQ(1, Target.SoftwareDraws); EXPECT_EQ(0, Target.HardwareDraws); Points(2, 3, 4);
    Target.DrawResult = FALSE;
    EXPECT_FALSE(buffer.Draw(Device, VX_TRIANGLELIST, nullptr, 0, 1, 3));
    EXPECT_EQ(2, Target.SoftwareDraws); Points(1, 2, 3);
}

TEST_F(VertexBufferDrawTest, HardwareSubmissionFailureIsReturnedWithoutDuplicateSoftwareDraw) {
    RCKVertexBuffer buffer(Context); Fill(buffer, CKRST_DP_TR_V); Target.DrawResult = FALSE;
    EXPECT_FALSE(buffer.Draw(Device, VX_TRIANGLELIST, nullptr, 0, 0, 3));
    EXPECT_EQ(1, Target.HardwareDraws); EXPECT_EQ(0, Target.SoftwareDraws); Points(0, 1, 2);
}

TEST_F(VertexBufferDrawTest, LockedBufferCannotSubmitUntilUnlockPublishesItsChanges) {
    RCKVertexBuffer buffer(Context); Fill(buffer, CKRST_DP_TR_V);
    VxDrawPrimitiveData *data = buffer.Lock(Device, 0, 3, CK_LOCK_DEFAULT); ASSERT_NE(nullptr, data);
    VxVector changed(71.0f, 72.0f, 73.0f); memcpy(data->PositionPtr, &changed, 12);
    EXPECT_FALSE(buffer.Draw(Device, VX_TRIANGLELIST, nullptr, 0, 0, 3));
    CKWORD indices[] = {0, 1, 2};
    EXPECT_FALSE(buffer.Draw(Device, VX_TRIANGLELIST, indices, 3, 0, 3));
    EXPECT_EQ(0, Target.HardwareDraws); EXPECT_EQ(0, Target.SoftwareDraws);
    buffer.Unlock(Device);
    ASSERT_TRUE(buffer.Draw(Device, VX_TRIANGLELIST, nullptr, 0, 0, 3));
    EXPECT_FLOAT_EQ(71.0f, Target.Position[0].x);
}

TEST_F(VertexBufferDrawTest, InvalidOrEmptyRangesDoNotSubmit) {
    RCKVertexBuffer buffer(Context); Fill(buffer, CKRST_DP_TR_V);
    EXPECT_FALSE(buffer.Draw(nullptr, VX_TRIANGLELIST, nullptr, 0, 0, 3));
    EXPECT_FALSE(buffer.Draw(Device, VX_TRIANGLELIST, nullptr, 0, 0, 0));
    EXPECT_FALSE(buffer.Draw(Device, VX_TRIANGLELIST, nullptr, 0, 6, 3));
    EXPECT_EQ(0, Target.HardwareDraws); EXPECT_EQ(0, Target.SoftwareDraws);
}





TEST_F(VertexBufferDrawTest, FailedHardwareDrawDoesNotCountVerticesOrPrimitives) {
    RCKVertexBuffer buffer(Context); Fill(buffer, CKRST_DP_TR_V);
    Target.DrawResult = FALSE;
    EXPECT_FALSE(buffer.Draw(Device, VX_TRIANGLELIST, nullptr, 0, 0, 6));
    EXPECT_EQ(0, Device->m_Stats.NbTrianglesDrawn); EXPECT_EQ(0, Device->m_Stats.NbVerticesProcessed);
}
