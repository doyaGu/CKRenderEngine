#include <gtest/gtest.h>
#include "../src/CKRenderPerfStats.h"
#include "CKRenderSettings.h"
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
struct PerfCountMember {
    const char *LogKey;
    CKDWORD CKRenderPerfStats::*Member;
};
const PerfCountMember Counts[] = {
    {"frame", &CKRenderPerfStats::FrameIndex}, {"entities3D", &CKRenderPerfStats::Entities3D},
    {"entities2D", &CKRenderPerfStats::Entities2D}, {"cameras", &CKRenderPerfStats::Cameras},
    {"lights", &CKRenderPerfStats::Lights}, {"meshRender", &CKRenderPerfStats::MeshRenderCalls},
    {"meshDefault", &CKRenderPerfStats::MeshDefaultCalls}, {"groups", &CKRenderPerfStats::MeshGroupCalls},
    {"channelPasses", &CKRenderPerfStats::MeshChannelPasses}, {"materialSet", &CKRenderPerfStats::MaterialSetCalls},
    {"drawPrimitive", &CKRenderPerfStats::DrawPrimitiveCalls}, {"drawVB", &CKRenderPerfStats::DrawVertexBufferCalls},
    {"primEntries", &CKRenderPerfStats::PrimitiveEntries}, {"swEntries", &CKRenderPerfStats::SoftwarePrimitiveEntries},
    {"hwEntries", &CKRenderPerfStats::HardwarePrimitiveEntries}, {"channelEntries", &CKRenderPerfStats::ChannelPrimitiveEntries},
    {"channelMeshes", &CKRenderPerfStats::RenderChannelMeshes}, {"alphaGroups", &CKRenderPerfStats::AlphaGroups},
    {"vbChecks", &CKRenderPerfStats::VertexBufferChecks}, {"vbReady", &CKRenderPerfStats::VertexBufferReady},
    {"groupIndices", &CKRenderPerfStats::TotalGroupIndices}, {"channelIndices", &CKRenderPerfStats::TotalChannelIndices}
};
struct PerfTimeMember {
    const char *LogKey;
    double CKRenderPerfStats::*Member;
};
const PerfTimeMember Times[] = {
    {"meshRenderUs", &CKRenderPerfStats::MeshRenderUs}, {"meshDefaultUs", &CKRenderPerfStats::MeshDefaultUs},
    {"meshGroupUs", &CKRenderPerfStats::MeshGroupUs}, {"meshChannelsUs", &CKRenderPerfStats::MeshChannelsUs},
    {"materialSetUs", &CKRenderPerfStats::MaterialSetUs}, {"drawPrimitiveWrapperUs", &CKRenderPerfStats::DrawPrimitiveWrapperUs}
};
const char *Sections[] = {"frameSetupUs", "beginFrameUs", "defaultStatesUs", "bg2DUs", "setupLightsUs",
    "preCallbacksUs", "opaqueTraversalUs", "sprite3DUs", "postCallbacksUs", "transparentSortRenderUs",
    "fg2DUs", "postSpriteCallbacksUs"};
const int CountCount = sizeof(Counts) / sizeof(Counts[0]);
const int TimeCount = sizeof(Times) / sizeof(Times[0]);

void Seed(CKRenderPerfStats &stats) {
    for (int i = 0; i < CountCount; ++i) stats.*Counts[i].Member = 101 + i;
    for (int i = 0; i < TimeCount; ++i) stats.*Times[i].Member = 41.5 + i;
    for (int i = 0; i < CKRPS_SECTION_COUNT; ++i) stats.SectionsUs[i] = 11.5 + i;
}
void Same(const CKRenderPerfStats &a, const CKRenderPerfStats &b) {
    for (int i = 0; i < CountCount; ++i) EXPECT_EQ(a.*Counts[i].Member, b.*Counts[i].Member) << Counts[i].LogKey;
    for (int i = 0; i < TimeCount; ++i) EXPECT_DOUBLE_EQ(a.*Times[i].Member, b.*Times[i].Member) << Times[i].LogKey;
    for (int i = 0; i < CKRPS_SECTION_COUNT; ++i) EXPECT_DOUBLE_EQ(a.SectionsUs[i], b.SectionsUs[i]) << i;
}

