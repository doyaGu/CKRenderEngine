#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKLight.h"
#include "RCKRenderObject.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class SerializationLight : public RCKLight {
public:
    explicit SerializationLight(CKContext *context) : RCKLight(context) {}
    CKDWORD RawFlags() const { return m_Flags; }
    void SetRawFlags(CKDWORD flags) { m_Flags = flags; }
};

class DerivedSerializationLight : public SerializationLight {
public:
    explicit DerivedSerializationLight(CKContext *context) : SerializationLight(context) {}
    CK_CLASSID GetClassID() override { return CKCID_TARGETLIGHT; }
};

class DispatchSerializationLight : public SerializationLight {
public:
    explicit DispatchSerializationLight(CKContext *context) : SerializationLight(context) {}
    void Active(CKBOOL value) override { ActiveCalls++; LastActive = value; RCKLight::Active(value); }
    void SetSpecularFlag(CKBOOL value) override {
        SpecularCalls++; LastSpecular = value; RCKLight::SetSpecularFlag(value);
    }
    int ActiveCalls = 0;
    int SpecularCalls = 0;
    CKBOOL LastActive = 0;
    CKBOOL LastSpecular = 0;
};

class LightSerializationTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKLight, CKCID_3DENTITY);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static void Seed(SerializationLight &light) {
        light.SetType(VX_LIGHTSPOT); light.SetRawFlags(0x40000300);
        light.SetColor(VxColor(.25f, .5f, .75f, .125f));
        light.SetConstantAttenuation(2); light.SetLinearAttenuation(3);
        light.SetQuadraticAttenuation(5); light.SetRange(257);
        light.SetFallOff(1.25f); light.SetHotSpot(.625f); light.SetFallOffShape(7);
        light.SetLightPower(-3);
    }
    static void Current(CKStateChunk &chunk, CKDWORD typeFlags, CKDWORD color, bool spot) {
        chunk.WriteIdentifier(0x00400000); chunk.WriteDword(typeFlags); chunk.WriteDword(color);
        chunk.WriteFloat(2); chunk.WriteFloat(3); chunk.WriteFloat(5); chunk.WriteFloat(257);
        if (spot) { chunk.WriteFloat(1.25f); chunk.WriteFloat(.625f); chunk.WriteFloat(7); }
    }
    static void Legacy(CKStateChunk &chunk, CKDWORD type, int active, int specular) {
        chunk.WriteIdentifier(0x00400000); chunk.WriteDword(type);
        chunk.WriteFloat(.125f); chunk.WriteFloat(.375f); chunk.WriteFloat(.625f); chunk.WriteFloat(.875f);
        chunk.WriteInt(active); chunk.WriteInt(specular);
        chunk.WriteFloat(11); chunk.WriteFloat(13); chunk.WriteFloat(17); chunk.WriteFloat(509);
        chunk.WriteFloat(1.75f); chunk.WriteFloat(.875f); chunk.WriteFloat(19);
    }
    static CKERROR Load(SerializationLight &light, CKStateChunk &chunk, CKFile *file = nullptr) {
        chunk.CloseChunk(); return light.Load(&chunk, file);
    }
    static void ReadMain(CKStateChunk &chunk, CKDWORD typeFlags, bool spot) {
        chunk.StartRead(); ASSERT_TRUE(chunk.SeekIdentifier(0x00400000));
        EXPECT_EQ(typeFlags, chunk.ReadDword());
        // Literal expected ARGB: quarter/half/three-quarter channels truncate to 63/127/191;
        // Save forces opaque alpha regardless of the input alpha.
        EXPECT_EQ(0xff3f7fbfu, chunk.ReadDword());
        EXPECT_FLOAT_EQ(2, chunk.ReadFloat()); EXPECT_FLOAT_EQ(3, chunk.ReadFloat());
        EXPECT_FLOAT_EQ(5, chunk.ReadFloat()); EXPECT_FLOAT_EQ(257, chunk.ReadFloat());
        if (spot) { EXPECT_FLOAT_EQ(1.25f, chunk.ReadFloat()); EXPECT_FLOAT_EQ(.625f, chunk.ReadFloat());
                    EXPECT_FLOAT_EQ(7, chunk.ReadFloat()); }
    }
};
}

TEST_F(LightSerializationTest, NullLoadPreservesOwnStateAndObjectModificationFlag) {
    SerializationLight light(Context); Seed(light); light.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, light.Load(nullptr, nullptr));
    EXPECT_EQ(VX_LIGHTSPOT, light.GetType()); EXPECT_EQ(0x40000300u, light.RawFlags());
    EXPECT_FLOAT_EQ(-3, light.GetLightPower()); EXPECT_FLOAT_EQ(.125f, light.GetColor().a);
    EXPECT_TRUE(light.GetObjectFlags() & CK_OBJECT_UPTODATE);
}

