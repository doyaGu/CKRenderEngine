#include <gtest/gtest.h>
#include <cstring>
#include <stdexcept>
#include "CKAll.h"
#include "RCKMesh.h"
#include "RCKMaterial.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "MaterialRenderObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class DelegationBoundary : public MaterialRenderObserver {
public:
    struct Draw {
        CKDWORD Lighting = 0, Flags = 0;
        int Count = 0, VertexCount = 0;
        CKWORD Indices[3] = {};
        VxVector Positions[3], Normals[3];
        Vx2DVector UVs[3];
        CKDWORD Colors[3] = {}, Specular[3] = {};
    } Draws[8];
    int Attempts = 0, FailAt = -1, ThrowAt = -1;
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count,
                         VxDrawPrimitiveData *data) override {
        EXPECT_EQ(VX_TRIANGLELIST, type); EXPECT_EQ(3, count);
        EXPECT_NE(nullptr, indices); EXPECT_NE(nullptr, data); EXPECT_LT(Attempts, 8);
        if (!data || !indices || count != 3 || Attempts >= 8) return FALSE;
        const int ordinal = Attempts++;
        Draw &draw = Draws[ordinal];
        draw.Lighting = States[VXRENDERSTATE_LIGHTING]; draw.Flags = data->Flags;
        draw.Count = count; draw.VertexCount = data->VertexCount;
        EXPECT_NE(nullptr, data->PositionPtr); EXPECT_NE(nullptr, data->NormalPtr);
        EXPECT_NE(nullptr, data->TexCoordPtr); EXPECT_NE(nullptr, data->ColorPtr);
        EXPECT_NE(nullptr, data->SpecularColorPtr);
        for (int i = 0; i < 3; ++i) {
            const CKWORD vertex = indices[i]; draw.Indices[i] = vertex;
            EXPECT_LT(vertex, data->VertexCount);
            if (vertex >= data->VertexCount) continue;
            if (data->PositionPtr) std::memcpy(&draw.Positions[i],
                static_cast<CKBYTE *>(data->PositionPtr) + vertex * data->PositionStride, sizeof(VxVector));
            if (data->NormalPtr) std::memcpy(&draw.Normals[i],
                static_cast<CKBYTE *>(data->NormalPtr) + vertex * data->NormalStride, sizeof(VxVector));
            if (data->TexCoordPtr) std::memcpy(&draw.UVs[i],
                static_cast<CKBYTE *>(data->TexCoordPtr) + vertex * data->TexCoordStride, sizeof(Vx2DVector));
            if (data->ColorPtr) std::memcpy(&draw.Colors[i],
                static_cast<CKBYTE *>(data->ColorPtr) + vertex * data->ColorStride, sizeof(CKDWORD));
            if (data->SpecularColorPtr) std::memcpy(&draw.Specular[i],
                static_cast<CKBYTE *>(data->SpecularColorPtr) + vertex * data->SpecularColorStride, sizeof(CKDWORD));
        }
        if (ordinal == ThrowAt) throw std::runtime_error("delegated draw refused by observer");
        return ordinal != FailAt;
    }
};

class DelegatingContext : public RCKRenderContext {
public:
    enum Plan { Once, RepeatAfterFalse, RepeatAfterCaughtThrow, Nested, NestedCaughtThrow } Mode = Once;
    DelegationBoundary *Boundary = nullptr;
    RCKMesh *Child = nullptr;
    VxDrawPrimitiveData *ChildData = nullptr;
    int Calls = 0, Depth = 0, Caught = 0;
    CKBOOL FirstResult = TRUE, SecondResult = FALSE;
    explicit DelegatingContext(CKContext *context) : RCKRenderContext(context) {}
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices, int count,
                         VxDrawPrimitiveData *data) override {
        ++Calls;
        // A renderer can need the normal attribute while keeping the material
        // owner's lighting choice. This declaration must not change that choice.
        data->Flags |= CKRST_DP_LIGHT;
        if (Depth) return RCKRenderContext::DrawPrimitive(type, indices, count, data);
        struct DepthScope {
            int &Depth;
            explicit DepthScope(int &depth) : Depth(depth) { ++Depth; }
            ~DepthScope() { --Depth; }
        } depth(Depth);
        if (Mode == Nested || Mode == NestedCaughtThrow) {
            EXPECT_NE(nullptr, Child); EXPECT_NE(nullptr, ChildData);
            if (Mode == NestedCaughtThrow) Boundary->ThrowAt = Boundary->Attempts;
            try {
                if (Child && ChildData) EXPECT_EQ(1, Child->RenderChannels(this, nullptr, ChildData, FALSE));
            } catch (const std::runtime_error &) {
                ++Caught;
            }
        }
        if (Mode == RepeatAfterFalse) {
            Boundary->FailAt = Boundary->Attempts;
            FirstResult = RCKRenderContext::DrawPrimitive(type, indices, count, data);
            SecondResult = RCKRenderContext::DrawPrimitive(type, indices, count, data);
            return SecondResult;
        }
        if (Mode == RepeatAfterCaughtThrow) {
            Boundary->ThrowAt = Boundary->Attempts;
            try {
                FirstResult = RCKRenderContext::DrawPrimitive(type, indices, count, data);
            } catch (const std::runtime_error &) {
                ++Caught;
            }
            SecondResult = RCKRenderContext::DrawPrimitive(type, indices, count, data);
            return SecondResult;
        }
        return RCKRenderContext::DrawPrimitive(type, indices, count, data);
    }
};