TEST(RenderPerfStatsValueTest, EveryCountAndTimeMemberCopiesWithoutAliasingAnotherField) {
    CKRenderPerfStats original = {};
    Seed(original);
    CKRenderPerfStats copy = original;
    for (int i = 0; i < CountCount; ++i) {
        EXPECT_EQ(static_cast<CKDWORD>(101 + i), original.*Counts[i].Member);
        copy.*Counts[i].Member = 201 + i;
    }
    for (int i = 0; i < TimeCount; ++i) copy.*Times[i].Member = 51.5 + i;
    for (int i = 0; i < CKRPS_SECTION_COUNT; ++i) copy.SectionsUs[i] = 21.5 + i;
    for (int i = 0; i < CountCount; ++i) EXPECT_EQ(static_cast<CKDWORD>(101 + i), original.*Counts[i].Member);
    for (int i = 0; i < TimeCount; ++i) EXPECT_DOUBLE_EQ(41.5 + i, original.*Times[i].Member);
    for (int i = 0; i < CKRPS_SECTION_COUNT; ++i) EXPECT_DOUBLE_EQ(11.5 + i, original.SectionsUs[i]);
}
} // namespace

#if CKRE_ENABLE_RENDER_STATS
#if !CKRE_ENABLE_DEBUG_LOGGER
#error The enabled statistics fixture requires the actual logger implementation.
#endif
#include "CKDebugLogger.h"
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKMaterial.h"
#include "RCKTexture.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "MaterialRenderObserver.h"

namespace {
class RenderPerfStatsEnabledTest : public ::testing::Test {
protected:
    char Path[1024] = {};
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        CKRenderSettingsClearOverridesForTests();
        Setting(true);
        CKRenderPerfCurrent() = CKRenderPerfStats{};
        std::snprintf(Path, sizeof(Path), "%srender-perf-%u-%llu.log", ::testing::TempDir().c_str(),
            static_cast<unsigned>(sizeof(void *)), static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(this)));
        std::remove(Path);
        CKDebugLogger &logger = CKDebugLogger::Instance();
        logger.EnableOutput(false);
        logger.EnableDebuggerOutput(false);
        logger.EnableFileOutput(true);
        logger.SetLogFilePath(Path);
        logger.EnableOutput(true);
    }
    void TearDown() override {
        CKDebugLogger::Instance().EnableOutput(false);
        CKDebugLogger::Instance().EnableFileOutput(false);
        CKRenderSettingsClearOverridesForTests();
        std::remove(Path);
    }
    void Setting(bool enabled) {
        CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugRenderStats, "Enabled", enabled ? "true" : "false");
    }
    std::string Read() {
        CKDebugLogger::Instance().Flush();
        FILE *file = std::fopen(Path, "rb");
        if (!file) return std::string();
        std::string result;
        char buffer[2048];
        size_t count;
        while ((count = std::fread(buffer, 1, sizeof(buffer), file)) != 0) result.append(buffer, count);
        std::fclose(file);
        return result;
    }
};

TEST_F(RenderPerfStatsEnabledTest, BeginFrameClearsAllActivityAndTimeButRetainsFrameIndexAndInstallsSceneCounts) {
    CKRenderPerfStats &stats = CKRenderPerfCurrent();
    Seed(stats);
    stats.FrameIndex = 73;
    CKRenderPerfBeginFrame(11, 13, 17, 19);
    EXPECT_EQ(&stats, &CKRenderPerfCurrent());
    EXPECT_EQ(73u, stats.FrameIndex);
    EXPECT_EQ(11u, stats.Entities3D); EXPECT_EQ(13u, stats.Entities2D);
    EXPECT_EQ(17u, stats.Cameras); EXPECT_EQ(19u, stats.Lights);
    for (int i = 5; i < CountCount; ++i) EXPECT_EQ(0u, stats.*Counts[i].Member) << Counts[i].LogKey;
    for (int i = 0; i < TimeCount; ++i) EXPECT_DOUBLE_EQ(0, stats.*Times[i].Member);
    for (int i = 0; i < CKRPS_SECTION_COUNT; ++i) EXPECT_DOUBLE_EQ(0, stats.SectionsUs[i]);
}

