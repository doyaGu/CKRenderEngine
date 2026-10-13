#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include "CKAll.h"
#include "RCKLight.h"
#include "RCKRenderObject.h"
#include "CKRasterizer.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
// Observes the real light submission at the public boundary; no GPU implementation.
class LightSubmissionSpy : public CKRasterizerContext {
public:
    CKLightData Data = {}; CKDWORD SetIndex = 0, EnableIndex = 0; CKBOOL Enabled = FALSE;
    int SetCalls = 0, EnableCalls = 0, Order = 0; CKBOOL Accepted = TRUE;
    CKBOOL Create(WIN_HANDLE Window, int PosX, int PosY, int Width, int Height, int Bpp, CKBOOL Fullscreen, int RefreshRate, int Zbpp, int StencilBpp) override { ADD_FAILURE() << "Unexpected light boundary call: Create"; return FALSE; }
    CKBOOL Resize(int PosX, int PosY, int Width, int Height, CKDWORD Flags) override { ADD_FAILURE() << "Unexpected light boundary call: Resize"; return FALSE; }
    CKBOOL SetOptions(const CKRasterizerOptions *Options) override { ADD_FAILURE() << "Unexpected light boundary call: SetOptions"; return FALSE; }
    CKBOOL GetCaps(CKRasterizerCapsDesc *Caps) const override { ADD_FAILURE() << "Unexpected light boundary call: GetCaps"; return FALSE; }
    CKERROR GetDeviceStatus() const override { ADD_FAILURE() << "Unexpected light boundary call: GetDeviceStatus"; return 0; }
    CKBOOL BeginShutdown() override { ADD_FAILURE() << "Unexpected light boundary call: BeginShutdown"; return FALSE; }
    CKBOOL IsIdle() const override { ADD_FAILURE() << "Unexpected light boundary call: IsIdle"; return FALSE; }
    CKBOOL Clear(CKDWORD Flags, CKDWORD Color, float Z, CKDWORD Stencil, int RectCount, CKRECT *Rects) override { ADD_FAILURE() << "Unexpected light boundary call: Clear"; return FALSE; }
    CKBOOL BeginScene() override { ADD_FAILURE() << "Unexpected light boundary call: BeginScene"; return FALSE; }
    CKBOOL EndScene() override { ADD_FAILURE() << "Unexpected light boundary call: EndScene"; return FALSE; }
    CKBOOL BeginOverlayPhase() override { ADD_FAILURE() << "Unexpected light boundary call: BeginOverlayPhase"; return FALSE; }
    CKBOOL BackToFront(CKBOOL VSync) override { ADD_FAILURE() << "Unexpected light boundary call: BackToFront"; return FALSE; }
    CKBOOL SetRenderState(VXRENDERSTATETYPE State, CKDWORD Value) override { ADD_FAILURE() << "Unexpected light boundary call: SetRenderState"; return FALSE; }
    CKBOOL GetRenderState(VXRENDERSTATETYPE State, CKDWORD *Value) override { ADD_FAILURE() << "Unexpected light boundary call: GetRenderState"; return FALSE; }
    CKBOOL SetTextureStageState(int Stage, CKRST_TEXTURESTAGESTATETYPE Tss, CKDWORD Value) override { ADD_FAILURE() << "Unexpected light boundary call: SetTextureStageState"; return FALSE; }
    CKBOOL GetTextureStageState(int Stage, CKRST_TEXTURESTAGESTATETYPE Tss, CKDWORD *Value) override { ADD_FAILURE() << "Unexpected light boundary call: GetTextureStageState"; return FALSE; }
    CKBOOL ResetTextureStages(int FirstStage, int StageCount) override { ADD_FAILURE() << "Unexpected light boundary call: ResetTextureStages"; return FALSE; }
    CKBOOL SetTexture(CKDWORD Texture, int Stage) override { ADD_FAILURE() << "Unexpected light boundary call: SetTexture"; return FALSE; }
    CKBOOL GetTexture(int Stage, CKDWORD *Texture) override { ADD_FAILURE() << "Unexpected light boundary call: GetTexture"; return FALSE; }
    CKBOOL SetTransformMatrix(VXMATRIX_TYPE Type, const VxMatrix &Mat) override { ADD_FAILURE() << "Unexpected light boundary call: SetTransformMatrix"; return FALSE; }
    CKBOOL GetTransformMatrix(VXMATRIX_TYPE Type, VxMatrix &Mat) override { ADD_FAILURE() << "Unexpected light boundary call: GetTransformMatrix"; return FALSE; }
    CKBOOL SetLight(CKDWORD Index, const CKLightData *DataArg) override { EXPECT_EQ(0, Order); Order = 1; ++SetCalls; SetIndex = Index; Data = *DataArg; return Accepted; }
    CKBOOL EnableLight(CKDWORD Index, CKBOOL Enable) override { EXPECT_EQ(1, Order); Order = 2; ++EnableCalls; EnableIndex = Index; Enabled = Enable; return Accepted; }
    CKBOOL SetMaterial(const CKMaterialData *Data) override { ADD_FAILURE() << "Unexpected light boundary call: SetMaterial"; return FALSE; }
    CKBOOL ApplyMaterial(const CKMaterialRenderState &State) override { ADD_FAILURE() << "Unexpected light boundary call: ApplyMaterial"; return FALSE; }
    CKBOOL SetViewport(const CKViewportData *Data) override { ADD_FAILURE() << "Unexpected light boundary call: SetViewport"; return FALSE; }
    CKBOOL SetUserClipPlane(CKDWORD Index, const VxPlane &Plane) override { ADD_FAILURE() << "Unexpected light boundary call: SetUserClipPlane"; return FALSE; }
    CKBOOL GetUserClipPlane(CKDWORD Index, VxPlane &Plane) override { ADD_FAILURE() << "Unexpected light boundary call: GetUserClipPlane"; return FALSE; }
    void InitDefaultRenderStatesValue() override { ADD_FAILURE() << "Unexpected light boundary call: InitDefaultRenderStatesValue"; }
    CKBOOL DrawPrimitive(VXPRIMITIVETYPE Type, CKWORD *Indices, int IndexCount, VxDrawPrimitiveData *Data) override { ADD_FAILURE() << "Unexpected light boundary call: DrawPrimitive"; return FALSE; }
    CKBOOL DrawPrimitiveVB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD StartVertex, CKDWORD VertexCount, CKWORD *Indices, int IndexCount) override { ADD_FAILURE() << "Unexpected light boundary call: DrawPrimitiveVB"; return FALSE; }
    CKBOOL DrawPrimitiveVBIB(VXPRIMITIVETYPE Type, CKDWORD VB, CKDWORD IB, CKDWORD MinVertexIndex, CKDWORD VertexCount, CKDWORD StartIndex, int IndexCount) override { ADD_FAILURE() << "Unexpected light boundary call: DrawPrimitiveVBIB"; return FALSE; }
    CKBOOL CreateTexture(const CKTextureDesc *Desc, CKDWORD *OutHandle) override { ADD_FAILURE() << "Unexpected light boundary call: CreateTexture"; return FALSE; }
    CKBOOL LoadTexture(CKDWORD Texture, const VxImageDescEx &Image, int MipLevel, CKRST_CUBEFACE Face, const CKRECT *Region) override { ADD_FAILURE() << "Unexpected light boundary call: LoadTexture"; return FALSE; }
    CKBOOL GetTextureDesc(CKDWORD Texture, CKTextureDesc *Desc) const override { ADD_FAILURE() << "Unexpected light boundary call: GetTextureDesc"; return FALSE; }
    CKBOOL CreateVertexBuffer(const CKVertexBufferDesc *Desc, const void *Data, CKDWORD *OutHandle) override { ADD_FAILURE() << "Unexpected light boundary call: CreateVertexBuffer"; return FALSE; }
    CKBOOL GetVertexBufferDesc(CKDWORD VB, CKVertexBufferDesc *Desc) const override { ADD_FAILURE() << "Unexpected light boundary call: GetVertexBufferDesc"; return FALSE; }
    CKBOOL CreateIndexBuffer(const CKIndexBufferDesc *Desc, const void *Data, CKDWORD *OutHandle) override { ADD_FAILURE() << "Unexpected light boundary call: CreateIndexBuffer"; return FALSE; }
    CKBOOL GetIndexBufferDesc(CKDWORD IB, CKIndexBufferDesc *Desc) const override { ADD_FAILURE() << "Unexpected light boundary call: GetIndexBufferDesc"; return FALSE; }
    void *LockVertexBuffer(CKDWORD VB, CKDWORD StartVertex, CKDWORD VertexCount, CKRST_LOCKFLAGS Flags) override { ADD_FAILURE() << "Unexpected light boundary call: LockVertexBuffer"; return nullptr; }
    CKBOOL UnlockVertexBuffer(CKDWORD VB) override { ADD_FAILURE() << "Unexpected light boundary call: UnlockVertexBuffer"; return FALSE; }
    void *LockIndexBuffer(CKDWORD IB, CKDWORD StartIndex, CKDWORD IndexCount, CKRST_LOCKFLAGS Flags) override { ADD_FAILURE() << "Unexpected light boundary call: LockIndexBuffer"; return nullptr; }
    CKBOOL UnlockIndexBuffer(CKDWORD IB) override { ADD_FAILURE() << "Unexpected light boundary call: UnlockIndexBuffer"; return FALSE; }
    CKBOOL DeleteObject(CKRST_HANDLE Handle, CKRST_OBJECTTYPE Type) override { ADD_FAILURE() << "Unexpected light boundary call: DeleteObject"; return FALSE; }
    CKBOOL FlushObjects(CKRST_OBJECTMASK TypeMask) override { ADD_FAILURE() << "Unexpected light boundary call: FlushObjects"; return FALSE; }
    CKBOOL SetResourceName(CKRST_HANDLE Handle, CKRST_OBJECTTYPE Type, CKSTRING Name) override { ADD_FAILURE() << "Unexpected light boundary call: SetResourceName"; return FALSE; }
    CKBOOL SetTargetTexture(CKDWORD Texture, int Width, int Height, CKRST_CUBEFACE Face) override { ADD_FAILURE() << "Unexpected light boundary call: SetTargetTexture"; return FALSE; }
    CKBOOL CopyToTexture(CKDWORD Texture, const VxRect *Src, const VxRect *Dst, CKRST_CUBEFACE Face) override { ADD_FAILURE() << "Unexpected light boundary call: CopyToTexture"; return FALSE; }
    int CopyToMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, VxImageDescEx &Image) override { ADD_FAILURE() << "Unexpected light boundary call: CopyToMemoryBuffer"; return 0; }
    int CopyFromMemoryBuffer(const CKRECT *Rect, VXBUFFER_TYPE Buffer, const VxImageDescEx &Image) override { ADD_FAILURE() << "Unexpected light boundary call: CopyFromMemoryBuffer"; return 0; }
    CKBOOL RequestReadback(const CKRECT *Rect, VXBUFFER_TYPE Buffer, CKReadbackCallback Callback, void *User) override { ADD_FAILURE() << "Unexpected light boundary call: RequestReadback"; return FALSE; }
};
class LightStateProbe : public RCKLight {
public:
    explicit LightStateProbe(CKContext *context) : RCKLight(context) {}
    CKLightData &Data() { return m_LightData; }
    CKDWORD Flags() const { return m_Flags; }
    void Flags(CKDWORD value) { m_Flags = value; }
};
class LightStateTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKLight, CKCID_3DENTITY); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Color(const VxColor &c, float r, float g, float b, float a) {
        EXPECT_FLOAT_EQ(r, c.r); EXPECT_FLOAT_EQ(g, c.g); EXPECT_FLOAT_EQ(b, c.b); EXPECT_FLOAT_EQ(a, c.a);
    }
    static void Vector(const VxVector &v, float x, float y, float z) {
        EXPECT_FLOAT_EQ(x, v.x); EXPECT_FLOAT_EQ(y, v.y); EXPECT_FLOAT_EQ(z, v.z);
    }
    static void NoCalls(const LightSubmissionSpy &spy) { EXPECT_EQ(0, spy.SetCalls); EXPECT_EQ(0, spy.EnableCalls); }
};
}
TEST_F(LightStateTest, ColorCopiesAllComponentsAndProvidesALiveConstReference) {
    LightStateProbe light(Context); VxColor input(-2.0f, 3.0f, .5f, .25f); light.SetColor(input);
    Color(light.GetColor(), -2, 3, .5f, .25f); EXPECT_EQ(&light.Data().Diffuse, &light.GetColor());
    const VxColor &reference = light.GetColor(); input.r = 8; Color(reference, -2, 3, .5f, .25f);
    light.SetColor(VxColor(4.0f, 5.0f, 6.0f, 7.0f)); Color(reference, 4, 5, 6, 7);
}
TEST_F(LightStateTest, AttenuationCoefficientsAreIndependentAndAreNotClamped) {
    LightStateProbe light(Context); light.SetConstantAttenuation(-2); light.SetLinearAttenuation(3); light.SetQuadraticAttenuation(.5f);
    EXPECT_FLOAT_EQ(-2, light.GetConstantAttenuation()); EXPECT_FLOAT_EQ(3, light.GetLinearAttenuation()); EXPECT_FLOAT_EQ(.5f, light.GetQuadraticAttenuation());
    EXPECT_FLOAT_EQ(-2, light.Data().Attenuation0); EXPECT_FLOAT_EQ(3, light.Data().Attenuation1); EXPECT_FLOAT_EQ(.5f, light.Data().Attenuation2);
    LightSubmissionSpy spy; ASSERT_TRUE(light.Setup(&spy, 0)); EXPECT_FLOAT_EQ(-2, spy.Data.Attenuation0); EXPECT_FLOAT_EQ(3, spy.Data.Attenuation1); EXPECT_FLOAT_EQ(.5f, spy.Data.Attenuation2);
}
TEST_F(LightStateTest, TypeSetterStoresPointSpotDirectionalAndUnknownValuesWithoutChangingFlags) {
    LightStateProbe light(Context); const CKDWORD flags = light.Flags();
    const int types[] = {1, 2, 3, 0, 255, -7};
    for (int type : types) { light.SetType(static_cast<VXLIGHT_TYPE>(type)); EXPECT_EQ(type, static_cast<int>(light.GetType())); EXPECT_EQ(flags, light.Flags()); }
}
TEST_F(LightStateTest, RangeAndConeSettersPreserveIndependentUnclampedValues) {
    LightStateProbe light(Context); light.SetRange(-9); light.SetHotSpot(8); light.SetFallOff(-3); light.SetFallOffShape(-.5f);
    EXPECT_FLOAT_EQ(-9, light.GetRange()); EXPECT_FLOAT_EQ(8, light.GetHotSpot()); EXPECT_FLOAT_EQ(-3, light.GetFallOff()); EXPECT_FLOAT_EQ(-.5f, light.GetFallOffShape());
    LightSubmissionSpy spy; light.SetType(VX_LIGHTSPOT); ASSERT_TRUE(light.Setup(&spy, 4));
    EXPECT_FLOAT_EQ(-9, spy.Data.Range); EXPECT_FLOAT_EQ(8, spy.Data.InnerSpotCone); EXPECT_FLOAT_EQ(-3, spy.Data.OuterSpotCone); EXPECT_FLOAT_EQ(-.5f, spy.Data.Falloff);
}
TEST_F(LightStateTest, ActivityChangesOnlyItsBitAndAcceptsAnyNonzeroBoolean) {
    LightStateProbe light(Context); light.Flags(0xfedcba55); const CKDWORD flags = light.Flags();
    light.Active(0); EXPECT_FALSE(light.GetActivity()); EXPECT_EQ(flags & ~0x100u, light.Flags());
    light.Active(-7); EXPECT_EQ(TRUE, light.GetActivity()); EXPECT_EQ(flags | 0x100u, light.Flags());
    light.Active(2); EXPECT_EQ(flags | 0x100u, light.Flags());
}
TEST_F(LightStateTest, SpecularChangesOnlyItsBitAndDoesNotActivateALight) {
    LightStateProbe light(Context); light.Flags(0xabcd0055); const CKDWORD flags = light.Flags();
    light.SetSpecularFlag(-9); EXPECT_EQ(TRUE, light.GetSpecularFlag()); EXPECT_FALSE(light.GetActivity()); EXPECT_EQ(flags | 0x200u, light.Flags());
    light.SetSpecularFlag(0); EXPECT_EQ(FALSE, light.GetSpecularFlag()); EXPECT_EQ(flags, light.Flags());
    light.Active(1); light.SetSpecularFlag(1); EXPECT_EQ(flags | 0x300u, light.Flags());
    light.Active(0); EXPECT_TRUE(light.GetSpecularFlag()); EXPECT_EQ(flags | 0x200u, light.Flags());
}
TEST_F(LightStateTest, PowerSetterPreservesDiffuseAndAllowsNegativeZeroAndExtrapolation) {
    LightStateProbe light(Context); light.SetColor(VxColor(.5f, 1.0f, 2.0f, .25f)); const float powers[] = {-3, 0, 1, 4};
    for (float power : powers) { light.SetLightPower(power); EXPECT_FLOAT_EQ(power, light.GetLightPower()); Color(light.GetColor(), .5f, 1, 2, .25f); }
}
TEST_F(LightStateTest, PlainLightTargetAccessIsANoopThroughTheSdkInterface) {
    LightStateProbe light(Context); RCK3dEntity target(Context); CKLight *api = reinterpret_cast<CKLight *>(&light);
    const CKDWORD flags = target.GetFlags(); api->SetTarget(reinterpret_cast<CK3dEntity *>(&target)); EXPECT_EQ(nullptr, api->GetTarget());
    api->SetTarget(reinterpret_cast<CK3dEntity *>(&light)); api->SetTarget(nullptr); EXPECT_EQ(nullptr, api->GetTarget()); EXPECT_EQ(flags, target.GetFlags());
}
TEST_F(LightStateTest, SetupSubmitsWorldPositionUnnormalizedDirectionAndDataBeforeEnabling) {
    LightStateProbe light(Context); VxMatrix world = VxMatrix::Identity(); world[2].Set(3, 0, 4); world[3].Set(7, 11, 13); light.SetWorldMatrix(world);
    light.SetColor(VxColor(.5f, .25f, 2.0f, .75f)); LightSubmissionSpy spy; ASSERT_TRUE(light.Setup(&spy, 6));
    EXPECT_EQ(1, spy.SetCalls); EXPECT_EQ(1, spy.EnableCalls); EXPECT_EQ(2, spy.Order); EXPECT_EQ(6u, spy.SetIndex); EXPECT_EQ(6u, spy.EnableIndex); EXPECT_EQ(TRUE, spy.Enabled);
    Vector(spy.Data.Position, 7, 11, 13); Vector(spy.Data.Direction, 3, 0, 4); EXPECT_EQ(VX_LIGHTPOINT, spy.Data.Type);
    Color(spy.Data.Diffuse, .5f, .25f, 2, .75f); Color(light.GetColor(), .5f, .25f, 2, .75f);
}
TEST_F(LightStateTest, SpecularAtUnitPowerIncludesTheDiffuseAlpha) {
    LightStateProbe light(Context); light.SetColor(VxColor(.5f, .25f, 2.0f, .375f)); light.SetSpecularFlag(TRUE);
    LightSubmissionSpy spy; ASSERT_TRUE(light.Setup(&spy, 1)); Color(spy.Data.Specular, .5f, .25f, 2, .375f); Color(light.Data().Specular, .5f, .25f, 2, .375f);
}
TEST_F(LightStateTest, SpecularAndDiffuseScaleAllComponentsAndRestoreTheStoredDiffuse) {
    LightStateProbe light(Context); light.SetColor(VxColor(.5f, .25f, 2.0f, .375f)); light.SetSpecularFlag(TRUE); light.SetLightPower(2);
    LightSubmissionSpy spy; ASSERT_TRUE(light.Setup(&spy, 2)); Color(spy.Data.Diffuse, 1, .5f, 4, .75f); Color(spy.Data.Specular, 1, .5f, 4, .75f);
    Color(light.GetColor(), .5f, .25f, 2, .375f); Color(light.Data().Specular, 1, .5f, 4, .75f);
}
TEST_F(LightStateTest, DisabledSpecularProducesOpaqueBlackIndependentOfPower) {
    LightStateProbe light(Context); light.SetColor(VxColor(.5f, .25f, 2.0f, .375f)); light.SetLightPower(-2);
    LightSubmissionSpy spy; ASSERT_TRUE(light.Setup(&spy, 1)); Color(spy.Data.Specular, 0, 0, 0, 1); Color(spy.Data.Diffuse, -1, -.5f, -4, -.75f);
}
TEST_F(LightStateTest, ZeroAndNegativePowerAreSubmittedWithoutClampingOrAccumulation) {
    LightStateProbe light(Context); light.SetColor(VxColor(.5f, .25f, 2.0f, .375f)); light.SetSpecularFlag(TRUE); const float powers[] = {0, -2};
    for (float power : powers) { LightSubmissionSpy spy; light.SetLightPower(power); ASSERT_TRUE(light.Setup(&spy, 0)); Color(spy.Data.Diffuse, .5f * power, .25f * power, 2 * power, .375f * power); Color(spy.Data.Specular, .5f * power, .25f * power, 2 * power, .375f * power); Color(light.GetColor(), .5f, .25f, 2, .375f); }
}
TEST_F(LightStateTest, RepeatedSetupRecomputesTheSpecularAfterChangingPowerAndFlags) {
    LightStateProbe light(Context); light.SetColor(VxColor(.5f, .25f, 2.0f, .375f)); light.SetSpecularFlag(TRUE); light.SetLightPower(2);
    LightSubmissionSpy first; ASSERT_TRUE(light.Setup(&first, 0)); light.SetLightPower(1); LightSubmissionSpy second; ASSERT_TRUE(light.Setup(&second, 1));
    Color(second.Data.Diffuse, .5f, .25f, 2, .375f); Color(second.Data.Specular, .5f, .25f, 2, .375f);
    light.SetSpecularFlag(FALSE); LightSubmissionSpy third; ASSERT_TRUE(light.Setup(&third, 2)); Color(third.Data.Specular, 0, 0, 0, 1);
    Color(light.GetColor(), .5f, .25f, 2, .375f);
}
TEST_F(LightStateTest, HiddenLightDoesNotSubmitOrMutateCachedLightData) {
    LightStateProbe light(Context); light.Data().Position.Set(1, 2, 3); light.Data().Specular.Set(4.0f, 5.0f, 6.0f, 7.0f); light.Show(CKHIDE);
    LightSubmissionSpy spy; EXPECT_FALSE(light.Setup(&spy, 0)); NoCalls(spy); Vector(light.Data().Position, 1, 2, 3); Color(light.Data().Specular, 4, 5, 6, 7);
}
TEST_F(LightStateTest, InactiveLightDoesNotSubmitOrMutateItsPose) {
    LightStateProbe light(Context); light.Data().Position.Set(1, 2, 3); light.SetPosition3f(4, 5, 6); light.Active(FALSE);
    LightSubmissionSpy spy; EXPECT_FALSE(light.Setup(&spy, 0)); NoCalls(spy); Vector(light.Data().Position, 1, 2, 3);
}
TEST_F(LightStateTest, AttenuationBelowTheNativeCutoffDoesNotSubmit) {
    LightStateProbe light(Context); const float values[] = {0, -1, .000001f};
    for (float value : values) { light.SetConstantAttenuation(value); LightSubmissionSpy spy; EXPECT_FALSE(light.Setup(&spy, 0)); NoCalls(spy); }
}
TEST_F(LightStateTest, DirectionalLightBypassesAttenuationWhileSpotAndUnknownTypesDoNot) {
    LightStateProbe light(Context); light.SetConstantAttenuation(0); light.SetType(VX_LIGHTDIREC); LightSubmissionSpy directional;
    EXPECT_TRUE(light.Setup(&directional, 0)); EXPECT_EQ(1, directional.SetCalls);
    light.SetType(VX_LIGHTSPOT); LightSubmissionSpy spot; EXPECT_FALSE(light.Setup(&spot, 0)); NoCalls(spot);
    light.SetType(static_cast<VXLIGHT_TYPE>(255)); LightSubmissionSpy unknown; EXPECT_FALSE(light.Setup(&unknown, 0)); NoCalls(unknown);
}
TEST_F(LightStateTest, CutoffUsesTheOriginalDoubleConstantBetweenAdjacentFloatValues) {
    LightStateProbe light(Context); const float below = 0.00001f, above = std::nextafter(below, std::numeric_limits<float>::infinity());
    ASSERT_LT(static_cast<double>(below), 0.00001); ASSERT_GT(static_cast<double>(above), 0.00001);
    light.SetConstantAttenuation(below); LightSubmissionSpy rejected; EXPECT_FALSE(light.Setup(&rejected, 0)); NoCalls(rejected);
    light.SetConstantAttenuation(above); LightSubmissionSpy accepted; EXPECT_TRUE(light.Setup(&accepted, 0)); EXPECT_EQ(1, accepted.SetCalls);
}
TEST_F(LightStateTest, AttenuationSumRetainsAMeaningfulTermAcrossCancellation) {
    LightStateProbe light(Context); light.SetConstantAttenuation(100000000); light.SetLinearAttenuation(1); light.SetQuadraticAttenuation(-100000000);
    LightSubmissionSpy spy; EXPECT_TRUE(light.Setup(&spy, 0)); EXPECT_EQ(1, spy.SetCalls); EXPECT_EQ(1, spy.EnableCalls);
}
TEST_F(LightStateTest, ParentedLightSubmitsItsWorldPose) {
    RCK3dEntity parent(Context); LightStateProbe light(Context); VxMatrix world = VxMatrix::Identity(); world[0].Set(0, 1, 0); world[1].Set(-1, 0, 0); world[3].Set(20, 30, 40);
    parent.SetWorldMatrix(world); light.SetParent(reinterpret_cast<CK3dEntity *>(&parent), FALSE); light.SetPosition3f(3, 4, 5, reinterpret_cast<CK3dEntity *>(&parent));
    LightSubmissionSpy spy; ASSERT_TRUE(light.Setup(&spy, 3)); Vector(spy.Data.Position, 16, 33, 45); Vector(spy.Data.Direction, 0, 0, 1);
    light.SetParent(nullptr, TRUE);
}
TEST_F(LightStateTest, SetupReturnDescribesEligibilityAndPreservesDiffuseWhenSubmissionReturnsFalse) {
    LightStateProbe light(Context); light.SetColor(VxColor(.5f, .25f, 2.0f, .375f)); light.SetLightPower(2); LightSubmissionSpy spy; spy.Accepted = FALSE;
    EXPECT_TRUE(light.Setup(&spy, 0)); EXPECT_EQ(1, spy.SetCalls); EXPECT_EQ(1, spy.EnableCalls); EXPECT_EQ(2, spy.Order); Color(light.GetColor(), .5f, .25f, 2, .375f);
}
