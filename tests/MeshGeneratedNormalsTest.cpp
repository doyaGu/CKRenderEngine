#include <gtest/gtest.h>
#include <cstring>
#include <stdexcept>
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "TextureTestObserver.h"

namespace {
class GeneratedNormalObserver : public TextureTestObserver {
public:
    struct Draw {
        CKDWORD Flags = 0, Lighting = 0, Coordinates[3] = {}, Operations[3] = {}, Textures[3] = {};
        int Count = 0, VertexCount = 0, NormalOffset = -1;
        CKDWORD NormalStride = 0;
        CKBOOL HasNormals = FALSE, HasColors = FALSE, HasSpecular = FALSE;
        CKWORD Indices[3] = {};
        VxVector Normals[3]; Vx2DVector UVs[3];
        CKDWORD Colors[3] = {}, Specular[3] = {};
    };
    Draw Draws[24]; int DrawCount = 0, FailDraw = -1, ThrowDraw = -1;
    CKBOOL RejectPositiveBind = FALSE; int RejectedBinds = 0;
    RCKRenderContext *RecursiveContext = nullptr;
    VxDrawPrimitiveData *RecursiveData = nullptr;
    CKBOOL RecurseAtDraw = FALSE;
    CKBOOL SetTextureStageState(int stage, CKRST_TEXTURESTAGESTATETYPE state, CKDWORD value) override {
        // This fixture uses only this legacy map-blend mode. Normalize its
        // public color-operation contract so OP0 does not fake a disabled chain.
        // No shader, pixel composition or other legacy blend mode is modeled.
        if (state == CKRST_TSS_TEXTUREMAPBLEND && value == VXTEXTUREBLEND_MODULATEALPHA)
            MaterialRenderObserver::SetTextureStageState(stage, CKRST_TSS_OP, CKRST_TOP_MODULATE);
        return MaterialRenderObserver::SetTextureStageState(stage, state, value);
    }
    CKBOOL SetTexture(CKDWORD handle, int stage) override {
        if (handle && stage == 1 && RejectPositiveBind && !RejectedBinds) {
            ++RejectedBinds; return FALSE;
        }
        return TextureTestObserver::SetTexture(handle, stage);
    }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        EXPECT_EQ(VX_TRIANGLELIST, type); EXPECT_EQ(3, count); EXPECT_NE(nullptr, indices); EXPECT_NE(nullptr, data);
        EXPECT_LT(DrawCount, 24);
        if (!data || !indices || DrawCount >= 24) return FALSE;
        const int ordinal = DrawCount++; Draw &draw = Draws[ordinal];
        draw.Count = count; draw.VertexCount = data->VertexCount; draw.Flags = data->Flags;
        draw.Lighting = States[VXRENDERSTATE_LIGHTING]; draw.NormalStride = data->NormalStride;
        draw.HasNormals = data->NormalPtr != nullptr; draw.HasColors = data->ColorPtr != nullptr;
        draw.HasSpecular = data->SpecularColorPtr != nullptr;
        CKRSTVertexLayout layout; CKRSTGetVertexLayout(data->Flags, nullptr, &layout); draw.NormalOffset = layout.NormalOffset;
        for (int stage = 0; stage < 3; ++stage) {
            draw.Coordinates[stage] = StageStates[stage][CKRST_TSS_TEXCOORDINDEX];
            draw.Operations[stage] = StageStates[stage][CKRST_TSS_OP]; draw.Textures[stage] = Textures[stage];
        }
        for (int i = 0; i < count && i < 3; ++i) {
            const CKWORD vertex = indices[i]; draw.Indices[i] = vertex;
            EXPECT_LT(vertex, data->VertexCount); if (vertex >= data->VertexCount) continue;
            if (data->NormalPtr) std::memcpy(&draw.Normals[i], static_cast<CKBYTE *>(data->NormalPtr) + vertex * data->NormalStride, sizeof(VxVector));
            if (data->TexCoordPtr) std::memcpy(&draw.UVs[i], static_cast<CKBYTE *>(data->TexCoordPtr) + vertex * data->TexCoordStride, sizeof(Vx2DVector));
            if (data->ColorPtr) std::memcpy(&draw.Colors[i], static_cast<CKBYTE *>(data->ColorPtr) + vertex * data->ColorStride, sizeof(CKDWORD));
            if (data->SpecularColorPtr) std::memcpy(&draw.Specular[i], static_cast<CKBYTE *>(data->SpecularColorPtr) + vertex * data->SpecularColorStride, sizeof(CKDWORD));
        }
        if (ordinal == ThrowDraw) throw std::runtime_error("generated-normal observer draw failure");
        if (RecurseAtDraw) {
            RecurseAtDraw = FALSE;
            EXPECT_NE(nullptr, RecursiveContext); EXPECT_NE(nullptr, RecursiveData);
            if (RecursiveContext && RecursiveData)
                EXPECT_TRUE(RecursiveContext->RCKRenderContext::DrawPrimitive(type, indices, count, RecursiveData));
        }
        return ordinal != FailDraw;
    }
};

