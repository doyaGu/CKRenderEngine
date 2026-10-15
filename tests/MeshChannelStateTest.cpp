#include <gtest/gtest.h>
#include <cstring>
#include "CKAll.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "RCKTexture.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "MaterialRenderObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();
namespace {
struct ChannelTrace { int Values[32] = {}; int Count = 0; };
class ChannelMaterialProbe : public RCKMaterial {
public:
    explicit ChannelMaterialProbe(CKContext *context) : RCKMaterial(context) {}
    CKDWORD RawFlags() const { return m_Flags; }
};
class ChannelMeshProbe : public RCKMesh {
public:
    explicit ChannelMeshProbe(CKContext *context) : RCKMesh(context) {}
    VxMaterialChannel &Channel(int index) { return m_MaterialChannels[index]; }
    void DrawFlags(CKDWORD flags) { m_DrawFlags = flags; }
};
class ChannelStateObserver : public MaterialRenderObserver {
public:
    struct Submission {
        CKDWORD Flags = 0, Fog = 0, Source = 0, Dest = 0;
        void *Color = nullptr, *Specular = nullptr, *UV = nullptr;
        CKDWORD UVStride = 0; float U[3] = {}, V[3] = {};
        int Count = 0; CKWORD Indices[6] = {}; VxMatrix Projection;
    };
    ChannelTrace *Trace = nullptr;
    Submission Draws[8]; int DrawCount = 0; int FailDraw = -1;
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        EXPECT_EQ(VX_TRIANGLELIST, type); EXPECT_LT(DrawCount, 8);
        if (DrawCount >= 8 || !data) return FALSE;
        const int ordinal = DrawCount++; Submission &draw = Draws[ordinal];
        draw.Flags = data->Flags; draw.Color = data->ColorPtr; draw.Specular = data->SpecularColorPtr;
        draw.UV = data->TexCoordPtr; draw.UVStride = data->TexCoordStride; draw.Count = count;
        draw.Fog = States[VXRENDERSTATE_FOGENABLE]; draw.Source = States[VXRENDERSTATE_SRCBLEND]; draw.Dest = States[VXRENDERSTATE_DESTBLEND];
        draw.Projection = Matrices[VXMATRIX_PROJECTION];
        for (int i = 0; i < count && i < 6; ++i) draw.Indices[i] = indices[i];
        for (int i = 0; i < data->VertexCount && i < 3; ++i) {
            if (data->TexCoordPtr) {
                float values[2]; std::memcpy(values, static_cast<CKBYTE *>(data->TexCoordPtr) + i * data->TexCoordStride, sizeof(values));
                draw.U[i] = values[0]; draw.V[i] = values[1];
            }
        }
        if (Trace) Trace->Values[Trace->Count++] = 10 + ordinal;
        return ordinal != FailDraw;
    }
};
struct ChannelCallbackData {
    ChannelTrace *Trace = nullptr; int Value = 0, Calls = 0, Result = 0;
    ChannelMaterialProbe *Expected = nullptr; CKRenderContext *Device = nullptr;
    VXBLEND_MODE Source = VXBLEND_ONE, Dest = VXBLEND_ZERO;
    CKBOOL Alpha = FALSE, ZWrite = TRUE;
};
int ObserveChannelMaterial(CKRenderContext *device, CKMaterial *material, void *argument) {
    ChannelCallbackData *data = static_cast<ChannelCallbackData *>(argument);
    ++data->Calls; data->Device = device;
    EXPECT_EQ(data->Expected, material);
    data->Source = material->GetSourceBlend(); data->Dest = material->GetDestBlend();
    data->Alpha = material->AlphaBlendEnabled(); data->ZWrite = material->ZWriteEnabled();
    if (data->Trace) data->Trace->Values[data->Trace->Count++] = data->Value;
    return data->Result;
}
void ObserveSubMesh(CKRenderContext *, CK3dEntity *, CKMesh *, CKMaterial *, void *argument) {
    ++*static_cast<int *>(argument);
}
class MeshChannelStateTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr; RCKRenderManager *Manager = nullptr; RCKRenderContext *Device = nullptr;
    ChannelStateObserver State; ChannelTrace Trace;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context); Device = new RCKRenderContext(Context);
        Device->DetachAll(); Device->m_RasterizerContext = &State; State.Trace = &Trace;
        VxMatrix projection = VxMatrix::Identity(); projection[3][2] = 7; projection[1][2] = 15; projection[0][3] = 9;
        Device->SetProjectionTransformationMatrix(projection);
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; delete Device; }
        if (Context) CKCloseContext(Context); CKShutdown();
    }
    VxDrawPrimitiveData Geometry(ChannelMeshProbe &mesh) {
        mesh.SetVertexCount(3); mesh.SetFaceCount(1); mesh.SetFaceVertexIndex(0, 0, 1, 2);
        for (int i = 0; i < 3; ++i) {
            VxVector position(static_cast<float>(i), static_cast<float>(i + 2), static_cast<float>(i + 4));
            mesh.SetVertexPosition(i, &position); mesh.SetVertexTextureCoordinates(i, i + .1f, i + .2f, -1);
        }
        VxDrawPrimitiveData data = {}; data.VertexCount = 3; data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_STAGES0;
        data.PositionPtr = mesh.GetPositionsPtr(&data.PositionStride); data.NormalPtr = mesh.GetNormalsPtr(&data.NormalStride);
        data.ColorPtr = mesh.GetColorsPtr(&data.ColorStride); data.SpecularColorPtr = mesh.GetSpecularColorsPtr(&data.SpecularColorStride);
        data.TexCoordPtr = mesh.GetTextureCoordinatesPtr(&data.TexCoordStride, -1);
        return data;
    }
    int Channel(ChannelMeshProbe &mesh, ChannelMaterialProbe &material, VXBLEND_MODE source = VXBLEND_SRCALPHA, VXBLEND_MODE dest = VXBLEND_INVSRCALPHA) {
        const int index = mesh.AddChannel(&material, FALSE);
        mesh.SetChannelSourceBlend(index, source); mesh.SetChannelDestBlend(index, dest); return index;
    }
};
}

