#include <gtest/gtest.h>
#include "CKAll.h"
#include "CKDependencies.h"
#include "RCKMaterial.h"
#include "RCKTexture.h"
#include "RCKMesh.h"
#include "RCKRenderManager.h"

extern void SetProcessorSpecific_FunctionsPtr();
extern CKBOOL g_UpdateTransparency;

namespace {
class DependencyMaterial : public RCKMaterial {
public:
    explicit DependencyMaterial(CKContext *context) : RCKMaterial(context) {}
    void Parameter(CKParameter *parameter) { m_EffectParameter = parameter; }
    const CKMaterialData &Data() const { return m_MaterialData; }
};
class MaterialDependencyMap : public CKDependenciesContext {
public:
    explicit MaterialDependencyMap(CKContext *context) : CKDependenciesContext(context) {}
    using CKDependenciesContext::Clear;
    void Map(CK_ID from, CK_ID to) { m_MapID.Insert(from, to); }
};
class MaterialDependenciesTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp()); CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT); CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT); CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
    static CKTexture *Texture(RCKTexture &texture) { return reinterpret_cast<CKTexture *>(&texture); }
    static void Options(CKDependencies &options, CKDWORD material = 0, CKDWORD beobject = 0) {
        options.Resize(80); for (int i = 0; i < 80; ++i) options[i] = 0; options.m_Flags = CK_DEPENDENCIES_CUSTOM;
        options[CKCID_MATERIAL] = material; options[CKCID_BEOBJECT] = beobject;
    }
    static int Callback(CKRenderContext *, CKMaterial *, void *) { return 1; }
    static void Triangle(RCKMesh &mesh, RCKMaterial &material) {
        ASSERT_TRUE(mesh.SetVertexCount(3)); ASSERT_TRUE(mesh.SetFaceCount(1));
        VxVector positions[] = {VxVector(0, 0, 0), VxVector(1, 0, 0), VxVector(0, 1, 0)};
        for (int i = 0; i < 3; ++i) mesh.SetVertexPosition(i, &positions[i]);
        mesh.SetFaceVertexIndex(0, 0, 1, 2); mesh.SetFaceMaterial(0, reinterpret_cast<CKMaterial *>(&material));
    }
    static void PackedClassification(CKStateChunk &chunk, CKBOOL blended) {
        // Independent native >=5 wire payload, not a Save/Load self roundtrip.
        chunk.StartWrite(); chunk.SetDataVersion(10); chunk.WriteIdentifier(0x1000);
        chunk.WriteDword(0xffb2b2b2); chunk.WriteDword(0xff4c4c4c); chunk.WriteDword(0xff7f7f7f); chunk.WriteDword(0xff000000);
        chunk.WriteFloat(0); chunk.WriteInt(0); chunk.WriteDword(0);
        chunk.WriteDword(blended ? 0x13262224 : 0x13212224); chunk.WriteDword(blended ? 0x0008040e : 0x00080406); chunk.CloseChunk();
    }
};
}

