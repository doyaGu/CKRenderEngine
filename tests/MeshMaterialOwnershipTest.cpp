#include <gtest/gtest.h>
#include <cstring>
#include "CKAll.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "RCK3dEntity.h"
#include "TextureTestObserver.h"

namespace {
void WriteOwnedStages(CKRenderContext *context, int marker) {
    CKRasterizerContext *state = static_cast<RCKRenderContext *>(context)->m_RasterizerContext;
    state->SetTexture(100 + marker, 0); state->SetTexture(200 + marker, 1);
    state->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_ADD);
    state->SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    state->SetTextureStageState(0, CKRST_TSS_TEXCOORDINDEX, 2);
    state->SetTextureStageState(1, CKRST_TSS_TEXCOORDINDEX, 3);
    state->SetTextureStageState(1, CKRST_TSS_STAGEBLEND, 0x65);
}
class OwnershipDrawObserver : public TextureTestObserver {
public:
    struct Draw {
        CKDWORD Textures[2] = {}, Coordinates[2] = {};
        CKDWORD Stage0Op = 0, Stage1Blend = 0;
    };
    Draw Draws[16]; int DrawCount = 0;
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        EXPECT_EQ(VX_TRIANGLELIST, type); EXPECT_EQ(3, count);
        EXPECT_NE(nullptr, indices); EXPECT_NE(nullptr, data); EXPECT_LT(DrawCount, 16);
        if (DrawCount >= 16) return FALSE;
        Draw &draw = Draws[DrawCount++];
        for (int stage = 0; stage < 2; ++stage) {
            draw.Textures[stage] = Textures[stage];
            draw.Coordinates[stage] = StageStates[stage][CKRST_TSS_TEXCOORDINDEX];
        }
        draw.Stage0Op = StageStates[0][CKRST_TSS_OP];
        draw.Stage1Blend = StageStates[1][CKRST_TSS_STAGEBLEND];
        return TRUE;
    }
};
class OwnershipMesh : public RCKMesh {
public:
    explicit OwnershipMesh(CKContext *context) : RCKMesh(context) {}
    void Select(int channel) { m_ActiveTextureChannels.PushBack(channel); }
};
struct OwnershipCallbackData {
    int Calls = 0, Depth = 0, Marker = 17;
    CKBOOL Result = TRUE, NestedResult = FALSE;
    CKBOOL Direct = FALSE, ThrowNested = FALSE, ThrowRoot = FALSE, Caught = FALSE;
    RCKMaterial *NestedMaterial = nullptr;
    OwnershipMesh *NestedMesh = nullptr;
    CKMaterialGroup *NestedGroup = nullptr;
    RCKRenderContext *NestedContext = nullptr;
    VxDrawPrimitiveData *NestedData = nullptr;
};
int OwnershipCallback(CKRenderContext *context, CKMaterial *, void *argument) {
    OwnershipCallbackData *data = static_cast<OwnershipCallbackData *>(argument); ++data->Calls;
    if (data->Depth) {
        if (data->ThrowNested) throw 37;
        WriteOwnedStages(context, data->Marker + 1); return data->NestedResult;
    }
    if (data->ThrowRoot) throw 41;
    if (data->NestedMaterial) {
        ++data->Depth;
        try {
            if (data->Direct)
                data->NestedMaterial->SetAsCurrent(data->NestedContext, TRUE, 0);
            else
                data->NestedMesh->RenderGroup(data->NestedContext, data->NestedGroup, nullptr, data->NestedData);
        } catch (int value) {
            EXPECT_EQ(37, value); data->Caught = TRUE;
        }
        --data->Depth;
    }
    WriteOwnedStages(context, data->Marker); return data->Result;
}
struct OwnershipEffectData {
    int Calls = 0, Marker = 29;
    VX_EFFECTCALLBACK_RETVAL Result = VXEFFECTRETVAL_SKIPALL;
    RCKMaterial *NestedMaterial = nullptr;
    RCKRenderContext *NestedContext = nullptr;
};
VX_EFFECTCALLBACK_RETVAL OwnershipEffect(CKRenderContext *context, CKMaterial *, int stage, void *argument) {
    OwnershipEffectData *data = static_cast<OwnershipEffectData *>(argument); ++data->Calls;
    if (data->NestedMaterial) data->NestedMaterial->SetAsCurrent(data->NestedContext, TRUE, 0);
    WriteOwnedStages(context, data->Marker);
    if (data->Result == VXEFFECTRETVAL_SKIPTEXMAT) {
        CKRasterizerContext *state = static_cast<RCKRenderContext *>(context)->m_RasterizerContext;
        VxMatrix matrix = VxMatrix::Identity(); matrix[3][0] = 3.25f;
        state->SetTransformMatrix(static_cast<VXMATRIX_TYPE>(VXMATRIX_TEXTURE0 + stage), matrix);
        state->SetTextureStageState(stage, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_COUNT2);
        state->SetTextureStageState(stage, CKRST_TSS_TEXCOORDINDEX,
            CKRSTPackTexcoordIndex(stage, CKRST_TEXGEN_CAMERASPACENORMAL));
    }
    return data->Result;
}
void OwnershipSubMeshCallback(CKRenderContext *context, CK3dEntity *, CKMesh *, CKMaterial *, void *) {
    WriteOwnedStages(context, 43);
}
class VirtualOwnershipMaterial : public RCKMaterial {
public:
    int Calls = 0;
    CKBOOL Delegate = FALSE;
    explicit VirtualOwnershipMaterial(CKContext *context) : RCKMaterial(context) {}
    CKBOOL SetAsCurrent(CKRenderContext *context, CKBOOL lit, int stage) override {
        ++Calls;
        if (Delegate) return RCKMaterial::SetAsCurrent(context, lit, stage);
        WriteOwnedStages(context, 53); return TRUE;
    }
};
class MeshMaterialOwnershipTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr, *OtherDevice = nullptr;
    OwnershipDrawObserver State, OtherState;
    CKRasterizerDriver Driver;
    VxVertex Vertices[3]; VxDrawPrimitiveData Data = {};
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        Manager->m_DriverCount = 1; Manager->m_Drivers = new VxDriverDescEx[1]();
        Manager->m_Drivers[0].CapsUpToDate = TRUE; Manager->m_Drivers[0].RasterizerDriver = &Driver;
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 3;
        VxImageDescEx alpha; VxPixelFormat2ImageDesc(_32_ARGB8888, alpha);
        Manager->m_Drivers[0].TextureFormats.PushBack(alpha);
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0));
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&OtherState, 0));
        Device = new RCKRenderContext(Context); OtherDevice = new RCKRenderContext(Context);
        Device->DetachAll(); OtherDevice->DetachAll();
        Device->m_RasterizerContext = &State; OtherDevice->m_RasterizerContext = &OtherState;
        Device->m_RasterizerDriver = OtherDevice->m_RasterizerDriver = &Driver;
        Device->m_DriverIndex = OtherDevice->m_DriverIndex = 0;
        for (int i = 0; i < 3; ++i) {
            Vertices[i].m_Position = VxVector(i, i + 1, i + 2);
            Vertices[i].m_Normal = VxVector(0, 0, 1); Vertices[i].m_UV = Vx2DVector(i * .25f, i * .5f);
        }
        Data.VertexCount = 3; Data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_STAGES0;
        Data.PositionPtr = &Vertices[0].m_Position; Data.PositionStride = sizeof(VxVertex);
        Data.NormalPtr = &Vertices[0].m_Normal; Data.NormalStride = sizeof(VxVertex);
        Data.TexCoordPtr = &Vertices[0].m_UV; Data.TexCoordStride = sizeof(VxVertex);
        State.MaterialWrites = OtherState.MaterialWrites = 0;
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; delete Device; }
        if (OtherDevice) { OtherDevice->m_RasterizerContext = nullptr; OtherDevice->m_RasterizerDriver = nullptr; delete OtherDevice; }
        if (Context) CKCloseContext(Context); CKShutdown();
    }
    static void Primitive(CKMaterialGroup &group) {
        group.m_Primitives.Resize(1); group.m_Primitives[0].m_Type = VX_TRIANGLELIST;
        group.m_Primitives[0].m_IndexBufferOffset = -1;
        group.m_Primitives[0].m_Indices.PushBack(0); group.m_Primitives[0].m_Indices.PushBack(1);
        group.m_Primitives[0].m_Indices.PushBack(2); group.m_VertexCount = 3;
    }
    static void Pixel(RCKTexture &texture) {
        ASSERT_TRUE(texture.CreateImage(1, 1, 32, 0));
        CKBYTE *pixels = texture.LockSurfacePtr(0); ASSERT_NE(nullptr, pixels);
        CKDWORD pixel = 0xFF234567; memcpy(pixels, &pixel, 4); texture.ReleaseSurfacePtr(0);
        texture.SetDesiredVideoFormat(_32_ARGB8888);
    }
    void Mono(OwnershipMesh &mesh, RCKMaterial &channel, RCKTexture &texture) {
        mesh.SetFlags(mesh.GetFlags() & ~VXMESH_PRELITMODE);
        Pixel(texture); channel.SetTexture(0, &texture);
        const int index = mesh.AddChannel(&channel, FALSE); ASSERT_GE(index, 0);
        mesh.SetChannelSourceBlend(index, VXBLEND_ZERO); mesh.SetChannelDestBlend(index, VXBLEND_SRCCOLOR);
        mesh.ActivateChannel(index, TRUE); mesh.Select(index);
    }
    static void Owned(const OwnershipDrawObserver::Draw &draw, int marker) {
        EXPECT_EQ(100u + marker, draw.Textures[0]); EXPECT_EQ(200u + marker, draw.Textures[1]);
        EXPECT_EQ(2u, draw.Coordinates[0]); EXPECT_EQ(3u, draw.Coordinates[1]);
        EXPECT_EQ(CKRST_TOP_ADD, draw.Stage0Op); EXPECT_EQ(0x65u, draw.Stage1Blend);
    }
    static void Normal(const OwnershipDrawObserver::Draw &draw, CKDWORD texture) {
        EXPECT_EQ(0u, draw.Textures[0]); EXPECT_NE(0u, texture); EXPECT_EQ(texture, draw.Textures[1]);
        EXPECT_EQ(CKRSTPackTexcoordIndex(1, CKRST_TEXGEN_PASSTHRU), draw.Coordinates[1]);
        EXPECT_EQ(STAGEBLEND(VXBLEND_ZERO, VXBLEND_SRCCOLOR), draw.Stage1Blend);
    }
    void Effect(RCKMaterial &material, OwnershipEffectData &effect) {
        VxEffectDescription description; description.Summary = "Mesh ownership callback";
        description.SetCallback = OwnershipEffect; description.CallbackArg = &effect;
        material.SetEffect(static_cast<VX_EFFECT>(Manager->AddEffect(description)));
    }
};
}

