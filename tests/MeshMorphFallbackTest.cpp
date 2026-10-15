#include <gtest/gtest.h>
#include <cstring>
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "TextureTestObserver.h"

namespace {
const CKDWORD ChannelA = 0xff004411u;
const CKDWORD ChannelB = 0xff006622u;

class MorphFallbackObserver : public TextureTestObserver {
public:
    struct Draw {
        int VertexCount = 0, Count = 0;
        CKDWORD Flags = 0, Textures[4] = {};
        CKWORD Indices[12] = {};
        VxVector Positions[12], Normals[12];
        Vx2DVector UVs[12];
        CKDWORD Colors[12] = {}, Speculars[12] = {};
    };
    Draw Draws[16];
    int Count = 0, BindFailures = 0, RejectStage = -1;
    float RejectDiffuse = -1.0f;
    CKDWORD Labels[64] = {};
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

class MorphFallbackMesh : public RCKMesh {
public:
    explicit MorphFallbackMesh(CKContext *context) : RCKMesh(context) {}
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
    void Cold() { m_Valid = 0; }
    int RemappedGroups() const {
        int result = 0;
        for (int i = 0; i < m_MaterialGroups.Size(); ++i)
            if (m_MaterialGroups[i]->m_RemapData) ++result;
        return result;
    }
    // Consumer-only premise: all authored faces have this channel. PM currently
    // retains an explicit index cache even for that case, which prevents mono
    // selection. Remove that redundant cache to exercise the real mono failure
    // consumer; this does not change vertices, primitives, masks or UVs.
    void AllFacesMonoConsumerPremise(int channel) {
        for (int f = 0; f < m_Faces.Size(); ++f)
            EXPECT_NE(0, m_Faces[f].m_ChannelMask & (1u << channel));
        delete m_MaterialChannels[channel].m_FaceIndices;
        m_MaterialChannels[channel].m_FaceIndices = nullptr;
        m_FaceChannelMask = 0;
    }
};

class MeshMorphFallbackTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    CKRasterizerDriver Driver;
    MorphFallbackObserver State;
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
        Device = new RCKRenderContext(Context); Device->DetachAll();
        Device->m_RasterizerContext = &State; Device->m_RasterizerDriver = &Driver;
        Device->m_DriverIndex = 0; Device->m_Current3dEntity = nullptr;
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; delete Device; }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    void Geometry(MorphFallbackMesh &mesh, RCKMaterial &first, RCKMaterial *second = nullptr) {
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
            VxVector normal(0, i == 3 ? 1.0f : 0.0f, i == 3 ? 0.0f : 1.0f);
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
    int Channel(MorphFallbackMesh &mesh, RCKMaterial &material, RCKTexture &texture,
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
    void Build(MorphFallbackMesh &mesh, CKBOOL morph = TRUE, int count = 3) {
        mesh.ConfigurePM(morph, count); mesh.BuildRenderMesh(); mesh.Cold();
    }
    void Draw(MorphFallbackMesh &mesh) { mesh.Cold(); ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); }
    void Triangle(int draw, CKBOOL reversed = FALSE, float topY = 4.0f) {
        ASSERT_LT(draw, State.Count);
        const MorphFallbackObserver::Draw &d = State.Draws[draw];
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
};
}

TEST_F(MeshMorphFallbackTest, FreshPMBuildCommitsGlobalLODGroupsBeforeDefaultRender) {
    MorphFallbackMesh mesh(Context); RCKMaterial base(Context); Geometry(mesh, base); Build(mesh);
    EXPECT_NE(0u, mesh.GetFlags() & VXMESH_OPTIMIZED);
    Draw(mesh); ASSERT_EQ(1, State.Count); Triangle(0); SameUV(0);
    EXPECT_FLOAT_EQ(.5f, State.Draws[0].Normals[2].y);
    EXPECT_FLOAT_EQ(.5f, State.Draws[0].Normals[2].z);
    VxVector source; mesh.GetVertexPosition(3, &source); EXPECT_FLOAT_EQ(8.0f, source.y);
    float u = 0, v = 0; mesh.GetVertexTextureCoordinates(3, &u, &v, -1);
    EXPECT_FLOAT_EQ(.75f, u); EXPECT_FLOAT_EQ(1.0f, v);
}

TEST_F(MeshMorphFallbackTest, PMBuildDiscardsAuthoredRemapAndKeepsScratchAndGlobalIndices) {
    MorphFallbackMesh mesh(Context); RCKMaterial first(Context), second(Context); Geometry(mesh, first, &second);
    ASSERT_EQ(1, mesh.CreateRenderGroups()); ASSERT_EQ(2, mesh.RemappedGroups()); Build(mesh);
    EXPECT_EQ(0, mesh.RemappedGroups()); Draw(mesh); ASSERT_EQ(2, State.Count);
    Triangle(0); Triangle(1, TRUE); SameUV(0); SameUV(1); EXPECT_EQ(0, mesh.RemappedGroups());
}

TEST_F(MeshMorphFallbackTest, CopiedPMRebuildsScratchGroupsThroughItsActualPreRenderCallback) {
    MorphFallbackMesh source(Context), copy(Context); RCKMaterial base(Context); Geometry(source, base); Build(source);
    CKDependenciesContext dependencies(Context); ASSERT_EQ(CK_OK, copy.Copy(source, dependencies));
    ASSERT_EQ(CK_OK, copy.Render(Device, nullptr)); ASSERT_EQ(1, State.Count); Triangle(0); SameUV(0);
}

TEST_F(MeshMorphFallbackTest, CopyOverBuiltPMInvalidatesTargetUpdateStateAndRebuildsCurrentTopology) {
    MorphFallbackMesh source(Context), copy(Context); RCKMaterial sourceBase(Context), oldBase(Context);
    Geometry(source, sourceBase); Build(source);
    Geometry(copy, oldBase); copy.SetFaceVertexIndex(0, 0, 1, 2); Build(copy);
    ASSERT_NE(0u, copy.GetObjectFlags() & CK_OBJECT_UPTODATE);
    CKDependenciesContext dependencies(Context); ASSERT_EQ(CK_OK, copy.Copy(source, dependencies));
    EXPECT_EQ(0u, copy.GetObjectFlags() & CK_OBJECT_UPTODATE);
    ASSERT_EQ(CK_OK, copy.Render(Device, nullptr)); ASSERT_EQ(1, State.Count); Triangle(0); SameUV(0);
}

TEST_F(MeshMorphFallbackTest, CopiedPMPrivateChannelMultipassUsesUpperLODVertexUV) {
    MorphFallbackMesh source(Context), copy(Context); RCKMaterial base(Context), channel(Context); RCKTexture texture(Context);
    Geometry(source, base); Channel(source, channel, texture, ChannelA); Build(source);
    CKDependenciesContext dependencies(Context); ASSERT_EQ(CK_OK, copy.Copy(source, dependencies));
    ASSERT_EQ(CK_OK, copy.Render(Device, nullptr)); ASSERT_EQ(2, State.Count);
    Triangle(0); Triangle(1); PrivateUV(1); EXPECT_EQ(ChannelA, State.Draws[1].Textures[0]);
}

TEST_F(MeshMorphFallbackTest, CopiedPMSameUVMultipassUsesTheInterpolatedScratchStream) {
    MorphFallbackMesh source(Context), copy(Context); RCKMaterial base(Context), channel(Context); RCKTexture texture(Context);
    Geometry(source, base); Channel(source, channel, texture, ChannelA, TRUE); Build(source);
    CKDependenciesContext dependencies(Context); ASSERT_EQ(CK_OK, copy.Copy(source, dependencies));
    ASSERT_EQ(CK_OK, copy.Render(Device, nullptr)); ASSERT_EQ(2, State.Count);
    Triangle(0); Triangle(1); SameUV(0); SameUV(1);
}

TEST_F(MeshMorphFallbackTest, CollapsedMaterialFaceIsAbsentFromBothBaseAndChannelPasses) {
    MorphFallbackMesh mesh(Context); RCKMaterial first(Context), second(Context), channel(Context); RCKTexture texture(Context);
    Geometry(mesh, first, &second); mesh.SetFaceVertexIndex(1, 0, 4, 5);
    Channel(mesh, channel, texture, ChannelA); Build(mesh); Draw(mesh); ASSERT_EQ(2, State.Count);
    Triangle(0); Triangle(1); PrivateUV(1);
}

TEST_F(MeshMorphFallbackTest, CopiedNonMorphPMKeepsCollapsedGlobalIndicesAndSourceMainUV) {
    MorphFallbackMesh source(Context), copy(Context); RCKMaterial base(Context); Geometry(source, base);
    Build(source, FALSE, 4); CKDependenciesContext dependencies(Context);
    ASSERT_EQ(CK_OK, copy.Copy(source, dependencies)); ASSERT_EQ(CK_OK, copy.Render(Device, nullptr));
    ASSERT_EQ(1, State.Count); Triangle(0, FALSE, 8.0f);
    EXPECT_FLOAT_EQ(.75f, State.Draws[0].UVs[2].x); EXPECT_FLOAT_EQ(1.0f, State.Draws[0].UVs[2].y);
}

TEST_F(MeshMorphFallbackTest, AllFacePMMonoConsumerFailureKeepsMorphScratchAndSameUV) {
    MorphFallbackMesh mesh(Context); RCKMaterial base(Context), channel(Context); RCKTexture texture(Context);
    Geometry(mesh, base); const int c = Channel(mesh, channel, texture, ChannelA, TRUE); Build(mesh);
    mesh.AllFacesMonoConsumerPremise(c); State.RejectStage = 1; Draw(mesh);
    ASSERT_EQ(1, State.BindFailures); ASSERT_EQ(2, State.Count);
    EXPECT_NE(0u, mesh.GetChannelFlags(c) & VXCHANNEL_MONO);
    Triangle(0); Triangle(1); SameUV(0); SameUV(1); FallbackStage(1, ChannelA);
    EXPECT_EQ(0u, State.Draws[1].Flags & (CKRST_DP_LIGHT | CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR));
}

TEST_F(MeshMorphFallbackTest, AllFacePMMonoConsumerPrivateUVFailureKeepsGlobalLODIndices) {
    MorphFallbackMesh mesh(Context); RCKMaterial base(Context), channel(Context); RCKTexture texture(Context);
    Geometry(mesh, base); const int c = Channel(mesh, channel, texture, ChannelA); Build(mesh);
    mesh.AllFacesMonoConsumerPremise(c); State.RejectStage = 1; Draw(mesh);
    ASSERT_EQ(1, State.BindFailures); ASSERT_EQ(2, State.Count);
    Triangle(0); Triangle(1); PrivateUV(1); FallbackStage(1, ChannelA);
}

TEST_F(MeshMorphFallbackTest, AllFacePMMonoConsumerSecondGroupFailureDoesNotReplayFirstGroup) {
    MorphFallbackMesh mesh(Context); RCKMaterial first(Context), second(Context), channel(Context); RCKTexture texture(Context);
    Geometry(mesh, first, &second); ASSERT_EQ(1, mesh.CreateRenderGroups());
    const int c = Channel(mesh, channel, texture, ChannelA); Build(mesh);
    mesh.AllFacesMonoConsumerPremise(c); State.RejectStage = 1; State.RejectDiffuse = .5f; Draw(mesh);
    ASSERT_EQ(1, State.BindFailures); ASSERT_EQ(3, State.Count);
    Triangle(0); Triangle(1, TRUE); Triangle(2, TRUE); PrivateUV(2);
    EXPECT_EQ(ChannelA, State.Draws[0].Textures[1]); EXPECT_EQ(0u, State.Draws[1].Textures[1]);
    FallbackStage(2, ChannelA); EXPECT_EQ(0, mesh.RemappedGroups());
}

TEST_F(MeshMorphFallbackTest, AllFacePMMonoConsumerSuffixFailureDoesNotReplayAcceptedPrefix) {
    MorphFallbackMesh mesh(Context); RCKMaterial base(Context), first(Context), second(Context); RCKTexture a(Context), b(Context);
    Geometry(mesh, base); const int c0 = Channel(mesh, first, a, ChannelA);
    const int c1 = Channel(mesh, second, b, ChannelB, TRUE); Build(mesh);
    mesh.AllFacesMonoConsumerPremise(c0); mesh.AllFacesMonoConsumerPremise(c1);
    State.RejectStage = 2; Draw(mesh); ASSERT_EQ(1, State.BindFailures); ASSERT_EQ(2, State.Count);
    Triangle(0); Triangle(1); SameUV(1); EXPECT_EQ(ChannelA, State.Draws[0].Textures[1]);
    FallbackStage(1, ChannelB);
}

TEST_F(MeshMorphFallbackTest, ChangingLODAfterFallbackRebuildsScratchAndRetryUsesNewUpperLevel) {
    MorphFallbackMesh mesh(Context); RCKMaterial base(Context), channel(Context); RCKTexture texture(Context);
    Geometry(mesh, base); const int c = Channel(mesh, channel, texture, ChannelA); Build(mesh);
    mesh.AllFacesMonoConsumerPremise(c); State.RejectStage = 1; Draw(mesh); ASSERT_EQ(2, State.Count);
    mesh.SetVerticesRendered(4); mesh.BuildRenderMesh(); mesh.AllFacesMonoConsumerPremise(c);
    State.Count = 0; Draw(mesh); ASSERT_EQ(1, State.Count);
    // LOD 4 starts the interval [4,6]. Its upper topology uses authored
    // corner 5, but t=0 supplies lower-level vertex 3's position/main UV.
    EXPECT_EQ(6, State.Draws[0].VertexCount); ASSERT_EQ(3, State.Draws[0].Count);
    EXPECT_EQ(0, State.Draws[0].Indices[0]); EXPECT_EQ(1, State.Draws[0].Indices[1]);
    EXPECT_EQ(5, State.Draws[0].Indices[2]); EXPECT_FLOAT_EQ(0.0f, State.Draws[0].Positions[2].x);
    EXPECT_FLOAT_EQ(8.0f, State.Draws[0].Positions[2].y);
    EXPECT_FLOAT_EQ(.75f, State.Draws[0].UVs[2].x); EXPECT_FLOAT_EQ(1.0f, State.Draws[0].UVs[2].y);
    EXPECT_EQ(ChannelA, State.Draws[0].Textures[1]); EXPECT_EQ(1, State.BindFailures);
}
