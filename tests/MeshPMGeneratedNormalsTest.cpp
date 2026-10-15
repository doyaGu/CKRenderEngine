#include <gtest/gtest.h>
#include <cstring>
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "TextureTestObserver.h"

namespace {
const CKDWORD ChannelA = 0xff004411u;
const CKDWORD ChannelB = 0xff006622u;

class GeneratedNormalObserver : public TextureTestObserver {
public:
    struct Draw {
        int VertexCount = 0, Count = 0;
        CKDWORD Flags = 0, Textures[4] = {}, Coordinates[3] = {}, Lighting = 0;
        CKBOOL HasNormal = FALSE;
        CKWORD Indices[12] = {};
        VxVector Positions[12], Normals[12];
        Vx2DVector UVs[12];
        CKDWORD Colors[12] = {}, Speculars[12] = {};
    };
    Draw Draws[16];
    int Count = 0, BindFailures = 0, RejectStage = -1;
    float RejectDiffuse = -1.0f;
    CKDWORD Labels[64] = {};
    CKBOOL SetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE state, CKDWORD value) override {
        const CKBOOL result = TextureTestObserver::SetTextureStageState(stage, state, value);
        if (state == CKRST_TSS_TEXTUREMAPBLEND && value == VXTEXTUREBLEND_MODULATEALPHA) {
            // This fixture uses only the SDK's default legacy blend. The
            // public rasterizer contract exposes its effective MODULATE op;
            // storing the legacy selector alone would falsely disable stage0.
            StageStates[stage][CKRST_TSS_OP] = CKRST_TOP_MODULATE;
        }
        return result;
    }
    CKBOOL LoadTexture(CKDWORD handle, const VxImageDescEx &image, int level,
                       CKRST_CUBEFACE face, const CKRECT *region) override {
        if (handle < 64 && image.Image) std::memcpy(&Labels[handle], image.Image, 4);
        return TextureTestObserver::LoadTexture(handle, image, level, face, region);
    }
    CKBOOL SetTexture(CKDWORD handle, int stage) override {
        const CKBOOL matches = RejectDiffuse < 0.0f ||
            (MaterialWrites > 1 && Material.Diffuse.r == RejectDiffuse);
        if (handle && stage == RejectStage && matches && !BindFailures) {
            ++BindFailures;
            return FALSE;
        }
        return TextureTestObserver::SetTexture(handle, stage);
    }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count,
                         VxDrawPrimitiveData *data) override {
        EXPECT_EQ(VX_TRIANGLELIST, type);
        EXPECT_NE(nullptr, data); EXPECT_LT(Count, 16);
        if (!data || Count >= 16) return FALSE;
        Draw &draw = Draws[Count++];
        draw.VertexCount = data->VertexCount; draw.Count = count; draw.Flags = data->Flags;
        draw.HasNormal = data->NormalPtr != nullptr; draw.Lighting = States[VXRENDERSTATE_LIGHTING];
        for (int stage = 0; stage < 3; ++stage) draw.Coordinates[stage] = StageStates[stage][CKRST_TSS_TEXCOORDINDEX];
        EXPECT_LE(count, 12);
        for (int stage = 0; stage < 4; ++stage)
            draw.Textures[stage] = Textures[stage] < 64 ? Labels[Textures[stage]] : 0;
        for (int i = 0; i < count && i < 12; ++i) {
            draw.Indices[i] = indices[i];
            EXPECT_LT(indices[i], data->VertexCount) << "Index and payload must use the same LOD space";
            if (indices[i] >= data->VertexCount) continue;
            EXPECT_NE(nullptr, data->PositionPtr);
            if (data->PositionPtr)
                draw.Positions[i] = *reinterpret_cast<VxVector *>(static_cast<CKBYTE *>(data->PositionPtr) + indices[i] * data->PositionStride);
            if (data->NormalPtr)
                draw.Normals[i] = *reinterpret_cast<VxVector *>(static_cast<CKBYTE *>(data->NormalPtr) + indices[i] * data->NormalStride);
            if (data->TexCoordPtr)
                draw.UVs[i] = *reinterpret_cast<Vx2DVector *>(static_cast<CKBYTE *>(data->TexCoordPtr) + indices[i] * data->TexCoordStride);
            if (data->ColorPtr)
                draw.Colors[i] = *reinterpret_cast<CKDWORD *>(static_cast<CKBYTE *>(data->ColorPtr) + indices[i] * data->ColorStride);
            if (data->SpecularColorPtr)
                draw.Speculars[i] = *reinterpret_cast<CKDWORD *>(static_cast<CKBYTE *>(data->SpecularColorPtr) + indices[i] * data->SpecularColorStride);
        }
        return TRUE;
    }
};