TEST_F(MeshChannelStateTest, EligibleChannelMaterialCallbacksRunInStoredSlotOrderBeforeEachDraw) {
    ChannelMeshProbe mesh(Context); auto data = Geometry(mesh); ChannelMaterialProbe a(Context), b(Context);
    Channel(mesh, a); Channel(mesh, b);
    ChannelCallbackData ca; ca.Trace = &Trace; ca.Value = 1; ca.Expected = &a;
    ChannelCallbackData cb; cb.Trace = &Trace; cb.Value = 2; cb.Expected = &b;
    a.SetCallback(ObserveChannelMaterial, &ca); b.SetCallback(ObserveChannelMaterial, &cb);
    EXPECT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, TRUE));
    ASSERT_EQ(4, Trace.Count); EXPECT_EQ(1, Trace.Values[0]); EXPECT_EQ(10, Trace.Values[1]); EXPECT_EQ(2, Trace.Values[2]); EXPECT_EQ(11, Trace.Values[3]);
    EXPECT_EQ(1, ca.Calls); EXPECT_EQ(1, cb.Calls); EXPECT_EQ(Device, ca.Device); EXPECT_EQ(Device, cb.Device);
}
TEST_F(MeshChannelStateTest, CallbackSeesTemporaryBlendAlphaAndDepthFieldsAndOriginalMetadataIsRestored) {
    ChannelMeshProbe mesh(Context); auto data = Geometry(mesh); ChannelMaterialProbe material(Context);
    material.SetSourceBlend(VXBLEND_ONE); material.SetDestBlend(VXBLEND_ZERO); material.EnableAlphaBlend(FALSE); material.EnableZWrite(TRUE);
    material.EnableAlphaTest(TRUE); material.SetTwoSided(TRUE); const CKDWORD before = material.RawFlags();
    Channel(mesh, material); ChannelCallbackData callback; callback.Expected = &material; material.SetCallback(ObserveChannelMaterial, &callback);
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, TRUE));
    EXPECT_EQ(VXBLEND_SRCALPHA, callback.Source); EXPECT_EQ(VXBLEND_INVSRCALPHA, callback.Dest);
    EXPECT_TRUE(callback.Alpha); EXPECT_FALSE(callback.ZWrite);
    EXPECT_EQ(VXBLEND_ONE, material.GetSourceBlend()); EXPECT_EQ(VXBLEND_ZERO, material.GetDestBlend()); EXPECT_EQ(before, material.RawFlags());
}
TEST_F(MeshChannelStateTest, FailedDrawStillRestoresMaterialAndProcessesFollowingChannel) {
    ChannelMeshProbe mesh(Context); auto data = Geometry(mesh); ChannelMaterialProbe a(Context), b(Context);
    Channel(mesh, a); Channel(mesh, b); const CKDWORD af = a.RawFlags(), bf = b.RawFlags();
    ChannelCallbackData ca; ca.Trace = &Trace; ca.Value = 1; ca.Expected = &a;
    ChannelCallbackData cb; cb.Trace = &Trace; cb.Value = 2; cb.Expected = &b;
    a.SetCallback(ObserveChannelMaterial, &ca); b.SetCallback(ObserveChannelMaterial, &cb); State.FailDraw = 0;
    mesh.RenderChannels(Device, nullptr, &data, TRUE);
    ASSERT_EQ(2, State.DrawCount); EXPECT_EQ(1, ca.Calls); EXPECT_EQ(1, cb.Calls);
    EXPECT_EQ(af, a.RawFlags()); EXPECT_EQ(bf, b.RawFlags()); EXPECT_EQ(VXBLEND_ONE, a.GetSourceBlend()); EXPECT_EQ(VXBLEND_ZERO, b.GetDestBlend());
    ASSERT_EQ(4, Trace.Count); EXPECT_EQ(2, Trace.Values[2]); EXPECT_EQ(11, Trace.Values[3]);
}
TEST_F(MeshChannelStateTest, FogIsEnabledForAlphaBlendAndDisabledForModulationThenRestored) {
    ChannelMeshProbe mesh(Context); auto data = Geometry(mesh); ChannelMaterialProbe a(Context), b(Context);
    Channel(mesh, a); Channel(mesh, b, VXBLEND_ZERO, VXBLEND_SRCCOLOR); State.States[VXRENDERSTATE_FOGENABLE] = 19;
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, TRUE)); ASSERT_EQ(2, State.DrawCount);
    EXPECT_EQ(1u, State.Draws[0].Fog); EXPECT_EQ(0u, State.Draws[1].Fog); EXPECT_EQ(19u, State.States[VXRENDERSTATE_FOGENABLE]);
}
TEST_F(MeshChannelStateTest, LitChannelAfterUnlitModulationReceivesOriginalColorStreams) {
    ChannelMeshProbe mesh(Context); auto data = Geometry(mesh); ChannelMaterialProbe a(Context), b(Context);
    mesh.DrawFlags(CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR);
    Channel(mesh, a, VXBLEND_ZERO, VXBLEND_SRCCOLOR); Channel(mesh, b);
    void *color = data.ColorPtr, *specular = data.SpecularColorPtr;
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, TRUE)); ASSERT_EQ(2, State.DrawCount);
    EXPECT_EQ(nullptr, State.Draws[0].Color); EXPECT_EQ(nullptr, State.Draws[0].Specular);
    EXPECT_EQ(color, State.Draws[1].Color); EXPECT_EQ(specular, State.Draws[1].Specular);
    EXPECT_TRUE(State.Draws[1].Flags & CKRST_DP_LIGHT); EXPECT_EQ(color, data.ColorPtr); EXPECT_EQ(specular, data.SpecularColorPtr);
}
TEST_F(MeshChannelStateTest, SameUVUsesCallerScratchCoordinatesAndStrideAndRestoresThem) {
    ChannelMeshProbe mesh(Context); auto data = Geometry(mesh); ChannelMaterialProbe material(Context);
    const int index = Channel(mesh, material); mesh.SetChannelFlags(index, VXCHANNEL_ACTIVE | VXCHANNEL_SAMEUV);
    struct PitchedUV { float U, V, Padding[3]; } uv[] = {{.7f, .8f, {}}, {.9f, 1.1f, {}}, {1.3f, 1.7f, {}}};
    data.TexCoordPtr = uv; data.TexCoordStride = sizeof(PitchedUV);
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, TRUE)); ASSERT_EQ(1, State.DrawCount);
    EXPECT_EQ(uv, State.Draws[0].UV); EXPECT_EQ(sizeof(PitchedUV), State.Draws[0].UVStride);
    EXPECT_FLOAT_EQ(.9f, State.Draws[0].U[1]); EXPECT_FLOAT_EQ(1.7f, State.Draws[0].V[2]);
    EXPECT_EQ(uv, data.TexCoordPtr); EXPECT_EQ(sizeof(PitchedUV), data.TexCoordStride);
}
TEST_F(MeshChannelStateTest, PrivateChannelUVsUseTheirOwnStreamAndSelectiveFaceIndices) {
    ChannelMeshProbe mesh(Context); auto data = Geometry(mesh); ChannelMaterialProbe material(Context);
    const int index = Channel(mesh, material);
    for (int i = 0; i < 3; ++i) mesh.SetVertexTextureCoordinates(i, i + .7f, i + .8f, index);
    mesh.Channel(index).m_FaceIndices = new XArray<CKWORD>; mesh.Channel(index).m_FaceIndices->Resize(3);
    (*mesh.Channel(index).m_FaceIndices)[0] = 2; (*mesh.Channel(index).m_FaceIndices)[1] = 0; (*mesh.Channel(index).m_FaceIndices)[2] = 1;
    void *base = data.TexCoordPtr;
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, TRUE)); ASSERT_EQ(1, State.DrawCount);
    EXPECT_EQ(sizeof(Vx2DVector), State.Draws[0].UVStride); EXPECT_FLOAT_EQ(2.7f, State.Draws[0].U[2]);
    EXPECT_EQ(2, State.Draws[0].Indices[0]); EXPECT_EQ(0, State.Draws[0].Indices[1]); EXPECT_EQ(1, State.Draws[0].Indices[2]); EXPECT_EQ(base, data.TexCoordPtr);
}
TEST_F(MeshChannelStateTest, ProjectionDepthBiasIsVisibleDuringDrawAndWholeInputMatrixIsRestored) {
    ChannelMeshProbe mesh(Context); auto data = Geometry(mesh); ChannelMaterialProbe material(Context); Channel(mesh, material);
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, TRUE)); ASSERT_EQ(1, State.DrawCount);
    EXPECT_NEAR(7.007f, State.Draws[0].Projection[3][2], 1e-5f);
    EXPECT_FLOAT_EQ(15, State.Draws[0].Projection[1][2]); EXPECT_FLOAT_EQ(9, State.Draws[0].Projection[0][3]);
    EXPECT_FLOAT_EQ(7, Device->GetProjectionTransformationMatrix()[3][2]);
    EXPECT_FLOAT_EQ(15, State.Matrices[VXMATRIX_PROJECTION][1][2]); EXPECT_FLOAT_EQ(9, State.Matrices[VXMATRIX_PROJECTION][0][3]);
}
TEST_F(MeshChannelStateTest, InactiveMonoNullAndEmptyFaceChannelsDoNotInvokeMaterialCallbacksOrDraw) {
    ChannelMeshProbe mesh(Context); auto data = Geometry(mesh); ChannelMaterialProbe a(Context), b(Context), c(Context);
    const int ai = Channel(mesh, a), bi = Channel(mesh, b), ci = Channel(mesh, c);
    mesh.SetChannelFlags(ai, 0); mesh.SetChannelFlags(bi, VXCHANNEL_ACTIVE | VXCHANNEL_MONO);
    mesh.Channel(ci).m_FaceIndices = new XArray<CKWORD>;
    ChannelCallbackData ca; ca.Expected = &a; ChannelCallbackData cb; cb.Expected = &b; ChannelCallbackData cc; cc.Expected = &c;
    a.SetCallback(ObserveChannelMaterial, &ca); b.SetCallback(ObserveChannelMaterial, &cb); c.SetCallback(ObserveChannelMaterial, &cc);
    EXPECT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, TRUE)); EXPECT_EQ(0, State.DrawCount);
    EXPECT_EQ(0, ca.Calls); EXPECT_EQ(0, cb.Calls); EXPECT_EQ(0, cc.Calls);
    mesh.SetChannelMaterial(ai, nullptr); EXPECT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, TRUE)); EXPECT_EQ(0, State.DrawCount);
}
TEST_F(MeshChannelStateTest, MultipassMaterialApplicationDoesNotInvokeSubmeshCallbacksAgain) {
    ChannelMeshProbe mesh(Context); auto data = Geometry(mesh); ChannelMaterialProbe material(Context); Channel(mesh, material);
    int submeshCalls = 0; mesh.AddSubMeshPreRenderCallBack(ObserveSubMesh, &submeshCalls, FALSE);
    mesh.AddSubMeshPostRenderCallBack(ObserveSubMesh, &submeshCalls, FALSE);
    ChannelCallbackData callback; callback.Expected = &material; material.SetCallback(ObserveChannelMaterial, &callback);
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, TRUE)); EXPECT_EQ(1, callback.Calls); EXPECT_EQ(0, submeshCalls);
}

