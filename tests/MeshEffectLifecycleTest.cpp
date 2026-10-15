#include <gtest/gtest.h>
#include <cstring>
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "TextureTestObserver.h"

namespace {
class EffectLifecycleObserver : public TextureTestObserver {
public:
    struct Draw {
        CKDWORD Textures[3] = {}, Flags = 0;
        int VertexCount = 0, Count = 0;
        CKWORD Indices[9] = {};
        VxVector Positions[9];
        Vx2DVector UV[3][9];
        CKBOOL HasUV[3] = {};
    };
    Draw Draws[16]; int Count = 0;
    CKBOOL SetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE state, CKDWORD value) override {
        // This observer uses only the default legacy blend selector. Its OP
        // alias is needed to observe the enabled chain, not to model pixels.
        if (state == CKRST_TSS_TEXTUREMAPBLEND && value == VXTEXTUREBLEND_MODULATEALPHA)
            StageStates[stage][CKRST_TSS_OP] = CKRST_TOP_MODULATE;
        return TextureTestObserver::SetTextureStageState(stage, state, value);
    }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        EXPECT_EQ(VX_TRIANGLELIST, type); EXPECT_NE(nullptr, indices); EXPECT_NE(nullptr, data);
        EXPECT_LT(Count, 16); EXPECT_LE(count, 9);
        if (!indices || !data || Count >= 16) return FALSE;
        Draw &draw = Draws[Count++];
        draw.Count = count; draw.VertexCount = data->VertexCount; draw.Flags = data->Flags;
        for (int stage = 0; stage < 3; ++stage) draw.Textures[stage] = Textures[stage];
        for (int i = 0; i < count && i < 9; ++i) {
            draw.Indices[i] = indices[i]; EXPECT_LT(indices[i], data->VertexCount);
            if (indices[i] >= data->VertexCount) continue;
            EXPECT_NE(nullptr, data->PositionPtr);
            if (data->PositionPtr)
                memcpy(&draw.Positions[i], static_cast<CKBYTE *>(data->PositionPtr) + indices[i] * data->PositionStride, sizeof(VxVector));
            for (int stage = 0; stage < 3; ++stage) {
                if ((data->Flags & CKRST_DP_STAGESMASK) < CKRST_DP_STAGE(stage)) continue;
                void *uv = stage ? data->TexCoordPtrs[stage - 1] : data->TexCoordPtr;
                CKDWORD stride = stage ? data->TexCoordStrides[stage - 1] : data->TexCoordStride;
                if (uv) {
                    draw.HasUV[stage] = TRUE;
                    memcpy(&draw.UV[stage][i], static_cast<CKBYTE *>(uv) + indices[i] * stride, sizeof(Vx2DVector));
                }
            }
        }
        return TRUE;
    }
};

class EffectLifecycleMesh : public RCKMesh {
public:
    explicit EffectLifecycleMesh(CKContext *context) : RCKMesh(context) {}
    void ConfigurePM(CKBOOL morph = TRUE, int count = 3) {
        m_ProgressiveMesh = new CKProgressiveMesh();
        m_ProgressiveMesh->m_VertexCount = count;
        m_ProgressiveMesh->m_MorphEnabled = morph;
        m_ProgressiveMesh->m_MorphStep = 2;
        const CKDWORD parents[] = {0, 0, 0, 2, 3, 4};
        m_ProgressiveMesh->m_Data.Resize(6);
        for (int i = 0; i < 6; ++i) m_ProgressiveMesh->m_Data[i] = parents[i];
        ModifyObjectFlags(0, CK_OBJECT_UPTODATE);
    }
    void Cold() { m_Valid = 0; }
};

class MeshEffectLifecycleTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    CKRasterizerDriver Driver;
    EffectLifecycleObserver State;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context); Manager->m_VertexCache.Value = 0;
        Manager->m_DriverCount = 1; Manager->m_Drivers = new VxDriverDescEx[1]();
        Manager->m_Drivers[0].CapsUpToDate = TRUE; Manager->m_Drivers[0].RasterizerDriver = &Driver;
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 4;
        VxImageDescEx format; VxPixelFormat2ImageDesc(_32_ARGB8888, format);
        Manager->m_Drivers[0].TextureFormats.PushBack(format);
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0));
        Device = new RCKRenderContext(Context); Device->DetachAll();
        Device->m_RasterizerContext = &State; Device->m_RasterizerDriver = &Driver;
        Device->m_DriverIndex = 0; Device->m_DisplayWireframe = FALSE;
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; delete Device; }
        if (Context) CKCloseContext(Context); CKShutdown();
    }
    static void Pixel(RCKTexture &texture, CKDWORD color) {
        ASSERT_TRUE(texture.CreateImage(1, 1, 32, 0)); CKBYTE *pixels = texture.LockSurfacePtr(0);
        ASSERT_NE(nullptr, pixels); memcpy(pixels, &color, 4); texture.ReleaseSurfacePtr(0);
        texture.SetDesiredVideoFormat(_32_ARGB8888);
    }
    static void Geometry(EffectLifecycleMesh &mesh, RCKMaterial &plain, RCKMaterial &effect) {
        ASSERT_TRUE(mesh.SetVertexCount(6)); ASSERT_TRUE(mesh.SetFaceCount(2));
        VxVector points[] = {VxVector(0,0,0), VxVector(2,0,0), VxVector(0,0,0),
                             VxVector(0,8,0), VxVector(12,0,0), VxVector(50,50,0)};
        const Vx2DVector uv[] = {Vx2DVector(.125f,.25f), Vx2DVector(.25f,.25f), Vx2DVector(.5f,.25f),
                                Vx2DVector(.75f,1.0f), Vx2DVector(.875f,.5f), Vx2DVector(1.0f,1.0f)};
        for (int i = 0; i < 6; ++i) {
            mesh.SetVertexPosition(i, &points[i]); mesh.SetVertexTextureCoordinates(i, uv[i].x, uv[i].y, -1);
        }
        mesh.SetFaceVertexIndex(0, 0, 1, 5); mesh.SetFaceMaterial(0, &plain);
        mesh.SetFaceVertexIndex(1, 1, 0, 5); mesh.SetFaceMaterial(1, &effect);
        mesh.SetFlags((mesh.GetFlags() | VXMESH_RENDERCHANNELS) & ~(VXMESH_PRELITMODE | VXMESH_STRIPIFY));
    }
    static int Channel(EffectLifecycleMesh &mesh, RCKMaterial &material, CKBOOL sameUV = FALSE) {
        int index = mesh.AddChannel(&material, FALSE); EXPECT_GE(index, 0);
        mesh.SetChannelSourceBlend(index, VXBLEND_ZERO); mesh.SetChannelDestBlend(index, VXBLEND_SRCCOLOR);
        mesh.SetChannelFlags(index, VXCHANNEL_ACTIVE | (sameUV ? VXCHANNEL_SAMEUV : 0));
        for (int i = 0; i < 6; ++i) mesh.SetVertexTextureCoordinates(i, .5f + i * .125f, .9375f, index);
        return index;
    }
    void Build(EffectLifecycleMesh &mesh, CKBOOL morph = TRUE, int count = 3) {
        mesh.ConfigurePM(morph, count); mesh.BuildRenderMesh(); mesh.Cold();
    }
    void Draw(EffectLifecycleMesh &mesh) { mesh.Cold(); ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr)); }
    const EffectLifecycleObserver::Draw *Find(RCKTexture &texture, int ordinal = 0) {
        for (int i = 0; i < State.Count; ++i)
            if (State.Draws[i].Textures[0] == texture.GetRstTextureIndex() && ordinal-- == 0) return &State.Draws[i];
        return nullptr;
    }
    static void Triangle(const EffectLifecycleObserver::Draw &draw, CKBOOL reversed, int count = 4, int corner = 3, float y = 4) {
        ASSERT_EQ(3, draw.Count); EXPECT_EQ(count, draw.VertexCount);
        EXPECT_EQ(reversed ? 1 : 0, draw.Indices[0]); EXPECT_EQ(reversed ? 0 : 1, draw.Indices[1]);
        EXPECT_EQ(corner, draw.Indices[2]); EXPECT_FLOAT_EQ(0, draw.Positions[2].x);
        EXPECT_FLOAT_EQ(y, draw.Positions[2].y); EXPECT_FLOAT_EQ(0, draw.Positions[2].z);
    }
    static void UV(const EffectLifecycleObserver::Draw &draw, int slot, float u, float v) {
        ASSERT_TRUE(draw.HasUV[slot]); EXPECT_FLOAT_EQ(u, draw.UV[slot][2].x); EXPECT_FLOAT_EQ(v, draw.UV[slot][2].y);
    }
};
}