TEST_F(LightSerializationTest, LiteralCurrentSpotDecodesPackedColorFlagsAndConeOrder) {
    SerializationLight light(Context); CKStateChunk chunk(CKCID_LIGHT, nullptr);
    chunk.StartWrite(); chunk.SetDataVersion(5); Current(chunk, 0x80000202, 0x80402010, true);
    chunk.WriteIdentifier(0x00800000); chunk.WriteFloat(-2.5f);
    ASSERT_EQ(CK_OK, Load(light, chunk)); EXPECT_EQ(VX_LIGHTSPOT, light.GetType());
    EXPECT_EQ(0x80000200u, light.RawFlags()); EXPECT_FALSE(light.GetActivity()); EXPECT_TRUE(light.GetSpecularFlag());
    EXPECT_FLOAT_EQ(64 / 255.f, light.GetColor().r); EXPECT_FLOAT_EQ(32 / 255.f, light.GetColor().g);
    EXPECT_FLOAT_EQ(16 / 255.f, light.GetColor().b); EXPECT_FLOAT_EQ(128 / 255.f, light.GetColor().a);
    EXPECT_FLOAT_EQ(2, light.GetConstantAttenuation()); EXPECT_FLOAT_EQ(3, light.GetLinearAttenuation());
    EXPECT_FLOAT_EQ(5, light.GetQuadraticAttenuation()); EXPECT_FLOAT_EQ(257, light.GetRange());
    EXPECT_FLOAT_EQ(1.25f, light.GetFallOff()); EXPECT_FLOAT_EQ(.625f, light.GetHotSpot());
    EXPECT_FLOAT_EQ(7, light.GetFallOffShape()); EXPECT_FLOAT_EQ(-2.5f, light.GetLightPower());
}

TEST_F(LightSerializationTest, CurrentPointAndDirectionalDoNotConsumeSpotParameters) {
    SerializationLight light(Context);
    const CKDWORD types[] = {1, 3};
    for (CKDWORD type : types) {
        Seed(light); CKStateChunk chunk(CKCID_LIGHT, nullptr); chunk.StartWrite(); chunk.SetDataVersion(8);
        Current(chunk, 0x100 | type, 0xff000000, false);
        chunk.WriteIdentifier(0x00800000); chunk.WriteFloat(13);
        ASSERT_EQ(CK_OK, Load(light, chunk)); EXPECT_EQ(type, static_cast<CKDWORD>(light.GetType()));
        EXPECT_FLOAT_EQ(1.25f, light.GetFallOff()); EXPECT_FLOAT_EQ(.625f, light.GetHotSpot());
        EXPECT_FLOAT_EQ(7, light.GetFallOffShape()); EXPECT_FLOAT_EQ(13, light.GetLightPower());
    }
}

TEST_F(LightSerializationTest, CurrentMissingPowerResetsToOneEvenWithoutMainSection) {
    SerializationLight light(Context);
    for (int main = 0; main < 2; ++main) {
        Seed(light); CKStateChunk chunk(CKCID_LIGHT, nullptr); chunk.StartWrite(); chunk.SetDataVersion(5);
        if (main) Current(chunk, 0x301, 0xff000000, false);
        else { chunk.WriteIdentifier(0x76543210); chunk.WriteInt(79); }
        ASSERT_EQ(CK_OK, Load(light, chunk)); EXPECT_FLOAT_EQ(1, light.GetLightPower());
        if (!main) { EXPECT_EQ(VX_LIGHTSPOT, light.GetType()); EXPECT_EQ(0x40000300u, light.RawFlags()); }
    }
}

TEST_F(LightSerializationTest, CurrentPowerOnlyChangesPowerAndAncestorModification) {
    SerializationLight light(Context); Seed(light); light.ModifyObjectFlags(CK_OBJECT_UPTODATE, 0);
    CKStateChunk chunk(CKCID_LIGHT, nullptr); chunk.StartWrite(); chunk.SetDataVersion(8);
    chunk.WriteIdentifier(0x00800000); chunk.WriteFloat(0);
    ASSERT_EQ(CK_OK, Load(light, chunk)); EXPECT_FLOAT_EQ(0, light.GetLightPower());
    EXPECT_EQ(VX_LIGHTSPOT, light.GetType()); EXPECT_EQ(0x40000300u, light.RawFlags());
    EXPECT_FLOAT_EQ(.125f, light.GetColor().a); EXPECT_FLOAT_EQ(257, light.GetRange());
    EXPECT_TRUE(light.GetObjectFlags() & CK_OBJECT_UPTODATE);
}