class GeneratedNormalContext : public RCKRenderContext {
public:
    int Calls = 0, GenerationOverride = -1;
    CKDWORD BeforeFlags = 0, AfterFlags = 0;
    CKBOOL DrawOtherData = FALSE, DrawOtherContext = FALSE, ArmRecursion = FALSE;
    GeneratedNormalObserver *Observer = nullptr;
    RCKRenderContext *Other = nullptr;
    explicit GeneratedNormalContext(CKContext *context) : RCKRenderContext(context) {}
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count, VxDrawPrimitiveData *data) override {
        ++Calls; BeforeFlags = data->Flags;
        if (GenerationOverride >= 0)
            m_RasterizerContext->SetTextureStageState(0, CKRST_TSS_TEXCOORDINDEX,
                CKRSTPackTexcoordIndex(0, static_cast<CKDWORD>(GenerationOverride)));
        if (DrawOtherData || DrawOtherContext) {
            VxDrawPrimitiveData unrelated;
            UserDrawPrimitiveDataClass::CopySubmissionData(unrelated, data);
            unrelated.Flags |= CKRST_DP_LIGHT;
            if (DrawOtherContext) {
                EXPECT_NE(nullptr, Other);
                if (Other) EXPECT_TRUE(Other->DrawPrimitive(type, indices, count, &unrelated));
            } else {
                EXPECT_TRUE(RCKRenderContext::DrawPrimitive(type, indices, count, &unrelated));
            }
            // A derived renderer's chosen state must survive its own delegation.
            m_RasterizerContext->SetRenderState(VXRENDERSTATE_LIGHTING, FALSE);
        }
        if (ArmRecursion && Observer) {
            Observer->RecursiveContext = this; Observer->RecursiveData = data; Observer->RecurseAtDraw = TRUE;
        }
        const CKBOOL result = RCKRenderContext::DrawPrimitive(type, indices, count, data);
        AfterFlags = data->Flags;
        return result;
    }
};

class GeneratedNormalMesh : public RCKMesh {
public:
    explicit GeneratedNormalMesh(CKContext *context) : RCKMesh(context) {}
    void Cold() { m_Valid = 0; }
};
struct OwnedNormalPlan {
    RCKTexture *Texture = nullptr;
    CKDWORD Generation = CKRST_TEXGEN_CAMERASPACEREFLECTIONVECTOR;
    CKBOOL Lighting = FALSE, DisableFirst = FALSE, StaleSuffix = FALSE;
    int Calls = 0;
};
int OwnGeneratedStage(CKRenderContext *context, CKMaterial *, void *argument) {
    OwnedNormalPlan &plan = *static_cast<OwnedNormalPlan *>(argument); ++plan.Calls;
    CKRasterizerContext *rst = static_cast<RCKRenderContext *>(context)->m_RasterizerContext;
    EXPECT_NE(nullptr, plan.Texture); if (plan.Texture) EXPECT_TRUE(plan.Texture->SetAsCurrent(context, FALSE, 0));
    rst->SetRenderState(VXRENDERSTATE_LIGHTING, plan.Lighting);
    rst->SetTextureStageState(0, CKRST_TSS_OP, plan.DisableFirst ? CKRST_TOP_DISABLE : CKRST_TOP_MODULATE);
    rst->SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    rst->SetTextureStageState(0, CKRST_TSS_TEXCOORDINDEX, CKRSTPackTexcoordIndex(0, plan.Generation));
    rst->SetTextureStageState(1, CKRST_TSS_OP, CKRST_TOP_DISABLE);
    if (plan.StaleSuffix) {
        rst->SetTextureStageState(2, CKRST_TSS_OP, CKRST_TOP_MODULATE);
        rst->SetTextureStageState(2, CKRST_TSS_TEXCOORDINDEX,
            CKRSTPackTexcoordIndex(2, CKRST_TEXGEN_CAMERASPACEREFLECTIONVECTOR));
    }
    return TRUE;
}

class MeshGeneratedNormalsTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr; RCKRenderManager *Manager = nullptr;
    GeneratedNormalContext *Device = nullptr;
    GeneratedNormalObserver State; CKRasterizerDriver Driver;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); Manager = new RCKRenderManager(Context);
        Manager->m_VertexCache.Value = 0; Manager->m_DriverCount = 1; Manager->m_Drivers = new VxDriverDescEx[1]();
        Manager->m_Drivers[0].CapsUpToDate = TRUE; Manager->m_Drivers[0].RasterizerDriver = &Driver;
        Manager->m_Drivers[0].Caps3D.MaxNumberTextureStage = 4;
        VxImageDescEx format; VxPixelFormat2ImageDesc(_32_ARGB8888, format); Manager->m_Drivers[0].TextureFormats.PushBack(format);
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0)); Device = new GeneratedNormalContext(Context);
        Device->DetachAll(); Device->m_RasterizerContext = &State; Device->Observer = &State;
        Device->m_RasterizerDriver = &Driver; Device->m_DriverIndex = 0; Device->m_DisplayWireframe = FALSE;
        Device->BeginFrameErrorTracking();
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; delete Device; }
        if (Context) CKCloseContext(Context); CKShutdown();
    }
    static void Pixel(RCKTexture &texture, CKDWORD value) {
        ASSERT_TRUE(texture.CreateImage(1, 1, 32, 0)); CKBYTE *bytes = texture.LockSurfacePtr(0); ASSERT_NE(nullptr, bytes);
        std::memcpy(bytes, &value, sizeof(value)); texture.ReleaseSurfacePtr(0); texture.SetDesiredVideoFormat(_32_ARGB8888);
    }
    static VxDrawPrimitiveData Geometry(GeneratedNormalMesh &mesh, RCKMaterial *base = nullptr) {
        EXPECT_TRUE(mesh.SetVertexCount(3)); EXPECT_TRUE(mesh.SetFaceCount(1)); mesh.SetFaceVertexIndex(0, 2, 0, 1);
        VxVector normals[3] = {VxVector(0, .6f, .8f), VxVector(.6f, .8f, 0), VxVector(.8f, 0, .6f)};
        for (int i = 0; i < 3; ++i) {
            VxVector position(static_cast<float>(i), static_cast<float>(i + 2), 1);
            mesh.SetVertexPosition(i, &position); mesh.SetVertexNormal(i, &normals[i]);
            mesh.SetVertexTextureCoordinates(i, .125f, .25f, -1);
            mesh.SetVertexColor(i, 0xFFAABB00u + static_cast<CKDWORD>(i));
            mesh.SetVertexSpecularColor(i, 0xFF112200u + static_cast<CKDWORD>(i));
        }
        mesh.SetLitMode(VX_LITMESH); if (base) mesh.SetFaceMaterial(0, base);
        mesh.SetFlags((mesh.GetFlags() | VXMESH_RENDERCHANNELS) & ~VXMESH_STRIPIFY);
        VxDrawPrimitiveData data = {}; data.VertexCount = 3; data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_STAGES0;
        data.PositionPtr = mesh.GetPositionsPtr(&data.PositionStride); data.NormalPtr = mesh.GetNormalsPtr(&data.NormalStride);
        data.TexCoordPtr = mesh.GetTextureCoordinatesPtr(&data.TexCoordStride, -1);
        data.ColorPtr = mesh.GetColorsPtr(&data.ColorStride); data.SpecularColorPtr = mesh.GetSpecularColorsPtr(&data.SpecularColorStride);
        return data;
    }
    static int Channel(GeneratedNormalMesh &mesh, RCKMaterial &material, VXBLEND_MODE source = VXBLEND_ZERO,
                       VXBLEND_MODE dest = VXBLEND_SRCCOLOR) {
        const int index = mesh.AddChannel(&material, FALSE); EXPECT_GE(index, 0); if (index < 0) return index;
        mesh.ActivateChannel(index, TRUE); mesh.SetChannelSourceBlend(index, source); mesh.SetChannelDestBlend(index, dest);
        for (int i = 0; i < 3; ++i) mesh.SetVertexTextureCoordinates(i, .375f, .625f, index);
        return index;
    }
    static void NormalPayload(const GeneratedNormalObserver::Draw &draw, CKBOOL lighting) {
        EXPECT_EQ(3, draw.Count); EXPECT_EQ(3, draw.VertexCount); EXPECT_EQ(lighting, draw.Lighting);
        EXPECT_NE(0u, draw.Flags & CKRST_DP_LIGHT); ASSERT_GE(draw.NormalOffset, 0); ASSERT_TRUE(draw.HasNormals);
        EXPECT_GT(draw.NormalStride, sizeof(VxVector));
        EXPECT_EQ(2, draw.Indices[0]); EXPECT_EQ(0, draw.Indices[1]); EXPECT_EQ(1, draw.Indices[2]);
        EXPECT_FLOAT_EQ(.8f, draw.Normals[0].x); EXPECT_FLOAT_EQ(0, draw.Normals[0].y); EXPECT_FLOAT_EQ(.6f, draw.Normals[0].z);
        EXPECT_FLOAT_EQ(0, draw.Normals[1].x); EXPECT_FLOAT_EQ(.6f, draw.Normals[1].y); EXPECT_FLOAT_EQ(.8f, draw.Normals[1].z);
        EXPECT_FLOAT_EQ(.6f, draw.Normals[2].x); EXPECT_FLOAT_EQ(.8f, draw.Normals[2].y); EXPECT_FLOAT_EQ(0, draw.Normals[2].z);
    }
    static void NoNormalDeclaration(const GeneratedNormalObserver::Draw &draw) {
        EXPECT_EQ(FALSE, draw.Lighting); EXPECT_EQ(0u, draw.Flags & CKRST_DP_LIGHT); EXPECT_EQ(-1, draw.NormalOffset);
    }
    static void Bump(RCKMaterial &material, RCKTexture &color, RCKTexture &bump, RCKTexture &environment) {
        Pixel(color, 0xFF112233); Pixel(bump, 0xFF445566); Pixel(environment, 0xFF778899);
        material.SetTexture(0, &color); material.SetTexture(1, &bump); material.SetTexture(2, &environment); material.SetEffect(VXEFFECT_BUMPENV);
        ASSERT_NE(nullptr, material.GetEffectParameter()); CKStructHelper helper(material.GetEffectParameter());
        ASSERT_EQ(4, helper.GetMemberCount()); CKDWORD generation = 0; ASSERT_EQ(CK_OK, helper[2]->GetValue(&generation));
        ASSERT_EQ(VXEFFECT_TGREFLECT, generation); // Unedited automatic default, not a synthetic override.
    }
    void Texgen(RCKMaterial &material, RCKTexture &texture, CKDWORD generation) {
        Pixel(texture, 0xFF112233); material.SetTexture(0, &texture); material.SetEffect(VXEFFECT_TEXGEN);
        ASSERT_NE(nullptr, material.GetEffectParameter()); ASSERT_EQ(CK_OK, material.GetEffectParameter()->SetValue(&generation));
    }
    void Owned(RCKMaterial &material, RCKTexture &texture, OwnedNormalPlan &plan) {
        Pixel(texture, 0xFF112233); plan.Texture = &texture; material.SetCallback(OwnGeneratedStage, &plan);
    }
};
}

