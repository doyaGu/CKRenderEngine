#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKMaterial.h"
#include "RCKTexture.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class SerializedMaterial : public RCKMaterial {
public:
    explicit SerializedMaterial(CKContext *context) : RCKMaterial(context) {}
    void Raw(CKDWORD flags, CKDWORD alpha = 0) { m_Flags = flags; m_AlphaRef = alpha; }
    CKDWORD Flags() const { return m_Flags; }
    const VxColor &LitSpecular() const { return m_MaterialData.Specular; }
    void Parameter(CKParameter *parameter) { m_EffectParameter = parameter; }
};
class ObservedMaterial : public SerializedMaterial {
public:
    explicit ObservedMaterial(CKContext *context) : SerializedMaterial(context) {}
    int EffectCalls = 0;
    VX_EFFECT Effects[2] = {};
    CKParameter *Parameters[2] = {};
    int PowerCalls = 0;
    VxColor SpecularAtPower;
    void SetEffect(VX_EFFECT effect) override {
        if (EffectCalls < 2) { Effects[EffectCalls] = effect; Parameters[EffectCalls] = m_EffectParameter; }
        ++EffectCalls; m_Flags = (m_Flags & ~0x3f00u) | ((effect & 0x3f) << 8);
    }
    void SetPower(float value) override {
        ++PowerCalls; SpecularAtPower = m_SpecularColor; SerializedMaterial::SetPower(value);
    }
};
class DerivedMaterial : public SerializedMaterial {
public:
    explicit DerivedMaterial(CKContext *context) : SerializedMaterial(context) {}
    CK_CLASSID GetClassID() override { return CKCID_SPRITE3D; }
};
class MaterialSerializationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Begin(CKStateChunk &chunk, int version = 10) { chunk.StartWrite(); chunk.SetDataVersion(version); }
    static CKERROR Load(RCKMaterial &material, CKStateChunk &chunk, CKFile *file = nullptr) {
        chunk.CloseChunk(); return material.Load(&chunk, file);
    }
    static void Color(const VxColor &c, float r, float g, float b, float a) {
        EXPECT_FLOAT_EQ(r, c.r); EXPECT_FLOAT_EQ(g, c.g); EXPECT_FLOAT_EQ(b, c.b); EXPECT_FLOAT_EQ(a, c.a);
    }
    static void Packed(CKStateChunk &chunk, int texture = 0, CKDWORD modes = 0x43216543, CKDWORD flags = 0xab050609, float power = 2) {
        // Independent 2.1 main payload: ARGB colors, power, reference, border, eight nibbles, packed flags.
        chunk.WriteIdentifier(0x1000); chunk.WriteDword(0x80402010); chunk.WriteDword(0x10203040);
        chunk.WriteDword(0xffabcdef); chunk.WriteDword(0x7f112233); chunk.WriteFloat(power); chunk.WriteInt(texture);
        chunk.WriteDword(0x91827364); chunk.WriteDword(modes); chunk.WriteDword(flags);
    }
    static void LegacyColor(CKStateChunk &chunk, float r, float g, float b, float a) {
        // Each legacy color is a literal 16-byte length prefix followed by four floats.
        chunk.WriteInt(16); chunk.WriteFloat(r); chunk.WriteFloat(g); chunk.WriteFloat(b); chunk.WriteFloat(a);
    }
    static void Legacy(CKStateChunk &chunk, int texture = 0, CKDWORD low = 0x17, CKDWORD source = 6, CKDWORD dest = 1, CKDWORD z = 7, CKDWORD shade = 2) {
        chunk.WriteIdentifier(0x1000); LegacyColor(chunk, .125f, .375f, .625f, .5f);
        LegacyColor(chunk, .25f, .5f, .75f, 1); LegacyColor(chunk, .75f, .25f, .5f, .875f);
        LegacyColor(chunk, 1, .5f, .25f, .125f); chunk.WriteFloat(2); chunk.WriteInt(texture); chunk.WriteDword(low);
        chunk.WriteDword(3); chunk.WriteDword(4); chunk.WriteDword(5); chunk.WriteDword(source); chunk.WriteDword(dest);
        chunk.WriteDword(shade); chunk.WriteDword(3); chunk.WriteDword(4); chunk.WriteDword(0x91827364); chunk.WriteDword(z);
    }
    static void Seed(SerializedMaterial &m) {
        m.SetDiffuse(VxColor(1.f, 0.f, 0.f, 1.f)); m.SetAmbient(VxColor(0.f, 1.f, 0.f, 0.f));
        m.SetSpecular(VxColor(0.f, 0.f, 1.f, 1.f)); m.SetEmissive(VxColor(1.f, 1.f, 1.f, 0.f)); m.SetPower(0);
        m.SetTextureBlendMode(static_cast<VXTEXTURE_BLENDMODE>(3)); m.SetTextureMinMode(static_cast<VXTEXTURE_FILTERMODE>(4));
        m.SetTextureMagMode(static_cast<VXTEXTURE_FILTERMODE>(5)); m.SetSourceBlend(static_cast<VXBLEND_MODE>(6));
        m.SetDestBlend(static_cast<VXBLEND_MODE>(1)); m.SetShadeMode(static_cast<VXSHADE_MODE>(2));
        m.SetFillMode(static_cast<VXFILL_MODE>(3)); m.SetTextureAddressMode(static_cast<VXTEXTURE_ADDRESSMODE>(4));
        m.SetTextureBorderColor(0x91827364); m.Raw(0x80000009u | (6u << 14) | (5u << 19), 0xab);
    }
    static void ReadMain(CKStateChunk &chunk, int reference) {
        chunk.StartRead(); ASSERT_TRUE(chunk.SeekIdentifier(0x1000)); EXPECT_EQ(0xffff0000u, chunk.ReadDword());
        EXPECT_EQ(0x0000ff00u, chunk.ReadDword()); EXPECT_EQ(0xff0000ffu, chunk.ReadDword());
        EXPECT_EQ(0x00ffffffu, chunk.ReadDword()); EXPECT_FLOAT_EQ(0, chunk.ReadFloat()); EXPECT_EQ(reference, chunk.ReadInt());
        EXPECT_EQ(0x91827364u, chunk.ReadDword()); EXPECT_EQ(0x43216543u, chunk.ReadDword()); EXPECT_EQ(0xab050609u, chunk.ReadDword());
    }
};
}