TEST_F(MaterialDependenciesTest, PrepareTextureBitCollectsAllDistinctTexturesAndAlwaysCollectsEffectParameter) {
    DependencyMaterial material(Context); RCKTexture first(Context), second(Context), unrelated(Context); CKParameter *parameterObject = static_cast<CKParameter *>(Context->CreateObject(CKCID_PARAMETER)); ASSERT_NE(nullptr, parameterObject); CKParameter &parameter = *parameterObject;
    material.SetTexture(0, Texture(first)); material.SetTexture(2, Texture(second)); material.SetTexture(3, Texture(first)); material.Parameter(&parameter);
    CKDependencies options; Options(options, 1); MaterialDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, material.PrepareDependencies(dependencies)); EXPECT_TRUE(dependencies.IsDependenciesHere(material.GetID()));
    EXPECT_TRUE(dependencies.IsDependenciesHere(first.GetID())); EXPECT_TRUE(dependencies.IsDependenciesHere(second.GetID()));
    EXPECT_TRUE(dependencies.IsDependenciesHere(parameter.GetID())); EXPECT_FALSE(dependencies.IsDependenciesHere(unrelated.GetID()));
    // This is the native preparation stack, not a list restricted to dynamic objects.
    // Texture's inherited CKBeObject hook does not finish CID_TEXTURE; Material
    // Finish pops the latest entry. The complete object dependency map remains valid.
    EXPECT_FALSE(material.IsDynamic()); EXPECT_FALSE(first.IsDynamic()); EXPECT_FALSE(second.IsDynamic()); EXPECT_FALSE(parameter.IsDynamic());
    ASSERT_EQ(2, dependencies.m_DynamicObjects.Size()); EXPECT_EQ(&material, dependencies.m_DynamicObjects[0]); EXPECT_EQ(&first, dependencies.m_DynamicObjects[1]);
    ::testing::Test::RecordProperty("PreparationStack0ID", static_cast<int>(dependencies.m_DynamicObjects[0]->GetID()));
    ::testing::Test::RecordProperty("PreparationStack1ID", static_cast<int>(dependencies.m_DynamicObjects[1]->GetID()));
    EXPECT_EQ(CKERR_ALREADYPRESENT, material.PrepareDependencies(dependencies)); ASSERT_EQ(2, dependencies.m_DynamicObjects.Size());
    EXPECT_EQ(&material, dependencies.m_DynamicObjects[0]); EXPECT_EQ(&first, dependencies.m_DynamicObjects[1]);
    dependencies.StopDependencies(); ASSERT_EQ(2, dependencies.m_DynamicObjects.Size());
    EXPECT_EQ(&material, dependencies.m_DynamicObjects[0]); EXPECT_EQ(&first, dependencies.m_DynamicObjects[1]);
    EXPECT_TRUE(dependencies.IsDependenciesHere(material.GetID())); EXPECT_TRUE(dependencies.IsDependenciesHere(first.GetID()));
    EXPECT_TRUE(dependencies.IsDependenciesHere(second.GetID())); EXPECT_TRUE(dependencies.IsDependenciesHere(parameter.GetID()));
    dependencies.Clear(); EXPECT_TRUE(dependencies.m_DynamicObjects.IsEmpty());
    EXPECT_FALSE(dependencies.IsDependenciesHere(material.GetID())); EXPECT_FALSE(dependencies.IsDependenciesHere(first.GetID()));
    EXPECT_FALSE(dependencies.IsDependenciesHere(second.GetID())); EXPECT_FALSE(dependencies.IsDependenciesHere(parameter.GetID()));
    EXPECT_EQ(&material, Context->GetObject(material.GetID())); EXPECT_EQ(&first, Context->GetObject(first.GetID()));
    EXPECT_EQ(&second, Context->GetObject(second.GetID())); EXPECT_EQ(&parameter, Context->GetObject(parameter.GetID()));
}
TEST_F(MaterialDependenciesTest, PrepareZeroTextureBitStillCollectsEffectButDoesNotCollectTexture) {
    DependencyMaterial material(Context); RCKTexture texture(Context); CKParameter *parameterObject = static_cast<CKParameter *>(Context->CreateObject(CKCID_PARAMETER)); ASSERT_NE(nullptr, parameterObject); CKParameter &parameter = *parameterObject; material.SetTexture(3, Texture(texture)); material.Parameter(&parameter);
    CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, material.PrepareDependencies(dependencies)); EXPECT_TRUE(dependencies.IsDependenciesHere(parameter.GetID()));
    EXPECT_TRUE(dependencies.IsDependenciesHere(material.GetID())); EXPECT_FALSE(dependencies.IsDependenciesHere(texture.GetID())); dependencies.StopDependencies();
}
TEST_F(MaterialDependenciesTest, PrepareEmptyReferencesFinishesMaterialAndDoesNotAddNullDependencies) {
    DependencyMaterial material(Context); CKDependencies options; Options(options, 1); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, material.PrepareDependencies(dependencies)); EXPECT_TRUE(dependencies.IsDependenciesHere(material.GetID()));
    EXPECT_FALSE(dependencies.IsDependenciesHere(0)); EXPECT_TRUE(dependencies.m_DynamicObjects.IsEmpty()); dependencies.StopDependencies();
}
TEST_F(MaterialDependenciesTest, PreparePropagatesMaterialSaveExclusionBeforeOwnReferences) {
    DependencyMaterial material(Context); RCKTexture texture(Context); CKParameter *parameterObject = static_cast<CKParameter *>(Context->CreateObject(CKCID_PARAMETER)); ASSERT_NE(nullptr, parameterObject); CKParameter &parameter = *parameterObject; material.SetTexture(0, Texture(texture)); material.Parameter(&parameter);
    material.ModifyObjectFlags(CK_OBJECT_NOTTOBESAVED, 0); CKDependencies options; Options(options, 1); CKDependenciesContext dependencies(Context);
    dependencies.SetOperationMode(CK_DEPENDENCIES_SAVE); dependencies.StartDependencies(&options);
    EXPECT_EQ(CKERR_INCOMPATIBLEPARAMETERS, material.PrepareDependencies(dependencies)); EXPECT_FALSE(dependencies.IsDependenciesHere(material.GetID()));
    EXPECT_FALSE(dependencies.IsDependenciesHere(texture.GetID())); EXPECT_FALSE(dependencies.IsDependenciesHere(parameter.GetID())); dependencies.StopDependencies();
}
TEST_F(MaterialDependenciesTest, PrepareNativeWrapperDoesNotPropagateReferencedTextureSaveExclusion) {
    DependencyMaterial material(Context); RCKTexture texture(Context); CKParameter *parameterObject = static_cast<CKParameter *>(Context->CreateObject(CKCID_PARAMETER)); ASSERT_NE(nullptr, parameterObject); CKParameter &parameter = *parameterObject; material.SetTexture(1, Texture(texture)); material.Parameter(&parameter);
    texture.ModifyObjectFlags(CK_OBJECT_NOTTOBESAVED, 0); CKDependencies options; Options(options, 1); CKDependenciesContext dependencies(Context);
    dependencies.SetOperationMode(CK_DEPENDENCIES_SAVE); dependencies.StartDependencies(&options);
    EXPECT_EQ(CK_OK, material.PrepareDependencies(dependencies)); EXPECT_TRUE(dependencies.IsDependenciesHere(material.GetID()));
    EXPECT_FALSE(dependencies.IsDependenciesHere(texture.GetID())); EXPECT_TRUE(dependencies.IsDependenciesHere(parameter.GetID())); dependencies.StopDependencies();
}
TEST_F(MaterialDependenciesTest, RemapTextureBitUpdatesAllSlotsAndAlwaysUpdatesEffectWithoutChangingColors) {
    DependencyMaterial material(Context); RCKTexture oldTexture(Context), newTexture(Context); CKParameter *oldParameterObject = static_cast<CKParameter *>(Context->CreateObject(CKCID_PARAMETER)); ASSERT_NE(nullptr, oldParameterObject); CKParameter &oldParameter = *oldParameterObject; CKParameter *newParameterObject = static_cast<CKParameter *>(Context->CreateObject(CKCID_PARAMETER)); ASSERT_NE(nullptr, newParameterObject); CKParameter &newParameter = *newParameterObject;
    material.SetTexture(0, Texture(oldTexture)); material.SetTexture(3, Texture(oldTexture)); material.Parameter(&oldParameter); material.SetDiffuse(VxColor(.2f, .3f, .4f, .5f));
    CKDependencies options; Options(options, 1); MaterialDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    dependencies.Map(oldTexture.GetID(), newTexture.GetID()); dependencies.Map(oldParameter.GetID(), newParameter.GetID());
    ASSERT_EQ(CK_OK, material.RemapDependencies(dependencies)); EXPECT_EQ(Texture(newTexture), material.GetTexture(0)); EXPECT_EQ(Texture(newTexture), material.GetTexture(3));
    EXPECT_EQ(nullptr, material.GetTexture(1)); EXPECT_EQ(nullptr, material.GetTexture(2)); EXPECT_EQ(&newParameter, material.GetEffectParameter());
    EXPECT_FLOAT_EQ(.2f, material.GetDiffuse().r); EXPECT_FLOAT_EQ(.5f, material.GetDiffuse().a); dependencies.StopDependencies();
}
TEST_F(MaterialDependenciesTest, RemapDisabledTextureBitKeepsTextureIdentityButStillRemapsEffect) {
    DependencyMaterial material(Context); RCKTexture oldTexture(Context), newTexture(Context); CKParameter *oldParameterObject = static_cast<CKParameter *>(Context->CreateObject(CKCID_PARAMETER)); ASSERT_NE(nullptr, oldParameterObject); CKParameter &oldParameter = *oldParameterObject; CKParameter *newParameterObject = static_cast<CKParameter *>(Context->CreateObject(CKCID_PARAMETER)); ASSERT_NE(nullptr, newParameterObject); CKParameter &newParameter = *newParameterObject;
    material.SetTexture(2, Texture(oldTexture)); material.Parameter(&oldParameter); CKDependencies options; Options(options); MaterialDependencyMap dependencies(Context);
    dependencies.StartDependencies(&options); dependencies.Map(oldTexture.GetID(), newTexture.GetID()); dependencies.Map(oldParameter.GetID(), newParameter.GetID());
    ASSERT_EQ(CK_OK, material.RemapDependencies(dependencies)); EXPECT_EQ(Texture(oldTexture), material.GetTexture(2)); EXPECT_EQ(&newParameter, material.GetEffectParameter()); dependencies.StopDependencies();
}
TEST_F(MaterialDependenciesTest, RemapExplicitZeroClearsReferencesAndUnmappedReferencesRetainIdentity) {
    DependencyMaterial material(Context); RCKTexture first(Context), second(Context); CKParameter *parameterObject = static_cast<CKParameter *>(Context->CreateObject(CKCID_PARAMETER)); ASSERT_NE(nullptr, parameterObject); CKParameter &parameter = *parameterObject;
    material.SetTexture(0, Texture(first)); material.SetTexture(1, Texture(second)); material.Parameter(&parameter); CKDependencies options; Options(options, 1);
    MaterialDependencyMap dependencies(Context); dependencies.StartDependencies(&options); dependencies.Map(first.GetID(), 0); dependencies.Map(parameter.GetID(), 0);
    ASSERT_EQ(CK_OK, material.RemapDependencies(dependencies)); EXPECT_EQ(nullptr, material.GetTexture(0)); EXPECT_EQ(Texture(second), material.GetTexture(1));
    EXPECT_EQ(nullptr, material.GetEffectParameter()); dependencies.StopDependencies();
}
TEST_F(MaterialDependenciesTest, CopyAlwaysTransfersRawOwnPropertiesBeforeExplicitRemappingAndKeepsCallback) {
    DependencyMaterial source(Context), destination(Context); RCKTexture oldTexture(Context), newTexture(Context); CKParameter *oldParameterObject = static_cast<CKParameter *>(Context->CreateObject(CKCID_PARAMETER)); ASSERT_NE(nullptr, oldParameterObject); CKParameter &oldParameter = *oldParameterObject; CKParameter *newParameterObject = static_cast<CKParameter *>(Context->CreateObject(CKCID_PARAMETER)); ASSERT_NE(nullptr, newParameterObject); CKParameter &newParameter = *newParameterObject;
    source.SetTexture(3, Texture(oldTexture)); source.Parameter(&oldParameter); source.SetDiffuse(VxColor(.2f, .3f, .4f, .5f)); source.SetAmbient(VxColor(.1f, .4f, .7f, .8f));
    source.SetSpecular(VxColor(.3f, .5f, .7f, .9f)); source.SetPower(8); source.SetEmissive(VxColor(.6f, .4f, .2f, .1f));
    source.SetTextureMinMode(VXTEXTUREFILTER_NEAREST); source.SetTextureMagMode(VXTEXTUREFILTER_NEAREST); source.SetTextureBlendMode(VXTEXTUREBLEND_DECAL);
    source.SetSourceBlend(VXBLEND_SRCALPHA); source.SetDestBlend(VXBLEND_INVSRCALPHA); source.SetTextureAddressMode(VXTEXTURE_ADDRESSCLAMP); source.SetTextureBorderColor(0xaabbccddu);
    source.SetTwoSided(TRUE); source.EnableZWrite(FALSE); source.EnablePerspectiveCorrection(FALSE); source.EnableAlphaBlend(TRUE); source.EnableAlphaTest(TRUE);
    source.SetZFunc(VXCMP_GREATER); source.SetAlphaFunc(VXCMP_NOTEQUAL); source.SetAlphaRef(173); source.SetFillMode(VXFILL_WIREFRAME); source.SetShadeMode(VXSHADE_FLAT);
    int sourceArgument = 11, destinationArgument = 17; source.SetCallback(Callback, &sourceArgument); destination.SetCallback(Callback, &destinationArgument);
    CKDependencies options; Options(options); MaterialDependencyMap dependencies(Context); dependencies.StartDependencies(&options);
    dependencies.Map(oldTexture.GetID(), newTexture.GetID()); dependencies.Map(oldParameter.GetID(), newParameter.GetID()); ASSERT_EQ(CK_OK, destination.Copy(source, dependencies));
    EXPECT_EQ(Texture(oldTexture), destination.GetTexture(3)); EXPECT_EQ(&oldParameter, destination.GetEffectParameter());
    EXPECT_FLOAT_EQ(.2f, destination.GetDiffuse().r); EXPECT_FLOAT_EQ(.5f, destination.GetDiffuse().a); EXPECT_FLOAT_EQ(.4f, destination.GetAmbient().g);
    EXPECT_FLOAT_EQ(.7f, destination.GetSpecular().b); EXPECT_FLOAT_EQ(8, destination.GetPower()); EXPECT_FLOAT_EQ(.6f, destination.GetEmissive().r);
    EXPECT_EQ(source.GetTextureMinMode(), destination.GetTextureMinMode()); EXPECT_EQ(source.GetTextureMagMode(), destination.GetTextureMagMode());
    EXPECT_EQ(source.GetTextureBlendMode(), destination.GetTextureBlendMode()); EXPECT_EQ(source.GetSourceBlend(), destination.GetSourceBlend()); EXPECT_EQ(source.GetDestBlend(), destination.GetDestBlend());
    EXPECT_EQ(source.GetTextureAddressMode(), destination.GetTextureAddressMode()); EXPECT_EQ(0xaabbccddu, destination.GetTextureBorderColor()); EXPECT_TRUE(destination.IsTwoSided());
    EXPECT_FALSE(destination.ZWriteEnabled()); EXPECT_FALSE(destination.PerspectiveCorrectionEnabled()); EXPECT_TRUE(destination.AlphaBlendEnabled()); EXPECT_TRUE(destination.AlphaTestEnabled());
    EXPECT_EQ(VXCMP_GREATER, destination.GetZFunc()); EXPECT_EQ(VXCMP_NOTEQUAL, destination.GetAlphaFunc()); EXPECT_EQ(173, destination.GetAlphaRef());
    EXPECT_EQ(VXFILL_WIREFRAME, destination.GetFillMode()); EXPECT_EQ(VXSHADE_FLAT, destination.GetShadeMode()); void *argument = nullptr;
    EXPECT_EQ(Callback, destination.GetCallback(&argument)); EXPECT_EQ(&destinationArgument, argument); EXPECT_EQ(nullptr, destination.GetSprite3DBatch());
    ASSERT_EQ(CK_OK, destination.RemapDependencies(dependencies)); EXPECT_EQ(Texture(oldTexture), destination.GetTexture(3)); EXPECT_EQ(&newParameter, destination.GetEffectParameter()); dependencies.StopDependencies();
}
TEST_F(MaterialDependenciesTest, CopyEmptySourceClearsRawTextureAndParameterReferences) {
    DependencyMaterial source(Context), destination(Context); RCKTexture texture(Context); CKParameter *parameterObject = static_cast<CKParameter *>(Context->CreateObject(CKCID_PARAMETER)); ASSERT_NE(nullptr, parameterObject); CKParameter &parameter = *parameterObject;
    destination.SetTexture(0, Texture(texture)); destination.SetTexture(3, Texture(texture)); destination.Parameter(&parameter); destination.SetAlphaRef(213);
    CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); for (int i = 0; i < 4; ++i) EXPECT_EQ(nullptr, destination.GetTexture(i));
    EXPECT_EQ(nullptr, destination.GetEffectParameter()); EXPECT_EQ(0, destination.GetAlphaRef()); EXPECT_EQ(&texture, Context->GetObject(texture.GetID()));
    EXPECT_EQ(&parameter, Context->GetObject(parameter.GetID())); dependencies.StopDependencies();
}
TEST_F(MaterialDependenciesTest, SelfCopyPreservesActualBehaviorOwnerAndScriptArray) {
    DependencyMaterial material(Context); CKBehavior *behaviorObject = static_cast<CKBehavior *>(Context->CreateObject(CKCID_BEHAVIOR)); ASSERT_NE(nullptr, behaviorObject); CKBehavior &behavior = *behaviorObject; ASSERT_EQ(CK_OK, material.AddScript(&behavior)); ASSERT_EQ(1, material.GetScriptCount());
    ASSERT_EQ(&material, behavior.GetOwner()); material.SetDiffuse(VxColor(.2f, .3f, .4f, .5f)); CKDependencies options; Options(options, 1, 1);
    CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options); ASSERT_EQ(CK_OK, material.Copy(material, dependencies));
    EXPECT_EQ(1, material.GetScriptCount()); EXPECT_EQ(&behavior, material.GetScript(0)); EXPECT_EQ(&material, behavior.GetOwner());
    EXPECT_FLOAT_EQ(.2f, material.GetDiffuse().r); EXPECT_FLOAT_EQ(.5f, material.GetDiffuse().a); dependencies.StopDependencies(); material.RemoveAllScripts();
}
TEST_F(MaterialDependenciesTest, ContextCopyTextureBitClonesSharedTextureOnceAndAlwaysClonesEffectParameter) {
    RCKMaterial *source = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL)); RCKTexture *texture = static_cast<RCKTexture *>(Context->CreateObject(CKCID_TEXTURE));
    CKParameter *parameter = static_cast<CKParameter *>(Context->CreateObject(CKCID_PARAMETER)); ASSERT_NE(nullptr, source); ASSERT_NE(nullptr, texture); ASSERT_NE(nullptr, parameter);
    source->SetTexture(0, reinterpret_cast<CKTexture *>(texture)); source->SetTexture(3, reinterpret_cast<CKTexture *>(texture));
    parameter->SetGUID(CKPGUID_INT); int value = 73; ASSERT_EQ(CK_OK, parameter->SetValue(&value, sizeof(value)));
    // Assign the actual effect reference without relying on renderer-specific effect catalogs.
    class Access : public RCKMaterial { public: static void Set(RCKMaterial &m, CKParameter *p) { m.*(&Access::m_EffectParameter) = p; } }; Access::Set(*source, parameter);
    CKDependencies options; Options(options, 1); RCKMaterial *copy = static_cast<RCKMaterial *>(Context->CopyObject(source, &options)); ASSERT_NE(nullptr, copy); EXPECT_NE(source, copy);
    ASSERT_NE(nullptr, copy->GetTexture(0)); EXPECT_NE(reinterpret_cast<CKTexture *>(texture), copy->GetTexture(0)); EXPECT_EQ(copy->GetTexture(0), copy->GetTexture(3));
    ASSERT_NE(nullptr, copy->GetEffectParameter()); EXPECT_NE(parameter, copy->GetEffectParameter()); EXPECT_EQ(CKPGUID_INT, copy->GetEffectParameter()->GetGUID());
    int result = 0; ASSERT_EQ(CK_OK, copy->GetEffectParameter()->GetValue(&result)); EXPECT_EQ(73, result);
    EXPECT_EQ(reinterpret_cast<CKTexture *>(texture), source->GetTexture(0)); EXPECT_EQ(parameter, source->GetEffectParameter());
}
TEST_F(MaterialDependenciesTest, ContextCopyWithoutTextureBitKeepsBorrowedTextureButClonesEffectParameter) {
    RCKMaterial *source = static_cast<RCKMaterial *>(Context->CreateObject(CKCID_MATERIAL)); RCKTexture *texture = static_cast<RCKTexture *>(Context->CreateObject(CKCID_TEXTURE));
    CKParameter *parameter = static_cast<CKParameter *>(Context->CreateObject(CKCID_PARAMETER)); ASSERT_NE(nullptr, source); ASSERT_NE(nullptr, texture); ASSERT_NE(nullptr, parameter);
    parameter->SetGUID(CKPGUID_INT); int value = 113; ASSERT_EQ(CKPGUID_INT, parameter->GetGUID());
    ASSERT_EQ(static_cast<int>(sizeof(value)), parameter->GetDataSize()); ASSERT_EQ(CK_OK, parameter->SetValue(&value, sizeof(value)));
    source->SetTexture(2, reinterpret_cast<CKTexture *>(texture)); class Access : public RCKMaterial { public: static void Set(RCKMaterial &m, CKParameter *p) { m.*(&Access::m_EffectParameter) = p; } }; Access::Set(*source, parameter);
    CKDependencies options; Options(options); RCKMaterial *copy = static_cast<RCKMaterial *>(Context->CopyObject(source, &options)); ASSERT_NE(nullptr, copy);
    EXPECT_EQ(reinterpret_cast<CKTexture *>(texture), copy->GetTexture(2)); ASSERT_NE(nullptr, copy->GetEffectParameter()); EXPECT_NE(parameter, copy->GetEffectParameter());
    EXPECT_NE(parameter->GetID(), copy->GetEffectParameter()->GetID()); EXPECT_EQ(CKPGUID_INT, copy->GetEffectParameter()->GetGUID());
    int copiedValue = 0; ASSERT_EQ(CK_OK, copy->GetEffectParameter()->GetValue(&copiedValue)); EXPECT_EQ(113, copiedValue);
    EXPECT_EQ(parameter, source->GetEffectParameter());
}
TEST_F(MaterialDependenciesTest, CopyOpaqueMaterialToBlendedInvalidatesActualMeshTransparencyCache) {
    RCKMaterial source(Context), destination(Context); RCKMesh mesh(Context);
    Triangle(mesh, destination); g_UpdateTransparency = FALSE; ASSERT_FALSE(mesh.IsTransparent());
    source.EnableAlphaBlend(TRUE); source.SetDestBlend(VXBLEND_INVSRCALPHA); g_UpdateTransparency = FALSE;
    CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); EXPECT_TRUE(g_UpdateTransparency); EXPECT_TRUE(mesh.IsTransparent());
    dependencies.StopDependencies(); g_UpdateTransparency = FALSE;
}
TEST_F(MaterialDependenciesTest, CopyBlendedMaterialToOpaqueInvalidatesActualMeshTransparencyCache) {
    RCKMaterial source(Context), destination(Context); RCKMesh mesh(Context); destination.EnableAlphaBlend(TRUE); destination.SetDestBlend(VXBLEND_INVSRCALPHA);
    Triangle(mesh, destination); g_UpdateTransparency = FALSE; ASSERT_TRUE(mesh.IsTransparent());
    CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); EXPECT_TRUE(g_UpdateTransparency); EXPECT_FALSE(mesh.IsTransparent());
    dependencies.StopDependencies(); g_UpdateTransparency = FALSE;
}
TEST_F(MaterialDependenciesTest, CopySameTransparencyClassificationDoesNotDirtyActualMeshCache) {
    RCKMaterial source(Context), destination(Context); RCKMesh mesh(Context);
    Triangle(mesh, destination); g_UpdateTransparency = FALSE; ASSERT_FALSE(mesh.IsTransparent());
    source.SetDiffuse(VxColor(.2f, .3f, .4f, .5f)); CKDependencies options; Options(options); CKDependenciesContext dependencies(Context); dependencies.StartDependencies(&options);
    ASSERT_EQ(CK_OK, destination.Copy(source, dependencies)); EXPECT_FALSE(g_UpdateTransparency); EXPECT_FALSE(mesh.IsTransparent());
    source.EnableAlphaBlend(TRUE); source.SetDestBlend(VXBLEND_INVSRCALPHA); destination.EnableAlphaBlend(TRUE); destination.SetDestBlend(VXBLEND_SRCALPHA);
    ASSERT_TRUE(mesh.IsTransparent()); g_UpdateTransparency = FALSE; ASSERT_EQ(CK_OK, destination.Copy(source, dependencies));
    EXPECT_FALSE(g_UpdateTransparency); EXPECT_TRUE(mesh.IsTransparent()); dependencies.StopDependencies(); g_UpdateTransparency = FALSE;
}
TEST_F(MaterialDependenciesTest, PackedLoadOpaqueMaterialToBlendedInvalidatesActualMeshTransparencyCache) {
    RCKMaterial material(Context); RCKMesh mesh(Context); Triangle(mesh, material); g_UpdateTransparency = FALSE; ASSERT_FALSE(mesh.IsTransparent());
    CKStateChunk chunk(CKCID_MATERIAL, nullptr); PackedClassification(chunk, TRUE); g_UpdateTransparency = FALSE;
    ASSERT_EQ(CK_OK, material.Load(&chunk, nullptr)); ASSERT_TRUE(material.IsAlphaTransparent()); EXPECT_TRUE(g_UpdateTransparency); EXPECT_TRUE(mesh.IsTransparent()); g_UpdateTransparency = FALSE;
}
TEST_F(MaterialDependenciesTest, PackedLoadBlendedMaterialToOpaqueInvalidatesActualMeshTransparencyCache) {
    RCKMaterial material(Context); RCKMesh mesh(Context); material.EnableAlphaBlend(TRUE); material.SetDestBlend(VXBLEND_INVSRCALPHA); Triangle(mesh, material);
    g_UpdateTransparency = FALSE; ASSERT_TRUE(mesh.IsTransparent()); CKStateChunk chunk(CKCID_MATERIAL, nullptr); PackedClassification(chunk, FALSE); g_UpdateTransparency = FALSE;
    ASSERT_EQ(CK_OK, material.Load(&chunk, nullptr)); ASSERT_FALSE(material.IsAlphaTransparent()); EXPECT_TRUE(g_UpdateTransparency); EXPECT_FALSE(mesh.IsTransparent()); g_UpdateTransparency = FALSE;
}
TEST_F(MaterialDependenciesTest, PackedLoadSameClassificationDoesNotDirtyActualMeshTransparencyCache) {
    RCKMaterial material(Context); RCKMesh mesh(Context); Triangle(mesh, material); g_UpdateTransparency = FALSE; ASSERT_FALSE(mesh.IsTransparent());
    CKStateChunk opaque(CKCID_MATERIAL, nullptr); PackedClassification(opaque, FALSE); ASSERT_EQ(CK_OK, material.Load(&opaque, nullptr)); EXPECT_FALSE(g_UpdateTransparency); EXPECT_FALSE(mesh.IsTransparent());
    material.EnableAlphaBlend(TRUE); material.SetDestBlend(VXBLEND_INVSRCALPHA); ASSERT_TRUE(mesh.IsTransparent()); g_UpdateTransparency = FALSE;
    CKStateChunk blend(CKCID_MATERIAL, nullptr); PackedClassification(blend, TRUE); ASSERT_EQ(CK_OK, material.Load(&blend, nullptr)); EXPECT_FALSE(g_UpdateTransparency); EXPECT_TRUE(mesh.IsTransparent()); g_UpdateTransparency = FALSE;
}