class GeneratedNormalMesh : public RCKMesh {
public:
    explicit GeneratedNormalMesh(CKContext *context) : RCKMesh(context) {}
    void ConfigurePM(CKBOOL morph = TRUE, int count = 3) {
        if (!m_ProgressiveMesh) m_ProgressiveMesh = new CKProgressiveMesh();
        m_ProgressiveMesh->m_VertexCount = count;
        m_ProgressiveMesh->m_MorphEnabled = morph;
        m_ProgressiveMesh->m_MorphStep = 2;
        const CKDWORD parents[] = {0, 0, 0, 2, 3, 4};
        m_ProgressiveMesh->m_Data.Resize(6);
        for (int i = 0; i < 6; ++i) m_ProgressiveMesh->m_Data[i] = parents[i];
        ModifyObjectFlags(0, CK_OBJECT_UPTODATE);
    }
    using RCKMesh::RenderMonoPassFallback;
    using RCKMesh::GetVBuffer;
    CKMaterialGroup *Group(int index = 0) { return m_MaterialGroups[index]; }
    void Cold() { m_Valid = 0; }
    int RemappedGroups() const {
        int result = 0;
        for (int i = 0; i < m_MaterialGroups.Size(); ++i)
            if (m_MaterialGroups[i]->m_RemapData) ++result;
        return result;
    }
};

class GeneratedNormalsContext : public RCKRenderContext {
public:
    int VirtualCalls = 0;
    explicit GeneratedNormalsContext(CKContext *context) : RCKRenderContext(context) {}
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        ++VirtualCalls;
        return RCKRenderContext::DrawPrimitive(type, indices, count, data);
    }
};

class MeshPMGeneratedNormalsTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    CKRasterizerDriver Driver;
    GeneratedNormalObserver State;
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
        Manager->m_VertexCache.Value = 0;
        Manager->m_Drivers[0].CapsUpToDate = TRUE;
        Manager->m_Drivers[0].RasterizerDriver = &Driver;
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 4;
        VxImageDescEx format; VxPixelFormat2ImageDesc(_32_ARGB8888, format);
        Manager->m_Drivers[0].TextureFormats.PushBack(format);
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0));
        Device = new GeneratedNormalsContext(Context); Device->DetachAll();
        Device->m_RasterizerContext = &State; Device->m_RasterizerDriver = &Driver;
        Device->m_DriverIndex = 0; Device->m_Current3dEntity = nullptr;
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; delete Device; }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    void Geometry(GeneratedNormalMesh &mesh, RCKMaterial &first, RCKMaterial *second = nullptr) {
        mesh.SetVertexCount(6); mesh.SetFaceCount(second ? 2 : 1);
        const VxVector positions[] = {
            VxVector(0,0,0), VxVector(2,0,0), VxVector(0,0,0),
            VxVector(0,8,0), VxVector(12,0,0), VxVector(50,50,0)
        };
        const Vx2DVector uv[] = {
            Vx2DVector(.125f,.25f), Vx2DVector(.25f,.25f), Vx2DVector(.5f,.25f),
            Vx2DVector(.75f,1.0f), Vx2DVector(.875f,.5f), Vx2DVector(1.0f,1.0f)
        };
        for (int i = 0; i < 6; ++i) {
            const VxVector normals[] = { VxVector(.6f,.2f,.4f), VxVector(-.3f,.7f,.2f), VxVector(.1f,-.1f,.8f), VxVector(.2f,.6f,.8f), VxVector(-.4f,.2f,.1f), VxVector(.9f,-.2f,.3f) };
            VxVector normal = normals[i];
            mesh.SetVertexPosition(i, const_cast<VxVector *>(&positions[i]));
            mesh.SetVertexNormal(i, &normal);
            mesh.SetVertexTextureCoordinates(i, uv[i].x, uv[i].y, -1);
            mesh.SetVertexColor(i, 0xff102030u + i);
            mesh.SetVertexSpecularColor(i, 0xff304050u + i);
        }
        mesh.SetFaceVertexIndex(0, 0, 1, 5);
        first.SetDiffuse(VxColor(.25f,1.0f,1.0f,1.0f)); mesh.SetFaceMaterial(0, &first);
        if (second) {
            mesh.SetFaceVertexIndex(1, 1, 0, 5);
            second->SetDiffuse(VxColor(.5f,1.0f,1.0f,1.0f)); mesh.SetFaceMaterial(1, second);
        }
        mesh.SetLitMode(VX_LITMESH);
        mesh.SetFlags((mesh.GetFlags() | VXMESH_RENDERCHANNELS) & ~VXMESH_STRIPIFY);
    }
    int Channel(GeneratedNormalMesh &mesh, RCKMaterial &material, RCKTexture &texture,
                CKDWORD label, CKBOOL sameUV = FALSE) {
        EXPECT_TRUE(texture.CreateImage(1,1,32,0));
        CKBYTE *pixels = texture.LockSurfacePtr(0); EXPECT_NE(nullptr, pixels);
        if (pixels) std::memcpy(pixels, &label, 4);
        texture.ReleaseSurfacePtr(0); material.SetTexture(0, &texture);
        const int channel = mesh.AddChannel(&material, FALSE); EXPECT_GE(channel, 0);
        mesh.SetChannelSourceBlend(channel, VXBLEND_DESTCOLOR);
        mesh.SetChannelDestBlend(channel, VXBLEND_ZERO);
        mesh.SetChannelFlags(channel, VXCHANNEL_ACTIVE | (sameUV ? VXCHANNEL_SAMEUV : 0));
        if (!sameUV) {
            for (int i = 0; i < 6; ++i) mesh.SetVertexTextureCoordinates(i, .5f + i*.125f, .9375f, channel);
        }
        return channel;
    }
    void Build(GeneratedNormalMesh &mesh, CKBOOL morph = TRUE, int count = 3) {
        mesh.ConfigurePM(morph, count); mesh.BuildRenderMesh(); mesh.Cold();
    }
    void Draw(GeneratedNormalMesh &mesh) { mesh.Cold(); ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); }
    void Triangle(int draw, CKBOOL reversed = FALSE, float topY = 4.0f) {
        ASSERT_LT(draw, State.Count);
        const GeneratedNormalObserver::Draw &d = State.Draws[draw];
        ASSERT_EQ(3, d.Count); EXPECT_EQ(4, d.VertexCount);
        EXPECT_EQ(reversed ? 1 : 0, d.Indices[0]); EXPECT_EQ(reversed ? 0 : 1, d.Indices[1]); EXPECT_EQ(3, d.Indices[2]);
        EXPECT_FLOAT_EQ(reversed ? 2.0f : 0.0f, d.Positions[0].x);
        EXPECT_FLOAT_EQ(reversed ? 0.0f : 2.0f, d.Positions[1].x);
        EXPECT_FLOAT_EQ(0.0f, d.Positions[2].x); EXPECT_FLOAT_EQ(topY, d.Positions[2].y);
        EXPECT_FLOAT_EQ(0.0f, d.Positions[2].z);
    }
    void SameUV(int draw) {
        ASSERT_LT(draw, State.Count);
        // LOD 3 lies halfway between levels 2 and 4. At level 2, the
        // third corner follows 5->4->3->2->0; at level 4 it is vertex 3.
        // Main UV = midpoint ((.125,.25)+(.75,1))/2 = (.4375,.625).
        EXPECT_FLOAT_EQ(.4375f, State.Draws[draw].UVs[2].x);
        EXPECT_FLOAT_EQ(.625f, State.Draws[draw].UVs[2].y);
    }
    void PrivateUV(int draw) {
        ASSERT_LT(draw, State.Count);
        // Private channel data belongs to rendered upper-LOD vertex 3,
        // rather than authored vertex 5 or an interpolated main UV.
        EXPECT_FLOAT_EQ(.875f, State.Draws[draw].UVs[2].x);
        EXPECT_FLOAT_EQ(.9375f, State.Draws[draw].UVs[2].y);
    }
    void FallbackStage(int draw, CKDWORD label) {
        ASSERT_LT(draw, State.Count); EXPECT_EQ(label, State.Draws[draw].Textures[0]);
        for (int stage = 1; stage < 4; ++stage) EXPECT_EQ(0u, State.Draws[draw].Textures[stage]);
        EXPECT_NE(0u, State.Draws[draw].Flags & CKRST_DP_TRANSFORM);
    }
    void Pixel(RCKTexture &texture, CKDWORD label) {
        ASSERT_TRUE(texture.CreateImage(1, 1, 32, 0)); CKBYTE *pixels = texture.LockSurfacePtr(0); ASSERT_NE(nullptr, pixels);
        std::memcpy(pixels, &label, 4); texture.ReleaseSurfacePtr(0);
    }
    int BumpChannel(GeneratedNormalMesh &mesh, RCKMaterial &material, RCKTexture &base,
                    RCKTexture &bump, RCKTexture &environment, CKBOOL sameUV = FALSE) {
        const int index = Channel(mesh, material, base, ChannelA, sameUV);
        Pixel(bump, ChannelB); Pixel(environment, 0xff778899u);
        material.SetTexture(1, &bump); material.SetTexture(2, &environment); material.SetEffect(VXEFFECT_BUMPENV);
        return index;
    }
    void GeneratedNormal(int draw, float x = .4f, float y = .4f, float z = .6f, CKBOOL lighting = FALSE) {
        ASSERT_LT(draw, State.Count); const GeneratedNormalObserver::Draw &actual = State.Draws[draw];
        // The base groups submit directly to the rasterizer. This fixture's
        // single channel/recovery primitive traverses the context virtual once.
        EXPECT_EQ(1, static_cast<GeneratedNormalsContext *>(Device)->VirtualCalls);
        ASSERT_TRUE(actual.HasNormal); EXPECT_NE(0u, actual.Flags & CKRST_DP_LIGHT);
        EXPECT_EQ(lighting, actual.Lighting);
        EXPECT_FLOAT_EQ(x, actual.Normals[2].x); EXPECT_FLOAT_EQ(y, actual.Normals[2].y); EXPECT_FLOAT_EQ(z, actual.Normals[2].z);
    }
    void BumpReflection(int draw) {
        ASSERT_LT(draw, State.Count);
        EXPECT_EQ(CKRSTPackTexcoordIndex(2, CKRST_TEXGEN_CAMERASPACEREFLECTIONVECTOR), State.Draws[draw].Coordinates[2]);
    }
};
}