TEST_F(LightSerializationTest, CurrentVersionFiveSixAndEightShareThePackedWireContract) {
    SerializationLight light(Context); const int versions[] = {5, 6, 8};
    for (int version : versions) {
        CKStateChunk chunk(CKCID_LIGHT, nullptr); chunk.StartWrite(); chunk.SetDataVersion(version);
        Current(chunk, 0x101, 0xff123456, false); ASSERT_EQ(CK_OK, Load(light, chunk));
        EXPECT_EQ(VX_LIGHTPOINT, light.GetType()); EXPECT_EQ(0x100u, light.RawFlags());
        EXPECT_FLOAT_EQ(18 / 255.f, light.GetColor().r); EXPECT_FLOAT_EQ(257, light.GetRange());
    }
}

TEST_F(LightSerializationTest, InvalidCurrentTypesClampAfterLowByteExtractionAndPreserveHighFlags) {
    SerializationLight light(Context); const CKDWORD types[] = {0, 4, 255};
    for (CKDWORD type : types) {
        CKStateChunk chunk(CKCID_LIGHT, nullptr); chunk.StartWrite(); chunk.SetDataVersion(5);
        Current(chunk, 0xa5a50300 | type, 0xff000000, false); ASSERT_EQ(CK_OK, Load(light, chunk));
        EXPECT_EQ(VX_LIGHTPOINT, light.GetType()); EXPECT_EQ(0xa5a50300u, light.RawFlags());
    }
}

TEST_F(LightSerializationTest, LiteralLegacyReadsAllConesPreservesAlphaAndUnknownFlags) {
    SerializationLight light(Context); Seed(light);
    CKStateChunk chunk(CKCID_LIGHT, nullptr); chunk.StartWrite(); chunk.SetDataVersion(4);
    Legacy(chunk, 1, 0, -7); ASSERT_EQ(CK_OK, Load(light, chunk));
    EXPECT_EQ(VX_LIGHTPOINT, light.GetType()); EXPECT_EQ(0x40000200u, light.RawFlags());
    EXPECT_FLOAT_EQ(.125f, light.GetColor().r); EXPECT_FLOAT_EQ(.375f, light.GetColor().g);
    EXPECT_FLOAT_EQ(.625f, light.GetColor().b); EXPECT_FLOAT_EQ(.125f, light.GetColor().a);
    EXPECT_FLOAT_EQ(11, light.GetConstantAttenuation()); EXPECT_FLOAT_EQ(13, light.GetLinearAttenuation());
    EXPECT_FLOAT_EQ(17, light.GetQuadraticAttenuation()); EXPECT_FLOAT_EQ(509, light.GetRange());
    EXPECT_FLOAT_EQ(1.75f, light.GetFallOff()); EXPECT_FLOAT_EQ(.875f, light.GetHotSpot());
    EXPECT_FLOAT_EQ(19, light.GetFallOffShape()); EXPECT_FLOAT_EQ(1, light.GetLightPower());
}

TEST_F(LightSerializationTest, LegacyVersionZeroAndFourUseFloatRGBRatherThanPackedColor) {
    SerializationLight light(Context); const int versions[] = {0, 4};
    for (int version : versions) {
        CKStateChunk chunk(CKCID_LIGHT, nullptr); chunk.StartWrite(); chunk.SetDataVersion(version);
        Legacy(chunk, 3, -11, 0); ASSERT_EQ(CK_OK, Load(light, chunk));
        EXPECT_EQ(VX_LIGHTDIREC, light.GetType()); EXPECT_TRUE(light.GetActivity()); EXPECT_FALSE(light.GetSpecularFlag());
        EXPECT_FLOAT_EQ(.375f, light.GetColor().g); EXPECT_FLOAT_EQ(1.75f, light.GetFallOff());
    }
}

TEST_F(LightSerializationTest, LegacyFlagSettersDispatchVirtuallyWithTheOriginalBoolValues) {
    DispatchSerializationLight light(Context); CKStateChunk chunk(CKCID_LIGHT, nullptr);
    chunk.StartWrite(); chunk.SetDataVersion(4); Legacy(chunk, 2, -11, 7);
    ASSERT_EQ(CK_OK, Load(light, chunk)); EXPECT_EQ(1, light.ActiveCalls); EXPECT_EQ(1, light.SpecularCalls);
    EXPECT_EQ(-11, light.LastActive); EXPECT_EQ(7, light.LastSpecular);
    EXPECT_TRUE(light.GetActivity()); EXPECT_TRUE(light.GetSpecularFlag());
}

