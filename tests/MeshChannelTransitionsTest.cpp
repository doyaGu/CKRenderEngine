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
        CKDWORD Flags = 0, Fog = 0, Source = 0, Dest = 0, Lighting = 0, Coordinates = 0;
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
        draw.Lighting = States[VXRENDERSTATE_LIGHTING];
        draw.Coordinates = StageStates[0][CKRST_TSS_TEXCOORDINDEX];
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
class MeshChannelTransitionsTest : public ::testing::Test {
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

namespace {
int OwnChannelLighting(CKRenderContext *context, CKMaterial *, void *argument) {
    const CKBOOL lighting = *static_cast<CKBOOL *>(argument);
    CKRasterizerContext *rst = static_cast<RCKRenderContext *>(context)->m_RasterizerContext;
    rst->SetRenderState(VXRENDERSTATE_LIGHTING, lighting);
    rst->SetRenderState(VXRENDERSTATE_FOGENABLE, 27);
    rst->SetTextureStageState(0, CKRST_TSS_TEXCOORDINDEX,
        CKRSTPackTexcoordIndex(0, CKRST_TEXGEN_CAMERASPACENORMAL));
    return TRUE;
}
class OverrideChannelLighting : public ChannelMaterialProbe {
public:
    explicit OverrideChannelLighting(CKContext *context) : ChannelMaterialProbe(context) {}
    CKBOOL SetAsCurrent(CKRenderContext *context, CKBOOL, int) override {
        CKBOOL lighting = FALSE; return OwnChannelLighting(context, this, &lighting);
    }
};
}
TEST_F(MeshChannelTransitionsTest, PrelitThenUnlitChannelDoesNotRetainUnavailableColorAttributes) {
    ChannelMeshProbe mesh(Context); auto data = Geometry(mesh); ChannelMaterialProbe a(Context), b(Context);
    mesh.SetLitMode(VX_PRELITMESH); Channel(mesh, a); Channel(mesh, b, VXBLEND_ZERO, VXBLEND_SRCCOLOR);
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, TRUE)); ASSERT_EQ(2, State.DrawCount);
    EXPECT_TRUE(State.Draws[0].Flags & CKRST_DP_DIFFUSE); EXPECT_TRUE(State.Draws[0].Flags & CKRST_DP_SPECULAR);
    EXPECT_EQ(nullptr, State.Draws[1].Color); EXPECT_EQ(nullptr, State.Draws[1].Specular);
    EXPECT_EQ(0u, State.Draws[1].Flags & (CKRST_DP_LIGHT | CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR));
}
TEST_F(MeshChannelTransitionsTest, ExplicitColorFlagsAreRemovedOnlyForOrdinaryUnlitPass) {
    ChannelMeshProbe mesh(Context); auto data = Geometry(mesh); ChannelMaterialProbe a(Context), b(Context);
    mesh.SetLitMode(VX_LITMESH); mesh.DrawFlags(CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR);
    Channel(mesh, a, VXBLEND_ZERO, VXBLEND_SRCCOLOR); Channel(mesh, b);
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, TRUE)); ASSERT_EQ(2, State.DrawCount);
    EXPECT_EQ(0u, State.Draws[0].Flags & (CKRST_DP_LIGHT | CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR));
    EXPECT_EQ(CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR | CKRST_DP_LIGHT,
        State.Draws[1].Flags & (CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR | CKRST_DP_LIGHT));
    EXPECT_EQ(data.ColorPtr, State.Draws[1].Color); EXPECT_EQ(data.SpecularColorPtr, State.Draws[1].Specular);
}
TEST_F(MeshChannelTransitionsTest, HandledCallbackKeepsLightingDisabledThroughContextSubmission) {
    ChannelMeshProbe mesh(Context); auto data = Geometry(mesh); ChannelMaterialProbe material(Context);
    mesh.SetLitMode(VX_LITMESH); Channel(mesh, material); CKBOOL lighting = FALSE;
    material.SetCallback(OwnChannelLighting, &lighting);
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, TRUE)); ASSERT_EQ(1, State.DrawCount);
    EXPECT_EQ(FALSE, State.Draws[0].Lighting); EXPECT_FALSE(State.Draws[0].Flags & CKRST_DP_LIGHT);
    EXPECT_EQ(27u, State.Draws[0].Fog);
    EXPECT_EQ(CKRSTPackTexcoordIndex(0, CKRST_TEXGEN_CAMERASPACENORMAL), State.Draws[0].Coordinates);
}
TEST_F(MeshChannelTransitionsTest, HandledCallbackKeepsLightingEnabledForDeclaredUnlitChannel) {
    ChannelMeshProbe mesh(Context); auto data = Geometry(mesh); ChannelMaterialProbe material(Context);
    mesh.SetLitMode(VX_LITMESH); Channel(mesh, material, VXBLEND_ZERO, VXBLEND_SRCCOLOR); CKBOOL lighting = TRUE;
    material.SetCallback(OwnChannelLighting, &lighting);
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, TRUE)); ASSERT_EQ(1, State.DrawCount);
    EXPECT_EQ(TRUE, State.Draws[0].Lighting); EXPECT_TRUE(State.Draws[0].Flags & CKRST_DP_LIGHT);
    EXPECT_EQ(data.ColorPtr, State.Draws[0].Color); EXPECT_EQ(data.SpecularColorPtr, State.Draws[0].Specular);
}
TEST_F(MeshChannelTransitionsTest, UnknownVirtualOverrideKeepsItsLightingChoice) {
    ChannelMeshProbe mesh(Context); auto data = Geometry(mesh); OverrideChannelLighting material(Context);
    mesh.SetLitMode(VX_LITMESH); Channel(mesh, material);
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, TRUE)); ASSERT_EQ(1, State.DrawCount);
    EXPECT_EQ(FALSE, State.Draws[0].Lighting); EXPECT_FALSE(State.Draws[0].Flags & CKRST_DP_LIGHT);
}
TEST_F(MeshChannelTransitionsTest, FalseCallbackContinuesOrdinaryLightingAndColorSelection) {
    ChannelMeshProbe mesh(Context); auto data = Geometry(mesh); ChannelMaterialProbe material(Context);
    mesh.SetLitMode(VX_LITMESH); Channel(mesh, material); ChannelCallbackData callback; callback.Expected = &material;
    material.SetCallback(ObserveChannelMaterial, &callback);
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, TRUE)); ASSERT_EQ(1, State.DrawCount);
    EXPECT_EQ(TRUE, State.Draws[0].Lighting); EXPECT_TRUE(State.Draws[0].Flags & CKRST_DP_LIGHT); EXPECT_EQ(1, callback.Calls);
}
