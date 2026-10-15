#include <gtest/gtest.h>
#include <cstring>
#include "CKAll.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "TextureTestObserver.h"

namespace {
class ChannelUnwindObserver : public TextureTestObserver {
public:
    struct Draw {
        CKBOOL Blend = FALSE, ZWrite = FALSE;
        CKDWORD Cull = 0, ZFunc = 0;
        Vx2DVector UV;
    };
    Draw Draws[16];
    int Count = 0, Attempts = 0, RejectedBinds = 0;
    CKBOOL RejectPositiveBind = FALSE, ThrowChannelDraw = FALSE, FailChannelDraw = FALSE;
    CKBOOL SawPatchedDraw = FALSE;
    CKBOOL SetTexture(CKDWORD handle, int stage) override {
        if (handle && stage == 1 && RejectPositiveBind && !RejectedBinds) {
            ++RejectedBinds;
            return FALSE;
        }
        return TextureTestObserver::SetTexture(handle, stage);
    }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count,
                         VxDrawPrimitiveData *data) override {
        ++Attempts;
        EXPECT_EQ(VX_TRIANGLELIST, type); EXPECT_EQ(3, count);
        EXPECT_NE(nullptr, indices); EXPECT_NE(nullptr, data);
        if (!indices || !data || count != 3) return FALSE;
        const CKBOOL channel = States[VXRENDERSTATE_ALPHABLENDENABLE] &&
            States[VXRENDERSTATE_SRCBLEND] == VXBLEND_ZERO &&
            States[VXRENDERSTATE_DESTBLEND] == VXBLEND_SRCCOLOR;
        if (channel) {
            SawPatchedDraw = TRUE;
            EXPECT_FALSE(States[VXRENDERSTATE_ZWRITEENABLE]);
            if (ThrowChannelDraw) {
                ThrowChannelDraw = FALSE;
                throw 99;
            }
            if (FailChannelDraw) {
                FailChannelDraw = FALSE;
                return FALSE;
            }
        }
        EXPECT_LT(Count, 16);
        if (Count >= 16) return FALSE;
        Draw &draw = Draws[Count++];
        draw.Blend = States[VXRENDERSTATE_ALPHABLENDENABLE] != 0;
        draw.ZWrite = States[VXRENDERSTATE_ZWRITEENABLE] != 0;
        draw.Cull = States[VXRENDERSTATE_CULLMODE]; draw.ZFunc = States[VXRENDERSTATE_ZFUNC];
        if (data->TexCoordPtr)
            memcpy(&draw.UV, static_cast<CKBYTE *>(data->TexCoordPtr) +
                indices[0] * data->TexCoordStride, sizeof(Vx2DVector));
        return TRUE;
    }
};

struct ChannelThrowCall {
    int Stage = 0, Calls = 0;
    CKBOOL Enabled = TRUE, SawPatchedMaterial = FALSE;
};

int ThrowChannelCallback(CKRenderContext *, CKMaterial *material, void *argument) {
    ChannelThrowCall *call = static_cast<ChannelThrowCall *>(argument);
    ++call->Calls;
    if (call->Enabled && call->Stage == 0) {
        call->Enabled = FALSE;
        call->SawPatchedMaterial = material->GetSourceBlend() == VXBLEND_ZERO &&
            material->GetDestBlend() == VXBLEND_SRCCOLOR &&
            material->AlphaBlendEnabled() && !material->ZWriteEnabled();
        throw 99;
    }
    return FALSE;
}

class UnwindMaterial : public RCKMaterial {
public:
    ChannelThrowCall *Call = nullptr;
    explicit UnwindMaterial(CKContext *context) : RCKMaterial(context) {}
    CKBOOL SetAsCurrent(CKRenderContext *context, CKBOOL lit, int stage) override {
        if (!Call) return RCKMaterial::SetAsCurrent(context, lit, stage);
        struct RestoreStage {
            ChannelThrowCall &Call;
            int Stage;
            RestoreStage(ChannelThrowCall &call, int stage) : Call(call), Stage(call.Stage) { Call.Stage = stage; }
            ~RestoreStage() { Call.Stage = Stage; }
        } restore(*Call, stage);
        return RCKMaterial::SetAsCurrent(context, lit, stage);
    }
};

class MeshChannelUnwindTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr;
    ChannelUnwindObserver State;
    CKRasterizerDriver Driver;
    VxMatrix Projection;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        Manager->m_DriverCount = 1; Manager->m_Drivers = new VxDriverDescEx[1]();
        Manager->m_Drivers[0].CapsUpToDate = TRUE;
        Manager->m_Drivers[0].RasterizerDriver = &Driver;
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 3;
        VxImageDescEx format; VxPixelFormat2ImageDesc(_32_ARGB8888, format);
        Manager->m_Drivers[0].TextureFormats.PushBack(format);
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0));
        Device = new RCKRenderContext(Context); Device->DetachAll();
        Device->m_RasterizerContext = &State; Device->m_RasterizerDriver = &Driver;
        Device->m_DriverIndex = 0; Device->m_DisplayWireframe = FALSE;
        Projection = VxMatrix::Identity();
        Projection[0][0] = 2.0f; Projection[1][1] = 3.0f; Projection[3][2] = -4.25f;
        Device->SetProjectionTransformationMatrix(Projection);
    }
    void TearDown() override {
        if (Device) {
            Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr;
            delete Device;
        }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    static void Pixel(RCKTexture &texture, CKDWORD value) {
        ASSERT_TRUE(texture.CreateImage(1, 1, 32, 0));
        CKBYTE *bytes = texture.LockSurfacePtr(0); ASSERT_NE(nullptr, bytes);
        memcpy(bytes, &value, sizeof(value)); texture.ReleaseSurfacePtr(0);
        texture.SetDesiredVideoFormat(_32_ARGB8888);
    }
    static void Geometry(RCKMesh &mesh, RCKMaterial &base, RCKMaterial *other = nullptr) {
        const int faces = other ? 2 : 1;
        ASSERT_TRUE(mesh.SetVertexCount(3 * faces)); ASSERT_TRUE(mesh.SetFaceCount(faces));
        for (int f = 0; f < faces; ++f) {
            mesh.SetFaceVertexIndex(f, f * 3, f * 3 + 1, f * 3 + 2);
            mesh.SetFaceMaterial(f, f ? other : &base);
            for (int v = 0; v < 3; ++v) {
                VxVector position(f * 10.0f + (v == 1 ? 1.0f : 0), v == 2 ? 1.0f : 0, 1);
                VxVector normal(.25f, .5f, .75f);
                mesh.SetVertexPosition(3 * f + v, &position); mesh.SetVertexNormal(3 * f + v, &normal);
                mesh.SetVertexTextureCoordinates(3 * f + v, .125f, .25f, -1);
                mesh.SetVertexColor(3 * f + v, 0xFF234567);
                mesh.SetVertexSpecularColor(3 * f + v, 0xFF123456);
            }
        }
        mesh.SetFlags((mesh.GetFlags() & ~(VXMESH_PRELITMODE | VXMESH_STRIPIFY)) | VXMESH_RENDERCHANNELS);
    }
    static void Authored(UnwindMaterial &material) {
        material.SetSourceBlend(VXBLEND_ONE); material.SetDestBlend(VXBLEND_ZERO);
        material.EnableAlphaBlend(FALSE); material.EnableZWrite(TRUE);
        material.SetTwoSided(TRUE); material.EnableAlphaTest(TRUE);
        material.SetAlphaFunc(VXCMP_GREATER); material.SetAlphaRef(113);
        material.EnablePerspectiveCorrection(TRUE); material.SetZFunc(VXCMP_LESSEQUAL);
    }
    static void Channel(RCKMesh &mesh, UnwindMaterial &material, RCKTexture &texture) {
        Authored(material); Pixel(texture, 0xFF445566); material.SetTexture(0, &texture);
        const int index = mesh.AddChannel(&material, FALSE); ASSERT_GE(index, 0);
        mesh.ActivateChannel(index, TRUE);
        mesh.SetChannelSourceBlend(index, VXBLEND_ZERO); mesh.SetChannelDestBlend(index, VXBLEND_SRCCOLOR);
        for (int v = 0; v < mesh.GetVertexCount(); ++v)
            mesh.SetVertexTextureCoordinates(v, .625f, .875f, index);
    }
    static VxDrawPrimitiveData Packet(RCKMesh &mesh) {
        VxDrawPrimitiveData data = {};
        data.VertexCount = mesh.GetVertexCount();
        data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_STAGES0;
        data.PositionPtr = mesh.GetPositionsPtr(&data.PositionStride);
        data.NormalPtr = mesh.GetNormalsPtr(&data.NormalStride);
        data.ColorPtr = mesh.GetColorsPtr(&data.ColorStride);
        data.SpecularColorPtr = mesh.GetSpecularColorsPtr(&data.SpecularColorStride);
        data.TexCoordPtr = mesh.GetTextureCoordinatesPtr(&data.TexCoordStride, -1);
        return data;
    }
    static void Streams(const VxDrawPrimitiveData &actual, const VxDrawPrimitiveData &saved) {
        EXPECT_EQ(saved.TexCoordPtr, actual.TexCoordPtr); EXPECT_EQ(saved.TexCoordStride, actual.TexCoordStride);
        EXPECT_EQ(saved.ColorPtr, actual.ColorPtr); EXPECT_EQ(saved.ColorStride, actual.ColorStride);
        EXPECT_EQ(saved.SpecularColorPtr, actual.SpecularColorPtr);
        EXPECT_EQ(saved.SpecularColorStride, actual.SpecularColorStride);
    }
    static void Material(UnwindMaterial &material) {
        EXPECT_EQ(VXBLEND_ONE, material.GetSourceBlend()); EXPECT_EQ(VXBLEND_ZERO, material.GetDestBlend());
        EXPECT_FALSE(material.AlphaBlendEnabled()); EXPECT_TRUE(material.ZWriteEnabled());
        EXPECT_TRUE(material.IsTwoSided()); EXPECT_TRUE(material.AlphaTestEnabled());
        EXPECT_EQ(VXCMP_GREATER, material.GetAlphaFunc()); EXPECT_EQ(113, material.GetAlphaRef());
        EXPECT_TRUE(material.PerspectiveCorrectionEnabled()); EXPECT_EQ(VXCMP_LESSEQUAL, material.GetZFunc());
    }
    void ProjectionRestored() {
        const VxMatrix &actual = Device->GetProjectionTransformationMatrix();
        const VxMatrix &rasterizer = State.Matrices[VXMATRIX_PROJECTION];
        for (int row = 0; row < 4; ++row)
            for (int column = 0; column < 4; ++column) {
                EXPECT_FLOAT_EQ(Projection[row][column], actual[row][column]);
                EXPECT_FLOAT_EQ(Projection[row][column], rasterizer[row][column]);
            }
    }
    void OrdinaryDraw(UnwindMaterial &material) {
        RCKMesh ordinary(Context); Geometry(ordinary, material);
        const int count = State.Count;
        ASSERT_EQ(1, ordinary.DefaultRender(Device, nullptr));
        ASSERT_EQ(count + 1, State.Count);
        EXPECT_FALSE(State.Draws[count].Blend); EXPECT_TRUE(State.Draws[count].ZWrite);
        EXPECT_EQ(VXCULL_NONE, State.Draws[count].Cull);
        EXPECT_EQ(VXCMP_LESSEQUAL, State.Draws[count].ZFunc);
        EXPECT_FLOAT_EQ(.125f, State.Draws[count].UV.x); EXPECT_FLOAT_EQ(.25f, State.Draws[count].UV.y);
        Material(material); ProjectionRestored();
    }
    static void ThrowCallback(UnwindMaterial &material, ChannelThrowCall &call) {
        material.Call = &call; material.SetCallback(ThrowChannelCallback, &call);
    }
};
}