TEST_F(RenderPerfStatsEnabledTest, RuntimeDisableFreezesCollectionAndLoggingUntilTheNextEnabledFrame) {
    CKRenderPerfStats &stats = CKRenderPerfCurrent();
    Seed(stats);
    const CKRenderPerfStats before = stats;
    Setting(false);
    EXPECT_FALSE(CKRenderPerfStatsEnabled());
    CKRenderPerfBeginFrame(1, 2, 3, 4);
    CKRenderPerfAddSection(CKRPS_FRAME_SETUP, 123);
    CKRenderPerfLogAndReset();
    Same(before, stats);
    EXPECT_TRUE(Read().empty());
    Setting(true);
    EXPECT_TRUE(CKRenderPerfStatsEnabled());
    Same(before, stats); // Changing the setting alone does not begin a new frame.
    CKRenderPerfBeginFrame(1, 2, 3, 4);
    EXPECT_EQ(before.FrameIndex, stats.FrameIndex);
    EXPECT_EQ(0u, stats.MeshRenderCalls);
    EXPECT_DOUBLE_EQ(0, stats.SectionsUs[0]);
}

TEST_F(RenderPerfStatsEnabledTest, EverySectionAccumulatesIndependentlyAndRejectsOutOfRangeIndices) {
    for (int i = 0; i < CKRPS_SECTION_COUNT; ++i) {
        CKRenderPerfAddSection(static_cast<CKRenderPerfSection>(i), i + .25);
        CKRenderPerfAddSection(static_cast<CKRenderPerfSection>(i), .5);
    }
    CKRenderPerfAddSection(static_cast<CKRenderPerfSection>(-1), 999);
    CKRenderPerfAddSection(CKRPS_SECTION_COUNT, 999);
    for (int i = 0; i < CKRPS_SECTION_COUNT; ++i) EXPECT_DOUBLE_EQ(i + .75, CKRenderPerfCurrent().SectionsUs[i]);
    EXPECT_EQ(0u, CKRenderPerfCurrent().MeshRenderCalls);
    EXPECT_DOUBLE_EQ(0, CKRenderPerfCurrent().MeshRenderUs);
}

TEST_F(RenderPerfStatsEnabledTest, LogReportsEveryFieldBeforeAdvancingAndBeginFrameResetsTheNextFrame) {
    CKRenderPerfStats &stats = CKRenderPerfCurrent();
    Seed(stats);
    const CKRenderPerfStats before = stats;
    CKRenderPerfLogAndReset();
    const std::string line = Read() + " ";
    EXPECT_NE(std::string::npos, line.find("[RenderStats]"));
    char expected[128];
    for (int i = 0; i < CountCount; ++i) {
        std::snprintf(expected, sizeof(expected), "%s=%u", Counts[i].LogKey, before.*Counts[i].Member);
        EXPECT_NE(std::string::npos, line.find(expected)) << expected;
    }
    for (int i = 0; i < TimeCount; ++i) {
        std::snprintf(expected, sizeof(expected), "%s=%.1f", Times[i].LogKey, before.*Times[i].Member);
        EXPECT_NE(std::string::npos, line.find(expected)) << expected;
    }
    for (int i = 0; i < CKRPS_SECTION_COUNT; ++i) {
        std::snprintf(expected, sizeof(expected), "%s=%.1f", Sections[i], before.SectionsUs[i]);
        EXPECT_NE(std::string::npos, line.find(expected)) << expected;
    }
    EXPECT_EQ(before.FrameIndex + 1, stats.FrameIndex);
    CKRenderPerfStats expectedStats = before;
    ++expectedStats.FrameIndex;
    Same(expectedStats, stats);
    CKRenderPerfBeginFrame(1, 2, 3, 4);
    EXPECT_EQ(expectedStats.FrameIndex, stats.FrameIndex);
    for (int i = 5; i < CountCount; ++i) EXPECT_EQ(0u, stats.*Counts[i].Member);
    for (int i = 0; i < TimeCount; ++i) EXPECT_DOUBLE_EQ(0, stats.*Times[i].Member);
}