TEST_F(MeshGeneratedNormalsTest, PublicWrapperNormalDeclarationStillEnablesLighting) {
    GeneratedNormalMesh mesh(Context); VxDrawPrimitiveData data = Geometry(mesh); CKWORD indices[3] = {2, 0, 1};
    State.SetRenderState(VXRENDERSTATE_LIGHTING, FALSE);
    CKRenderContext *api = Device; ASSERT_TRUE(api->DrawPrimitive(VX_TRIANGLELIST, indices, 3, &data)); ASSERT_EQ(1, State.DrawCount);
    NormalPayload(State.Draws[0], TRUE); EXPECT_EQ(1, Device->Calls);
}
TEST_F(MeshGeneratedNormalsTest, PublicWrapperPointerWithoutNormalDeclarationKeepsLightingOff) {
    GeneratedNormalMesh mesh(Context); VxDrawPrimitiveData data = Geometry(mesh); CKWORD indices[3] = {2, 0, 1}; data.Flags &= ~CKRST_DP_LIGHT;
    State.SetRenderState(VXRENDERSTATE_LIGHTING, TRUE);
    ASSERT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, indices, 3, &data)); ASSERT_EQ(1, State.DrawCount);
    NoNormalDeclaration(State.Draws[0]); EXPECT_EQ(0u, data.Flags & (CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR));
    EXPECT_EQ(CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR, State.Draws[0].Flags & (CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR));
}
TEST_F(MeshGeneratedNormalsTest, OrdinaryUnlitBumpReflectionDeclaresStridedNormalsWithoutLighting) {
    GeneratedNormalMesh mesh(Context); VxDrawPrimitiveData data = Geometry(mesh); RCKMaterial material(Context);
    RCKTexture color(Context), bump(Context), environment(Context); Bump(material, color, bump, environment); Channel(mesh, material);
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, FALSE)); ASSERT_EQ(1, State.DrawCount); NormalPayload(State.Draws[0], FALSE);
    EXPECT_FALSE(State.Draws[0].HasColors); EXPECT_FALSE(State.Draws[0].HasSpecular);
    EXPECT_EQ(0u, State.Draws[0].Flags & (CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR));
    EXPECT_EQ(CKRSTPackTexcoordIndex(2, CKRST_TEXGEN_CAMERASPACEREFLECTIONVECTOR), State.Draws[0].Coordinates[2]);
    EXPECT_FLOAT_EQ(.375f, State.Draws[0].UVs[0].x); EXPECT_FLOAT_EQ(.625f, State.Draws[0].UVs[0].y);
    EXPECT_EQ(3, State.Loads);
}
TEST_F(MeshGeneratedNormalsTest, PrelitBumpKeepsAuthoredColorsAndGeneratedNormalWithoutLighting) {
    GeneratedNormalMesh mesh(Context); VxDrawPrimitiveData data = Geometry(mesh); mesh.SetLitMode(VX_PRELITMESH);
    RCKMaterial material(Context); RCKTexture color(Context), bump(Context), environment(Context); Bump(material, color, bump, environment);
    Channel(mesh, material, VXBLEND_ONE, VXBLEND_ONE);
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, FALSE)); ASSERT_EQ(1, State.DrawCount); NormalPayload(State.Draws[0], FALSE);
    EXPECT_TRUE(State.Draws[0].HasColors); EXPECT_TRUE(State.Draws[0].HasSpecular);
    EXPECT_EQ(CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR, State.Draws[0].Flags & (CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR));
    EXPECT_EQ(0xFFAABB02u, State.Draws[0].Colors[0]); EXPECT_EQ(0xFF112202u, State.Draws[0].Specular[0]);
}
TEST_F(MeshGeneratedNormalsTest, OrdinaryUnlitPositionGenerationDoesNotDeclareNormal) {
    GeneratedNormalMesh mesh(Context); VxDrawPrimitiveData data = Geometry(mesh); RCKMaterial material(Context); RCKTexture texture(Context);
    Texgen(material, texture, VXEFFECT_TGPLANAR); Channel(mesh, material);
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, FALSE)); ASSERT_EQ(1, State.DrawCount); NoNormalDeclaration(State.Draws[0]);
    EXPECT_EQ(CKRSTPackTexcoordIndex(0, CKRST_TEXGEN_CAMERASPACEPOSITION), State.Draws[0].Coordinates[0]);
}
TEST_F(MeshGeneratedNormalsTest, OrdinaryUnlitPassthroughDoesNotDeclareUnusedNormal) {
    GeneratedNormalMesh mesh(Context); VxDrawPrimitiveData data = Geometry(mesh); RCKMaterial material(Context); RCKTexture texture(Context);
    Texgen(material, texture, VXEFFECT_TGNONE); Channel(mesh, material);
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, FALSE)); ASSERT_EQ(1, State.DrawCount); NoNormalDeclaration(State.Draws[0]);
    EXPECT_EQ(0u, State.Draws[0].Coordinates[0]);
}
TEST_F(MeshGeneratedNormalsTest, OwnedNormalGenerationKeepsCallbackLightingDisabled) {
    GeneratedNormalMesh mesh(Context); VxDrawPrimitiveData data = Geometry(mesh); RCKMaterial material(Context); RCKTexture texture(Context); OwnedNormalPlan plan;
    plan.Generation = CKRST_TEXGEN_CAMERASPACENORMAL; Owned(material, texture, plan); Channel(mesh, material);
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, FALSE)); ASSERT_EQ(1, State.DrawCount); NormalPayload(State.Draws[0], FALSE); EXPECT_EQ(1, plan.Calls);
    EXPECT_EQ(CKRSTPackTexcoordIndex(0, CKRST_TEXGEN_CAMERASPACENORMAL), State.Draws[0].Coordinates[0]);
    EXPECT_TRUE(State.Draws[0].HasColors); EXPECT_TRUE(State.Draws[0].HasSpecular);
    EXPECT_EQ(CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR, State.Draws[0].Flags & (CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR));
    EXPECT_EQ(0xFFAABB02u, State.Draws[0].Colors[0]); EXPECT_EQ(0xFF112202u, State.Draws[0].Specular[0]);
    EXPECT_EQ(0xFFAABB00u, State.Draws[0].Colors[1]); EXPECT_EQ(0xFF112200u, State.Draws[0].Specular[1]);
    EXPECT_EQ(0xFFAABB01u, State.Draws[0].Colors[2]); EXPECT_EQ(0xFF112201u, State.Draws[0].Specular[2]);
}
TEST_F(MeshGeneratedNormalsTest, OwnedReflectionKeepsCallbackLightingEnabledForModulation) {
    GeneratedNormalMesh mesh(Context); VxDrawPrimitiveData data = Geometry(mesh); RCKMaterial material(Context); RCKTexture texture(Context); OwnedNormalPlan plan;
    plan.Lighting = TRUE; Owned(material, texture, plan); Channel(mesh, material);
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, FALSE)); ASSERT_EQ(1, State.DrawCount); NormalPayload(State.Draws[0], TRUE); EXPECT_EQ(1, plan.Calls);
    EXPECT_TRUE(State.Draws[0].HasColors); EXPECT_TRUE(State.Draws[0].HasSpecular);
}
TEST_F(MeshGeneratedNormalsTest, DisabledFirstStageDoesNotDemandItsReflectionNormal) {
    GeneratedNormalMesh mesh(Context); VxDrawPrimitiveData data = Geometry(mesh); RCKMaterial material(Context); RCKTexture texture(Context); OwnedNormalPlan plan;
    plan.DisableFirst = TRUE; Owned(material, texture, plan); Channel(mesh, material);
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, FALSE)); ASSERT_EQ(1, State.DrawCount); NoNormalDeclaration(State.Draws[0]);
    EXPECT_EQ(CKRST_TOP_DISABLE, State.Draws[0].Operations[0]);
}
TEST_F(MeshGeneratedNormalsTest, DisabledStageStopsBeforeStaleLaterReflectionCoordinates) {
    GeneratedNormalMesh mesh(Context); VxDrawPrimitiveData data = Geometry(mesh); RCKMaterial material(Context); RCKTexture texture(Context); OwnedNormalPlan plan;
    plan.Generation = CKRST_TEXGEN_PASSTHRU; plan.StaleSuffix = TRUE; Owned(material, texture, plan); Channel(mesh, material);
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, FALSE)); ASSERT_EQ(1, State.DrawCount); NoNormalDeclaration(State.Draws[0]);
    EXPECT_EQ(CKRST_TOP_DISABLE, State.Draws[0].Operations[1]);
    EXPECT_EQ(CKRSTPackTexcoordIndex(2, CKRST_TEXGEN_CAMERASPACEREFLECTIONVECTOR), State.Draws[0].Coordinates[2]);
}
TEST_F(MeshGeneratedNormalsTest, DerivedContextSamePointerDelegationPreservesVirtualDispatchAndInputFlags) {
    GeneratedNormalMesh mesh(Context); VxDrawPrimitiveData data = Geometry(mesh); RCKMaterial material(Context); RCKTexture texture(Context); OwnedNormalPlan plan;
    Owned(material, texture, plan); Channel(mesh, material);
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, FALSE)); ASSERT_EQ(1, State.DrawCount); NormalPayload(State.Draws[0], FALSE);
    EXPECT_EQ(1, Device->Calls); EXPECT_EQ(Device->BeforeFlags, Device->AfterFlags); EXPECT_EQ(0u, Device->AfterFlags & CKRST_DP_LIGHT);
}
TEST_F(MeshGeneratedNormalsTest, DerivedContextGenerationChangeIsObservedAtBaseDelegation) {
    GeneratedNormalMesh mesh(Context); VxDrawPrimitiveData data = Geometry(mesh); RCKMaterial material(Context); RCKTexture texture(Context); OwnedNormalPlan plan;
    Owned(material, texture, plan); Channel(mesh, material); Device->GenerationOverride = CKRST_TEXGEN_CAMERASPACEPOSITION;
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, FALSE)); ASSERT_EQ(1, State.DrawCount); NoNormalDeclaration(State.Draws[0]);
    EXPECT_EQ(CKRSTPackTexcoordIndex(0, CKRST_TEXGEN_CAMERASPACEPOSITION), State.Draws[0].Coordinates[0]); EXPECT_EQ(1, Device->Calls);
}
TEST_F(MeshGeneratedNormalsTest, DerivedContextDifferentDataCallUsesOrdinaryPublicLightingContract) {
    GeneratedNormalMesh mesh(Context); VxDrawPrimitiveData data = Geometry(mesh); RCKMaterial material(Context); RCKTexture texture(Context); OwnedNormalPlan plan;
    Owned(material, texture, plan); Channel(mesh, material); Device->DrawOtherData = TRUE;
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, FALSE)); ASSERT_EQ(2, State.DrawCount);
    NormalPayload(State.Draws[0], TRUE); NormalPayload(State.Draws[1], FALSE); EXPECT_EQ(1, Device->Calls);
}
TEST_F(MeshGeneratedNormalsTest, DerivedContextOtherContextCallDoesNotInheritMeshSubmissionScope) {
    GeneratedNormalObserver otherState; RCKRenderContext other(Context); other.DetachAll(); other.m_RasterizerContext = &otherState;
    GeneratedNormalMesh mesh(Context); VxDrawPrimitiveData data = Geometry(mesh); RCKMaterial material(Context); RCKTexture texture(Context); OwnedNormalPlan plan;
    Owned(material, texture, plan); Channel(mesh, material); Device->DrawOtherContext = TRUE; Device->Other = &other;
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, FALSE)); ASSERT_EQ(1, State.DrawCount); ASSERT_EQ(1, otherState.DrawCount);
    NormalPayload(otherState.Draws[0], TRUE); NormalPayload(State.Draws[0], FALSE); other.m_RasterizerContext = nullptr;
}
TEST_F(MeshGeneratedNormalsTest, RecursiveBaseCallWithSameOriginalDataCannotClaimActiveOuterScope) {
    GeneratedNormalMesh mesh(Context); VxDrawPrimitiveData data = Geometry(mesh); RCKMaterial material(Context); RCKTexture texture(Context); OwnedNormalPlan plan;
    Owned(material, texture, plan); Channel(mesh, material); Device->ArmRecursion = TRUE;
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, FALSE)); ASSERT_EQ(2, State.DrawCount);
    NormalPayload(State.Draws[0], FALSE); NoNormalDeclaration(State.Draws[1]); EXPECT_EQ(1, Device->Calls);
}
TEST_F(MeshGeneratedNormalsTest, DrawFailureEndsScopeBeforeNextOrdinaryPublicCall) {
    GeneratedNormalMesh mesh(Context); VxDrawPrimitiveData data = Geometry(mesh); RCKMaterial material(Context); RCKTexture texture(Context); OwnedNormalPlan plan;
    Owned(material, texture, plan); Channel(mesh, material); State.FailDraw = 0;
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, FALSE)); ASSERT_EQ(1, State.DrawCount); NormalPayload(State.Draws[0], FALSE);
    EXPECT_EQ(CKERR_INVALIDOPERATION, Device->m_FrameRenderError); EXPECT_EQ(0, Device->GetStats().NbTrianglesDrawn);
    data.Flags |= CKRST_DP_LIGHT; CKWORD indices[3] = {2, 0, 1}; State.FailDraw = -1;
    ASSERT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, indices, 3, &data)); ASSERT_EQ(2, State.DrawCount); NormalPayload(State.Draws[1], TRUE);
    EXPECT_EQ(1, Device->GetStats().NbTrianglesDrawn);
}
TEST_F(MeshGeneratedNormalsTest, ThrownDrawEndsScopeBeforeNextPublicCallWithSameDataAndContext) {
    GeneratedNormalMesh mesh(Context); VxDrawPrimitiveData data = Geometry(mesh); RCKMaterial material(Context); RCKTexture texture(Context); OwnedNormalPlan plan;
    Owned(material, texture, plan); Channel(mesh, material); State.ThrowDraw = 0;
    EXPECT_THROW(mesh.RenderChannels(Device, nullptr, &data, FALSE), std::runtime_error); ASSERT_EQ(1, State.DrawCount); NormalPayload(State.Draws[0], FALSE);
    data.Flags |= CKRST_DP_LIGHT; CKWORD indices[3] = {2, 0, 1}; State.ThrowDraw = -1;
    ASSERT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, indices, 3, &data)); ASSERT_EQ(2, State.DrawCount); NormalPayload(State.Draws[1], TRUE);
}
TEST_F(MeshGeneratedNormalsTest, FailedPositiveBindingFallbackDeclaresGeneratedNormalsAndKeepsUnlit) {
    GeneratedNormalMesh mesh(Context); RCKMaterial base(Context), material(Context); VxDrawPrimitiveData data = Geometry(mesh, &base); RCKTexture texture(Context);
    Texgen(material, texture, VXEFFECT_TGREFLECT); Channel(mesh, material); State.RejectPositiveBind = TRUE; State.FailDraw = 1;
    ASSERT_EQ(1, mesh.CreateRenderGroups()); mesh.Cold(); ASSERT_EQ(1, mesh.DefaultRender(Device, nullptr));
    EXPECT_EQ(1, State.RejectedBinds); ASSERT_EQ(2, State.DrawCount); NormalPayload(State.Draws[1], FALSE);
    EXPECT_EQ(CKRSTPackTexcoordIndex(0, CKRST_TEXGEN_CAMERASPACEREFLECTIONVECTOR), State.Draws[1].Coordinates[0]);
    EXPECT_EQ(CKERR_INVALIDOPERATION, Device->m_FrameRenderError);
    EXPECT_FLOAT_EQ(.375f, State.Draws[1].UVs[0].x); EXPECT_FLOAT_EQ(.625f, State.Draws[1].UVs[0].y);
    data.Flags |= CKRST_DP_LIGHT; CKWORD indices[3] = {2, 0, 1}; State.FailDraw = -1;
    ASSERT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, indices, 3, &data)); ASSERT_EQ(3, State.DrawCount); NormalPayload(State.Draws[2], TRUE);
}