namespace {
class ChannelFallbackStateObserver : public ChannelStateObserver {
public:
    int FailedBinds = 0;
    CKDWORD DrawTexture3[8] = {}, DrawCoords0[8] = {}, DrawLighting[8] = {};
    VxMatrix DrawTextureMatrix0[8];
    CKBOOL SetTexture(CKDWORD texture, int stage) override {
        if (texture && stage == 1 && !FailedBinds) { ++FailedBinds; return FALSE; }
        return MaterialRenderObserver::SetTexture(texture, stage);
    }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        const int ordinal = DrawCount;
        if (ordinal < 8) {
            DrawTexture3[ordinal] = Textures[3]; DrawCoords0[ordinal] = StageStates[0][CKRST_TSS_TEXCOORDINDEX];
            DrawTextureMatrix0[ordinal] = Matrices[VXMATRIX_TEXTURE0]; DrawLighting[ordinal] = States[VXRENDERSTATE_LIGHTING];
        }
        return ChannelStateObserver::DrawPrimitive(type, indices, count, data);
    }
};
class ChannelFallbackTexture : public RCKTexture {
public:
    explicit ChannelFallbackTexture(CKContext *context) : RCKTexture(context) {}
    CKBOOL SetAsCurrent(CKRenderContext *context, CKBOOL, int stage) override {
        // Actual SDK virtual handoff to the public resource state interface.
        return static_cast<RCKRenderContext *>(context)->m_RasterizerContext->SetTexture(73, stage);
    }
};
struct FallbackOwner { int Calls = 0; };
int OwnFallbackStages(CKRenderContext *context, CKMaterial *, void *argument) {
    FallbackOwner *owner = static_cast<FallbackOwner *>(argument);
    if (++owner->Calls == 1) return FALSE; // The attempted positive-stage bind fails.
    CKRasterizerContext *rst = static_cast<RCKRenderContext *>(context)->m_RasterizerContext;
    VxMatrix matrix = VxMatrix::Identity(); matrix[3][0] = 2.5f;
    rst->SetTexture(99, 3);
    rst->SetTextureStageState(0, CKRST_TSS_TEXCOORDINDEX, CKRSTPackTexcoordIndex(0, CKRST_TEXGEN_CAMERASPACENORMAL));
    rst->SetTextureStageState(0, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_COUNT2);
    rst->SetTransformMatrix(VXMATRIX_TEXTURE0, matrix);
    rst->SetRenderState(VXRENDERSTATE_FOGENABLE, 27);
    rst->SetRenderState(VXRENDERSTATE_LIGHTING, FALSE);
    return TRUE;
}
class MeshChannelFallbackStateTest : public MeshChannelStateTest {
protected:
    CKRasterizerDriver Driver;
    ChannelFallbackStateObserver FallbackState;
    void SetUp() override {
        MeshChannelStateTest::SetUp();
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        Manager->m_DriverCount = 1; Manager->m_Drivers = new VxDriverDescEx[1](); Manager->m_VertexCache.Value = 0;
        Manager->m_Drivers[0].CapsUpToDate = TRUE; Manager->m_Drivers[0].RasterizerDriver = &Driver;
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 2;
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&FallbackState, 0));
        Device->m_RasterizerContext = &FallbackState; Device->m_RasterizerDriver = &Driver; Device->m_DriverIndex = 0;
        Device->m_Current3dEntity = nullptr; FallbackState.Trace = &Trace;
        Device->SetProjectionTransformationMatrix(Device->GetProjectionTransformationMatrix());
    }
    void TearDown() override {
        if (Device) Device->m_RasterizerDriver = nullptr;
        MeshChannelStateTest::TearDown();
    }
    int SelectedChannel(ChannelMeshProbe &mesh, ChannelMaterialProbe &material, ChannelFallbackTexture &texture) {
        material.SetTexture(0, &texture);
        return Channel(mesh, material, VXBLEND_DESTCOLOR, VXBLEND_ZERO);
    }
    void DrawFallback(ChannelMeshProbe &mesh) {
        ASSERT_EQ(1, mesh.CreateRenderGroups()); ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr));
        ASSERT_EQ(1, FallbackState.FailedBinds); ASSERT_EQ(2, FallbackState.DrawCount);
    }
};
}