TEST_F(MeshMaterialOwnershipTest, TrueCallbackOwnsPrimaryAndMonoStagesWithoutChannelUpload) {
    OwnershipMesh mesh(Context); RCKMaterial material(Context), channel(Context); RCKTexture texture(Context); Mono(mesh, channel, texture);
    CKMaterialGroup group(&material); Primitive(group); OwnershipCallbackData callback; material.SetCallback(OwnershipCallback, &callback);
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(1, State.DrawCount);
    Owned(State.Draws[0], 17); EXPECT_EQ(1, callback.Calls); EXPECT_EQ(0, State.Loads); EXPECT_EQ(0, State.MaterialWrites);
}
TEST_F(MeshMaterialOwnershipTest, FalseCallbackRunsDefaultMaterialAndRestoresMonoBinding) {
    OwnershipMesh mesh(Context); RCKMaterial material(Context), channel(Context); RCKTexture texture(Context); Mono(mesh, channel, texture);
    CKMaterialGroup group(&material); Primitive(group); OwnershipCallbackData callback; callback.Result = FALSE; material.SetCallback(OwnershipCallback, &callback);
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(1, State.DrawCount);
    Normal(State.Draws[0], texture.GetRstTextureIndex()); EXPECT_EQ(1, callback.Calls); EXPECT_EQ(1, State.Loads); EXPECT_EQ(1, State.MaterialWrites);
}
TEST_F(MeshMaterialOwnershipTest, SameCallbackCanChangeOwnershipBetweenConsecutiveDraws) {
    OwnershipMesh mesh(Context); RCKMaterial material(Context), channel(Context); RCKTexture texture(Context); Mono(mesh, channel, texture);
    CKMaterialGroup group(&material); Primitive(group); OwnershipCallbackData callback; material.SetCallback(OwnershipCallback, &callback);
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); callback.Result = FALSE;
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(2, State.DrawCount);
    Owned(State.Draws[0], 17); Normal(State.Draws[1], texture.GetRstTextureIndex());
    EXPECT_EQ(2, callback.Calls); EXPECT_EQ(1, State.Loads); EXPECT_EQ(1, State.MaterialWrites);
}
TEST_F(MeshMaterialOwnershipTest, PreSubmeshCallbackOwnsStagesAndDoesNotInvokeMaterialCallback) {
    OwnershipMesh mesh(Context); RCKMaterial material(Context), channel(Context); RCKTexture texture(Context); Mono(mesh, channel, texture);
    CKMaterialGroup group(&material); Primitive(group); OwnershipCallbackData callback; material.SetCallback(OwnershipCallback, &callback);
    ASSERT_TRUE(mesh.AddSubMeshPreRenderCallBack(OwnershipSubMeshCallback, nullptr, FALSE));
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(1, State.DrawCount);
    Owned(State.Draws[0], 43); EXPECT_EQ(0, callback.Calls); EXPECT_EQ(0, State.Loads);
}
TEST_F(MeshMaterialOwnershipTest, EffectSkipAllOwnsStagesAndRenderMaterialState) {
    OwnershipMesh mesh(Context); RCKMaterial material(Context), channel(Context); RCKTexture texture(Context); Mono(mesh, channel, texture);
    CKMaterialGroup group(&material); Primitive(group); OwnershipEffectData effect; Effect(material, effect);
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(1, State.DrawCount);
    Owned(State.Draws[0], 29); EXPECT_EQ(1, effect.Calls); EXPECT_EQ(0, State.Loads); EXPECT_EQ(0, State.MaterialWrites);
}
TEST_F(MeshMaterialOwnershipTest, EffectSkipAllTexturesOwnsStagesWhileNormalMaterialStateApplies) {
    OwnershipMesh mesh(Context); RCKMaterial material(Context), channel(Context); RCKTexture texture(Context); Mono(mesh, channel, texture);
    CKMaterialGroup group(&material); Primitive(group); OwnershipEffectData effect; effect.Result = VXEFFECTRETVAL_SKIPALLTEX; Effect(material, effect);
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(1, State.DrawCount);
    Owned(State.Draws[0], 29); EXPECT_EQ(1, effect.Calls); EXPECT_EQ(0, State.Loads); EXPECT_EQ(1, State.MaterialWrites);
}
TEST_F(MeshMaterialOwnershipTest, EffectNormalFallbackRestoresMonoAfterCallbackStageWrites) {
    OwnershipMesh mesh(Context); RCKMaterial material(Context), channel(Context); RCKTexture texture(Context); Mono(mesh, channel, texture);
    CKMaterialGroup group(&material); Primitive(group); OwnershipEffectData effect; effect.Result = VXEFFECTRETVAL_SKIPNONE; Effect(material, effect);
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(1, State.DrawCount);
    Normal(State.Draws[0], texture.GetRstTextureIndex()); EXPECT_EQ(1, effect.Calls); EXPECT_EQ(1, State.MaterialWrites);
}
TEST_F(MeshMaterialOwnershipTest, EffectOwnedPrimaryMatrixDoesNotSuppressNormalMonoRestoration) {
    OwnershipMesh mesh(Context); RCKMaterial material(Context), channel(Context); RCKTexture texture(Context); Mono(mesh, channel, texture);
    material.SetTexture(0, &texture); CKMaterialGroup group(&material); Primitive(group);
    OwnershipEffectData effect; effect.Result = VXEFFECTRETVAL_SKIPTEXMAT; Effect(material, effect);
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(1, State.DrawCount);
    EXPECT_EQ(1, effect.Calls); EXPECT_EQ(1, State.Loads);
    EXPECT_EQ(texture.GetRstTextureIndex(), State.Draws[0].Textures[0]);
    EXPECT_EQ(texture.GetRstTextureIndex(), State.Draws[0].Textures[1]);
    EXPECT_EQ(CKRSTPackTexcoordIndex(0, CKRST_TEXGEN_CAMERASPACENORMAL), State.Draws[0].Coordinates[0]);
    EXPECT_EQ(CKRSTPackTexcoordIndex(1, CKRST_TEXGEN_PASSTHRU), State.Draws[0].Coordinates[1]);
    EXPECT_FLOAT_EQ(3.25f, State.Matrices[VXMATRIX_TEXTURE0][3][0]);
    EXPECT_EQ(CKRST_TTF_COUNT2, State.StageStates[0][CKRST_TSS_TEXTURETRANSFORMFLAGS]);
}
TEST_F(MeshMaterialOwnershipTest, UnknownVirtualOverrideKeepsItsOwnedStagesAndVirtualDispatch) {
    OwnershipMesh mesh(Context); VirtualOwnershipMaterial material(Context); RCKMaterial channel(Context); RCKTexture texture(Context); Mono(mesh, channel, texture);
    CKMaterialGroup group(&material); Primitive(group); ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(1, State.DrawCount);
    Owned(State.Draws[0], 53); EXPECT_EQ(1, material.Calls); EXPECT_EQ(0, State.Loads); EXPECT_EQ(0, State.MaterialWrites);
}
TEST_F(MeshMaterialOwnershipTest, DelegatingVirtualOverrideReportsNormalBindingThroughBaseCall) {
    OwnershipMesh mesh(Context); VirtualOwnershipMaterial material(Context); material.Delegate = TRUE;
    RCKMaterial channel(Context); RCKTexture texture(Context); Mono(mesh, channel, texture);
    CKMaterialGroup group(&material); Primitive(group); ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(1, State.DrawCount);
    Normal(State.Draws[0], texture.GetRstTextureIndex()); EXPECT_EQ(1, material.Calls); EXPECT_EQ(1, State.MaterialWrites);
}
TEST_F(MeshMaterialOwnershipTest, NestedDirectSameMaterialFallbackCannotClaimOuterOwnedBinding) {
    OwnershipMesh mesh(Context); RCKMaterial material(Context), channel(Context); RCKTexture texture(Context); Mono(mesh, channel, texture);
    CKMaterialGroup group(&material); Primitive(group); OwnershipCallbackData callback;
    callback.Direct = TRUE; callback.NestedMaterial = &material; callback.NestedContext = Device; material.SetCallback(OwnershipCallback, &callback);
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(1, State.DrawCount);
    Owned(State.Draws[0], 17); EXPECT_EQ(2, callback.Calls); EXPECT_EQ(0, State.Loads); EXPECT_EQ(1, State.MaterialWrites);
}
TEST_F(MeshMaterialOwnershipTest, NestedSameMaterialOwnedCallCannotSuppressOuterFalseFallback) {
    OwnershipMesh mesh(Context); RCKMaterial material(Context), channel(Context); RCKTexture texture(Context); Mono(mesh, channel, texture);
    CKMaterialGroup group(&material); Primitive(group); OwnershipCallbackData callback; callback.Result = FALSE; callback.NestedResult = TRUE;
    callback.Direct = TRUE; callback.NestedMaterial = &material; callback.NestedContext = Device; material.SetCallback(OwnershipCallback, &callback);
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(1, State.DrawCount);
    Normal(State.Draws[0], texture.GetRstTextureIndex()); EXPECT_EQ(2, callback.Calls); EXPECT_EQ(1, State.Loads);
}
TEST_F(MeshMaterialOwnershipTest, NestedSameMaterialRenderGroupHasIndependentOwnership) {
    OwnershipMesh mesh(Context); RCKMaterial material(Context), channel(Context); RCKTexture texture(Context); Mono(mesh, channel, texture);
    CKMaterialGroup group(&material); Primitive(group); OwnershipCallbackData callback;
    callback.NestedMaterial = &material; callback.NestedMesh = &mesh; callback.NestedGroup = &group;
    callback.NestedContext = Device; callback.NestedData = &Data; material.SetCallback(OwnershipCallback, &callback);
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(2, State.DrawCount);
    Normal(State.Draws[0], texture.GetRstTextureIndex()); Owned(State.Draws[1], 17); EXPECT_EQ(2, callback.Calls);
}
TEST_F(MeshMaterialOwnershipTest, NestedDifferentMaterialCannotLeakItsNormalOutcomeIntoOuterOwner) {
    OwnershipMesh mesh(Context); RCKMaterial material(Context), other(Context), channel(Context); RCKTexture texture(Context); Mono(mesh, channel, texture);
    CKMaterialGroup group(&material); Primitive(group); OwnershipCallbackData callback;
    callback.Direct = TRUE; callback.NestedMaterial = &other; callback.NestedContext = Device; material.SetCallback(OwnershipCallback, &callback);
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(1, State.DrawCount);
    Owned(State.Draws[0], 17); EXPECT_EQ(1, callback.Calls); EXPECT_EQ(0, State.Loads); EXPECT_EQ(1, State.MaterialWrites);
}
TEST_F(MeshMaterialOwnershipTest, NestedSameMaterialInOtherContextKeepsBothContextsSeparate) {
    OwnershipMesh mesh(Context); RCKMaterial material(Context), channel(Context); RCKTexture texture(Context); Mono(mesh, channel, texture);
    CKMaterialGroup group(&material); Primitive(group); OwnershipCallbackData callback;
    callback.Direct = TRUE; callback.NestedMaterial = &material; callback.NestedContext = OtherDevice; material.SetCallback(OwnershipCallback, &callback);
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(1, State.DrawCount);
    Owned(State.Draws[0], 17); EXPECT_EQ(2, callback.Calls); EXPECT_EQ(0, State.Loads); EXPECT_EQ(0, State.MaterialWrites);
    EXPECT_EQ(1, OtherState.MaterialWrites); EXPECT_EQ(0u, OtherState.Textures[0]); EXPECT_EQ(0u, OtherState.Textures[1]);
}
TEST_F(MeshMaterialOwnershipTest, NestedRenderGroupInOtherContextHasIndependentNormalMonoDraw) {
    OwnershipMesh mesh(Context); RCKMaterial material(Context), channel(Context); RCKTexture texture(Context); Mono(mesh, channel, texture);
    CKMaterialGroup group(&material); Primitive(group); OwnershipCallbackData callback;
    callback.NestedMaterial = &material; callback.NestedMesh = &mesh; callback.NestedGroup = &group;
    callback.NestedContext = OtherDevice; callback.NestedData = &Data; material.SetCallback(OwnershipCallback, &callback);
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(1, State.DrawCount); ASSERT_EQ(1, OtherState.DrawCount);
    Owned(State.Draws[0], 17); EXPECT_EQ(1, OtherState.Loads); EXPECT_NE(0u, OtherState.Draws[0].Textures[1]);
    EXPECT_EQ(STAGEBLEND(VXBLEND_ZERO, VXBLEND_SRCCOLOR), OtherState.Draws[0].Stage1Blend); EXPECT_EQ(0, State.Loads);
}
TEST_F(MeshMaterialOwnershipTest, EffectOwnedTexturesSurviveNestedDifferentMaterialBinding) {
    OwnershipMesh mesh(Context); RCKMaterial material(Context), other(Context), channel(Context); RCKTexture texture(Context); Mono(mesh, channel, texture);
    CKMaterialGroup group(&material); Primitive(group); OwnershipEffectData effect; effect.Result = VXEFFECTRETVAL_SKIPALLTEX;
    effect.NestedMaterial = &other; effect.NestedContext = Device; Effect(material, effect);
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(1, State.DrawCount);
    Owned(State.Draws[0], 29); EXPECT_EQ(1, effect.Calls); EXPECT_EQ(2, State.MaterialWrites); EXPECT_EQ(0, State.Loads);
}
TEST_F(MeshMaterialOwnershipTest, ThrowingNestedGroupRestoresOuterOwnershipAndLaterFallback) {
    OwnershipMesh mesh(Context); RCKMaterial material(Context), channel(Context); RCKTexture texture(Context); Mono(mesh, channel, texture);
    CKMaterialGroup group(&material); Primitive(group); OwnershipCallbackData callback; callback.ThrowNested = TRUE;
    callback.NestedMaterial = &material; callback.NestedMesh = &mesh; callback.NestedGroup = &group;
    callback.NestedContext = Device; callback.NestedData = &Data; material.SetCallback(OwnershipCallback, &callback);
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_TRUE(callback.Caught); ASSERT_EQ(1, State.DrawCount);
    Owned(State.Draws[0], 17); EXPECT_EQ(2, callback.Calls);
    material.SetCallback(nullptr, nullptr);
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(2, State.DrawCount);
    Normal(State.Draws[1], texture.GetRstTextureIndex()); EXPECT_EQ(1, State.Loads);
}
TEST_F(MeshMaterialOwnershipTest, ThrowingOuterCallbackDoesNotPoisonLaterNormalRendering) {
    OwnershipMesh mesh(Context); RCKMaterial material(Context), channel(Context); RCKTexture texture(Context); Mono(mesh, channel, texture);
    CKMaterialGroup group(&material); Primitive(group); OwnershipCallbackData callback; callback.ThrowRoot = TRUE; material.SetCallback(OwnershipCallback, &callback);
    EXPECT_THROW(mesh.RenderGroup(Device, &group, nullptr, &Data), int); EXPECT_EQ(0, State.DrawCount);
    material.SetCallback(nullptr, nullptr);
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(1, State.DrawCount);
    Normal(State.Draws[0], texture.GetRstTextureIndex()); EXPECT_EQ(1, State.Loads); EXPECT_EQ(1, State.MaterialWrites);
}