struct LightingOwner { CKBOOL Lighting = FALSE; int Calls = 0; };
int SelectDelegatedLighting(CKRenderContext *context, CKMaterial *, void *argument) {
    LightingOwner &owner = *static_cast<LightingOwner *>(argument); ++owner.Calls;
    static_cast<RCKRenderContext *>(context)->m_RasterizerContext->SetRenderState(
        VXRENDERSTATE_LIGHTING, owner.Lighting);
    return TRUE;
}

class MeshDrawDelegationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    DelegatingContext *Device = nullptr;
    DelegationBoundary State;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context); Device = new DelegatingContext(Context);
        Device->DetachAll(); Device->m_RasterizerContext = &State; Device->Boundary = &State;
        Device->BeginFrameErrorTracking();
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; delete Device; }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
    static VxDrawPrimitiveData Geometry(RCKMesh &mesh, RCKMaterial &material, LightingOwner &owner, int marker) {
        EXPECT_TRUE(mesh.SetVertexCount(3)); EXPECT_TRUE(mesh.SetFaceCount(1));
        mesh.SetFaceVertexIndex(0, 2, 0, 1); mesh.SetLitMode(VX_LITMESH);
        for (int i = 0; i < 3; ++i) {
            VxVector position(static_cast<float>(marker + i), static_cast<float>(2 + i), 1);
            VxVector normal(.25f * i, .5f, .75f);
            mesh.SetVertexPosition(i, &position); mesh.SetVertexNormal(i, &normal);
            mesh.SetVertexTextureCoordinates(i, marker + .125f + i, marker + .25f + i, -1);
            mesh.SetVertexColor(i, 0xFFA10000u + marker * 8 + i);
            mesh.SetVertexSpecularColor(i, 0xFFB20000u + marker * 8 + i);
        }
        material.SetCallback(SelectDelegatedLighting, &owner);
        const int channel = mesh.AddChannel(&material, FALSE); EXPECT_GE(channel, 0);
        mesh.SetChannelSourceBlend(channel, VXBLEND_SRCALPHA);
        mesh.SetChannelDestBlend(channel, VXBLEND_INVSRCALPHA);
        mesh.SetChannelFlags(channel, VXCHANNEL_ACTIVE | VXCHANNEL_SAMEUV);
        VxDrawPrimitiveData data = {}; data.VertexCount = 3;
        data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_STAGES0;
        data.PositionPtr = mesh.GetPositionsPtr(&data.PositionStride);
        data.NormalPtr = mesh.GetNormalsPtr(&data.NormalStride);
        data.ColorPtr = mesh.GetColorsPtr(&data.ColorStride);
        data.SpecularColorPtr = mesh.GetSpecularColorsPtr(&data.SpecularColorStride);
        data.TexCoordPtr = mesh.GetTextureCoordinatesPtr(&data.TexCoordStride, -1);
        return data;
    }
    static void Payload(const DelegationBoundary::Draw &draw, int marker, CKBOOL lighting) {
        EXPECT_EQ(lighting, draw.Lighting); EXPECT_NE(0u, draw.Flags & CKRST_DP_LIGHT);
        ASSERT_EQ(3, draw.Count); ASSERT_EQ(3, draw.VertexCount);
        const int order[] = {2, 0, 1};
        for (int i = 0; i < 3; ++i) {
            const int source = order[i]; EXPECT_EQ(source, draw.Indices[i]);
            EXPECT_FLOAT_EQ(marker + source, draw.Positions[i].x);
            EXPECT_FLOAT_EQ(2 + source, draw.Positions[i].y); EXPECT_FLOAT_EQ(1, draw.Positions[i].z);
            EXPECT_FLOAT_EQ(.25f * source, draw.Normals[i].x);
            EXPECT_FLOAT_EQ(.5f, draw.Normals[i].y); EXPECT_FLOAT_EQ(.75f, draw.Normals[i].z);
            EXPECT_FLOAT_EQ(marker + .125f + source, draw.UVs[i].x);
            EXPECT_FLOAT_EQ(marker + .25f + source, draw.UVs[i].y);
            EXPECT_EQ(0xFFA10000u + marker * 8 + source, draw.Colors[i]);
            EXPECT_EQ(0xFFB20000u + marker * 8 + source, draw.Specular[i]);
        }
    }
    void PublicAfter(VxDrawPrimitiveData &data, int marker) {
        const int ordinal = State.Attempts;
        Device->Mode = DelegatingContext::Once; CKWORD indices[] = {2, 0, 1};
        State.SetRenderState(VXRENDERSTATE_LIGHTING, FALSE);
        ASSERT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, indices, 3, &data));
        ASSERT_EQ(ordinal + 1, State.Attempts); Payload(State.Draws[ordinal], marker, TRUE);
    }
};
}