TEST_F(MeshChannelUnwindTest, ChannelMaterialCallbackThrowRestoresDefinitionCallerStreamsAndProjection) {
    RCKMesh mesh(Context); RCKMaterial base(Context); UnwindMaterial material(Context); RCKTexture texture(Context);
    Geometry(mesh, base); Channel(mesh, material, texture);
    ASSERT_EQ(1, mesh.CreateRenderGroups()); mesh.UpdateChannelIndices();
    VxDrawPrimitiveData data = Packet(mesh), saved = data;
    ChannelThrowCall call; ThrowCallback(material, call);
    EXPECT_THROW(mesh.RenderChannels(Device, nullptr, &data, FALSE), int);
    EXPECT_TRUE(call.SawPatchedMaterial); EXPECT_EQ(0, State.Attempts);
    Material(material); Streams(data, saved); ProjectionRestored(); OrdinaryDraw(material);
}

TEST_F(MeshChannelUnwindTest, ChannelDrawThrowRestoresDefinitionCallerStreamsAndProjection) {
    RCKMesh mesh(Context); RCKMaterial base(Context); UnwindMaterial material(Context); RCKTexture texture(Context);
    Geometry(mesh, base); Channel(mesh, material, texture);
    ASSERT_EQ(1, mesh.CreateRenderGroups()); mesh.UpdateChannelIndices();
    VxDrawPrimitiveData data = Packet(mesh), saved = data;
    State.ThrowChannelDraw = TRUE;
    EXPECT_THROW(mesh.RenderChannels(Device, nullptr, &data, FALSE), int);
    EXPECT_TRUE(State.SawPatchedDraw); EXPECT_EQ(1, State.Attempts);
    Material(material); Streams(data, saved); ProjectionRestored(); OrdinaryDraw(material);
}