TEST_F(RenderPerfStatsEnabledTest, MacroGatesAvoidEvaluatingDisabledPayloadsAndCountEnabledWork) {
    CKRenderPerfResetNowCallCountForTests();
    Setting(false);
    CK_RENDER_PERF_DECLARE_ENABLED(disabled);
    CK_RENDER_PERF_DECLARE_TIMER(noStart, disabled);
    int payload = 0;
    CK_RENDER_PERF_INC(disabled, MeshRenderCalls);
    CK_RENDER_PERF_ADD(disabled, TotalGroupIndices, ++payload);
    CK_RENDER_PERF_IF(disabled, ++payload;);
    EXPECT_EQ(0, payload);
    EXPECT_EQ(0u, CKRenderPerfNowCallCountForTests());
    EXPECT_DOUBLE_EQ(0, noStart);
    Setting(true);
    CK_RENDER_PERF_DECLARE_ENABLED(enabled);
    CK_RENDER_PERF_DECLARE_TIMER(start, enabled);
    EXPECT_GE(start, 0);
    EXPECT_EQ(1u, CKRenderPerfNowCallCountForTests());
    CK_RENDER_PERF_INC(enabled, MeshRenderCalls);
    CK_RENDER_PERF_ADD(enabled, TotalGroupIndices, ++payload);
    CK_RENDER_PERF_IF(enabled, CKRenderPerfCurrent().Cameras += 2;);
    EXPECT_EQ(1, payload);
    EXPECT_EQ(1u, CKRenderPerfCurrent().MeshRenderCalls);
    EXPECT_EQ(1u, CKRenderPerfCurrent().TotalGroupIndices);
    EXPECT_EQ(2u, CKRenderPerfCurrent().Cameras);
}

TEST_F(RenderPerfStatsEnabledTest, FrameAndActivityCountersWrapAsDwordsEvenWhenLogOutputIsDisabled) {
    CKDebugLogger::Instance().EnableOutput(false);
    CKRenderPerfStats &stats = CKRenderPerfCurrent();
    stats.FrameIndex = stats.MeshRenderCalls = stats.TotalGroupIndices = 0xffffffffu;
    CK_RENDER_PERF_INC(true, MeshRenderCalls);
    CK_RENDER_PERF_ADD(true, TotalGroupIndices, 2);
    CKRenderPerfLogAndReset();
    EXPECT_EQ(0u, stats.FrameIndex); EXPECT_EQ(0u, stats.MeshRenderCalls);
    EXPECT_EQ(1u, stats.TotalGroupIndices);
    CKRenderPerfBeginFrame(3, 5, 7, 11);
    EXPECT_EQ(0u, stats.FrameIndex); EXPECT_EQ(0u, stats.TotalGroupIndices);
}

class PerfSubmissionObserver : public MaterialRenderObserver {
public:
    CKBOOL Accept = TRUE;
    int Draws = 0;
    VxVector Last;
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *, int, VxDrawPrimitiveData *data) override {
        EXPECT_EQ(VX_TRIANGLELIST, type);
        ++Draws;
        Last = *reinterpret_cast<VxVector *>(static_cast<CKBYTE *>(data->PositionPtr) + 2 * data->PositionStride);
        return Accept;
    }
};
class RenderPerfStatsWorldTest : public RenderPerfStatsEnabledTest {
protected:
    CKContext *Context = nullptr;
    RCKRenderContext *Device = nullptr;
    PerfSubmissionObserver Boundary;
    void SetUp() override {
        RenderPerfStatsEnabledTest::SetUp();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
        Device = new RCKRenderContext(Context);
        Device->m_RasterizerContext = &Boundary;
        Device->BeginFrameErrorTracking();
        CKRenderPerfBeginFrame(0, 0, 0, 0);
    }
    void TearDown() override {
        if (Device) { Device->m_RasterizerContext = nullptr; delete Device; }
        if (Context) CKCloseContext(Context);
        CKShutdown();
        RenderPerfStatsEnabledTest::TearDown();
    }
};

