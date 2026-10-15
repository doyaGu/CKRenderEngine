#include <gtest/gtest.h>
#include <cstring>
#include "CKAll.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "RCK3dEntity.h"
#include "TextureTestObserver.h"

namespace {
struct SubmissionTrace { int Count = 0; int Values[32] = {}; };
class MeshGroupObserver : public TextureTestObserver {
public:
    struct Draw {
        int Path = 0;
        VXPRIMITIVETYPE Type = VX_TRIANGLELIST;
        CKDWORD Cull = 0, Source = 0, Dest = 0, Blend = 0;
        CKDWORD ZFunc = 0, ZWrite = 0;
        CKDWORD VB = 0, IB = 0, Base = 0, Vertices = 0, Offset = 0;
        int Count = 0;
        CKWORD Indices[12] = {};
        CKDWORD Stage1Texture = 0, Stage1Coordinates = 0, Stage1Blend = 0;
        VxVector FirstPosition;
        Vx2DVector ExtraUV;
    };
    Draw Draws[16]; int DrawCount = 0;
    int FailDraw = -1;
    SubmissionTrace *Trace = nullptr;
    Draw &Capture(int path, VXPRIMITIVETYPE type, CKWORD *indices, int count) {
        EXPECT_LT(DrawCount, 16); EXPECT_LE(count, 12);
        Draw &draw = Draws[DrawCount++];
        draw.Path = path; draw.Type = type; draw.Count = count;
        draw.Cull = States[VXRENDERSTATE_CULLMODE];
        draw.Source = States[VXRENDERSTATE_SRCBLEND]; draw.Dest = States[VXRENDERSTATE_DESTBLEND];
        draw.Blend = States[VXRENDERSTATE_ALPHABLENDENABLE];
        draw.ZFunc = States[VXRENDERSTATE_ZFUNC]; draw.ZWrite = States[VXRENDERSTATE_ZWRITEENABLE];
        draw.Stage1Texture = Textures[1];
        draw.Stage1Coordinates = StageStates[1][CKRST_TSS_TEXCOORDINDEX];
        draw.Stage1Blend = StageStates[1][CKRST_TSS_STAGEBLEND];
        for (int i = 0; indices && i < count && i < 12; ++i) draw.Indices[i] = indices[i];
        if (Trace) { EXPECT_LT(Trace->Count, 32); Trace->Values[Trace->Count++] = 50; }
        return draw;
    }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        const int call = DrawCount; Draw &draw = Capture(0, type, indices, count);
        EXPECT_NE(nullptr, data);
        if (data) {
            draw.Vertices = data->VertexCount;
            if (data->PositionPtr) draw.FirstPosition = *static_cast<VxVector *>(data->PositionPtr);
            if (data->TexCoordPtrs[0]) draw.ExtraUV = *static_cast<Vx2DVector *>(data->TexCoordPtrs[0]);
        }
        return call != FailDraw;
    }
    CKBOOL DrawPrimitiveVB(VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD base, CKDWORD vertices,
                           CKWORD *indices, int count) override {
        const int call = DrawCount; Draw &draw = Capture(1, type, indices, count);
        draw.VB = vb; draw.Base = base; draw.Vertices = vertices;
        return call != FailDraw;
    }
    CKBOOL DrawPrimitiveVBIB(VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib, CKDWORD base,
                             CKDWORD vertices, CKDWORD offset, int count) override {
        const int call = DrawCount; Draw &draw = Capture(2, type, nullptr, count);
        draw.VB = vb; draw.IB = ib; draw.Base = base; draw.Vertices = vertices; draw.Offset = offset;
        return call != FailDraw;
    }
};
class SubmissionMesh : public RCKMesh {
public:
    explicit SubmissionMesh(CKContext *context) : RCKMesh(context) {}
    void SelectChannel(int index) { m_ActiveTextureChannels.PushBack(index); }
    void ChannelsInactive(int index) { m_MaterialChannels[index].m_Flags &= ~VXCHANNEL_ACTIVE; }
    void Hardware(CKDWORD vb, CKDWORD ib, CKBOOL wrapped = FALSE, CKDWORD count = 0) {
        m_VertexBuffer = vb; m_IndexBuffer = ib;
        m_VertexBufferWrapAware = wrapped; m_VertexBufferVertexCount = count;
    }
};
struct GroupCallbackData {
    SubmissionTrace *Trace;
    int Value;
    CKBOOL OwnStage = FALSE;
};
void GroupCallback(CKRenderContext *context, CK3dEntity *, CKMesh *, CKMaterial *, void *argument) {
    GroupCallbackData *data = static_cast<GroupCallbackData *>(argument);
    EXPECT_LT(data->Trace->Count, 32); data->Trace->Values[data->Trace->Count++] = data->Value;
    if (data->OwnStage) {
        RCKRenderContext *device = static_cast<RCKRenderContext *>(context);
        device->m_RasterizerContext->SetTexture(47, 1);
        device->m_RasterizerContext->SetTextureStageState(1, CKRST_TSS_TEXCOORDINDEX, 3);
        device->m_RasterizerContext->SetTextureStageState(1, CKRST_TSS_STAGEBLEND, 0x65);
    }
}
class MeshGroupSubmissionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    MeshGroupObserver State;
    CKRasterizerDriver Driver;
    VxVertex Vertices[3]; VxColors Colors[3];
    VxDrawPrimitiveData Data = {};
    SubmissionTrace Trace;
    GroupCallbackData Pre = {&Trace, 10}, Post = {&Trace, 30};
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
        Manager->m_Drivers[0].CapsUpToDate = TRUE;
        Manager->m_Drivers[0].RasterizerDriver = &Driver;
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 3;
        VxImageDescEx alpha; VxPixelFormat2ImageDesc(_32_ARGB8888, alpha);
        Manager->m_Drivers[0].TextureFormats.PushBack(alpha);
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0));
        Device = new RCKRenderContext(Context); Device->DetachAll();
        Device->m_RasterizerContext = &State; Device->m_RasterizerDriver = &Driver; Device->m_DriverIndex = 0;
        Vertices[0].m_Position = VxVector(2, 4, 6);
        Vertices[1].m_Position = VxVector(8, 10, 12);
        Vertices[2].m_Position = VxVector(14, 16, 18);
        for (int i = 0; i < 3; ++i) {
            Vertices[i].m_Normal = VxVector(0, 0, 1);
            Vertices[i].m_UV = Vx2DVector(i * .25f, i * .5f);
            Colors[i].Color = 0xFF112233; Colors[i].Specular = 0;
        }
        Data.VertexCount = 3; Data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_STAGES0;
        Data.PositionPtr = &Vertices[0].m_Position; Data.PositionStride = sizeof(VxVertex);
        Data.NormalPtr = &Vertices[0].m_Normal; Data.NormalStride = sizeof(VxVertex);
        Data.TexCoordPtr = &Vertices[0].m_UV; Data.TexCoordStride = sizeof(VxVertex);
        Data.ColorPtr = &Colors[0].Color; Data.ColorStride = sizeof(VxColors);
        Data.SpecularColorPtr = &Colors[0].Specular; Data.SpecularColorStride = sizeof(VxColors);
        State.Trace = &Trace;
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; delete Device; }
        if (Context) CKCloseContext(Context); CKShutdown();
    }
    static void Primitive(CKMaterialGroup &group, int slot = 0, VXPRIMITIVETYPE type = VX_TRIANGLELIST) {
        if (group.m_Primitives.Size() <= slot) group.m_Primitives.Resize(slot + 1);
        CKPrimitiveEntry &primitive = group.m_Primitives[slot];
        primitive.m_Type = type; primitive.m_IndexBufferOffset = -1;
        primitive.m_Indices.PushBack(2); primitive.m_Indices.PushBack(0); primitive.m_Indices.PushBack(1);
        group.m_VertexCount = 3;
    }
    static void Transparent(RCKMaterial &material) {
        material.SetFillMode(VXFILL_SOLID); material.SetTwoSided(TRUE);
        material.EnableAlphaBlend(TRUE); material.SetSourceBlend(VXBLEND_SRCALPHA);
        material.SetDestBlend(VXBLEND_INVSRCALPHA);
    }
    static void Pixel(RCKTexture &texture, CKDWORD color = 0xFF234567) {
        ASSERT_TRUE(texture.CreateImage(1, 1, 32, 0));
        CKBYTE *pixels = texture.LockSurfacePtr(0); ASSERT_NE(nullptr, pixels);
        memcpy(pixels, &color, 4); texture.ReleaseSurfacePtr(0);
        texture.SetDesiredVideoFormat(_32_ARGB8888);
    }
    int Channel(SubmissionMesh &mesh, RCKMaterial &material, VXBLEND_MODE source = VXBLEND_ZERO,
                VXBLEND_MODE destination = VXBLEND_SRCCOLOR) {
        const int index = mesh.AddChannel(&material, FALSE); EXPECT_GE(index, 0);
        mesh.SetChannelSourceBlend(index, source); mesh.SetChannelDestBlend(index, destination);
        mesh.ActivateChannel(index, TRUE); mesh.SelectChannel(index); return index;
    }
    void Order(const int *values, int count) {
        ASSERT_EQ(count, Trace.Count);
        for (int i = 0; i < count; ++i) EXPECT_EQ(values[i], Trace.Values[i]);
    }
};
}

