#include <gtest/gtest.h>
#include <cstring>
#include "CKAll.h"
#include "CKRasterizer.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "MaterialRenderObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
// Explicit instantiation can name a private member without changing the class
// declaration. This preserves MSVC's access-dependent symbol decoration.
template<class Tag, typename Tag::Type Method>
struct DiagnosticMethodAccess {
    friend typename Tag::Type DiagnosticMethod(Tag) { return Method; }
};
struct StateSummaryTag {
    using Type = void (RCKRenderContext::*)();
    friend Type DiagnosticMethod(StateSummaryTag);
};
struct RejectedCountTag {
    using Type = CKDWORD (RCKRenderContext::*)();
    friend Type DiagnosticMethod(RejectedCountTag);
};
template struct DiagnosticMethodAccess<StateSummaryTag, &RCKRenderContext::FillStateString>;
template struct DiagnosticMethodAccess<RejectedCountTag, &RCKRenderContext::RejectedDrawCount>;

// Observe the renderer's queries and frame decisions, without simulating a GPU.
class DiagnosticBoundary : public MaterialRenderObserver {
public:
    CKDWORD Rejected = 0;
    CKBOOL RejectQueries = FALSE;
    int Queries = 0;
    CKBOOL GetRenderState(VXRENDERSTATETYPE state, CKDWORD *value) override {
        ++Queries;
        if (RejectQueries) {
            *value = 0xDEADBEEF;
            return FALSE;
        }
        return MaterialRenderObserver::GetRenderState(state, value);
    }
    void GetStats(CKRenderStats &stats) const override {
        stats = {};
        stats.Diagnostics[CKRST_DIAG_REJECT_UNSUPPORTED_STATE] = Rejected;
    }
};

class RenderContextDiagnosticCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderContext *Device = nullptr;
    DiagnosticBoundary State;
    void FillSummary() { (Device->*DiagnosticMethod(StateSummaryTag()))(); }
    CKDWORD RejectedCount() { return (Device->*DiagnosticMethod(RejectedCountTag()))(); }
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
        Device = new RCKRenderContext(Context);
        Device->m_RasterizerContext = &State;
    }
    void TearDown() override {
        if (Device) {
            Device->m_RasterizerContext = nullptr;
            delete Device;
        }
        if (Context) CKCloseContext(Context);
        CKShutdown();
    }
};