TEST_F(MaterialSerializationTest, NullLoadPreservesColorsFlagsPowerAndEveryTexture) {
    SerializedMaterial m(Context); RCKTexture t(Context); Seed(m); for (int i = 0; i < 4; ++i) m.SetTexture(i, &t);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, m.Load(nullptr, nullptr)); Color(m.GetDiffuse(), 1, 0, 0, 1);
    EXPECT_EQ(0x80298009u, m.Flags()); EXPECT_FLOAT_EQ(0, m.GetPower()); for (int i = 0; i < 4; ++i) EXPECT_EQ(&t, m.GetTexture(i));
}
TEST_F(MaterialSerializationTest, MissingMaterialSectionsClearTexturesButRetainOtherOwnState) {
    SerializedMaterial m(Context); RCKTexture t(Context); Seed(m); m.Raw(m.Flags() | (3 << 8), m.GetAlphaRef());
    for (int i = 0; i < 4; ++i) m.SetTexture(i, &t); CKStateChunk chunk(CKCID_MATERIAL, nullptr); Begin(chunk);
    chunk.WriteIdentifier(0x76543210); chunk.WriteInt(17); ASSERT_EQ(CK_OK, Load(m, chunk));
    Color(m.GetDiffuse(), 1, 0, 0, 1); EXPECT_EQ(static_cast<VX_EFFECT>(3), m.GetEffect());
    EXPECT_EQ(0xabu, m.GetAlphaRef()); for (int i = 0; i < 4; ++i) EXPECT_EQ(nullptr, m.GetTexture(i));
}
TEST_F(MaterialSerializationTest, LiteralPackedColorsAndModesDecodeEveryIndependentNibble) {
    SerializedMaterial m(Context); CKStateChunk chunk(CKCID_MATERIAL, nullptr); Begin(chunk); Packed(chunk);
    ASSERT_EQ(CK_OK, Load(m, chunk)); Color(m.GetDiffuse(), 64/255.f, 32/255.f, 16/255.f, 128/255.f);
    Color(m.GetAmbient(), 32/255.f, 48/255.f, 64/255.f, 16/255.f);
    Color(m.GetSpecular(), 171/255.f, 205/255.f, 239/255.f, 1);
    Color(m.GetEmissive(), 17/255.f, 34/255.f, 51/255.f, 127/255.f);
    EXPECT_EQ(3, m.GetTextureBlendMode()); EXPECT_EQ(4, m.GetTextureMinMode()); EXPECT_EQ(5, m.GetTextureMagMode());
    EXPECT_EQ(6, m.GetSourceBlend()); EXPECT_EQ(1, m.GetDestBlend()); EXPECT_EQ(2, m.GetShadeMode());
    EXPECT_EQ(3, m.GetFillMode()); EXPECT_EQ(4, m.GetTextureAddressMode()); EXPECT_EQ(0x91827364u, m.GetTextureBorderColor());
    EXPECT_TRUE(m.IsTwoSided()); EXPECT_TRUE(m.AlphaBlendEnabled()); EXPECT_FALSE(m.ZWriteEnabled());
    EXPECT_EQ(6, m.GetZFunc()); EXPECT_EQ(5, m.GetAlphaFunc()); EXPECT_EQ(0xabu, m.GetAlphaRef());
}
TEST_F(MaterialSerializationTest, VersionsFiveTenAndTwelveShareThePackedWireContract) {
    for (int version : {5, 10, 12}) { SerializedMaterial m(Context); CKStateChunk chunk(CKCID_MATERIAL, nullptr); Begin(chunk, version); Packed(chunk);
        ASSERT_EQ(CK_OK, Load(m, chunk)); EXPECT_FLOAT_EQ(2, m.GetPower()); EXPECT_EQ(0xabu, m.GetAlphaRef()); EXPECT_EQ(6, m.GetZFunc()); }
}
TEST_F(MaterialSerializationTest, PackedReservedFlagNibblesAreIgnoredAndZeroAlphaDefaultsToAlways) {
    SerializedMaterial m(Context); m.Raw(0x80000306); CKStateChunk chunk(CKCID_MATERIAL, nullptr); Begin(chunk); Packed(chunk, 0, 0x43216543, 0x45f0f009);
    ASSERT_EQ(CK_OK, Load(m, chunk)); EXPECT_EQ(0, m.GetZFunc()); EXPECT_EQ(VXCMP_ALWAYS, m.GetAlphaFunc());
    EXPECT_EQ(0x45u, m.GetAlphaRef()); EXPECT_EQ(static_cast<VX_EFFECT>(3), m.GetEffect()); EXPECT_TRUE(m.Flags() & 0x80000000u);
}
TEST_F(MaterialSerializationTest, PackedPowerInvokesVirtualSetterAfterCachedSpecularIsDecoded) {
    ObservedMaterial m(Context); CKStateChunk chunk(CKCID_MATERIAL, nullptr); Begin(chunk); Packed(chunk);
    ASSERT_EQ(CK_OK, Load(m, chunk)); EXPECT_EQ(1, m.PowerCalls);
    Color(m.SpecularAtPower, 171/255.f, 205/255.f, 239/255.f, 1); Color(m.LitSpecular(), 171/255.f, 205/255.f, 239/255.f, 1);
}
TEST_F(MaterialSerializationTest, PackedZeroPowerDisablesLightingWithoutDestroyingCachedSpecular) {
    SerializedMaterial m(Context); CKStateChunk chunk(CKCID_MATERIAL, nullptr); Begin(chunk); Packed(chunk, 0, 0x43216543, 0xab050609, 0);
    ASSERT_EQ(CK_OK, Load(m, chunk)); Color(m.LitSpecular(), 0, 0, 0, 0); Color(m.GetSpecular(), 171/255.f, 205/255.f, 239/255.f, 1);
}
TEST_F(MaterialSerializationTest, PackedAndLegacyShadeValuesAboveGouraudAreLimited) {
    for (int version : {4, 5}) { SerializedMaterial m(Context); CKStateChunk chunk(CKCID_MATERIAL, nullptr); Begin(chunk, version);
        if (version == 5) Packed(chunk, 0, 0x43f16543); else Legacy(chunk, 0, 0x17, 6, 1, 7, 15);
        ASSERT_EQ(CK_OK, Load(m, chunk)); EXPECT_EQ(VXSHADE_GOURAUD, m.GetShadeMode()); }
}
TEST_F(MaterialSerializationTest, LegacyFourLengthPrefixedColorsDoNotShiftPowerOrFollowingModes) {
    SerializedMaterial m(Context); CKStateChunk chunk(CKCID_MATERIAL, nullptr); Begin(chunk, 4); Legacy(chunk);
    ASSERT_EQ(CK_OK, Load(m, chunk)); Color(m.GetDiffuse(), .125f, .375f, .625f, .5f);
    Color(m.GetAmbient(), .25f, .5f, .75f, 1); Color(m.GetSpecular(), .75f, .25f, .5f, .875f); Color(m.GetEmissive(), 1, .5f, .25f, .125f);
    EXPECT_FLOAT_EQ(2, m.GetPower()); EXPECT_EQ(3, m.GetTextureBlendMode()); EXPECT_EQ(4, m.GetTextureMinMode()); EXPECT_EQ(5, m.GetTextureMagMode());
    EXPECT_EQ(6, m.GetSourceBlend()); EXPECT_EQ(1, m.GetDestBlend()); EXPECT_EQ(3, m.GetFillMode()); EXPECT_EQ(4, m.GetTextureAddressMode());
    EXPECT_EQ(0x91827364u, m.GetTextureBorderColor()); EXPECT_EQ(7, m.GetZFunc()); EXPECT_EQ(VXCMP_ALWAYS, m.GetAlphaFunc());
    EXPECT_EQ(0, m.GetAlphaRef()); EXPECT_TRUE(m.AlphaTestEnabled()); EXPECT_TRUE(m.PerspectiveCorrectionEnabled());
}
TEST_F(MaterialSerializationTest, LegacyUsesVirtualPowerSetterAfterTheFourthBuffer) {
    ObservedMaterial m(Context); CKStateChunk chunk(CKCID_MATERIAL, nullptr); Begin(chunk, 4); Legacy(chunk);
    ASSERT_EQ(CK_OK, Load(m, chunk)); EXPECT_EQ(1, m.PowerCalls); Color(m.SpecularAtPower, .75f, .25f, .5f, .875f);
}
TEST_F(MaterialSerializationTest, LegacyVersionFourKeepsFlagsAndBlendFactorsWithoutVersionThreeMigration) {
    SerializedMaterial m(Context); CKStateChunk chunk(CKCID_MATERIAL, nullptr); Begin(chunk, 4); Legacy(chunk, 0, 1, 2, 1, 0);
    ASSERT_EQ(CK_OK, Load(m, chunk)); EXPECT_EQ(1u, m.Flags() & 0xff); EXPECT_EQ(VXBLEND_ONE, m.GetSourceBlend());
    EXPECT_EQ(VXBLEND_ZERO, m.GetDestBlend()); EXPECT_EQ(VXCMP_LESSEQUAL, m.GetZFunc());
}
TEST_F(MaterialSerializationTest, LegacyVersionThreeMigratesTransparentDiffuseAndDisablesZWrite) {
    SerializedMaterial m(Context); CKStateChunk chunk(CKCID_MATERIAL, nullptr); Begin(chunk, 3); Legacy(chunk, 0, 1, 2, 1, 0);
    ASSERT_EQ(CK_OK, Load(m, chunk)); EXPECT_EQ(VXBLEND_SRCALPHA, m.GetSourceBlend()); EXPECT_EQ(VXBLEND_INVSRCALPHA, m.GetDestBlend());
    EXPECT_TRUE(m.AlphaBlendEnabled()); EXPECT_FALSE(m.ZWriteEnabled()); EXPECT_TRUE(m.IsTwoSided()); EXPECT_TRUE(m.PerspectiveCorrectionEnabled());
}
TEST_F(MaterialSerializationTest, LegacyZeroFlagsMigrateAndAllLegacyLoadsClearTheEffectBits) {
    ObservedMaterial m(Context); m.Raw(0x80000700); CKStateChunk chunk(CKCID_MATERIAL, nullptr); Begin(chunk, 3); Legacy(chunk, 0, 0, 6, 1, 7);
    ASSERT_EQ(CK_OK, Load(m, chunk)); EXPECT_EQ(6u, m.Flags() & 0xff); EXPECT_EQ(VXEFFECT_NONE, m.GetEffect());
    EXPECT_EQ(0, m.EffectCalls); EXPECT_TRUE(m.Flags() & 0x80000000u);
}
TEST_F(MaterialSerializationTest, LiteralFileTextureIndexZeroAndMinusOneAreDistinctInBothFormats) {
    RCKTexture t(Context); CKFile file(Context); file.m_FileObjects.Resize(1); file.m_FileObjects[0].CreatedObject = t.GetID();
    for (int version : {4, 5, 10, 12}) for (int index : {0, -1, 99}) { SerializedMaterial m(Context); m.SetTexture0(&t);
        CKStateChunk chunk(CKCID_MATERIAL, &file); Begin(chunk, version); if (version < 5) Legacy(chunk, index); else Packed(chunk, index);
        ASSERT_EQ(CK_OK, Load(m, chunk, &file)); EXPECT_EQ(index == 0 ? &t : nullptr, m.GetTexture()); }
}
TEST_F(MaterialSerializationTest, FilelessPrimaryAndExtraTextureReferencesUseRawIDsAndZeroNull) {
    RCKTexture a(Context), b(Context); SerializedMaterial m(Context); CKStateChunk chunk(CKCID_MATERIAL, nullptr); Begin(chunk); Packed(chunk, a.GetID());
    chunk.WriteIdentifier(0x2000); chunk.WriteInt(b.GetID()); chunk.WriteInt(0); chunk.WriteInt(a.GetID());
    ASSERT_EQ(CK_OK, Load(m, chunk)); EXPECT_EQ(&a, m.GetTexture(0)); EXPECT_EQ(&b, m.GetTexture(1));
    EXPECT_EQ(nullptr, m.GetTexture(2)); EXPECT_EQ(&a, m.GetTexture(3)); EXPECT_EQ(VXEFFECT_NONE, m.GetEffect());
}
TEST_F(MaterialSerializationTest, FileExtraTexturesResolveZeroMinusOneAndUnresolvedIndexIndependently) {
    RCKTexture t(Context); SerializedMaterial m(Context); CKFile file(Context); file.m_FileObjects.Resize(1); file.m_FileObjects[0].CreatedObject = t.GetID();
    CKStateChunk chunk(CKCID_MATERIAL, &file); Begin(chunk); chunk.WriteIdentifier(0x2000); chunk.WriteInt(0); chunk.WriteInt(-1); chunk.WriteInt(99);
    ASSERT_EQ(CK_OK, Load(m, chunk, &file)); EXPECT_EQ(nullptr, m.GetTexture(0)); EXPECT_EQ(&t, m.GetTexture(1));
    EXPECT_EQ(nullptr, m.GetTexture(2)); EXPECT_EQ(nullptr, m.GetTexture(3));
}
TEST_F(MaterialSerializationTest, PrimaryWrongClassReferenceDoesNotBecomeATexture) {
    CKObject object(Context); SerializedMaterial m(Context); CKStateChunk chunk(CKCID_MATERIAL, nullptr); Begin(chunk); Packed(chunk, object.GetID());
    ASSERT_EQ(CK_OK, Load(m, chunk)); EXPECT_EQ(nullptr, m.GetTexture());
}
TEST_F(MaterialSerializationTest, AdditionalWrongClassReferencesDoNotBecomeTextures) {
    CKObject object(Context); SerializedMaterial m(Context); CKStateChunk chunk(CKCID_MATERIAL, nullptr); Begin(chunk);
    chunk.WriteIdentifier(0x2000); chunk.WriteInt(object.GetID()); chunk.WriteInt(0); chunk.WriteInt(object.GetID());
    ASSERT_EQ(CK_OK, Load(m, chunk)); for (int i = 0; i < 4; ++i) EXPECT_EQ(nullptr, m.GetTexture(i));
}
TEST_F(MaterialSerializationTest, OwnLoadAlsoAppliesIndependentAncestorVisibilitySection) {
    SerializedMaterial m(Context); CKStateChunk chunk(CKCID_MATERIAL, nullptr); Begin(chunk); Packed(chunk);
    chunk.WriteIdentifier(CK_STATESAVE_OBJECTHIDDEN); ASSERT_EQ(CK_OK, Load(m, chunk));
    EXPECT_FALSE(m.IsVisible()); EXPECT_EQ(0xabu, m.GetAlphaRef());
}
TEST_F(MaterialSerializationTest, BothEffectSectionsDispatchInOrderWithParameterInstalledBeforeSecondCall) {
    CKParameter *createdParameter = static_cast<CKParameter *>(Context->CreateObject(CKCID_PARAMETER, nullptr, CK_OBJECTCREATION_NONAMECHECK));
    ASSERT_NE(nullptr, createdParameter); CKParameter &parameter = *createdParameter;
    ObservedMaterial m(Context); CKStateChunk chunk(CKCID_MATERIAL, nullptr); Begin(chunk);
    chunk.WriteIdentifier(0x4000); chunk.WriteDword(2); chunk.WriteIdentifier(0x10000); chunk.WriteInt(parameter.GetID()); chunk.WriteDword(3);
    ASSERT_EQ(CK_OK, Load(m, chunk)); ASSERT_EQ(2, m.EffectCalls); EXPECT_EQ(static_cast<VX_EFFECT>(2), m.Effects[0]);
    EXPECT_EQ(static_cast<VX_EFFECT>(3), m.Effects[1]); EXPECT_EQ(nullptr, m.Parameters[0]); EXPECT_EQ(&parameter, m.Parameters[1]);
    EXPECT_EQ(&parameter, m.GetEffectParameter());
}
TEST_F(MaterialSerializationTest, EffectParameterFileIndexZeroResolvesAndMinusOneClearsOldParameter) {
    CKParameter *createdParameter = static_cast<CKParameter *>(Context->CreateObject(CKCID_PARAMETER, nullptr, CK_OBJECTCREATION_NONAMECHECK));
    ASSERT_NE(nullptr, createdParameter); CKParameter &parameter = *createdParameter;
    CKFile file(Context); file.m_FileObjects.Resize(1); file.m_FileObjects[0].CreatedObject = parameter.GetID();
    for (int index : {0, -1, 99}) { ObservedMaterial m(Context); m.Parameter(&parameter); CKStateChunk chunk(CKCID_MATERIAL, &file); Begin(chunk);
        chunk.WriteIdentifier(0x10000); chunk.WriteInt(index); chunk.WriteDword(4); ASSERT_EQ(CK_OK, Load(m, chunk, &file));
        ASSERT_EQ(1, m.EffectCalls); EXPECT_EQ(index == 0 ? &parameter : nullptr, m.Parameters[0]); }
}
TEST_F(MaterialSerializationTest, DeprecatedMaterialDataFourDoesNotChangeTheActiveEffectOrParameter) {
    CKParameter *createdParameter = static_cast<CKParameter *>(Context->CreateObject(CKCID_PARAMETER, nullptr, CK_OBJECTCREATION_NONAMECHECK));
    ASSERT_NE(nullptr, createdParameter); CKParameter &parameter = *createdParameter;
    ObservedMaterial m(Context); m.Raw(0x410306); m.Parameter(&parameter);
    CKStateChunk chunk(CKCID_MATERIAL, nullptr); Begin(chunk); chunk.WriteIdentifier(0x8000); chunk.WriteDword(4); chunk.WriteInt(parameter.GetID());
    ASSERT_EQ(CK_OK, Load(m, chunk)); EXPECT_EQ(0, m.EffectCalls); EXPECT_EQ(static_cast<VX_EFFECT>(3), m.GetEffect()); EXPECT_EQ(&parameter, m.GetEffectParameter());
}
TEST_F(MaterialSerializationTest, FilelessZeroAndUnrelatedMasksReturnOnlyTheAncestorChunk) {
    SerializedMaterial m(Context); Seed(m);
    for (CKDWORD flags : {CKDWORD(0), CKDWORD(0x00400000)}) { CKStateChunk *chunk = m.Save(nullptr, flags); ASSERT_NE(nullptr, chunk);
        chunk->StartRead(); EXPECT_FALSE(chunk->SeekIdentifier(0x1000)); EXPECT_NE(CKCID_MATERIAL, chunk->GetChunkClassID()); DeleteCKStateChunk(chunk); }
}
TEST_F(MaterialSerializationTest, EveryOwnAndReservedBitWritesTheCompleteOrderedMainPayload) {
    SerializedMaterial m(Context); Seed(m);
    for (CKDWORD bit = 0x1000; bit <= 0x80000; bit <<= 1) { CKStateChunk *chunk = m.Save(nullptr, bit); ASSERT_NE(nullptr, chunk);
        EXPECT_EQ(CKCID_MATERIAL, chunk->GetChunkClassID()); EXPECT_EQ(CHUNKDATA_CURRENTVERSION, chunk->GetDataVersion());
        EXPECT_EQ(-1, chunk->GetCurrentPos()); ReadMain(*chunk, 0); EXPECT_FALSE(chunk->SeekIdentifier(0x4000));
        EXPECT_FALSE(chunk->SeekIdentifier(0x10000)); EXPECT_FALSE(chunk->SeekIdentifier(0x2000)); DeleteCKStateChunk(chunk); }
}
TEST_F(MaterialSerializationTest, FileZeroMaskWritesMainPayloadAndRegisteredTextureIndexZero) {
    SerializedMaterial m(Context); RCKTexture t(Context); Seed(m); m.SetTexture0(&t); CKFile file(Context); file.m_IndexByClassId.Resize(CKGetClassCount());
    file.SaveObject(&t, 0); CKStateChunk *chunk = m.Save(&file, 0); ASSERT_NE(nullptr, chunk); ReadMain(*chunk, 0); DeleteCKStateChunk(chunk);
}
TEST_F(MaterialSerializationTest, FileSaveRetainsInheritedExecutionPrioritySection) {
    SerializedMaterial m(Context); m.SetPriority(731); CKFile file(Context); CKStateChunk *chunk = m.Save(&file, 0);
    ASSERT_NE(nullptr, chunk); chunk->StartRead(); ASSERT_TRUE(chunk->SeekIdentifier(CK_STATESAVE_DATAS));
    EXPECT_EQ(0x10000000u, chunk->ReadDword()); EXPECT_EQ(731, chunk->ReadInt());
    EXPECT_TRUE(chunk->SeekIdentifier(0x1000)); DeleteCKStateChunk(chunk);
}
TEST_F(MaterialSerializationTest, FileUnregisteredOrNullTextureUsesMinusOneWhileFilelessUsesRawID) {
    SerializedMaterial m(Context); RCKTexture t(Context); Seed(m); CKFile file(Context);
    for (CKTexture *texture : {static_cast<CKTexture *>(nullptr), static_cast<CKTexture *>(&t)}) { m.SetTexture0(texture); CKStateChunk *chunk = m.Save(&file, 0);
        ASSERT_NE(nullptr, chunk); ReadMain(*chunk, -1); DeleteCKStateChunk(chunk); chunk = m.Save(nullptr, 0x1000);
        ASSERT_NE(nullptr, chunk); ReadMain(*chunk, texture ? static_cast<int>(t.GetID()) : 0); DeleteCKStateChunk(chunk); }
}
TEST_F(MaterialSerializationTest, SavePreservesExtraTexturesEvenWhenTheEffectIsNone) {
    SerializedMaterial m(Context); RCKTexture t(Context); m.SetTexture(2, &t); CKStateChunk *chunk = m.Save(nullptr, 0x1000); ASSERT_NE(nullptr, chunk);
    chunk->StartRead(); ASSERT_TRUE(chunk->SeekIdentifier(0x2000)); EXPECT_EQ(0, chunk->ReadInt()); EXPECT_EQ(t.GetID(), chunk->ReadDword());
    EXPECT_EQ(0, chunk->ReadInt()); EXPECT_FALSE(chunk->SeekIdentifier(0x4000)); EXPECT_FALSE(chunk->SeekIdentifier(0x10000)); DeleteCKStateChunk(chunk);
}
TEST_F(MaterialSerializationTest, SaveEffectWithoutParameterAndWithParameterUseDistinctWireSections) {
    CKParameter *createdParameter = static_cast<CKParameter *>(Context->CreateObject(CKCID_PARAMETER, nullptr, CK_OBJECTCREATION_NONAMECHECK));
    ASSERT_NE(nullptr, createdParameter); CKParameter &parameter = *createdParameter;
    SerializedMaterial m(Context); m.Raw(0x410306);
    CKStateChunk *chunk = m.Save(nullptr, 0x1000); ASSERT_NE(nullptr, chunk); chunk->StartRead();
    ASSERT_TRUE(chunk->SeekIdentifier(0x4000)); EXPECT_EQ(3u, chunk->ReadDword()); EXPECT_FALSE(chunk->SeekIdentifier(0x10000)); DeleteCKStateChunk(chunk);
    m.Parameter(&parameter); chunk = m.Save(nullptr, 0x1000); ASSERT_NE(nullptr, chunk); chunk->StartRead();
    ASSERT_TRUE(chunk->SeekIdentifier(0x10000)); EXPECT_EQ(parameter.GetID(), chunk->ReadDword()); EXPECT_EQ(3u, chunk->ReadDword());
    EXPECT_FALSE(chunk->SeekIdentifier(0x4000)); DeleteCKStateChunk(chunk);
}
TEST_F(MaterialSerializationTest, SaveCachedSpecularAndFlagNibblesOmitDisabledLightingAndInternalEffectBits) {
    SerializedMaterial m(Context); Seed(m); m.Raw(m.Flags() | (0x12u << 8) | (0x10u << 14) | (0x10u << 19), m.GetAlphaRef());
    CKStateChunk *chunk = m.Save(nullptr, 0x1000); ASSERT_NE(nullptr, chunk); ReadMain(*chunk, 0); DeleteCKStateChunk(chunk);
}
TEST_F(MaterialSerializationTest, DerivedMaterialStillReturnsAClosedMaterialClassChunk) {
    DerivedMaterial m(Context); CKStateChunk *chunk = m.Save(nullptr, 0x1000); ASSERT_NE(nullptr, chunk);
    EXPECT_EQ(CKCID_MATERIAL, chunk->GetChunkClassID()); EXPECT_EQ(-1, chunk->GetCurrentPos()); DeleteCKStateChunk(chunk);
}
TEST_F(MaterialSerializationTest, PreSaveRegistersParameterBeforeTexturesAndKeepsSeparateSaveMasks) {
    CKParameter *createdParameter = static_cast<CKParameter *>(Context->CreateObject(CKCID_PARAMETER, nullptr, CK_OBJECTCREATION_NONAMECHECK));
    ASSERT_NE(nullptr, createdParameter); CKParameter &parameter = *createdParameter;
    RCKTexture a(Context), b(Context); SerializedMaterial m(Context); m.Raw(0x410306); m.Parameter(&parameter);
    m.SetTexture(0, &a); m.SetTexture(2, &b); CKFile file(Context); file.m_IndexByClassId.Resize(CKGetClassCount());
    m.PreSave(&file, 0x1234); m.PreSave(&file, 0x1234); ASSERT_EQ(3, file.m_FileObjects.Size());
    EXPECT_EQ(parameter.GetID(), file.m_FileObjects[0].Object); EXPECT_EQ(CK_STATESAVE_ALL, file.m_FileObjects[0].SaveFlags);
    EXPECT_EQ(a.GetID(), file.m_FileObjects[1].Object); EXPECT_EQ(b.GetID(), file.m_FileObjects[2].Object);
    EXPECT_EQ(0x1234u, file.m_FileObjects[1].SaveFlags); EXPECT_EQ(0x1234u, file.m_FileObjects[2].SaveFlags);
}
TEST_F(MaterialSerializationTest, PreSaveIgnoresInactiveParameterAndLetsFileFilterExcludedTextures) {
    CKParameter *createdParameter = static_cast<CKParameter *>(Context->CreateObject(CKCID_PARAMETER, nullptr, CK_OBJECTCREATION_NONAMECHECK));
    ASSERT_NE(nullptr, createdParameter); CKParameter &parameter = *createdParameter;
    RCKTexture a(Context), b(Context); SerializedMaterial m(Context); m.Parameter(&parameter);
    a.ModifyObjectFlags(CK_OBJECT_DYNAMIC, 0); b.ModifyObjectFlags(CK_OBJECT_NOTTOBESAVED, 0); m.SetTexture(0, &a); m.SetTexture(3, &b);
    CKFile file(Context); file.m_IndexByClassId.Resize(CKGetClassCount()); m.PreSave(&file, 0); EXPECT_EQ(0, file.m_FileObjects.Size());
}