TEST_F(MeshDrawDelegationTest, SamePacketCanDelegateAgainAfterTheFirstBackendRefusal) {
    RCKMesh mesh(Context); RCKMaterial material(Context); LightingOwner owner;
    VxDrawPrimitiveData data = Geometry(mesh, material, owner, 13);
    Device->Mode = DelegatingContext::RepeatAfterFalse;
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, FALSE));
    EXPECT_FALSE(Device->FirstResult); EXPECT_TRUE(Device->SecondResult);
    ASSERT_EQ(2, State.Attempts); EXPECT_EQ(1, Device->Calls); EXPECT_EQ(1, owner.Calls);
    Payload(State.Draws[0], 13, FALSE); Payload(State.Draws[1], 13, FALSE);
    EXPECT_EQ(CKERR_INVALIDOPERATION, Device->m_FrameRenderError);
    EXPECT_EQ(1, Device->GetStats().NbTrianglesDrawn); PublicAfter(data, 13);
}

TEST_F(MeshDrawDelegationTest, CaughtBaseDrawExceptionAllowsASecondDelegationOfTheSamePacket) {
    RCKMesh mesh(Context); RCKMaterial material(Context); LightingOwner owner;
    VxDrawPrimitiveData data = Geometry(mesh, material, owner, 17);
    Device->Mode = DelegatingContext::RepeatAfterCaughtThrow;
    ASSERT_EQ(1, mesh.RenderChannels(Device, nullptr, &data, FALSE));
    EXPECT_EQ(1, Device->Caught); EXPECT_TRUE(Device->SecondResult);
    ASSERT_EQ(2, State.Attempts); EXPECT_EQ(1, Device->Calls); EXPECT_EQ(1, owner.Calls);
    Payload(State.Draws[0], 17, FALSE); Payload(State.Draws[1], 17, FALSE);
    EXPECT_EQ(1, Device->GetStats().NbTrianglesDrawn); PublicAfter(data, 17);
}

TEST_F(MeshDrawDelegationTest, NestedMeshSubmissionRestoresTheOuterLightingOwnerAndGeometry) {
    RCKMesh outer(Context), inner(Context); RCKMaterial outerMaterial(Context), innerMaterial(Context);
    LightingOwner outerOwner, innerOwner; innerOwner.Lighting = TRUE;
    VxDrawPrimitiveData outerData = Geometry(outer, outerMaterial, outerOwner, 23);
    VxDrawPrimitiveData innerData = Geometry(inner, innerMaterial, innerOwner, 61);
    Device->Mode = DelegatingContext::Nested; Device->Child = &inner; Device->ChildData = &innerData;
    ASSERT_EQ(1, outer.RenderChannels(Device, nullptr, &outerData, FALSE));
    ASSERT_EQ(2, State.Attempts); EXPECT_EQ(2, Device->Calls);
    EXPECT_EQ(0, Device->Caught);
    EXPECT_EQ(1, outerOwner.Calls); EXPECT_EQ(1, innerOwner.Calls);
    Payload(State.Draws[0], 61, TRUE); Payload(State.Draws[1], 23, FALSE);
    EXPECT_EQ(2, Device->GetStats().NbTrianglesDrawn); PublicAfter(outerData, 23);
}

TEST_F(MeshDrawDelegationTest, CaughtNestedMeshExceptionRestoresTheEnclosingDelegation) {
    RCKMesh outer(Context), inner(Context); RCKMaterial outerMaterial(Context), innerMaterial(Context);
    LightingOwner outerOwner, innerOwner; innerOwner.Lighting = TRUE;
    VxDrawPrimitiveData outerData = Geometry(outer, outerMaterial, outerOwner, 29);
    VxDrawPrimitiveData innerData = Geometry(inner, innerMaterial, innerOwner, 67);
    Device->Mode = DelegatingContext::NestedCaughtThrow; Device->Child = &inner; Device->ChildData = &innerData;
    ASSERT_EQ(1, outer.RenderChannels(Device, nullptr, &outerData, FALSE));
    EXPECT_EQ(1, Device->Caught); ASSERT_EQ(2, State.Attempts); EXPECT_EQ(2, Device->Calls);
    EXPECT_EQ(1, outerOwner.Calls); EXPECT_EQ(1, innerOwner.Calls);
    Payload(State.Draws[0], 67, TRUE); Payload(State.Draws[1], 29, FALSE);
    EXPECT_EQ(1, Device->GetStats().NbTrianglesDrawn); PublicAfter(outerData, 29);
}