TEST_F(RenderContextDiagnosticCompletionTest, StateSummaryReportsObservedEnumsBitsAndUnsignedValues) {
    State.States[VXRENDERSTATE_CLIPPING] = TRUE;
    State.States[VXRENDERSTATE_LIGHTING] = TRUE;
    State.States[VXRENDERSTATE_TEXTUREPERSPECTIVE] = TRUE;
    State.States[VXRENDERSTATE_FILLMODE] = VXFILL_WIREFRAME;
    State.States[VXRENDERSTATE_SHADEMODE] = VXSHADE_GOURAUD;
    State.States[VXRENDERSTATE_CULLMODE] = VXCULL_CCW;
    State.States[VXRENDERSTATE_ZENABLE] = TRUE;
    State.States[VXRENDERSTATE_ZWRITEENABLE] = TRUE;
    State.States[VXRENDERSTATE_ZFUNC] = VXCMP_LESSEQUAL;
    State.States[VXRENDERSTATE_ZBIAS] = 19;
    State.States[VXRENDERSTATE_ALPHATESTENABLE] = TRUE;
    State.States[VXRENDERSTATE_ALPHAFUNC] = VXCMP_GREATER;
    State.States[VXRENDERSTATE_ALPHAREF] = 128;
    State.States[VXRENDERSTATE_ALPHABLENDENABLE] = TRUE;
    State.States[VXRENDERSTATE_SRCBLEND] = VXBLEND_SRCALPHA;
    State.States[VXRENDERSTATE_DESTBLEND] = VXBLEND_INVSRCALPHA;
    State.States[VXRENDERSTATE_FOGENABLE] = TRUE;
    State.States[VXRENDERSTATE_FOGPIXELMODE] = VXFOG_LINEAR;
    State.States[VXRENDERSTATE_SPECULARENABLE] = TRUE;
    State.States[VXRENDERSTATE_LOCALVIEWER] = TRUE;
    State.States[VXRENDERSTATE_EDGEANTIALIAS] = TRUE;
    State.States[VXRENDERSTATE_STENCILENABLE] = TRUE;
    State.States[VXRENDERSTATE_STENCILFAIL] = VXSTENCILOP_KEEP;
    State.States[VXRENDERSTATE_STENCILZFAIL] = VXSTENCILOP_INVERT;
    State.States[VXRENDERSTATE_STENCILPASS] = VXSTENCILOP_REPLACE;
    State.States[VXRENDERSTATE_STENCILFUNC] = VXCMP_ALWAYS;
    State.States[VXRENDERSTATE_STENCILREF] = 23;
    State.States[VXRENDERSTATE_STENCILMASK] = 0xFFFFFFFF;
    State.States[VXRENDERSTATE_STENCILWRITEMASK] = 255;
    State.States[VXRENDERSTATE_NORMALIZENORMALS] = TRUE;
    State.States[VXRENDERSTATE_CLIPPLANEENABLE] = 5;
    State.States[VXRENDERSTATE_TEXTURETARGET] = TRUE;
    FillSummary();
    EXPECT_STREQ(
        "Render States\n\nClipping : On\nLighting : On\nAntialias : Off\n"
        "Perspective Tex : On\nFill Mode : Wireframe\nShade Mode : Gouraud\nCull Mode : CCW\n"
        "ZWrite : On\nZ Cmp Function : LessEqual\nZbias : 19\n"
        "Alpha Cmp Func : Greater\nAlpha Ref Value : 128\nDithering : Off\n"
        "Alpha Blending : SrcAlpha\n->InvSrcAlpha\nFog : Linear\nSpecular : Local Viewer\n"
        "Edge Antialias : On\nStencil Fail : Keep\nStencil Zfail : Invert\nStencil Pass : Replace\n"
        "Stencil Cmp Func : Always\nStencil Ref Value : 23\nStencil Mask : 4294967295\n"
        "Stencil Write Mask : 255\nNormal Normalize : On\nClip Planes Enable : 5\n"
        "Inverse Winding : Off\nTexture Target : On\n",
        Device->m_StateString.CStr());
    EXPECT_EQ(0, State.StateWrites);
}

TEST_F(RenderContextDiagnosticCompletionTest, RebuildingSummaryDropsDisabledSectionsAndShowsInfiniteSpecularViewer) {
    State.States[VXRENDERSTATE_ZENABLE] = TRUE;
    State.States[VXRENDERSTATE_ALPHATESTENABLE] = TRUE;
    State.States[VXRENDERSTATE_FOGENABLE] = TRUE;
    State.States[VXRENDERSTATE_STENCILENABLE] = TRUE;
    FillSummary();
    ASSERT_NE(nullptr, strstr(Device->m_StateString.CStr(), "Stencil Fail :"));
    State.States[VXRENDERSTATE_ZENABLE] = FALSE;
    State.States[VXRENDERSTATE_ALPHATESTENABLE] = FALSE;
    State.States[VXRENDERSTATE_FOGENABLE] = FALSE;
    State.States[VXRENDERSTATE_STENCILENABLE] = FALSE;
    State.States[VXRENDERSTATE_SPECULARENABLE] = TRUE;
    FillSummary();
    const char *text = Device->m_StateString.CStr();
    EXPECT_EQ(nullptr, strstr(text, "ZWrite :"));
    EXPECT_EQ(nullptr, strstr(text, "Alpha Cmp Func :"));
    EXPECT_EQ(nullptr, strstr(text, "Fog :"));
    EXPECT_EQ(nullptr, strstr(text, "Stencil Fail :"));
    EXPECT_NE(nullptr, strstr(text, "Specular : Infinite\n"));
    EXPECT_EQ(0, State.StateWrites);
}