TEST_F(MeshGroupSubmissionTest, SoftwareSubmissionPreservesPrimitiveTypeAndExplicitIndices) {
    SubmissionMesh mesh(Context); RCKMaterial material(Context); CKMaterialGroup group(&material); Primitive(group);
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(1, State.DrawCount);
    EXPECT_EQ(0, State.Draws[0].Path); EXPECT_EQ(VX_TRIANGLELIST, State.Draws[0].Type);
    EXPECT_EQ(2, State.Draws[0].Indices[0]); EXPECT_EQ(0, State.Draws[0].Indices[1]); EXPECT_EQ(1, State.Draws[0].Indices[2]);
    EXPECT_EQ(3u, State.Draws[0].Vertices); EXPECT_FLOAT_EQ(2, State.Draws[0].FirstPosition.x);
}
TEST_F(MeshGroupSubmissionTest, EmptyPrimitiveDoesNotDrawAndRemainingEntriesKeepOrder) {
    SubmissionMesh mesh(Context); RCKMaterial material(Context); CKMaterialGroup group(&material);
    group.m_Primitives.Resize(3); group.m_Primitives[1].m_Type = VX_TRIANGLELIST;
    Primitive(group, 0, VX_TRIANGLESTRIP); Primitive(group, 2, VX_TRIANGLELIST);
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(2, State.DrawCount);
    EXPECT_EQ(VX_TRIANGLESTRIP, State.Draws[0].Type); EXPECT_EQ(VX_TRIANGLELIST, State.Draws[1].Type);
}
TEST_F(MeshGroupSubmissionTest, SolidTwoSidedTransparentDrawsBackFacesBeforeFrontFaces) {
    SubmissionMesh mesh(Context); RCKMaterial material(Context); Transparent(material); CKMaterialGroup group(&material); Primitive(group);
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(2, State.DrawCount);
    EXPECT_EQ(VXCULL_CW, State.Draws[0].Cull); EXPECT_EQ(VXCULL_CCW, State.Draws[1].Cull);
    EXPECT_EQ(VXCULL_CCW, State.States[VXRENDERSTATE_CULLMODE]);
    EXPECT_EQ(VXBLEND_SRCALPHA, State.Draws[0].Source); EXPECT_EQ(VXBLEND_INVSRCALPHA, State.Draws[1].Dest);
}
TEST_F(MeshGroupSubmissionTest, TwoSidedOpaqueMaterialUsesOneUnculledPass) {
    SubmissionMesh mesh(Context); RCKMaterial material(Context); material.SetTwoSided(TRUE); CKMaterialGroup group(&material); Primitive(group);
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(1, State.DrawCount);
    EXPECT_EQ(VXCULL_NONE, State.Draws[0].Cull);
}
TEST_F(MeshGroupSubmissionTest, OneSidedTransparentMaterialUsesOneFrontPass) {
    SubmissionMesh mesh(Context); RCKMaterial material(Context); Transparent(material); material.SetTwoSided(FALSE);
    CKMaterialGroup group(&material); Primitive(group); ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data));
    ASSERT_EQ(1, State.DrawCount); EXPECT_EQ(VXCULL_CCW, State.Draws[0].Cull);
}
TEST_F(MeshGroupSubmissionTest, WireframeTwoSidedTransparentMaterialUsesOnePass) {
    SubmissionMesh mesh(Context); RCKMaterial material(Context); Transparent(material); material.SetFillMode(VXFILL_WIREFRAME);
    CKMaterialGroup group(&material); Primitive(group); ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data));
    ASSERT_EQ(1, State.DrawCount); EXPECT_EQ(VXCULL_NONE, State.Draws[0].Cull);
}
TEST_F(MeshGroupSubmissionTest, CallbacksSurroundBothTransparentPassesOnce) {
    SubmissionMesh mesh(Context); RCKMaterial material(Context); Transparent(material); CKMaterialGroup group(&material); Primitive(group);
    ASSERT_TRUE(mesh.AddSubMeshPreRenderCallBack(GroupCallback, &Pre, FALSE));
    ASSERT_TRUE(mesh.AddSubMeshPostRenderCallBack(GroupCallback, &Post, FALSE));
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data));
    const int expected[] = {10, 50, 50, 30}; Order(expected, 4);
}
TEST_F(MeshGroupSubmissionTest, EveryBackFacePrimitivePrecedesEveryFrontFacePrimitive) {
    SubmissionMesh mesh(Context); RCKMaterial material(Context); Transparent(material); CKMaterialGroup group(&material);
    Primitive(group, 0, VX_TRIANGLESTRIP); Primitive(group, 1, VX_TRIANGLELIST);
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(4, State.DrawCount);
    EXPECT_EQ(VXCULL_CW, State.Draws[0].Cull); EXPECT_EQ(VXCULL_CW, State.Draws[1].Cull);
    EXPECT_EQ(VXCULL_CCW, State.Draws[2].Cull); EXPECT_EQ(VXCULL_CCW, State.Draws[3].Cull);
    EXPECT_EQ(VX_TRIANGLESTRIP, State.Draws[0].Type); EXPECT_EQ(VX_TRIANGLELIST, State.Draws[1].Type);
    EXPECT_EQ(VX_TRIANGLESTRIP, State.Draws[2].Type); EXPECT_EQ(VX_TRIANGLELIST, State.Draws[3].Type);
}
TEST_F(MeshGroupSubmissionTest, FailedBackDrawStillAttemptsFrontAndInvokesPostCallback) {
    SubmissionMesh mesh(Context); RCKMaterial material(Context); Transparent(material); CKMaterialGroup group(&material); Primitive(group);
    State.FailDraw = 0; ASSERT_TRUE(mesh.AddSubMeshPostRenderCallBack(GroupCallback, &Post, FALSE));
    EXPECT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data));
    const int expected[] = {50, 50, 30}; Order(expected, 3); EXPECT_EQ(2, State.DrawCount);
}
TEST_F(MeshGroupSubmissionTest, HardwarePrimitiveUsesGroupBaseAndCpuIndicesWithoutIndexBuffer) {
    SubmissionMesh mesh(Context); mesh.Hardware(71, 73); RCKMaterial material(Context); CKMaterialGroup group(&material); Primitive(group);
    group.m_BaseVertex = 4; ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, nullptr)); ASSERT_EQ(1, State.DrawCount);
    EXPECT_EQ(1, State.Draws[0].Path); EXPECT_EQ(71u, State.Draws[0].VB); EXPECT_EQ(4u, State.Draws[0].Base);
    EXPECT_EQ(3u, State.Draws[0].Vertices); EXPECT_EQ(2, State.Draws[0].Indices[0]);
}
TEST_F(MeshGroupSubmissionTest, HardwareIndexBufferSubmissionPreservesItsAbsoluteOffset) {
    SubmissionMesh mesh(Context); mesh.Hardware(71, 73); RCKMaterial material(Context); CKMaterialGroup group(&material); Primitive(group);
    group.m_BaseVertex = 4; group.m_Primitives[0].m_IndexBufferOffset = 9;
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, nullptr)); ASSERT_EQ(1, State.DrawCount);
    EXPECT_EQ(2, State.Draws[0].Path); EXPECT_EQ(71u, State.Draws[0].VB); EXPECT_EQ(73u, State.Draws[0].IB);
    EXPECT_EQ(4u, State.Draws[0].Base); EXPECT_EQ(9u, State.Draws[0].Offset); EXPECT_EQ(3, State.Draws[0].Count);
}
TEST_F(MeshGroupSubmissionTest, TransparentHardwareBackPassUsesCpuIndicesAndFrontPassUsesIndexBuffer) {
    SubmissionMesh mesh(Context); mesh.Hardware(71, 73); RCKMaterial material(Context); Transparent(material);
    CKMaterialGroup group(&material); Primitive(group); group.m_BaseVertex = 4; group.m_Primitives[0].m_IndexBufferOffset = 9;
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, nullptr)); ASSERT_EQ(2, State.DrawCount);
    EXPECT_EQ(1, State.Draws[0].Path); EXPECT_EQ(2, State.Draws[1].Path);
    EXPECT_EQ(VXCULL_CW, State.Draws[0].Cull); EXPECT_EQ(VXCULL_CCW, State.Draws[1].Cull);
    EXPECT_EQ(4u, State.Draws[0].Base); EXPECT_EQ(9u, State.Draws[1].Offset);
}
TEST_F(MeshGroupSubmissionTest, WrapAwareHardwareBufferUsesFullExpandedVertexRange) {
    SubmissionMesh mesh(Context); mesh.Hardware(71, 73, TRUE, 12); RCKMaterial material(Context); CKMaterialGroup group(&material); Primitive(group);
    group.m_BaseVertex = 4; ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, nullptr)); ASSERT_EQ(1, State.DrawCount);
    EXPECT_EQ(0u, State.Draws[0].Base); EXPECT_EQ(12u, State.Draws[0].Vertices);
}
TEST_F(MeshGroupSubmissionTest, FailedHardwareDrawStillInvokesPostCallbackAndKeepsNativeReturn) {
    SubmissionMesh mesh(Context); mesh.Hardware(71, 73); RCKMaterial material(Context); CKMaterialGroup group(&material); Primitive(group);
    State.FailDraw = 0; ASSERT_TRUE(mesh.AddSubMeshPostRenderCallBack(GroupCallback, &Post, FALSE));
    EXPECT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, nullptr));
    const int expected[] = {50, 30}; Order(expected, 2);
}
TEST_F(MeshGroupSubmissionTest, NullMaterialPrelitForcedTransparencyUsesVertexAlphaBlend) {
    SubmissionMesh mesh(Context); mesh.SetFlags(mesh.GetFlags() | VXMESH_PRELITMODE | VXMESH_FORCETRANSPARENCY);
    CKMaterialGroup group; Primitive(group); ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(1, State.DrawCount);
    EXPECT_EQ(TRUE, State.Draws[0].Blend); EXPECT_EQ(VXBLEND_SRCALPHA, State.Draws[0].Source);
    EXPECT_EQ(VXBLEND_INVSRCALPHA, State.Draws[0].Dest);
}
TEST_F(MeshGroupSubmissionTest, ExplicitOpaqueMaterialIsNotForcedToVertexAlphaBlend) {
    SubmissionMesh mesh(Context); mesh.SetFlags(mesh.GetFlags() | VXMESH_PRELITMODE | VXMESH_FORCETRANSPARENCY);
    RCKMaterial material(Context); CKMaterialGroup group(&material); Primitive(group);
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(1, State.DrawCount);
    EXPECT_EQ(FALSE, State.Draws[0].Blend);
}
TEST_F(MeshGroupSubmissionTest, PreCallbackOwnsMaterialAndMonoStageState) {
    SubmissionMesh mesh(Context); RCKMaterial material(Context), channelMaterial(Context); RCKTexture texture(Context); Pixel(texture);
    channelMaterial.SetTexture(0, &texture); Channel(mesh, channelMaterial);
    CKMaterialGroup group(&material); Primitive(group); Pre.OwnStage = TRUE;
    ASSERT_TRUE(mesh.AddSubMeshPreRenderCallBack(GroupCallback, &Pre, FALSE));
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(1, State.DrawCount);
    EXPECT_EQ(0, State.ApplyWrites); EXPECT_EQ(0, State.Loads);
    EXPECT_EQ(47u, State.Draws[0].Stage1Texture); EXPECT_EQ(3u, State.Draws[0].Stage1Coordinates);
    EXPECT_EQ(0x65u, State.Draws[0].Stage1Blend);
}
TEST_F(MeshGroupSubmissionTest, PostCallbackAloneDoesNotSuppressMaterialOrMonoTextureBinding) {
    SubmissionMesh mesh(Context); RCKMaterial material(Context), channelMaterial(Context); RCKTexture texture(Context); Pixel(texture);
    mesh.SetFlags(mesh.GetFlags() & ~VXMESH_PRELITMODE);
    channelMaterial.SetTexture(0, &texture); Channel(mesh, channelMaterial);
    CKMaterialGroup group(&material); Primitive(group); ASSERT_TRUE(mesh.AddSubMeshPostRenderCallBack(GroupCallback, &Post, FALSE));
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(1, State.DrawCount);
    EXPECT_EQ(1, State.MaterialWrites); EXPECT_EQ(1, State.Loads);
    EXPECT_EQ(texture.GetRstTextureIndex(), State.Draws[0].Stage1Texture);
    const int expected[] = {50, 30}; Order(expected, 2);
}
TEST_F(MeshGroupSubmissionTest, MonoModulateUploadBindsAdditionalStageWithoutChangingPrimaryTexture) {
    SubmissionMesh mesh(Context); RCKMaterial material(Context); RCKTexture texture(Context); Pixel(texture);
    material.SetTexture(0, &texture); Channel(mesh, material); State.Textures[0] = 29;
    mesh.BindMonoPassTextureChannels(Device); ASSERT_EQ(1, State.Loads);
    CKDWORD uploaded; memcpy(&uploaded, State.Uploads[0].Bytes, 4); EXPECT_EQ(0xFF234567u, uploaded);
    EXPECT_EQ(29u, State.Textures[0]); EXPECT_EQ(texture.GetRstTextureIndex(), State.Textures[1]);
    EXPECT_EQ(STAGEBLEND(VXBLEND_ZERO, VXBLEND_SRCCOLOR), State.StageStates[1][CKRST_TSS_STAGEBLEND]);
    EXPECT_EQ(CKRSTPackTexcoordIndex(1, CKRST_TEXGEN_PASSTHRU), State.StageStates[1][CKRST_TSS_TEXCOORDINDEX]);
}
TEST_F(MeshGroupSubmissionTest, SecondModulateBlendAndMaterialSamplerPropertiesReachAdditionalStage) {
    SubmissionMesh mesh(Context); RCKMaterial material(Context); RCKTexture texture(Context); Pixel(texture);
    material.SetTexture(0, &texture); material.SetTextureMinMode(VXTEXTUREFILTER_LINEAR);
    material.SetTextureMagMode(VXTEXTUREFILTER_NEAREST); material.SetTextureBorderColor(0xFFAABBCC);
    material.SetTextureAddressMode(VXTEXTURE_ADDRESSWRAP);
    Channel(mesh, material, VXBLEND_DESTCOLOR, VXBLEND_ZERO); mesh.BindMonoPassTextureChannels(Device);
    EXPECT_EQ(STAGEBLEND(VXBLEND_DESTCOLOR, VXBLEND_ZERO), State.StageStates[1][CKRST_TSS_STAGEBLEND]);
    EXPECT_EQ(VXTEXTUREFILTER_LINEAR, State.StageStates[1][CKRST_TSS_MINFILTER]);
    EXPECT_EQ(VXTEXTUREFILTER_NEAREST, State.StageStates[1][CKRST_TSS_MAGFILTER]);
    EXPECT_EQ(0xFFAABBCCu, State.StageStates[1][CKRST_TSS_BORDERCOLOR]);
}
TEST_F(MeshGroupSubmissionTest, UnsupportedBlendDoesNotBindOrResetAdditionalStage) {
    SubmissionMesh mesh(Context); RCKMaterial material(Context); RCKTexture texture(Context); Pixel(texture);
    material.SetTexture(0, &texture); Channel(mesh, material, VXBLEND_SRCALPHA, VXBLEND_INVSRCALPHA);
    State.Textures[1] = 37; State.StageStates[1][CKRST_TSS_TEXCOORDINDEX] = 5;
    mesh.BindMonoPassTextureChannels(Device);
    EXPECT_EQ(0, State.Loads); EXPECT_EQ(37u, State.Textures[1]); EXPECT_EQ(5u, State.StageStates[1][CKRST_TSS_TEXCOORDINDEX]);
}
TEST_F(MeshGroupSubmissionTest, InactiveAndInvalidSelectedChannelsDoNotTouchPrimaryOrExtraStages) {
    SubmissionMesh mesh(Context); RCKMaterial material(Context); RCKTexture texture(Context); Pixel(texture);
    material.SetTexture(0, &texture); int index = Channel(mesh, material); mesh.ChannelsInactive(index);
    mesh.SelectChannel(-1); mesh.SelectChannel(99); State.Textures[0] = 29; State.Textures[1] = 37;
    mesh.BindMonoPassTextureChannels(Device);
    EXPECT_EQ(0, State.Loads); EXPECT_EQ(29u, State.Textures[0]); EXPECT_EQ(37u, State.Textures[1]);
}
TEST_F(MeshGroupSubmissionTest, EntityDepthOverridesReachSubmissionWithoutChangingMaterialDefinition) {
    SubmissionMesh mesh(Context); RCKMaterial material(Context); CKMaterialGroup group(&material); Primitive(group);
    RCK3dEntity entity(Context); entity.m_MoveableFlags |= VX_MOVEABLE_NOZBUFFERTEST | VX_MOVEABLE_NOZBUFFERWRITE;
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, &entity, &Data)); ASSERT_EQ(1, State.DrawCount);
    EXPECT_EQ(VXCMP_ALWAYS, State.Draws[0].ZFunc); EXPECT_EQ(FALSE, State.Draws[0].ZWrite);
    EXPECT_EQ(VXCMP_LESSEQUAL, material.GetZFunc()); EXPECT_TRUE(material.ZWriteEnabled());
}
TEST_F(MeshGroupSubmissionTest, RemappedGroupSubmitsItsOwnVertexAndSelectedChannelStreams) {
    SubmissionMesh mesh(Context); RCKMaterial material(Context), unusedChannel(Context), selectedChannel(Context);
    mesh.AddChannel(&unusedChannel, FALSE); const int selected = mesh.AddChannel(&selectedChannel, FALSE);
    ASSERT_EQ(1, selected); mesh.SelectChannel(selected);
    CKMaterialGroup group(&material); Primitive(group); group.m_VertexCount = 4;
    CKVBuffer remap(4); remap.m_Vertices[0].m_Position = VxVector(31, 37, 41);
    remap.m_UVs.Resize(2); remap.m_UVs[1].Resize(4); remap.m_UVs[1][0] = Vx2DVector(.375f, .625f);
    group.m_RemapData = reinterpret_cast<CKUINTPTR>(&remap);
    ASSERT_EQ(1, mesh.RenderGroup(Device, &group, nullptr, &Data)); ASSERT_EQ(1, State.DrawCount);
    EXPECT_EQ(4u, State.Draws[0].Vertices); EXPECT_FLOAT_EQ(31, State.Draws[0].FirstPosition.x);
    EXPECT_FLOAT_EQ(.375f, State.Draws[0].ExtraUV.x); EXPECT_FLOAT_EQ(.625f, State.Draws[0].ExtraUV.y);
    EXPECT_EQ(&remap.m_Vertices[0].m_Position, Data.PositionPtr);
    EXPECT_EQ(remap.m_UVs[1].Begin(), Data.TexCoordPtrs[0]);
}