TEST_F(MeshChannelFallbackStateTest, GroupFallbackUnlitModulationDoesNotReuseVertexColorAttributes) {
    ChannelMeshProbe mesh(Context); Geometry(mesh); ChannelMaterialProbe channel(Context); ChannelFallbackTexture texture(Context);
    SelectedChannel(mesh, channel, texture);
    for (int i = 0; i < 3; ++i) mesh.SetVertexColor(i, 0xff000000u);
    DrawFallback(mesh);
    EXPECT_EQ(nullptr, FallbackState.Draws[1].Color); EXPECT_EQ(nullptr, FallbackState.Draws[1].Specular);
    EXPECT_EQ(0u, FallbackState.Draws[1].Flags & (CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR | CKRST_DP_LIGHT));
}
TEST_F(MeshChannelFallbackStateTest, GroupFallbackAppliesDepthBiasAndRestoresBothProjectionCopies) {
    ChannelMeshProbe mesh(Context); Geometry(mesh); ChannelMaterialProbe channel(Context); ChannelFallbackTexture texture(Context);
    SelectedChannel(mesh, channel, texture); DrawFallback(mesh);
    EXPECT_FLOAT_EQ(7, FallbackState.Draws[0].Projection[3][2]);
    EXPECT_NEAR(7.007f, FallbackState.Draws[1].Projection[3][2], 1e-5f);
    EXPECT_FLOAT_EQ(7, Device->GetProjectionTransformationMatrix()[3][2]);
    EXPECT_FLOAT_EQ(7, FallbackState.Matrices[VXMATRIX_PROJECTION][3][2]);
}
TEST_F(MeshChannelFallbackStateTest, GroupFallbackSameUVUsesActualBaseGeometryCoordinates) {
    ChannelMeshProbe mesh(Context); Geometry(mesh); ChannelMaterialProbe channel(Context); ChannelFallbackTexture texture(Context);
    const int index = SelectedChannel(mesh, channel, texture); mesh.SetChannelFlags(index, VXCHANNEL_ACTIVE | VXCHANNEL_SAMEUV);
    DrawFallback(mesh);
    EXPECT_FLOAT_EQ(.1f, FallbackState.Draws[1].U[0]); EXPECT_FLOAT_EQ(1.1f, FallbackState.Draws[1].U[1]);
    EXPECT_FLOAT_EQ(2.2f, FallbackState.Draws[1].V[2]); EXPECT_EQ(sizeof(VxVertex), FallbackState.Draws[1].UVStride);
}
TEST_F(MeshChannelFallbackStateTest, GroupFallbackPreservesStageZeroCallbackTextureMatrixAuxiliaryStagesAndGlobalChoices) {
    ChannelMeshProbe mesh(Context); Geometry(mesh); ChannelMaterialProbe channel(Context); ChannelFallbackTexture texture(Context);
    SelectedChannel(mesh, channel, texture); FallbackOwner owner; channel.SetCallback(OwnFallbackStages, &owner);
    DrawFallback(mesh); EXPECT_EQ(2, owner.Calls);
    EXPECT_EQ(99u, FallbackState.DrawTexture3[1]);
    EXPECT_EQ(CKRSTPackTexcoordIndex(0, CKRST_TEXGEN_CAMERASPACENORMAL), FallbackState.DrawCoords0[1]);
    EXPECT_FLOAT_EQ(2.5f, FallbackState.DrawTextureMatrix0[1][3][0]);
    EXPECT_EQ(27u, FallbackState.Draws[1].Fog); EXPECT_EQ(0u, FallbackState.DrawLighting[1]);
}