TEST_F(MeshPMGeneratedNormalsTest, NaturalPMPrivateUVUnlitBumpDeclaresInterpolatedNormalsWithoutEnablingLighting) {
    GeneratedNormalMesh mesh(Context); RCKMaterial base(Context), layer(Context); RCKTexture color(Context), bump(Context), environment(Context);
    Geometry(mesh, base); BumpChannel(mesh, layer, color, bump, environment); Build(mesh); Draw(mesh);
    ASSERT_EQ(2, State.Count); Triangle(1); PrivateUV(1); BumpReflection(1); GeneratedNormal(1);
}
TEST_F(MeshPMGeneratedNormalsTest, NaturalPMSameUVUnlitBumpDeclaresInterpolatedNormalsWithScratchUV) {
    GeneratedNormalMesh mesh(Context); RCKMaterial base(Context), layer(Context); RCKTexture color(Context), bump(Context), environment(Context);
    Geometry(mesh, base); BumpChannel(mesh, layer, color, bump, environment, TRUE); Build(mesh); Draw(mesh);
    ASSERT_EQ(2, State.Count); Triangle(1); SameUV(1); BumpReflection(1); GeneratedNormal(1);
}
TEST_F(MeshPMGeneratedNormalsTest, NaturalNonMorphPMUsesCollapsedGlobalSourceNormalForUnlitReflection) {
    GeneratedNormalMesh mesh(Context); RCKMaterial base(Context), layer(Context); RCKTexture color(Context), bump(Context), environment(Context);
    Geometry(mesh, base); BumpChannel(mesh, layer, color, bump, environment); Build(mesh, FALSE, 4); Draw(mesh);
    ASSERT_EQ(2, State.Count); Triangle(1, FALSE, 8); BumpReflection(1); GeneratedNormal(1, .2f, .6f, .8f);
}
TEST_F(MeshPMGeneratedNormalsTest, NaturalLODChangeUpdatesUpperTopologyAndNormalPayloadInsteadOfReusingAuthoredCornerFive) {
    GeneratedNormalMesh mesh(Context); RCKMaterial base(Context), layer(Context); RCKTexture color(Context), bump(Context), environment(Context);
    Geometry(mesh, base); BumpChannel(mesh, layer, color, bump, environment, TRUE); Build(mesh); Draw(mesh);
    ASSERT_EQ(2, State.Count); GeneratedNormal(1);
    mesh.SetVerticesRendered(4); mesh.BuildRenderMesh(); State.Count = 0;
    static_cast<GeneratedNormalsContext *>(Device)->VirtualCalls = 0; Draw(mesh);
    ASSERT_EQ(2, State.Count); EXPECT_EQ(6, State.Draws[1].VertexCount);
    EXPECT_EQ(0, State.Draws[1].Indices[0]); EXPECT_EQ(1, State.Draws[1].Indices[1]); EXPECT_EQ(5, State.Draws[1].Indices[2]);
    EXPECT_FLOAT_EQ(8, State.Draws[1].Positions[2].y); EXPECT_FLOAT_EQ(.75f, State.Draws[1].UVs[2].x);
    BumpReflection(1); GeneratedNormal(1, .2f, .6f, .8f);
}
TEST_F(MeshPMGeneratedNormalsTest, RealAuthoredRemapToNaturalPMTransitionKeepsGlobalScratchNormalDomain) {
    GeneratedNormalMesh mesh(Context); RCKMaterial first(Context), second(Context), layer(Context); RCKTexture color(Context), bump(Context), environment(Context);
    Geometry(mesh, first, &second); ASSERT_EQ(1, mesh.CreateRenderGroups()); ASSERT_EQ(2, mesh.RemappedGroups());
    BumpChannel(mesh, layer, color, bump, environment); Build(mesh); ASSERT_EQ(0, mesh.RemappedGroups()); Draw(mesh);
    ASSERT_EQ(3, State.Count); ASSERT_EQ(6, State.Draws[2].Count);
    EXPECT_EQ(0, State.Draws[2].Indices[0]); EXPECT_EQ(1, State.Draws[2].Indices[1]); EXPECT_EQ(3, State.Draws[2].Indices[2]);
    EXPECT_EQ(1, State.Draws[2].Indices[3]); EXPECT_EQ(0, State.Draws[2].Indices[4]); EXPECT_EQ(3, State.Draws[2].Indices[5]);
    BumpReflection(2); GeneratedNormal(2); EXPECT_FLOAT_EQ(.4f, State.Draws[2].Normals[5].x); EXPECT_FLOAT_EQ(.6f, State.Draws[2].Normals[5].z);
}
namespace {
struct GeneratedNormalOwner { int Calls = 0; CKBOOL Lighting = FALSE; };
int OwnGeneratedNormalState(CKRenderContext *context, CKMaterial *, void *argument) {
    GeneratedNormalOwner *owner = static_cast<GeneratedNormalOwner *>(argument); ++owner->Calls;
    CKRasterizerContext *rst = static_cast<RCKRenderContext *>(context)->m_RasterizerContext;
    rst->SetRenderState(VXRENDERSTATE_LIGHTING, owner->Lighting);
    rst->SetTextureStageState(0, CKRST_TSS_TEXCOORDINDEX, CKRSTPackTexcoordIndex(0, CKRST_TEXGEN_CAMERASPACENORMAL));
    rst->SetTextureStageState(0, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_COUNT2);
    rst->SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_MODULATE);
    rst->SetTexture(99, 0);
    return TRUE;
}
int ContinueGeneratedNormalState(CKRenderContext *, CKMaterial *, void *argument) { ++*static_cast<int *>(argument); return FALSE; }
}
TEST_F(MeshPMGeneratedNormalsTest, NaturalPMTrueCallbackKeepsOwnedLightingFalseWhileCameraNormalInputIsDeclared) {
    GeneratedNormalMesh mesh(Context); RCKMaterial base(Context), layer(Context); RCKTexture color(Context), bump(Context), environment(Context);
    Geometry(mesh, base); BumpChannel(mesh, layer, color, bump, environment); GeneratedNormalOwner owner;
    layer.SetCallback(OwnGeneratedNormalState, &owner); Build(mesh); Draw(mesh);
    ASSERT_EQ(2, State.Count); EXPECT_EQ(1, owner.Calls); GeneratedNormal(1);
    EXPECT_EQ(CKRSTPackTexcoordIndex(0, CKRST_TEXGEN_CAMERASPACENORMAL), State.Draws[1].Coordinates[0]);
    // The same owning callback can intentionally light the next draw. The
    // mesh scope must preserve either actual choice, rather than force FALSE.
    owner.Lighting = TRUE; owner.Calls = 0; State.Count = 0;
    static_cast<GeneratedNormalsContext *>(Device)->VirtualCalls = 0; Draw(mesh);
    ASSERT_EQ(2, State.Count); EXPECT_EQ(1, owner.Calls); GeneratedNormal(1, .4f, .4f, .6f, TRUE);
}
TEST_F(MeshPMGeneratedNormalsTest, NaturalPMFalseCallbackContinuesDefaultBumpWithoutLosingNormalsOrEnablingLighting) {
    GeneratedNormalMesh mesh(Context); RCKMaterial base(Context), layer(Context); RCKTexture color(Context), bump(Context), environment(Context);
    Geometry(mesh, base); BumpChannel(mesh, layer, color, bump, environment); int calls = 0;
    layer.SetCallback(ContinueGeneratedNormalState, &calls); Build(mesh); Draw(mesh);
    ASSERT_EQ(2, State.Count); EXPECT_EQ(1, calls); BumpReflection(1); GeneratedNormal(1);
}
TEST_F(MeshPMGeneratedNormalsTest, PublicDrawOutsideMeshRetainsAutomaticLightingSelectionForRealNormalPointer) {
    VxVector points[] = {VxVector(0,0,1), VxVector(1,0,1), VxVector(0,1,1)};
    VxVector normals[] = {VxVector(.6f,.2f,.4f), VxVector(-.3f,.7f,.2f), VxVector(.2f,.6f,.8f)};
    CKWORD indices[] = {0,1,2}; VxDrawPrimitiveData data = {}; data.VertexCount = 3;
    data.PositionPtr = points; data.PositionStride = sizeof(VxVector); data.NormalPtr = normals; data.NormalStride = sizeof(VxVector);
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_LIGHT; State.SetRenderState(VXRENDERSTATE_LIGHTING, FALSE);
    CKRenderContext *publicContext = Device; ASSERT_TRUE(publicContext->DrawPrimitive(VX_TRIANGLELIST, indices, 3, &data));
    ASSERT_EQ(1, State.Count); GeneratedNormal(0, .2f, .6f, .8f, TRUE);
    data.Flags &= ~CKRST_DP_LIGHT; ASSERT_TRUE(publicContext->DrawPrimitive(VX_TRIANGLELIST, indices, 3, &data));
    ASSERT_EQ(2, State.Count); EXPECT_FALSE(State.Draws[1].Lighting); EXPECT_EQ(0u, State.Draws[1].Flags & CKRST_DP_LIGHT);
}
TEST_F(MeshPMGeneratedNormalsTest, ConditionalFallbackEntryConsumesActualPMBuildScratchNormalsAndSameUV) {
    // This directly tests conditional recovery's consumer contract. Natural PM
    // mono eligibility is not claimed and its authored face cache is untouched.
    GeneratedNormalMesh mesh(Context); RCKMaterial base(Context), layer(Context); RCKTexture color(Context), bump(Context), environment(Context);
    Geometry(mesh, base); const int channel = BumpChannel(mesh, layer, color, bump, environment, TRUE); Build(mesh);
    const int groupIndex = mesh.GetMaterialGroupIndex(&base, FALSE); ASSERT_GE(groupIndex, 0);
    CKMaterialGroup *group = mesh.Group(groupIndex); ASSERT_TRUE(group->m_HasValidPrimitives);
    ASSERT_GT(group->m_Primitives.Size(), 0); ASSERT_EQ(3, group->m_Primitives[0].m_Indices.Size());
    ASSERT_EQ(nullptr, mesh.GetVBuffer(group));
    ASSERT_EQ(1, mesh.RenderMonoPassFallback(Device, group, nullptr, nullptr, static_cast<CKWORD>(1u << channel)));
    ASSERT_EQ(1, State.Count); Triangle(0); SameUV(0); BumpReflection(0); GeneratedNormal(0);
}
TEST_F(MeshPMGeneratedNormalsTest, ConditionalFallbackEntryConsumesActualCreateRenderGroupsRemappedNormalsAndPrivateUV) {
    GeneratedNormalMesh mesh(Context); RCKMaterial first(Context), second(Context), layer(Context); RCKTexture color(Context), bump(Context), environment(Context);
    Geometry(mesh, first, &second); const int channel = BumpChannel(mesh, layer, color, bump, environment);
    ASSERT_EQ(1, mesh.CreateRenderGroups()); ASSERT_EQ(2, mesh.RemappedGroups());
    const int groupIndex = mesh.GetMaterialGroupIndex(&first, FALSE); ASSERT_GE(groupIndex, 0);
    CKMaterialGroup *group = mesh.Group(groupIndex); ASSERT_TRUE(group->m_HasValidPrimitives);
    ASSERT_GT(group->m_Primitives.Size(), 0); ASSERT_EQ(3, group->m_Primitives[0].m_Indices.Size());
    ASSERT_NE(nullptr, mesh.GetVBuffer(group));
    ASSERT_EQ(1, mesh.RenderMonoPassFallback(Device, group, nullptr, nullptr, static_cast<CKWORD>(1u << channel)));
    ASSERT_EQ(1, State.Count); EXPECT_EQ(3, State.Draws[0].VertexCount);
    EXPECT_EQ(0, State.Draws[0].Indices[0]); EXPECT_EQ(1, State.Draws[0].Indices[1]); EXPECT_EQ(2, State.Draws[0].Indices[2]);
    EXPECT_FLOAT_EQ(50, State.Draws[0].Positions[2].x); EXPECT_FLOAT_EQ(1.125f, State.Draws[0].UVs[2].x);
    BumpReflection(0); GeneratedNormal(0, .9f, -.2f, .3f);
}