TEST_F(MeshEffectLifecycleTest, MaterialReassignmentRebuildsTheCurrentMorphTopology) {
    EffectLifecycleMesh mesh(Context); RCKMaterial plain(Context), combined(Context), replacement(Context), provider(Context);
    RCKTexture first(Context), second(Context), extra(Context), replacementTexture(Context), replacementExtra(Context), layer(Context);
    Pixel(first, 0xFF112233); Pixel(second, 0xFF445566); Pixel(extra, 0xFF778899);
    Pixel(replacementTexture, 0xFF99AABB); Pixel(replacementExtra, 0xFF223344); Pixel(layer, 0xFF556677);
    plain.SetTexture(0, &first); combined.SetTexture(0, &second); combined.SetTexture(1, &extra); combined.SetEffect(VXEFFECT_2TEXTURES);
    replacement.SetTexture(0, &replacementTexture); replacement.SetTexture(1, &replacementExtra); replacement.SetEffect(VXEFFECT_2TEXTURES);
    provider.SetTexture(0, &layer); Geometry(mesh, plain, combined); Channel(mesh, provider, TRUE); Build(mesh); Draw(mesh);
    ASSERT_EQ(3, State.Count);
    mesh.SetFaceMaterial(0, &replacement); mesh.BuildRenderMesh(); State.Count = 0; Draw(mesh);
    ASSERT_EQ(2, State.Count);
    const EffectLifecycleObserver::Draw *a = Find(replacementTexture), *b = Find(second);
    ASSERT_NE(nullptr, a); ASSERT_NE(nullptr, b); Triangle(*a, FALSE); Triangle(*b, TRUE);
    UV(*a, 1, .4375f, .625f); UV(*b, 1, .4375f, .625f);
}

TEST_F(MeshEffectLifecycleTest, MixedMorphPrivateProviderAndIndependentLayerUseTheUpperLODDomain) {
    EffectLifecycleMesh mesh(Context); RCKMaterial plain(Context), combined(Context), provider(Context);
    RCKTexture first(Context), second(Context), extra(Context), layer(Context);
    Pixel(first, 0xFF112233); Pixel(second, 0xFF445566); Pixel(extra, 0xFF778899); Pixel(layer, 0xFF99AABB);
    plain.SetTexture(0, &first); combined.SetTexture(0, &second); combined.SetTexture(1, &extra); combined.SetEffect(VXEFFECT_2TEXTURES);
    provider.SetTexture(0, &layer); Geometry(mesh, plain, combined); Channel(mesh, provider); Build(mesh); Draw(mesh);
    ASSERT_EQ(3, State.Count); const EffectLifecycleObserver::Draw *a = Find(layer), *b = Find(second);
    ASSERT_NE(nullptr, a); ASSERT_NE(nullptr, b); Triangle(*a, FALSE); Triangle(*b, TRUE);
    UV(*a, 0, .875f, .9375f); UV(*b, 1, .875f, .9375f);
}

TEST_F(MeshEffectLifecycleTest, MixedMorphSameUVUsesTheInterpolatedMainStreamForBothRoles) {
    EffectLifecycleMesh mesh(Context); RCKMaterial plain(Context), combined(Context), provider(Context);
    RCKTexture first(Context), second(Context), extra(Context), layer(Context);
    Pixel(first, 0xFF112233); Pixel(second, 0xFF445566); Pixel(extra, 0xFF778899); Pixel(layer, 0xFF99AABB);
    plain.SetTexture(0, &first); combined.SetTexture(0, &second); combined.SetTexture(1, &extra); combined.SetEffect(VXEFFECT_2TEXTURES);
    provider.SetTexture(0, &layer); Geometry(mesh, plain, combined); Channel(mesh, provider, TRUE); Build(mesh); Draw(mesh);
    ASSERT_EQ(3, State.Count); const EffectLifecycleObserver::Draw *a = Find(layer), *b = Find(second);
    ASSERT_NE(nullptr, a); ASSERT_NE(nullptr, b); Triangle(*a, FALSE); Triangle(*b, TRUE);
    // At LOD 3 the upper corner is halfway from vertex 0 to vertex 3.
    UV(*a, 0, .4375f, .625f); UV(*b, 1, .4375f, .625f);
}