TEST_F(LightSerializationTest, CurrentFlagWireAssignsDirectlyWithoutVirtualSetterCalls) {
    DispatchSerializationLight light(Context); CKStateChunk chunk(CKCID_LIGHT, nullptr);
    chunk.StartWrite(); chunk.SetDataVersion(5); Current(chunk, 0x301, 0xff000000, false);
    ASSERT_EQ(CK_OK, Load(light, chunk)); EXPECT_EQ(0, light.ActiveCalls); EXPECT_EQ(0, light.SpecularCalls);
    EXPECT_EQ(0x300u, light.RawFlags());
}

TEST_F(LightSerializationTest, LegacyMissingMainPreservesPowerAndIgnoresModernPowerSection) {
    SerializationLight light(Context); Seed(light); CKStateChunk chunk(CKCID_LIGHT, nullptr);
    chunk.StartWrite(); chunk.SetDataVersion(4); chunk.WriteIdentifier(0x00800000); chunk.WriteFloat(13);
    ASSERT_EQ(CK_OK, Load(light, chunk)); EXPECT_FLOAT_EQ(-3, light.GetLightPower());
    EXPECT_EQ(VX_LIGHTSPOT, light.GetType()); EXPECT_FLOAT_EQ(.125f, light.GetColor().a);
}

TEST_F(LightSerializationTest, LegacyMainDefaultsPowerToOneAndIgnoresModernPowerSection) {
    SerializationLight light(Context); Seed(light); CKStateChunk chunk(CKCID_LIGHT, nullptr);
    chunk.StartWrite(); chunk.SetDataVersion(4); Legacy(chunk, 2, 1, 1);
    chunk.WriteIdentifier(0x00800000); chunk.WriteFloat(13);
    ASSERT_EQ(CK_OK, Load(light, chunk)); EXPECT_FLOAT_EQ(1, light.GetLightPower());
}

TEST_F(LightSerializationTest, InvalidLegacySignedAndLargeTypesClampToPoint) {
    SerializationLight light(Context); const CKDWORD types[] = {0, 4, 0xffffffff, 0x80000000};
    for (CKDWORD type : types) {
        CKStateChunk chunk(CKCID_LIGHT, nullptr); chunk.StartWrite(); chunk.SetDataVersion(4);
        Legacy(chunk, type, 1, 0); ASSERT_EQ(CK_OK, Load(light, chunk)); EXPECT_EQ(VX_LIGHTPOINT, light.GetType());
    }
}

TEST_F(LightSerializationTest, LiteralAncestorWorldMatrixLoadsAlongsideCurrentLightPayload) {
    SerializationLight light(Context); CKFile file(Context); CKStateChunk chunk(CKCID_LIGHT, nullptr);
    chunk.StartWrite(); chunk.SetDataVersion(6);
    chunk.WriteIdentifier(CK_STATESAVE_3DENTITYNDATA); chunk.WriteDword(0); chunk.WriteDword(VX_MOVEABLE_VISIBLE);
    chunk.WriteVector(VxVector(1, 0, 0)); chunk.WriteVector(VxVector(0, 1, 0));
    chunk.WriteVector(VxVector(0, 0, 1)); chunk.WriteVector(VxVector(7, 11, 13));
    Current(chunk, 0x101, 0xff000000, false); ASSERT_EQ(CK_OK, Load(light, chunk, &file));
    EXPECT_FLOAT_EQ(7, light.GetWorldMatrix()[3][0]); EXPECT_FLOAT_EQ(11, light.GetWorldMatrix()[3][1]);
    EXPECT_FLOAT_EQ(13, light.GetWorldMatrix()[3][2]); EXPECT_FLOAT_EQ(257, light.GetRange());
}

TEST_F(LightSerializationTest, FilelessZeroAndUnrelatedMasksReturnAncestorWithoutLightSection) {
    SerializationLight light(Context); const CKDWORD masks[] = {0, 1, 0x10000000};
    for (CKDWORD mask : masks) {
        CKStateChunk *chunk = light.Save(nullptr, mask); ASSERT_NE(nullptr, chunk);
        EXPECT_NE(CKCID_LIGHT, chunk->GetChunkClassID()); chunk->StartRead();
        EXPECT_FALSE(chunk->SeekIdentifier(0x00400000)); DeleteCKStateChunk(chunk);
    }
}