TEST_F(MeshChannelUnwindTest, OrdinaryChannelDrawFalseKeepsNativeReturnAndRestoresTemporaryState) {
    RCKMesh mesh(Context); RCKMaterial base(Context); UnwindMaterial material(Context); RCKTexture texture(Context);
    Geometry(mesh, base); Channel(mesh, material, texture);
    ASSERT_EQ(1, mesh.CreateRenderGroups()); mesh.UpdateChannelIndices();
    VxDrawPrimitiveData data = Packet(mesh), saved = data;
    State.FailChannelDraw = TRUE;
    EXPECT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, FALSE));
    EXPECT_TRUE(State.SawPatchedDraw); EXPECT_EQ(1, State.Attempts);
    Material(material); Streams(data, saved); ProjectionRestored(); OrdinaryDraw(material);
}

TEST_F(MeshChannelUnwindTest, SelectedMonoFallbackMaterialThrowRestoresDefinitionAndProjection) {
    RCKMesh mesh(Context); RCKMaterial base(Context); UnwindMaterial material(Context); RCKTexture texture(Context);
    Geometry(mesh, base); Channel(mesh, material, texture);
    ChannelThrowCall call; ThrowCallback(material, call);
    State.RejectPositiveBind = TRUE;
    EXPECT_THROW(mesh.DefaultRender(Device, nullptr), int);
    EXPECT_EQ(1, State.RejectedBinds); EXPECT_TRUE(call.SawPatchedMaterial);
    EXPECT_EQ(1, State.Count);
    Material(material); ProjectionRestored(); OrdinaryDraw(material);
}

TEST_F(MeshChannelUnwindTest, SelectedMonoFallbackDrawThrowRestoresDefinitionAndProjection) {
    RCKMesh mesh(Context); RCKMaterial base(Context); UnwindMaterial material(Context); RCKTexture texture(Context);
    Geometry(mesh, base); Channel(mesh, material, texture);
    State.RejectPositiveBind = TRUE; State.ThrowChannelDraw = TRUE;
    EXPECT_THROW(mesh.DefaultRender(Device, nullptr), int);
    EXPECT_EQ(1, State.RejectedBinds); EXPECT_TRUE(State.SawPatchedDraw);
    EXPECT_EQ(1, State.Count); EXPECT_EQ(2, State.Attempts);
    Material(material); ProjectionRestored(); OrdinaryDraw(material);
}

TEST_F(MeshChannelUnwindTest, MixedGroupLayerThrowRestoresItsMaterialAndProjection) {
    RCKMesh mesh(Context); RCKMaterial plain(Context), combined(Context); UnwindMaterial material(Context);
    RCKTexture texture(Context), primary(Context), extra(Context);
    Pixel(primary, 0xFF112233); Pixel(extra, 0xFF778899);
    combined.SetTexture(0, &primary); combined.SetTexture(1, &extra); combined.SetEffect(VXEFFECT_2TEXTURES);
    Geometry(mesh, plain, &combined); Channel(mesh, material, texture);
    State.ThrowChannelDraw = TRUE;
    EXPECT_THROW(mesh.DefaultRender(Device, nullptr), int);
    EXPECT_TRUE(State.SawPatchedDraw); EXPECT_EQ(1, State.Count); EXPECT_EQ(2, State.Attempts);
    Material(material); ProjectionRestored(); OrdinaryDraw(material);
}