TEST_F(RenderContextDiagnosticCompletionTest, UnknownStateEnumsAreReadableFullWidthNumbers) {
    State.States[VXRENDERSTATE_FILLMODE] = 0xFFFFFFFF;
    State.States[VXRENDERSTATE_SHADEMODE] = 1000;
    State.States[VXRENDERSTATE_CULLMODE] = 0x80000000;
    FillSummary();
    const char *text = Device->m_StateString.CStr();
    EXPECT_NE(nullptr, strstr(text, "Fill Mode : 4294967295\n"));
    EXPECT_NE(nullptr, strstr(text, "Shade Mode : 1000\n"));
    EXPECT_NE(nullptr, strstr(text, "Cull Mode : 2147483648\n"));
}

TEST_F(RenderContextDiagnosticCompletionTest, RejectedStateQueriesNeverPublishTheirUnacknowledgedOutput) {
    State.RejectQueries = TRUE;
    FillSummary();
    EXPECT_GT(State.Queries, 0);
    const char *text = Device->m_StateString.CStr();
    EXPECT_EQ(nullptr, strstr(text, "3735928559"));
    EXPECT_EQ(nullptr, strstr(text, "Stencil Fail :"));
    EXPECT_NE(nullptr, strstr(text, "Clipping : Off\n"));
    EXPECT_NE(nullptr, strstr(text, "Fill Mode : \n"));
    EXPECT_EQ(0, State.StateWrites);
}

TEST_F(RenderContextDiagnosticCompletionTest, HeadlessSummaryReplacesPriorDeviceStateWithoutQueryingIt) {
    FillSummary();
    const int queries = State.Queries;
    Device->m_RasterizerContext = nullptr;
    FillSummary();
    EXPECT_STREQ("Render States\n\n", Device->m_StateString.CStr());
    EXPECT_EQ(queries, State.Queries);
}

TEST_F(RenderContextDiagnosticCompletionTest, RejectionCountsFollowTheCurrentFrameAndBackendCounterReset) {
    State.Rejected = 9;
    Device->BeginFrameErrorTracking();
    EXPECT_EQ(0u, RejectedCount());
    State.Rejected = 12;
    EXPECT_EQ(3u, RejectedCount());
    State.Rejected = 2; // A backend starts a new diagnostic collection interval.
    EXPECT_EQ(2u, RejectedCount());
    Device->BeginFrameErrorTracking();
    EXPECT_EQ(0u, RejectedCount());
    Device->m_RasterizerContext = nullptr;
    EXPECT_EQ(0u, RejectedCount());
}

TEST_F(RenderContextDiagnosticCompletionTest, PresentationConsumesNewRejectionsAndPreservesTheFirstFrameError) {
    Device->BeginFrameErrorTracking();
    State.Rejected = 1;
    EXPECT_EQ(CKERR_INVALIDOPERATION, Device->BackToFront(CK_RENDER_DOBACKTOFRONT));
    EXPECT_EQ(1, State.PresentCount);
    EXPECT_EQ(CK_OK, Device->BackToFront(CK_RENDER_DOBACKTOFRONT));
    State.Rejected = 3;
    Device->RecordFrameRenderError(CKERR_INVALIDPARAMETER);
    Device->RecordFrameRenderError(CKERR_INVALIDSIZE);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, Device->BackToFront(CK_RENDER_DOBACKTOFRONT));
    Device->BeginFrameErrorTracking();
    EXPECT_EQ(CK_OK, Device->BackToFront(CK_RENDER_DOBACKTOFRONT));
    EXPECT_EQ(4, State.PresentCount);
}
} // namespace