TEST_F(MeshEffectLifecycleTest, MixedNonMorphFaceMaskFiltersOnlyTheIndependentRole) {
    EffectLifecycleMesh mesh(Context); RCKMaterial plain(Context), combined(Context), provider(Context);
    RCKTexture first(Context), second(Context), extra(Context), layer(Context);
    Pixel(first, 0xFF112233); Pixel(second, 0xFF445566); Pixel(extra, 0xFF778899); Pixel(layer, 0xFF99AABB);
    plain.SetTexture(0, &first); combined.SetTexture(0, &second); combined.SetTexture(1, &extra); combined.SetEffect(VXEFFECT_2TEXTURES);
    provider.SetTexture(0, &layer); Geometry(mesh, plain, combined); int channel = Channel(mesh, provider);
    mesh.SetFaceChannelMask(0, 0); mesh.SetFaceChannelMask(1, static_cast<CKWORD>(1u << channel));
    Build(mesh, FALSE, 4); Draw(mesh); ASSERT_EQ(2, State.Count);
    const EffectLifecycleObserver::Draw *effect = Find(second); ASSERT_NE(nullptr, effect);
    Triangle(*effect, TRUE, 4, 3, 8); UV(*effect, 1, .875f, .9375f); EXPECT_EQ(nullptr, Find(layer));
}

TEST_F(MeshEffectLifecycleTest, MixedLODChangeRefreshesMissingProviderAndSameUVFallbackStreams) {
    EffectLifecycleMesh mesh(Context); RCKMaterial plain(Context), combined(Context), provider(Context);
    RCKTexture first(Context), second(Context), extra1(Context), extra2(Context), layer(Context);
    Pixel(first, 0xFF112233); Pixel(second, 0xFF445566); Pixel(extra1, 0xFF778899); Pixel(extra2, 0xFF99AABB); Pixel(layer, 0xFF223344);
    plain.SetTexture(0, &first); combined.SetTexture(0, &second); combined.SetTexture(1, &extra1);
    combined.SetTexture(2, &extra2); combined.SetEffect(VXEFFECT_3TEXTURES);
    provider.SetTexture(0, &layer); Geometry(mesh, plain, combined); Channel(mesh, provider, TRUE); Build(mesh); Draw(mesh);
    ASSERT_EQ(3, State.Count); const EffectLifecycleObserver::Draw *before = Find(second); ASSERT_NE(nullptr, before);
    Triangle(*before, TRUE); UV(*before, 1, .4375f, .625f); UV(*before, 2, .4375f, .625f);
    mesh.SetVerticesRendered(4); mesh.BuildRenderMesh(); State.Count = 0; Draw(mesh);
    ASSERT_EQ(3, State.Count); const EffectLifecycleObserver::Draw *after = Find(second), *independent = Find(layer);
    ASSERT_NE(nullptr, after); ASSERT_NE(nullptr, independent);
    Triangle(*after, TRUE, 6, 5, 8); Triangle(*independent, FALSE, 6, 5, 8);
    UV(*after, 1, .75f, 1); UV(*after, 2, .75f, 1); UV(*independent, 0, .75f, 1);
}

TEST_F(MeshEffectLifecycleTest, MixedPMDegenerateAuthoredFacesNeverReappearAsIndependentLayers) {
    EffectLifecycleMesh mesh(Context); RCKMaterial plain(Context), combined(Context), provider(Context);
    RCKTexture first(Context), second(Context), extra(Context), layer(Context);
    Pixel(first, 0xFF112233); Pixel(second, 0xFF445566); Pixel(extra, 0xFF778899); Pixel(layer, 0xFF99AABB);
    plain.SetTexture(0, &first); combined.SetTexture(0, &second); combined.SetTexture(1, &extra); combined.SetEffect(VXEFFECT_2TEXTURES);
    provider.SetTexture(0, &layer); Geometry(mesh, plain, combined);
    ASSERT_TRUE(mesh.SetFaceCount(3)); mesh.SetFaceVertexIndex(2, 0, 4, 5); mesh.SetFaceMaterial(2, &plain);
    Channel(mesh, provider); Build(mesh); Draw(mesh); ASSERT_EQ(3, State.Count);
    const EffectLifecycleObserver::Draw *base = Find(first), *independent = Find(layer), *effect = Find(second);
    ASSERT_NE(nullptr, base); ASSERT_NE(nullptr, independent); ASSERT_NE(nullptr, effect);
    Triangle(*base, FALSE); Triangle(*independent, FALSE); Triangle(*effect, TRUE);
    UV(*independent, 0, .875f, .9375f); UV(*effect, 1, .875f, .9375f);
}