TEST_F(LightSerializationTest, EveryLightOnlyMaskBitWritesCompleteMainAndPowerPayload) {
    SerializationLight light(Context); Seed(light);
    const CKDWORD masks[] = {0x00400000, 0x00800000, 0x01000000, 0x02000000, 0x04000000, 0x08000000};
    for (CKDWORD mask : masks) {
        CKStateChunk *chunk = light.Save(nullptr, mask); ASSERT_NE(nullptr, chunk);
        EXPECT_EQ(CKCID_LIGHT, chunk->GetChunkClassID()); EXPECT_EQ(-1, chunk->GetCurrentPos());
        ReadMain(*chunk, 0x40000302, true); ASSERT_TRUE(chunk->SeekIdentifier(0x00800000));
        EXPECT_FLOAT_EQ(-3, chunk->ReadFloat()); DeleteCKStateChunk(chunk);
    }
}

TEST_F(LightSerializationTest, SavedPointAndDirectionalPayloadExcludeConeWords) {
    SerializationLight light(Context); const VXLIGHT_TYPE types[] = {VX_LIGHTPOINT, VX_LIGHTDIREC};
    for (VXLIGHT_TYPE type : types) {
        Seed(light); light.SetType(type); light.SetLightPower(1);
        CKStateChunk *chunk = light.Save(nullptr, 0x00400000); ASSERT_NE(nullptr, chunk);
        ReadMain(*chunk, 0x40000300 | type, false);
        EXPECT_EQ(chunk->GetDataSize(), chunk->GetCurrentPos() * 4);
        EXPECT_FALSE(chunk->SeekIdentifier(0x00800000)); DeleteCKStateChunk(chunk);
    }
}

TEST_F(LightSerializationTest, SaveDefaultPowerOmitsOptionalSectionAndDoesNotMutateLight) {
    SerializationLight light(Context); Seed(light); light.SetLightPower(1);
    CKStateChunk *chunk = light.Save(nullptr, 0x00400000); ASSERT_NE(nullptr, chunk);
    ReadMain(*chunk, 0x40000302, true); EXPECT_FALSE(chunk->SeekIdentifier(0x00800000));
    EXPECT_FLOAT_EQ(.125f, light.GetColor().a); EXPECT_FLOAT_EQ(.25f, light.GetColor().r);
    EXPECT_EQ(0x40000300u, light.RawFlags()); EXPECT_FLOAT_EQ(1, light.GetLightPower()); DeleteCKStateChunk(chunk);
}

TEST_F(LightSerializationTest, SaveZeroNegativeAndFractionalPowerWritesOptionalLiteralFloat) {
    SerializationLight light(Context); const float powers[] = {0, -2.5f, .5f};
    for (float power : powers) {
        Seed(light); light.SetLightPower(power); CKStateChunk *chunk = light.Save(nullptr, 0x00400000);
        ASSERT_NE(nullptr, chunk); chunk->StartRead(); ASSERT_TRUE(chunk->SeekIdentifier(0x00800000));
        EXPECT_FLOAT_EQ(power, chunk->ReadFloat()); EXPECT_FLOAT_EQ(power, light.GetLightPower()); DeleteCKStateChunk(chunk);
    }
}

TEST_F(LightSerializationTest, FileSaveZeroMaskIncludesLightAndEntityPayloads) {
    SerializationLight light(Context); Seed(light); CKFile file(Context);
    CKStateChunk *chunk = light.Save(&file, 0); ASSERT_NE(nullptr, chunk);
    EXPECT_EQ(CKCID_LIGHT, chunk->GetChunkClassID()); EXPECT_EQ(-1, chunk->GetCurrentPos());
    ReadMain(*chunk, 0x40000302, true); EXPECT_TRUE(chunk->SeekIdentifier(CK_STATESAVE_3DENTITYNDATA));
    ASSERT_TRUE(chunk->SeekIdentifier(0x00800000)); EXPECT_FLOAT_EQ(-3, chunk->ReadFloat()); DeleteCKStateChunk(chunk);
}

TEST_F(LightSerializationTest, DerivedSaveLeavesChunkWritableForSubclassAppend) {
    DerivedSerializationLight light(Context); Seed(light);
    CKStateChunk *chunk = light.Save(nullptr, 0x00400000); ASSERT_NE(nullptr, chunk);
    EXPECT_EQ(CKCID_LIGHT, chunk->GetChunkClassID()); EXPECT_GE(chunk->GetCurrentPos(), 0);
    chunk->WriteIdentifier(0x76543210); chunk->WriteInt(79); chunk->CloseChunk();
    ReadMain(*chunk, 0x40000302, true); ASSERT_TRUE(chunk->SeekIdentifier(0x76543210));
    EXPECT_EQ(79, chunk->ReadInt()); DeleteCKStateChunk(chunk);
}