TEST_F(RenderPerfStatsWorldTest, ActualMaterialAndDrawPathsCountAttemptsWithoutChangingSubmissionResults) {
    RCKMaterial material(Context);
    EXPECT_TRUE(material.SetAsCurrent(Device, FALSE, 0));
    EXPECT_FALSE(material.SetAsCurrent(nullptr, FALSE, -1));
    EXPECT_EQ(2u, CKRenderPerfCurrent().MaterialSetCalls);
    VxVector vertices[] = {VxVector(1, 2, 3), VxVector(4, 5, 6), VxVector(7, 8, 9)};
    VxDrawPrimitiveData data = {};
    data.Flags = CKRST_DP_TRANSFORM; data.VertexCount = 3;
    data.PositionPtr = vertices; data.PositionStride = sizeof(VxVector);
    EXPECT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &data));
    Boundary.Accept = FALSE;
    EXPECT_FALSE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &data));
    EXPECT_EQ(CKERR_INVALIDOPERATION, Device->m_FrameRenderError);
    EXPECT_EQ(2u, CKRenderPerfCurrent().DrawPrimitiveCalls);
    EXPECT_EQ(2, Boundary.Draws); EXPECT_FLOAT_EQ(9, Boundary.Last.z);
    EXPECT_FALSE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, nullptr));
    EXPECT_EQ(2u, CKRenderPerfCurrent().DrawPrimitiveCalls);
    EXPECT_TRUE(std::isfinite(CKRenderPerfCurrent().MaterialSetUs));
    EXPECT_TRUE(std::isfinite(CKRenderPerfCurrent().DrawPrimitiveWrapperUs));
    EXPECT_GE(CKRenderPerfCurrent().DrawPrimitiveWrapperUs, 0);
    const CKRenderPerfStats before = CKRenderPerfCurrent();
    Setting(false);
    Boundary.Accept = TRUE;
    CKRenderPerfResetNowCallCountForTests();
    EXPECT_TRUE(material.SetAsCurrent(Device, FALSE, 0));
    EXPECT_TRUE(Device->DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &data));
    Same(before, CKRenderPerfCurrent());
    EXPECT_EQ(0u, CKRenderPerfNowCallCountForTests());
    EXPECT_EQ(3, Boundary.Draws); EXPECT_FLOAT_EQ(9, Boundary.Last.z);
}
} // namespace

#else
namespace {
TEST(RenderPerfStatsDisabledTest, RuntimeSettingCannotEnableCompiledOutCollectionOrResetTheFallbackRecord) {
    SetProcessorSpecific_FunctionsPtr();
    CKRenderSettingsClearOverridesForTests();
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::DebugRenderStats, "Enabled", "true");
    CKRenderPerfStats &stats = CKRenderPerfCurrent();
    Seed(stats);
    const CKRenderPerfStats before = stats;
    EXPECT_FALSE(CKRenderPerfStatsEnabled());
    CKRenderPerfBeginFrame(1, 2, 3, 4);
    CKRenderPerfAddSection(CKRPS_FRAME_SETUP, 99);
    CKRenderPerfLogAndReset();
    EXPECT_EQ(&stats, &CKRenderPerfCurrent());
    Same(before, stats);
    EXPECT_DOUBLE_EQ(0, CKRenderPerfNow());
    EXPECT_DOUBLE_EQ(0, CKRenderPerfElapsedUs(19));
    CKRenderSettingsClearOverridesForTests();
}

TEST(RenderPerfStatsDisabledTest, CompiledOutMacrosDoNotEvaluateConditionsAmountsOrBodies) {
    CKRenderPerfCurrent() = CKRenderPerfStats{};
    int condition = 0, amount = 0, body = 0;
    CK_RENDER_PERF_DECLARE_ENABLED(unused);
    CK_RENDER_PERF_DECLARE_TIMER(noStart, ++condition);
    CK_RENDER_PERF_INC(++condition, MeshRenderCalls);
    CK_RENDER_PERF_ADD(++condition, TotalGroupIndices, ++amount);
    CK_RENDER_PERF_IF(++condition, ++body;);
    EXPECT_EQ(0, condition); EXPECT_EQ(0, amount); EXPECT_EQ(0, body);
    EXPECT_EQ(0u, CKRenderPerfCurrent().MeshRenderCalls);
    EXPECT_EQ(0u, CKRenderPerfCurrent().TotalGroupIndices);
}
} // namespace
#endif
