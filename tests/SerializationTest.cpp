#include <cstdio>
#include <cmath>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "CKContext.h"
#include "CKGlobals.h"
#include "CKFile.h"
#include "CKGridManager.h"
#include "CKParameterLocal.h"
#include "CKStateChunk.h"
#include "RCK3dEntity.h"
#include "RCKBodyPart.h"
#include "RCKCharacter.h"
#include "RCKGrid.h"
#include "RCKMaterial.h"
#include "RCKLayer.h"
#include "RCKKeyframeData.h"
#include "RCKKeyedAnimation.h"
#include "RCKMesh.h"
#include "RCKObjectAnimation.h"
#include "RCKPatchMesh.h"
#include "RCKRenderManager.h"
#include "RCKSkin.h"
#include "RCKSpriteText.h"
#include "RCKTexture.h"
#include "SerializationGridRegistry.h"
#include "TCBReferenceSamples.h"
#include "BezierReferenceSamples.h"
#include "MorphReferenceSamples.h"
#include "MorphEditReferenceSamples.h"
#include "LinearReferenceSamples.h"
#include "VxSharedLibrary.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

using Chunk = std::unique_ptr<CKStateChunk, decltype(&DeleteCKStateChunk)>;

void Check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

Chunk NewChunk(CK_CLASSID cid, int version = CHUNKDATA_CURRENTVERSION) {
    Chunk chunk(CreateCKStateChunk(cid, nullptr), &DeleteCKStateChunk);
    chunk->StartWrite();
    chunk->SetDataVersion(version);
    return chunk;
}

void LoadChunk(CKObject &object, CKStateChunk *chunk) {
    chunk->CloseChunk();
    chunk->StartRead();
    Check(object.Load(chunk, nullptr) == CK_OK, "Load failed");
}

bool Equal(const VxVector &a, const VxVector &b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

// CK2_3D.dll 0x1000A2B2 writes raw normals, with no count prefix.
void SkinNormalsUseOriginalWireLayout() {
    CKContext context(nullptr, 0, 0);
    RCK3dEntity source(&context, "SkinSource");
    CKSkin *skin = source.CreateSkin();
    skin->SetVertexCount(2);
    skin->SetNormalCount(2);
    const VxVector normals[] = {VxVector(0.25f, -0.5f, 0.75f), VxVector(-1.0f, 0.0f, 1.0f)};
    for (int i = 0; i < 2; ++i)
        skin->SetNormal(i, normals[i]);

    Chunk chunk(source.Save(nullptr, CK_STATESAVE_3DENTITYONLY), &DeleteCKStateChunk);
    chunk->StartRead();
    Check(chunk->SeekIdentifierAndReturnSize(CK_STATESAVE_3DENTITYSKINDATANORMALS) == 24,
          "Skin normals must contain exactly 12 bytes per vertex");
    for (const VxVector &normal : normals) {
        VxVector saved;
        chunk->ReadVector(&saved);
        Check(Equal(saved, normal), "Skin normal wire data changed");
    }

    RCK3dEntity loaded(&context, "SkinLoaded");
    chunk->StartRead();
    Check(loaded.Load(chunk.get(), nullptr) == CK_OK, "Skin Load failed");
    Check(loaded.GetSkin()->GetNormalCount() == 2, "Normal count did not survive Load");
    for (int i = 0; i < 2; ++i)
        Check(Equal(loaded.GetSkin()->GetNormal(i), normals[i]), "Skin normals did not round trip");
}

void SkinBoneFlagsSurviveSaveLoad() {
    CKContext context(nullptr, 0, 0);
    RCK3dEntity source(&context, "BoneSource"), loaded(&context, "BoneLoaded");
    CKSkin *skin = source.CreateSkin();
    skin->SetBoneCount(1);
    static_cast<RCKSkinBoneData *>(skin->GetBoneData(0))->SetFlags(0x12345678u);
    Chunk chunk(source.Save(nullptr, CK_STATESAVE_3DENTITYONLY), &DeleteCKStateChunk);
    chunk->StartRead();
    Check(loaded.Load(chunk.get(), nullptr) == CK_OK, "Skin Load failed");
    Check(static_cast<RCKSkinBoneData *>(loaded.GetSkin()->GetBoneData(0))->GetFlags() == 0x12345678u,
          "Save discarded skin bone flags");
}

void SavingSkinInvalidatesCachedBonePoints() {
    CKContext context(nullptr, 0, 0);
    RCK3dEntity entity(&context, "SkinOwner"), bone(&context, "Bone");
    CKSkin *skin = entity.CreateSkin();
    skin->SetBoneCount(1);
    skin->SetVertexCount(1);
    skin->GetBoneData(0)->SetBone(reinterpret_cast<CK3dEntity *>(&bone));
    skin->GetBoneData(0)->SetBoneInitialInverseMatrix(VxMatrix::Identity());
    CKSkinVertexData *vertex = skin->GetVertexData(0);
    vertex->SetBoneCount(1);
    vertex->SetBone(0, 0);
    vertex->SetWeight(0, 1.0f);
    VxVector position(1.0f, 2.0f, 3.0f), output;
    vertex->SetInitialPos(position);
    skin->ConstructBoneTransfoMatrices(&context);
    Check(skin->CalcPoints(1, reinterpret_cast<CKBYTE *>(&output), sizeof(output)), "Initial skin calculation failed");
    Check(Equal(output, position), "Initial skin point was incorrect");
    position = VxVector(4.0f, 5.0f, 6.0f);
    vertex->SetInitialPos(position);
    Chunk chunk(entity.Save(nullptr, CK_STATESAVE_3DENTITYONLY), &DeleteCKStateChunk);
    Check(skin->CalcPoints(1, reinterpret_cast<CKBYTE *>(&output), sizeof(output)), "Skin calculation after Save failed");
    Check(Equal(output, position), "Save retained stale cached bone points");
}

// 0x1000A7B9 calls CreateSkin even when a skin is already present.
void LoadingSkinReplacesPreviousNormals() {
    CKContext context(nullptr, 0, 0);
    RCK3dEntity source(&context, "NoNormals"), loaded(&context, "OldNormals");
    source.CreateSkin()->SetVertexCount(1);
    CKSkin *oldSkin = loaded.CreateSkin();
    oldSkin->SetVertexCount(1);
    oldSkin->SetNormalCount(1);
    oldSkin->SetNormal(0, VxVector(0.0f, 1.0f, 0.0f));
    Chunk chunk(source.Save(nullptr, CK_STATESAVE_3DENTITYONLY), &DeleteCKStateChunk);
    chunk->StartRead();
    Check(loaded.Load(chunk.get(), nullptr) == CK_OK, "Skin Load failed");
    Check(loaded.GetSkin()->GetNormalCount() == 0, "Load retained normals absent from the saved skin");
}

void LoadingAnimationListReplacesPreviousAnimations() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation oldAnimation(&context, "OldAnimation"), newAnimation(&context, "NewAnimation");
    RCK3dEntity entity(&context, "Entity");
    entity.AddObjectAnimation(&oldAnimation);
    Chunk chunk = NewChunk(CKCID_3DENTITY);
    chunk->WriteIdentifier(CK_STATESAVE_ANIMATION);
    XObjectPointerArray animations;
    animations.PushBack(&newAnimation);
    animations.Save(chunk.get());
    LoadChunk(entity, chunk.get());
    Check(entity.GetObjectAnimationCount() == 1 && entity.GetObjectAnimation(0) == &newAnimation,
          "Loading an animation list appended to stale animations");

    Chunk empty = NewChunk(CKCID_3DENTITY);
    empty->WriteIdentifier(CK_STATESAVE_ANIMATION);
    empty->WriteObjectArray(nullptr, 0);
    LoadChunk(entity, empty.get());
    Check(entity.GetObjectAnimationCount() == 0, "An empty saved animation list must clear previous animations");
}

void LegacyEntityWithoutParentDetachesPreviousParent() {
    CKContext context(nullptr, 0, 0);
    RCK3dEntity parent(&context, "Parent"), entity(&context, "Entity");
    entity.SetParent(reinterpret_cast<CK3dEntity *>(&parent), TRUE);
    Chunk chunk = NewChunk(CKCID_3DENTITY, 4);
    chunk->WriteIdentifier(CK_STATESAVE_3DENTITYFLAGS);
    chunk->WriteDword(0);
    chunk->WriteDword(VX_MOVEABLE_VISIBLE);
    LoadChunk(entity, chunk.get());
    Check(entity.GetParent() == nullptr, "Missing legacy parent must detach the existing parent");
}

void ModernEntityDataTakesPrecedenceOverLegacyData() {
    CKContext context(nullptr, 0, 0);
    RCK3dEntity oldParent(&context, "LegacyParent"), entity(&context, "Entity");
    entity.SetMoveableFlags(entity.GetMoveableFlags() & ~VX_MOVEABLE_WORLDALIGNED);
    Chunk chunk = NewChunk(CKCID_3DENTITY);
    chunk->WriteIdentifier(CK_STATESAVE_3DENTITYNDATA);
    chunk->WriteDword(0);
    chunk->WriteDword(VX_MOVEABLE_VISIBLE);
    const VxVector rows[] = {VxVector(1, 0, 0), VxVector(0, 1, 0),
                             VxVector(0, 0, 1), VxVector(3, 4, 5)};
    for (const VxVector &row : rows)
        chunk->WriteVector(&row);
    chunk->WriteIdentifier(CK_STATESAVE_PARENT);
    chunk->WriteObject(&oldParent);
    chunk->WriteIdentifier(CK_STATESAVE_3DENTITYMATRIX);
    chunk->WriteInt(sizeof(VxMatrix));
    chunk->WriteMatrix(VxMatrix::Identity());
    LoadChunk(entity, chunk.get());
    Check(entity.GetParent() == nullptr, "Legacy parent overrode modern entity data");
    VxVector position;
    entity.GetPosition(&position);
    Check(Equal(position, rows[3]), "Legacy matrix overrode modern entity data");
}

class LegacyTexture : public RCKTexture {
public:
    explicit LegacyTexture(CKContext *context) : RCKTexture(context, "LegacyTexture") {}
    CKBOOL UseMipmap(int value) override { mipmapRequest = value; return TRUE; }
    int mipmapRequest = -100;
};

// 0x10079D4D: legacy mipmaps use 0x40000; save options use 0x80000.
void LegacyTextureReadsVideoFormatIdentifier() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context); // Owned by CKContext.
    LegacyTexture texture(&context);
    Chunk chunk = NewChunk(CKCID_TEXTURE, 4);
    chunk->WriteIdentifier(CK_STATESAVE_TEXVIDEOFORMAT);
    chunk->WriteInt(1);
    LoadChunk(texture, chunk.get());
    Check(texture.mipmapRequest == 1, "Legacy mipmap request was ignored");
}

void LegacyTextureReadsSaveFormatIdentifier() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context); // Owned by CKContext.
    LegacyTexture texture(&context);
    texture.SetSaveOptions(CKTEXTURE_RAWDATA);
    Chunk chunk = NewChunk(CKCID_TEXTURE, 4);
    chunk->WriteIdentifier(CK_STATESAVE_TEXSAVEFORMAT);
    chunk->WriteDword(CKTEXTURE_EXTERNAL);
    chunk->WriteBuffer(0, nullptr);
    LoadChunk(texture, chunk.get());
    Check(texture.GetSaveOptions() == CKTEXTURE_EXTERNAL, "Legacy texture save options were ignored");
}

class BodyPartForTest : public RCKBodyPart {
public:
    explicit BodyPartForTest(CKContext *context) : RCKBodyPart(context, "BodyPart") {}
    const CKIkJoint &Joint() const { return m_RotationJoint; }
};

void LegacyBodyPartUsesIntegerFlagsAndX86ShiftCounts() {
    CKContext context(nullptr, 0, 0);
    BodyPartForTest body(&context);
    struct LegacyJoint {
        CKDWORD flags[3][3];
        VxVector minimum, maximum, damping;
    } joint = {};
    // 0x80000000 compares nonzero as an integer, but equals -0.0f as a float.
    joint.flags[0][0] = 0x80000000u;
    joint.flags[1][1] = 1;
    joint.flags[2][2] = 1;
    joint.minimum = VxVector(-1, -2, -3);
    joint.maximum = VxVector(1, 2, 3);
    joint.damping = VxVector(0.1f, 0.2f, 0.3f);
    static_assert(sizeof(LegacyJoint) == 72, "Legacy joint layout must stay 72 bytes");
    Chunk chunk = NewChunk(CKCID_BODYPART, 4);
    chunk->WriteIdentifier(CK_STATESAVE_BODYPARTROTJOINT);
    chunk->WriteBuffer_LEndian(sizeof(joint), &joint);
    LoadChunk(body, chunk.get());
    Check(body.Joint().m_Flags == 0x80000210u, "Legacy integer flags or x86 shift masking changed");
    Check(Equal(body.Joint().m_Min, joint.minimum) && Equal(body.Joint().m_Max, joint.maximum) &&
              Equal(body.Joint().m_Damping, joint.damping), "Legacy joint vectors changed");
}

// 0x100621FF calls RCK2dEntity::Save, bypassing sprite bitmap state.
void SpriteTextSaveOmitsSpriteBitmapState() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context); // Owned by CKContext.
    RCKSpriteText text(&context, "Text");
    Chunk chunk(text.Save(nullptr, CK_STATESAVE_ALL), &DeleteCKStateChunk);
    chunk->StartRead();
    Check(!chunk->SeekIdentifier(CK_STATESAVE_SPRITETRANSPARENT), "Text Save unexpectedly emitted sprite transparency");
    Check(!chunk->SeekIdentifier(CK_STATESAVE_SPRITECURRENTIMAGE), "Text Save unexpectedly emitted sprite slots");
    Check(chunk->SeekIdentifier(CK_STATESAVE_SPRITETEXT), "Text Save omitted text data");
}

// 0x1007C340 handles three distinct vertex layouts and material-grouped faces.
template <int Version, bool Lit, CKDWORD SaveFlags = 0>
void LegacyMeshLayout() {
    CKContext context(nullptr, 0, 0);
    RCKMesh mesh(&context, "LegacyMesh");
    RCKMaterial materialA(&context, "MaterialA"), materialB(&context, "MaterialB");
    const VxVector positions[] = {VxVector(0, 0, 0), VxVector(1, 0, 0), VxVector(0, 1, 0)};
    const VxVector normal(0.6f, 0.8f, 0.0f);
    const CKDWORD colors[] = {0xFF123456u, 0xFF234567u, 0xFF345678u};
    const CKDWORD specular[] = {0x00123456u, 0x00234567u, 0x00345678u};
    const float u[] = {0.25f, 0.5f, 0.75f}, v[] = {0.75f, 0.5f, 0.25f};
    mesh.SetLitMode(Lit ? VX_LITMESH : VX_PRELITMESH);
    Chunk chunk = NewChunk(CKCID_MESH, Version);
    chunk->WriteIdentifier(CK_STATESAVE_MESHFLAGS);
    chunk->WriteDword(mesh.GetFlags());
    chunk->WriteIdentifier(CK_STATESAVE_MESHVERTICES);
    chunk->WriteInt(3);
    if (Version >= 5) {
        chunk->WriteDword(SaveFlags);
        for (const VxVector &position : positions)
            chunk->WriteVector(&position);
        if (Lit) {
            if (!(SaveFlags & 4)) {
                for (int i = 0; i < 3; ++i)
                    chunk->WriteVector(&normal);
            }
        } else {
            for (int i = 0; i < ((SaveFlags & 1) ? 1 : 3); ++i)
                chunk->WriteDword(colors[i]);
            for (int i = 0; i < ((SaveFlags & 2) ? 1 : 3); ++i)
                chunk->WriteDword(specular[i]);
        }
        for (int i = 0; i < ((SaveFlags & 8) ? 1 : 3); ++i) {
            chunk->WriteFloat(u[i]);
            chunk->WriteFloat(v[i]);
        }
    } else {
        for (int i = 0; i < 3; ++i) {
            if (Version == 0) {
                VxVector position = positions[i], legacyNormal = normal;
                chunk->WriteBuffer_LEndian(sizeof(VxVector), &position);
                chunk->WriteBuffer_LEndian(sizeof(VxVector), &legacyNormal);
            } else {
                chunk->WriteVector(&positions[i]);
                if (Lit)
                    chunk->WriteVector(&normal);
            }
            if (Version == 0 || !Lit) {
                chunk->WriteDword(colors[i]);
                chunk->WriteDword(specular[i]);
            }
            chunk->WriteFloat(u[i]);
            chunk->WriteFloat(v[i]);
        }
    }
    chunk->WriteIdentifier(CK_STATESAVE_MESHFACES);
    chunk->WriteInt(2);
    if (Version >= 1)
        chunk->WriteInt(2); // Material group count.
    CKMaterial *materials[] = {reinterpret_cast<CKMaterial *>(&materialA), reinterpret_cast<CKMaterial *>(&materialB)};
    for (CKMaterial *material : materials) {
        if (Version >= 1) {
            chunk->WriteObject(material);
            chunk->WriteInt(1);
            chunk->WriteDwordAsWords(0 | (1u << 16));
            chunk->WriteDwordAsWords(2);
        } else {
            chunk->WriteInt(0);
            chunk->WriteInt(1);
            chunk->WriteInt(2);
            chunk->WriteDword(0); // Obsolete face data.
            chunk->WriteObject(material);
        }
    }
    chunk->WriteIdentifier(CK_STATESAVE_MESHLINES);
    chunk->WriteInt(1);
    if (Version >= 1) {
        CKWORD indices[] = {1, 2};
        chunk->WriteBuffer_LEndian16(sizeof(indices), indices);
    } else {
        chunk->WriteInt(1);
        chunk->WriteInt(2);
    }
    LoadChunk(mesh, chunk.get());
    Check(mesh.GetVertexCount() == 3 && mesh.GetFaceCount() == 2, "Legacy mesh counts changed");
    for (int i = 0; i < 3; ++i) {
        VxVector position;
        mesh.GetVertexPosition(i, &position);
        Check(Equal(position, positions[i]), "Legacy vertex position layout is incorrect");
        float actualU, actualV;
        mesh.GetVertexTextureCoordinates(i, &actualU, &actualV, -1);
        const int uvIndex = (SaveFlags & 8) ? 0 : i;
        Check(actualU == u[uvIndex] && actualV == v[uvIndex], "Legacy UV layout is incorrect");
        if (Lit) {
            VxVector actualNormal;
            mesh.GetVertexNormal(i, &actualNormal);
            const VxVector expectedNormal = (SaveFlags & 4) ? VxVector(0, 0, 1) : normal;
            Check(std::fabs(actualNormal.x - expectedNormal.x) < 0.001f &&
                      std::fabs(actualNormal.y - expectedNormal.y) < 0.001f &&
                      std::fabs(actualNormal.z - expectedNormal.z) < 0.001f,
                  "Legacy normals were lost or not rebuilt");
        } else {
            Check(mesh.GetVertexColor(i) == colors[(SaveFlags & 1) ? 0 : i], "Legacy diffuse color layout is incorrect");
            Check(mesh.GetVertexSpecularColor(i) == specular[(SaveFlags & 2) ? 0 : i], "Legacy specular color layout is incorrect");
        }
    }
    for (int i = 0; i < 2; ++i) {
        int a, b, c;
        mesh.GetFaceVertexIndex(i, a, b, c);
        Check(a == 0 && b == 1 && c == 2, "Legacy face indices changed");
        Check(mesh.GetFaceMaterial(i) == materials[i], "Legacy face material grouping changed");
    }
    int a, b;
    mesh.GetLine(0, &a, &b);
    Check(a == 1 && b == 2, "Legacy line index buffer layout is incorrect");
}

template <bool NonUnit>
void MeshNormalSaveDecisionMatchesOriginal() {
    CKContext context(nullptr, 0, 0);
    RCKMesh mesh(&context, "NormalSaveDecision");
    mesh.SetVertexCount(3);
    mesh.SetFaceCount(1);
    VxVector positions[] = {VxVector(0, 0, 0), VxVector(1, 0, 0), VxVector(0, 1, 0)};
    for (int i = 0; i < 3; ++i) mesh.SetVertexPosition(i, &positions[i]);
    mesh.SetFaceVertexIndex(0, 0, 1, 2);
    mesh.BuildNormals();
    VxVector normal;
    mesh.GetVertexNormal(0, &normal);
    if (NonUnit) normal *= 2.0f;
    else normal.x += 0.002f;
    mesh.SetVertexNormal(0, &normal);
    mesh.SetFlags(mesh.GetFlags() & ~VXMESH_GENNORMALS);
    const bool omitted = (mesh.GetSaveFlags() & 0x04) != 0;
    Check(omitted != NonUnit, NonUnit ? "Save must preserve non-unit normal magnitude" :
          "Save must use average normal error rather than maximum per-vertex error");
    Chunk chunk(mesh.Save(nullptr, CK_STATESAVE_MESHONLY), &DeleteCKStateChunk);
    chunk->StartRead();
    Check(chunk->SeekIdentifier(CK_STATESAVE_MESHVERTICES), "Missing vertex block");
    chunk->ReadInt();
    Check(((chunk->ReadDword() & 0x04) != 0) == omitted, "Save ignored normal encoding decision");
}

// Only the type registry participates in these layer serialization tests.
class LayerRegistry : public CKGridManager {
public:
    explicit LayerRegistry(CKContext *context) : CKGridManager(context, GRID_MANAGER_GUID, "LayerRegistry") {
        m_Remap = remap;
        m_RemapCount = 1;
        context->RegisterNewManager(this);
    }
    int GetTypeFromName(CKSTRING) override { return 1; }
    CKSTRING GetTypeName(int) override { return "Layer"; }
    CKERROR SetTypeName(int, CKSTRING) override { return CK_OK; }
    int RegisterType(CKSTRING) override { return 1; }
    int UnRegisterType(CKSTRING) override { return 0; }
    CKERROR SetAssociatedParam(int, CKGUID guid) override { parameter = guid; return CK_OK; }
    CKGUID GetAssociatedParam(int) override { return parameter; }
    CKERROR SetAssociatedColor(int, VxColor *value) override { color = *value; return CK_OK; }
    CKERROR GetAssociatedColor(int, VxColor *value) override { *value = color; return CK_OK; }
    int GetLayerTypeCount() override { return 2; }
    int GetClassificationFromName(CKSTRING) override { return 0; }
    CKSTRING GetClassificationName(int) override { return nullptr; }
    int RegisterClassification(CKSTRING) override { return 0; }
    int GetGridClassificationCategory() override { return 0; }
    const XObjectPointerArray &GetGridArray(int) override { return grids; }
    CKGrid *GetNearestGrid(VxVector *, CK3dEntity *) override { return nullptr; }
    CKGrid *GetPreferredGrid(VxVector *, CK3dEntity *) override { return nullptr; }
    CKBOOL IsInGrid(CKGrid *, VxVector *, CK3dEntity *) override { return FALSE; }
    int GetGridObjectCount(int) override { return 0; }
    CKGrid *GetGridObject(int, int) override { return nullptr; }
    void FillGridWithObjectShape(CK3dEntity *, int, void *) override {}
    void FillGridWithObjectShape(CK3dEntity *, int, int, void *) override {}
    void Init() override {}
    CKERROR OnCKInit() override { return CK_OK; }
    CKERROR PostClearAll() override { return CK_OK; }
    CKERROR PostLoad() override { return CK_OK; }
    CKERROR PreSave() override { return CK_OK; }
    CKERROR OnCKReset() override { return CK_OK; }
    CKERROR PreProcess() override { return CK_OK; }
    CKDWORD GetValidFunctionsMask() override { return 0; }
    CKERROR LoadData(CKStateChunk *, CKFile *) override { return CK_OK; }
    CKStateChunk *SaveData(CKFile *) override { return nullptr; }
    void ClearData() override {}
    void InitData() override {}
    CKGUID parameter;
    VxColor color;
    int remap[2] = {};
    XObjectPointerArray grids;
};

void LayerLegacyParameterGuid() {
    CKContext context(nullptr, 0, 0);
    LayerRegistry *registry = new LayerRegistry(&context);
    RCKLayer layer(&context, "Layer", 0);
    CKFile file(&context);
    Chunk chunk = NewChunk(CKCID_LAYER);
    chunk->WriteIdentifier(CK_STATESAVE_LAYERDATA);
    chunk->WriteObject(nullptr);
    chunk->WriteInt(1); // No square buffer.
    chunk->WriteInt(2); // Layer metadata version.
    chunk->WriteDword(0xFF123456);
    chunk->WriteInt(1);
    chunk->CloseChunk();
    chunk->StartRead();
    Check(layer.Load(chunk.get(), &file) == CK_OK, "Layer Load failed");
    Check(registry->parameter == CKGUID(0x5A5716FD, 0x44E276D7), "Legacy layer parameter GUID differs from the DLL");
}

void LayerLoadsSquaresBeforeItsGrid() {
    CKContext context(nullptr, 0, 0);
    new LayerRegistry(&context);
    RCKLayer layer(&context, "Layer", 0);
    Chunk chunk = NewChunk(CKCID_LAYER);
    chunk->WriteIdentifier(CK_STATESAVE_LAYERDATA);
    chunk->WriteObject(nullptr);
    chunk->WriteInt(1); // Runtime type index.
    chunk->WriteInt(0); // Square data format.
    chunk->WriteInt(1); // Visibility.
    CKDWORD values[] = {0x12345678u, 0x23456789u, 0x3456789Au};
    chunk->WriteBuffer_LEndian(sizeof(values), values);
    LoadChunk(layer, chunk.get());
    Check(layer.GetSquareArray() != nullptr, "Layer data was discarded because its grid was not loaded yet");
    for (int i = 0; i < 3; ++i)
        Check(layer.GetSquareArray()[i].dval == values[i], "Layer square wire stride must be four bytes");
}

void LayerSaveRegistersTypeRemapping() {
    CKContext context(nullptr, 0, 0);
    LayerRegistry *registry = new LayerRegistry(&context);
    RCKLayer layer(&context, "Layer", 0);
    layer.SetFormat(1);
    CKFile file(&context);
    Chunk first(layer.Save(&file, CK_STATESAVE_ALL), &DeleteCKStateChunk);
    Check(registry->remap[1] == 1 && registry->m_RemapCount == 2, "Layer Save did not register its type remapping");
    Chunk second(layer.Save(&file, CK_STATESAVE_ALL), &DeleteCKStateChunk);
    Check(registry->m_RemapCount == 2, "Layer Save registered the same type more than once");
}

void LayerSquaresUseFourByteWireStride() {
    CKContext context(nullptr, 0, 0);
    new LayerRegistry(&context);
    RCKGrid grid(&context, "Grid");
    grid.SetDimensions(3, 1, 3.0f, 1.0f);
    RCKLayer layer(&context, "Layer", grid.GetID());
    const CKDWORD values[] = {0x12345678u, 0xABCDEF01u, 0x87654321u};
    for (int i = 0; i < 3; ++i)
        layer.GetSquareArray()[i].dval = values[i];
    Chunk chunk(layer.Save(nullptr, CK_STATESAVE_ALL), &DeleteCKStateChunk);
    chunk->StartRead();
    Check(chunk->SeekIdentifier(CK_STATESAVE_LAYERDATA), "Missing layer data");
    chunk->ReadObjectID();
    chunk->ReadInt(); // type
    chunk->ReadInt(); // format
    chunk->ReadInt(); // flags
    void *buffer = nullptr;
    const int size = chunk->ReadBuffer(&buffer);
    const bool matches = size == sizeof(values) && buffer && std::memcmp(buffer, values, sizeof(values)) == 0;
    CKDeletePointer(buffer);
    Check(matches, "Layer wire data used host CKSquare stride instead of four bytes");
}

// 0x100586B5 only shares with an owner whose CKFileObject::Data already exists.
template<bool AliasFirst>
void SharedAnimationUsesPreviouslySavedOwner() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation owner(&context, "Owner"), alias(&context, "Alias");
    RCKObjectAnimation firstLoaded(&context, "FirstLoaded"), secondLoaded(&context, "SecondLoaded");
    VxVector position(2.0f, -3.0f, 4.0f);
    owner.AddPositionKey(0.0f, &position);
    owner.SetLength(20.0f);
    Check(alias.ShareDataFrom(reinterpret_cast<CKObjectAnimation *>(&owner)), "Cannot share animation data");
    RCKObjectAnimation *objects[] = {AliasFirst ? &alias : &owner, AliasFirst ? &owner : &alias};
    CKFile file(&context);
    file.m_FileObjects.Resize(2);
    for (int i = 0; i < 2; ++i) {
        CKFileObject &entry = file.m_FileObjects[i];
        entry.Object = entry.CreatedObject = objects[i]->GetID();
        entry.ObjPtr = objects[i];
        file.m_ObjectsHashTable.Insert(entry.Object, i);
    }
    for (int i = 0; i < 2; ++i) {
        CKFileObject &entry = file.m_FileObjects[i];
        entry.Data = entry.ObjPtr->Save(&file, CK_STATESAVE_ALL);
        Check(entry.Data != nullptr, "Animation Save failed");
        entry.Data->StartRead();
        Check(entry.Data->SeekIdentifier(i == 0 ? CK_STATESAVE_OBJANIMCONTROLLERS : CK_STATESAVE_OBJANIMSHARED),
              "Shared animation must refer backward to already serialized controller data");
    }
    RCKObjectAnimation *loaded[] = {&firstLoaded, &secondLoaded};
    for (int i = 0; i < 2; ++i) {
        file.m_FileObjects[i].ObjPtr = loaded[i];
        file.m_FileObjects[i].CreatedObject = loaded[i]->GetID();
    }
    for (int i = 0; i < 2; ++i) {
        CKStateChunk *chunk = file.m_FileObjects[i].Data;
        chunk->StartRead();
        Check(loaded[i]->Load(chunk, &file) == CK_OK, "Animation reload failed");
        VxVector actual;
        Check(loaded[i]->EvaluatePosition(0.0f, actual) && Equal(actual, position), "Reload lost shared position keys");
        Check(loaded[i]->GetLength() == 20.0f, "Reload lost shared animation length");
    }
    Check(firstLoaded.GetPositionController() == secondLoaded.GetPositionController(), "Reload duplicated shared controllers");
}

// Stop at the tessellation boundary so this test can also inspect the unused
// fourth index of a triangle (the original loader preserves its -1 sentinel).
class SerializationPatchMesh : public RCKPatchMesh {
public:
    explicit SerializationPatchMesh(CKContext *context) : RCKPatchMesh(context, "Patch") {}
    void BuildRenderMesh() override { built = true; }
    bool built = false;
};

template<bool Triangle>
void LegacyPatchMeshPreservesEdgeIndices() {
    CKContext context(nullptr, 0, 0);
    SerializationPatchMesh mesh(&context), reloaded(&context);
    Chunk chunk = NewChunk(CKCID_PATCHMESH, 8);
    chunk->WriteIdentifier(CK_STATESAVE_PATCHMESHDATA2);
    chunk->WriteDword(0); // flags
    chunk->WriteObject(nullptr); // default material
    chunk->WriteInt(1); // iterations
    chunk->WriteInt(0); // vectors
    chunk->WriteDword(0); // vertex bytes
    chunk->WriteDword(0); // vertex count
    CKDWORD record[22] = {};
    record[0] = Triangle ? 3 : 4;
    const short edges[] = {2, 0, 3, Triangle ? short(-1) : short(1)};
    for (int i = 0; i < 4; ++i)
        record[18 + i] = static_cast<CKDWORD>(edges[i]);
    record[17] = 0x76543210; // unused legacy field must not become an edge
    chunk->WriteDword(sizeof(record));
    chunk->WriteDword(1);
    chunk->WriteBufferNoSize_LEndian(sizeof(record), record);
    for (int i = 0; i < 3; ++i) { // empty edges, texture patches, UVs
        chunk->WriteDword(0);
        chunk->WriteDword(0);
    }
    LoadChunk(mesh, chunk.get());
    Check(mesh.built && mesh.GetPatchCount() == 1, "Legacy patch did not reach the tessellator");
    CKPatch patch;
    mesh.GetPatch(0, &patch);
    Check(std::memcmp(patch.edge, edges, sizeof(edges)) == 0, "Legacy patch edge indices were discarded");
    Check(patch.SmoothingGroup == 0xFFFFFFFFu, "Legacy default smoothing group changed");
    Chunk modern(mesh.Save(nullptr, CK_STATESAVE_PATCHMESHONLY), &DeleteCKStateChunk);
    LoadChunk(reloaded, modern.get());
    reloaded.GetPatch(0, &patch);
    Check(std::memcmp(patch.edge, edges, sizeof(edges)) == 0, "Patch edges did not survive modern Save/Load");
}

void CheckGridDisplay(RCKGrid &grid) {
    CKMesh *mesh = grid.GetCurrentMesh();
    Check(mesh != nullptr, "Grid PostLoad did not restore its display mesh");
    Check((mesh->GetVertexColor(0) >> 24) == 127, "Grid PostLoad ignored its display opacity");
    float u = 0.0f, v = 0.0f;
    mesh->GetVertexTextureCoordinates(2, &u, &v, -1);
    Check(u == 0.25f && v == 0.25f, "Grid display coordinates did not reach the base UV channel");
    CKMaterial *material = mesh->GetFaceMaterial(0);
    CKTexture *texture = material->GetTexture();
    Check(texture && texture->GetWidth() == 16 && texture->GetHeight() == 16, "Grid display texture missing");
    for (CKObject *object : {static_cast<CKObject *>(mesh), static_cast<CKObject *>(material),
                            static_cast<CKObject *>(mesh->GetFaceMaterial(2)), static_cast<CKObject *>(texture)}) {
        const CKDWORD flags = object->GetObjectFlags();
        Check((flags & 0x23) == 0x23 && !(flags & (CK_OBJECT_DYNAMIC | CK_OBJECT_NOTTOBEDELETED)),
              "Grid generated resources must have the original private/interface/non-saving flags");
    }
    const CKDWORD *pixels = reinterpret_cast<const CKDWORD *>(texture->LockSurfacePtr());
    Check(pixels != nullptr, "Cannot inspect grid display pixels");
    const CKDWORD expected[] = {0xFF0A2E55u, 0xFF1E62AAu, 0xFFFFFFFFu, 0xFF000000u};
    bool matches = true;
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx)
                    matches &= pixels[(y * 2 + dy) * 16 + x * 2 + dx] == expected[y * 2 + x];
    texture->ReleaseSurfacePtr();
    Check(matches, "Grid display lost layer colors, linker scaling, visibility, saturation or 2x2 cell layout");
}

void PopulateGrid(RCKGrid &grid, CKGridManager &registry) {
    grid.SetDimensions(2, 2, 2.0f, 2.0f);
    const char *names[] = {"Intensity", "Linker", "Hidden"};
    const int values[][4] = {{10, 30, 300, 0}, {1, 2, 3, 0}, {255, 255, 255, 255}};
    VxColor colors[] = {VxColor(1.0f, 0.5f, 0.0f), VxColor(0.0f, 0.5f, 1.0f), VxColor(1.0f, 1.0f, 1.0f)};
    for (int i = 0; i < 3; ++i) {
        const int type = registry.RegisterType(const_cast<char *>(names[i]));
        // Use wire-representable colors so file metadata quantization is explicit.
        colors[i] = VxColor(colors[i].GetRGBA());
        registry.SetAssociatedColor(type, &colors[i]);
        registry.SetAssociatedParam(type, i == 1 ? CKPGUID_LINKERGRAPH_ENUM : CKGUID(0, 0));
        CKLayer *layer = grid.AddLayer(type);
        Check(layer != nullptr, "Cannot create grid layer");
        for (int j = 0; j < 4; ++j)
            layer->GetSquareArray()[j].ival = values[i][j];
        layer->SetVisible(i != 2);
    }
}

template<bool LayerFirst>
void GridLayerFileLoadRestoresDisplay() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    auto *registry = new SerializationGridRegistry(&context);
    RCKGrid source(&context, "SourceGrid"), loaded(&context, "LoadedGrid");
    PopulateGrid(source, *registry);
    CKFile file(&context);
    file.m_FileObjects.Resize(4);
    const int gridIndex = LayerFirst ? 3 : 0;
    for (int i = 0; i < 4; ++i) {
        CKObject *object = i == gridIndex ? static_cast<CKObject *>(&source) :
            static_cast<CKObject *>(source.GetLayerByIndex(LayerFirst ? i : i - 1));
        CKFileObject &entry = file.m_FileObjects[i];
        entry.Object = entry.CreatedObject = object->GetID();
        entry.ObjPtr = object;
        file.m_ObjectsHashTable.Insert(entry.Object, i);
    }
    registry->PreSave();
    for (CKFileObject &entry : file.m_FileObjects)
        entry.Data = entry.ObjPtr->Save(&file, CK_STATESAVE_ALL);
    // Loading must restore metadata from the chunks, not reuse the save-side registry.
    VxColor black(0.0f, 0.0f, 0.0f, 0.0f);
    for (int type = 1; type < registry->GetLayerTypeCount(); ++type) {
        registry->SetAssociatedColor(type, &black);
        registry->SetAssociatedParam(type, CKGUID(0, 0));
    }
    for (int i = 0; i < 4; ++i) {
        CKFileObject &entry = file.m_FileObjects[i];
        entry.ObjPtr = i == gridIndex ? static_cast<CKObject *>(&loaded) :
            context.CreateObject(CKCID_LAYER, entry.ObjPtr->GetName(), CK_OBJECTCREATION_NONAMECHECK);
        Check(entry.ObjPtr != nullptr, "Cannot create reload layer");
        entry.CreatedObject = entry.ObjPtr->GetID();
    }
    for (CKFileObject &entry : file.m_FileObjects) {
        entry.Data->StartRead();
        Check(entry.ObjPtr->Load(entry.Data, &file) == CK_OK, "Grid/Layer Load failed");
    }
    Check(loaded.GetWidth() == 2 && loaded.GetLength() == 2 && loaded.GetLayerCount() == 3,
          "Grid dimensions or layer links did not survive file reload");
    for (int i = 0; i < 3; ++i) {
        CKLayer *layer = loaded.GetLayerByIndex(i);
        Check(layer && layer->GetOwner() == loaded.GetID(), "Reload layer points at the wrong grid");
        for (int j = 0; j < 4; ++j)
            Check(layer->GetSquareArray()[j].ival == source.GetLayerByIndex(i)->GetSquareArray()[j].ival,
                  "Grid/Layer file ordering changed cell values");
    }
    loaded.PostLoad();
    CheckGridDisplay(loaded);
}

void GridMemorySnapshotRestoresEmbeddedLayers() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    auto *registry = new SerializationGridRegistry(&context);
    RCKGrid grid(&context, "Grid");
    PopulateGrid(grid, *registry);
    Chunk snapshot(grid.Save(nullptr, CK_STATESAVE_ALL), &DeleteCKStateChunk);
    grid.GetLayerByIndex(0)->GetSquareArray()[0].ival = 99;
    grid.SetDimensions(1, 1, 1.0f, 1.0f);
    LoadChunk(grid, snapshot.get());
    Check(grid.GetWidth() == 2 && grid.GetLength() == 2 && grid.GetLayerByIndex(0)->GetSquareArray()[0].ival == 10,
          "Grid snapshot did not restore embedded layer values and dimensions");
    grid.PostLoad();
    CheckGridDisplay(grid);
}

using WireWords = std::vector<CKDWORD>;

void AppendWire(WireWords &words, const void *data, size_t size) {
    const size_t offset = words.size();
    words.resize(offset + size / sizeof(CKDWORD));
    std::memcpy(words.data() + offset, data, size);
}

Chunk AnimationWithController(CKANIMATION_CONTROLLER type, WireWords &words) {
    Chunk chunk = NewChunk(CKCID_OBJECTANIMATION);
    chunk->WriteIdentifier(CK_STATESAVE_OBJANIMCONTROLLERS);
    for (int i = 0; i < 7; ++i) chunk->WriteFloat(0.0f);
    chunk->WriteDword(0); // flags
    chunk->WriteObject(nullptr);
    chunk->WriteFloat(20.0f);
    chunk->WriteDword(type);
    chunk->WriteDword(static_cast<CKDWORD>(words.size()));
    chunk->WriteBufferNoSize_LEndian(static_cast<int>(words.size() * 4), words.data());
    chunk->WriteDword(0);
    // Extra backing allows the pre-fix fixed-stride readers to fail assertions
    // on this valid controller fixture without reading beyond the chunk buffer.
    chunk->WriteIdentifier(0x7FFFFFFFu);
    for (int i = 0; i < 64; ++i) chunk->WriteDword(0);
    return chunk;
}

void CheckSavedController(RCKObjectAnimation &animation, CKANIMATION_CONTROLLER type, const WireWords &expected) {
    Chunk saved(animation.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
    saved->StartRead();
    Check(saved->SeekIdentifier(CK_STATESAVE_OBJANIMCONTROLLERS), "Missing controller identifier");
    saved->Skip(10); // root/reserved, flags, entity, length
    Check(saved->ReadDword() == type, "Incorrect serialized controller type");
    Check(saved->ReadDword() == expected.size(), "Controller payload size differs from original wire layout");
    Check(std::memcmp(saved->LockReadBuffer(), expected.data(), expected.size() * 4) == 0,
          "Controller payload bytes differ from original wire layout");
    saved->Skip(static_cast<int>(expected.size()));
    Check(saved->ReadDword() == 0, "Controller payload did not end at the next controller tag");
}

template<bool Normals>
WireWords MorphWire() {
    // Two vertices and two keys: one global normal-presence flag, then each
    // key's time, positions, and optional packed normals (0x100505D9/10050744).
    WireWords words = {2, 2, Normals ? 1u : 0u};
    for (int i = 0; i < 2; ++i) {
        const float time = i ? 10.0f : 2.0f;
        const VxVector positions[] = {VxVector(1.0f + i, 2.0f, 3.0f), VxVector(4.0f, 5.0f + i, 6.0f)};
        const CKDWORD normals[] = {0x00100020u + static_cast<CKDWORD>(i), 0x00300040u + static_cast<CKDWORD>(i)};
        AppendWire(words, &time, sizeof(time));
        AppendWire(words, positions, sizeof(positions));
        if (Normals) AppendWire(words, normals, sizeof(normals));
    }
    return words;
}

template<bool Normals>
void MorphSaveUsesGlobalNormalFlag() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation animation(&context, "Morph");
    auto *controller = static_cast<RCKMorphController *>(animation.CreateController(CKANIMATION_MORPH_CONTROL));
    controller->SetMorphVertexCount(2);
    for (int i = 0; i < 2; ++i) {
        const int index = controller->AddKey(i ? 10.0f : 2.0f, Normals);
        auto *key = static_cast<CKMorphKey *>(controller->GetKey(index));
        key->PosArray[0] = VxVector(1.0f + i, 2.0f, 3.0f);
        key->PosArray[1] = VxVector(4.0f, 5.0f + i, 6.0f);
        if (Normals) {
            const CKDWORD normals[] = {0x00100020u + static_cast<CKDWORD>(i), 0x00300040u + static_cast<CKDWORD>(i)};
            std::memcpy(key->NormArray, normals, sizeof(normals));
        }
    }
    CheckSavedController(animation, CKANIMATION_MORPH_CONTROL, MorphWire<Normals>());
}

template<bool Normals>
void MorphLoadReadsOriginalControllerLayout() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation animation(&context, "Morph");
    WireWords words = MorphWire<Normals>();
    Chunk chunk = AnimationWithController(CKANIMATION_MORPH_CONTROL, words);
    LoadChunk(animation, chunk.get());
    auto *controller = animation.GetMorphController();
    Check(controller && controller->GetKeyCount() == 2, "Morph key count changed");
    Check(animation.GetMorphVertexCount() == 2, "Morph vertex count changed");
    for (int i = 0; i < 2; ++i) {
        auto *key = static_cast<CKMorphKey *>(controller->GetKey(i));
        Check(key->TimeStep == (i ? 10.0f : 2.0f), "Morph key time read at the wrong offset");
        Check(Equal(key->PosArray[0], VxVector(1.0f + i, 2.0f, 3.0f)) &&
              Equal(key->PosArray[1], VxVector(4.0f, 5.0f + i, 6.0f)), "Morph positions changed");
        Check((key->NormArray != nullptr) == Normals, "Morph normal presence changed");
        if (Normals) Check(key->NormArray[0].xa == 0x20 + i && key->NormArray[1].ya == 0x30,
                           "Morph packed normals changed");
    }
    CheckSavedController(animation, CKANIMATION_MORPH_CONTROL, words);
}

void EmptyMorphStillWritesThreeWordHeader() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation animation(&context, "EmptyMorph");
    animation.CreateController(CKANIMATION_MORPH_CONTROL);
    CheckSavedController(animation, CKANIMATION_MORPH_CONTROL, {0, 0, 0});
}

WireWords BezierWire(CKBezierPositionKey (&keys)[4]) {
    WireWords words = {4};
    for (int i = 0; i < 4; ++i) {
        keys[i].TimeStep = static_cast<float>(i * 5);
        keys[i].Pos = VxVector(1.0f + i, 2.0f, 3.0f);
        keys[i].In = VxVector(-1.0f, -2.0f - i, -3.0f);
        keys[i].Out = VxVector(4.0f, 5.0f, 6.0f + i);
        keys[i].Flags.SetInTangentMode((i & 1) ? BEZIER_KEY_TANGENTS : BEZIER_KEY_LINEAR);
        keys[i].Flags.SetOutTangentMode((i & 2) ? BEZIER_KEY_TANGENTS : BEZIER_KEY_AUTOSMOOTH);
        AppendWire(words, &keys[i].TimeStep, 4);
        AppendWire(words, &keys[i].Pos, 12);
        const CKDWORD flags = ((i & 1) ? 0x20u : 2u) | ((i & 2) ? 0x00200000u : 0x00010000u);
        words.push_back(flags);
        if (i & 1) AppendWire(words, &keys[i].In, 12);
        if (i & 2) AppendWire(words, &keys[i].Out, 12);
    }
    return words;
}

template<bool Scale, bool Load>
void BezierSerializationUsesConditionalTangents() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation animation(&context, "Bezier");
    const auto type = Scale ? CKANIMATION_BEZIERSCL_CONTROL : CKANIMATION_BEZIERPOS_CONTROL;
    CKBezierPositionKey keys[4];
    WireWords words = BezierWire(keys);
    if (Load) {
        Chunk chunk = AnimationWithController(type, words);
        LoadChunk(animation, chunk.get());
        CKAnimController *controller = Scale ? animation.GetScaleController() : animation.GetPositionController();
        Check(controller && controller->GetKeyCount() == 4, "Bezier key count changed");
        for (int i = 0; i < 4; ++i) {
            auto *key = static_cast<CKBezierPositionKey *>(controller->GetKey(i));
            Check(key->Compare(keys[i], 0.0f), "Bezier key or explicit tangent read at the wrong offset");
        }
    } else {
        CKAnimController *controller = animation.CreateController(type);
        for (auto &key : keys) controller->AddKey(&key);
    }
    CheckSavedController(animation, type, words);
}

void ControllerFactoryAcceptsOriginalBaseTypes() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation animation(&context, "ControllerTypes");
    animation.SetLength(37.0f);
    const CKANIMATION_CONTROLLER base[] = {CKANIMATION_CONTROLLER_POS, CKANIMATION_CONTROLLER_ROT,
                                         CKANIMATION_CONTROLLER_SCL, CKANIMATION_CONTROLLER_MORPH};
    const CKANIMATION_CONTROLLER expected[] = {CKANIMATION_LINPOS_CONTROL, CKANIMATION_LINROT_CONTROL,
                                              CKANIMATION_LINSCL_CONTROL, CKANIMATION_MORPH_CONTROL};
    for (int i = 0; i < 4; ++i) {
        CKAnimController *controller = animation.CreateController(base[i]);
        Check(controller && controller->GetType() == expected[i], "Original base controller type was rejected");
        Check(controller->GetLength() == 37.0f, "Created controller did not inherit animation length");
    }
}

void UnsupportedControllerPreservesExistingKeys() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation animation(&context, "ExistingController");
    VxVector position(2.0f, 3.0f, 4.0f), actual;
    animation.AddPositionKey(0.0f, &position);
    Check(!animation.CreateController(static_cast<CKANIMATION_CONTROLLER>(0x11111101)), "Unknown controller accepted");
    Check(animation.EvaluatePosition(0.0f, actual) && Equal(actual, position), "Unknown controller type destroyed existing keys");
}

Chunk TexturedQuadPatch(CKMaterial *material, CKDWORD channelFlags = VXCHANNEL_ACTIVE | VXCHANNEL_NOTLIT) {
    Chunk chunk = NewChunk(CKCID_PATCHMESH);
    chunk->WriteIdentifier(CK_STATESAVE_PATCHMESHDATA3);
    chunk->WriteDword(CK_PATCHMESH_BUILDNORMALS);
    chunk->WriteInt(0); // no subdivisions
    chunk->WriteInt(12); // edge and interior control vectors
    VxVector points[16] = {VxVector(0.0f, 0.0f, 0.0f), VxVector(1.0f, 0.0f, 0.0f),
                          VxVector(1.0f, 1.0f, 0.0f), VxVector(0.0f, 1.0f, 0.0f)};
    for (int i = 4; i < 16; ++i) points[i] = VxVector(0.5f, 0.5f, 0.0f);
    chunk->WriteDword(sizeof(points));
    chunk->WriteDword(16);
    chunk->WriteBufferNoSize_LEndian(sizeof(points), points);
    chunk->StartObjectIDSequence(1);
    chunk->WriteObjectSequence(material);
    chunk->WriteDword(CK_PATCH_QUAD);
    chunk->WriteDword(0xFFFFFFFFu);
    short indices[] = {0, 1, 2, 3, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 1, 2, 3};
    chunk->WriteBufferNoSize_LEndian16(sizeof(indices), indices);
    CKPatchEdge edges[] = {{0, 0, 1, 1, 0, -1}, {1, 2, 3, 2, 0, -1},
                          {2, 4, 5, 3, 0, -1}, {3, 6, 7, 0, 0, -1}};
    chunk->WriteDword(sizeof(edges));
    chunk->WriteDword(4);
    chunk->WriteBufferNoSize_LEndian16(sizeof(edges), edges);
    chunk->StartObjectIDSequence(2);
    chunk->WriteObjectSequence(nullptr); // base UV channel
    chunk->WriteObjectSequence(material);
    for (int channel = 0; channel < 2; ++channel) {
        // These historical fields carry source blend, destination blend, flags.
        chunk->WriteDword(channel ? VXBLEND_SRCALPHA : 0);
        chunk->WriteDword(channel ? VXBLEND_INVSRCCOLOR : 0);
        chunk->WriteDword(channel ? channelFlags : 0);
        short tv[] = {0, 1, 2, 3};
        chunk->WriteDword(sizeof(tv));
        chunk->WriteDword(1);
        chunk->WriteBufferNoSize_LEndian16(sizeof(tv), tv);
        float uv[] = {0.0f, 0.0f, 1.0f + channel, 0.0f, 1.0f + channel, 1.0f + channel, 0.0f, 1.0f + channel};
        chunk->WriteDword(sizeof(uv));
        chunk->WriteDword(4);
        chunk->WriteBufferNoSize_LEndian(sizeof(uv), uv);
    }
    return chunk;
}

template<bool ExistingChannel>
void PatchRebuildRestoresMaterialChannelState() {
    CKContext context(nullptr, 0, 0);
    RCKMaterial material(&context, "PatchMaterial"), stale(&context, "StaleMaterial");
    RCKPatchMesh mesh(&context, "Patch");
    auto *expectedMaterial = reinterpret_cast<CKMaterial *>(&material);
    Chunk chunk = TexturedQuadPatch(expectedMaterial);
    LoadChunk(mesh, chunk.get());
    if (ExistingChannel) {
        mesh.SetChannelMaterial(0, reinterpret_cast<CKMaterial *>(&stale));
        mesh.SetChannelSourceBlend(0, VXBLEND_ONE);
        mesh.SetChannelDestBlend(0, VXBLEND_ZERO);
        mesh.SetChannelFlags(0, 0);
        mesh.SetIterationCount(1);
        mesh.BuildRenderMesh();
    }
    Check(mesh.GetChannelCount() == 1 && mesh.GetChannelMaterial(0) == expectedMaterial,
          "Patch rebuild did not restore the texture channel material");
    Check(mesh.GetChannelSourceBlend(0) == VXBLEND_SRCALPHA && mesh.GetChannelDestBlend(0) == VXBLEND_INVSRCCOLOR,
          "Patch rebuild discarded saved channel blending");
    Check(mesh.GetChannelFlags(0) == (VXCHANNEL_ACTIVE | VXCHANNEL_NOTLIT), "Patch rebuild discarded channel flags");
    float u = 0.0f, v = 0.0f;
    mesh.GetVertexTextureCoordinates(2, &u, &v, 0);
    Check(u == 2.0f && v == 2.0f, "Patch extra channel UVs did not reach the render mesh");
    Chunk saved(mesh.Save(nullptr, CK_STATESAVE_ALL), &DeleteCKStateChunk);
    RCKPatchMesh loaded(&context, "ReloadedPatch");
    LoadChunk(loaded, saved.get());
    Check(loaded.GetChannelSourceBlend(0) == VXBLEND_SRCALPHA && loaded.GetChannelDestBlend(0) == VXBLEND_INVSRCCOLOR,
          "Patch channel blending did not survive Save/Load");
}

void PatchSameUVPreservesBaseCoordinates() {
    CKContext context(nullptr, 0, 0);
    RCKMaterial material(&context, "SameUVMaterial");
    RCKPatchMesh mesh(&context, "SameUVPatch");
    Chunk chunk = TexturedQuadPatch(reinterpret_cast<CKMaterial *>(&material), VXCHANNEL_ACTIVE | VXCHANNEL_SAMEUV);
    LoadChunk(mesh, chunk.get());
    float u = 0.0f, v = 0.0f;
    mesh.GetVertexTextureCoordinates(2, &u, &v, -1);
    Check(u == 1.0f && v == 1.0f, "Patch SAMEUV channel overwrote base UVs");
    mesh.GetVertexTextureCoordinates(2, &u, &v, 0);
    Check(u == 2.0f && v == 2.0f, "Patch SAMEUV channel lost its independent tessellation UVs");
    CKDWORD baseStride = 0, channelStride = 0;
    void *baseUVs = mesh.GetTextureCoordinatesPtr(&baseStride, -1);
    Check(mesh.GetTextureCoordinatesPtr(&channelStride, 0) == baseUVs && channelStride == baseStride,
          "Patch SAMEUV channel does not render with base coordinates");
    Chunk saved(mesh.Save(nullptr, CK_STATESAVE_ALL), &DeleteCKStateChunk);
    RCKPatchMesh loaded(&context, "ReloadedSameUVPatch");
    LoadChunk(loaded, saved.get());
    loaded.GetVertexTextureCoordinates(2, &u, &v, -1);
    Check(u == 1.0f && v == 1.0f, "Patch SAMEUV base coordinates changed after Save/Load");
}

void AnimationLengthUpdatesExistingControllers() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation owner(&context, "LengthOwner"), alias(&context, "LengthAlias");
    const CKANIMATION_CONTROLLER types[] = {CKANIMATION_LINPOS_CONTROL, CKANIMATION_LINROT_CONTROL,
        CKANIMATION_LINSCL_CONTROL, CKANIMATION_LINSCLAXIS_CONTROL, CKANIMATION_MORPH_CONTROL};
    for (CKANIMATION_CONTROLLER type : types) owner.CreateController(type);
    alias.ShareDataFrom(&owner);
    alias.SetLength(37.0f);
    CKAnimController *controllers[] = {owner.GetPositionController(), owner.GetRotationController(),
        owner.GetScaleController(), owner.GetScaleAxisController(), owner.GetMorphController()};
    Check(owner.GetLength() == 37.0f, "Shared animation length was not updated");
    for (CKAnimController *controller : controllers)
        Check(controller && controller->GetLength() == 37.0f, "SetLength left an existing controller at its previous length");
    Chunk saved(owner.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
    RCKObjectAnimation loaded(&context, "ReloadedLength");
    LoadChunk(loaded, saved.get());
    Check(loaded.GetLength() == 37.0f && loaded.GetMorphController()->GetLength() == 37.0f,
          "Edited animation length changed after Save/Load");
}

template<bool ClearOwner>
void ClearingAnimationClearsSharedControllers() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation owner(&context, "ClearOwner"), alias(&context, "ClearAlias");
    VxVector position(1.0f, 2.0f, 3.0f);
    owner.AddPositionKey(0.0f, &position);
    owner.CreateController(CKANIMATION_LINROT_CONTROL);
    owner.CreateController(CKANIMATION_LINSCL_CONTROL);
    owner.CreateController(CKANIMATION_LINSCLAXIS_CONTROL);
    owner.CreateController(CKANIMATION_MORPH_CONTROL);
    alias.ShareDataFrom(&owner);
    RCKObjectAnimation &target = ClearOwner ? owner : alias;
    target.SetFlags(CK_OBJECTANIMATION_MERGED);
    target.SetMergeFactor(0.25f);
    target.ClearAll();
    Check(!owner.GetPositionController() && !owner.GetRotationController() && !owner.GetScaleController() &&
          !owner.GetScaleAxisController() && !owner.GetMorphController(), "ClearAll left shared controllers alive");
    Check(alias.GetLength() == 0.0f && owner.GetLength() == 0.0f, "ClearAll did not reset shared length");
    Check(alias.Shared() == &owner && owner.Shared() == &owner, "ClearAll changed shared-data ownership");
    Check(target.GetFlags() == 0 && target.GetMergeFactor() == 0.5f, "ClearAll did not reset animation state");
    Chunk saved(owner.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
    saved->StartRead();
    Check(saved->SeekIdentifier(CK_STATESAVE_OBJANIMCONTROLLERS), "Cleared owner did not save its own data");
    saved->Skip(9);
    Check(saved->ReadFloat() == 0.0f && saved->ReadDword() == 0, "ClearAll still serialized a controller or length");
}

class DestructionTrackedPositionController : public RCKLinearPositionController {
public:
    explicit DestructionTrackedPositionController(int &destroyed) : m_Destroyed(destroyed) {}
    ~DestructionTrackedPositionController() override { ++m_Destroyed; }
private:
    int &m_Destroyed;
};

class AnimationWithTrackedController : public RCKObjectAnimation {
public:
    AnimationWithTrackedController(CKContext *context, int &destroyed) : RCKObjectAnimation(context, "Tracked") {
        m_KeyframeData->m_PositionController = new DestructionTrackedPositionController(destroyed);
    }
};

void ClearAllReleasesControllerStorage() {
    CKContext context(nullptr, 0, 0);
    int destroyed = 0;
    AnimationWithTrackedController animation(&context, destroyed);
    CKAnimController *controller = animation.GetPositionController();
    animation.ClearAll();
    const bool released = destroyed == 1;
    if (!released) delete controller; // Clean up the pre-fix orphan in the failing test.
    Check(released, "ClearAll dropped controller storage without releasing it");
    animation.ClearAll();
    Check(destroyed == 1, "Repeated ClearAll released a controller twice");
}

template<int Version>
void AnimationLoadRestoresSharedOwnership() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation owner(&context, "OriginalOwner"), alias(&context, "LoadedAlias");
    VxVector oldPosition(9.0f, 8.0f, 7.0f);
    owner.AddPositionKey(0.0f, &oldPosition);
    alias.ShareDataFrom(&owner);
    float key[] = {2.0f, 3.0f, 4.0f, 5.0f};
    Chunk chunk = NewChunk(CKCID_OBJECTANIMATION, Version);
    if (Version == 0) {
        chunk->WriteIdentifier(CK_STATESAVE_OBJANIMPOSKEYS);
    } else if (Version == 1) {
        chunk->WriteIdentifier(CK_STATESAVE_OBJANIMNEWDATA);
        for (int i = 0; i < 7; ++i) chunk->WriteFloat(0.0f);
        chunk->WriteInt(0); // morph vertices
        chunk->WriteInt(0); // morph keys
        chunk->WriteDword(0);
        chunk->WriteObject(nullptr);
        chunk->WriteFloat(23.0f);
    } else {
        chunk->WriteIdentifier(CK_STATESAVE_OBJANIMCONTROLLERS);
        for (int i = 0; i < 7; ++i) chunk->WriteFloat(0.0f);
        chunk->WriteDword(0);
        chunk->WriteObject(nullptr);
        chunk->WriteFloat(23.0f);
        chunk->WriteDword(CKANIMATION_LINPOS_CONTROL);
        chunk->WriteDword(5); // count plus one 16-byte key
    }
    if (Version <= 1) chunk->WriteDword(sizeof(key));
    chunk->WriteDword(1);
    chunk->WriteBufferNoSize_LEndian(sizeof(key), key);
    if (Version == 0) {
        chunk->WriteIdentifier(CK_STATESAVE_OBJANIMLENGTH);
        chunk->WriteFloat(23.0f);
    } else if (Version == 1) {
        for (int i = 0; i < 6; ++i) chunk->WriteDword(0); // empty scale, rotation, scale-axis arrays
    } else {
        chunk->WriteDword(0);
    }
    LoadChunk(alias, chunk.get());
    VxVector actual;
    Check(owner.EvaluatePosition(2.0f, actual) && Equal(actual, VxVector(3.0f, 4.0f, 5.0f)),
          "Loading a shared animation did not replace its shared key data");
    CKObjectAnimation *expectedOwner = Version == 0 ? &owner : &alias;
    Check(alias.Shared() == expectedOwner && owner.Shared() == expectedOwner,
          "Animation Load changed ownership differently from the original format");
    Check(alias.GetLength() == 23.0f && alias.GetPositionController()->GetLength() == (Version == 0 ? 0.0f : 23.0f),
          "Animation Load lost the version-specific controller length behavior");
    Chunk saved(alias.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
    saved->StartRead();
    Check(saved->SeekIdentifier(Version == 0 ? CK_STATESAVE_OBJANIMSHARED : CK_STATESAVE_OBJANIMCONTROLLERS),
          "Reloaded animation saved the wrong shared/full representation");
}

// CK2_3D.dll 0x10058C51: shared data wins over controllers, which win
// over the legacy snapshot, regardless of identifier order in the chunk.
void WriteAnimationStateBlock(CKStateChunk *chunk, CKDWORD identifier,
                             RCKObjectAnimation *owner, RCKObjectAnimation *first,
                             RCKObjectAnimation *second, bool merged, float position, float length = 20.0f) {
    chunk->WriteIdentifier(identifier);
    if (identifier == CK_STATESAVE_OBJANIMSHARED) chunk->WriteObject(owner);
    for (int i = 0; i < 7; ++i) chunk->WriteFloat(0.0f);
    if (identifier == CK_STATESAVE_OBJANIMNEWDATA) {
        chunk->WriteInt(0); // morph vertices
        chunk->WriteInt(0); // morph keys
    }
    chunk->WriteDword(merged ? CK_OBJECTANIMATION_MERGED : 0);
    chunk->WriteObject(nullptr);
    if (identifier != CK_STATESAVE_OBJANIMSHARED) chunk->WriteFloat(length);
    if (merged) {
        chunk->WriteFloat(0.25f);
        chunk->WriteObject(first);
        chunk->WriteObject(second);
    }
    if (identifier == CK_STATESAVE_OBJANIMSHARED) return;
    if (identifier == CK_STATESAVE_OBJANIMCONTROLLERS) {
        chunk->WriteDword(CKANIMATION_LINPOS_CONTROL);
        chunk->WriteDword(5);
    } else {
        chunk->WriteDword(16);
    }
    chunk->WriteDword(1);
    chunk->WriteFloat(0.0f);
    chunk->WriteFloat(position);
    chunk->WriteFloat(0.0f);
    chunk->WriteFloat(0.0f);
    if (identifier == CK_STATESAVE_OBJANIMCONTROLLERS) {
        chunk->WriteDword(0);
    } else {
        for (int i = 0; i < 6; ++i) chunk->WriteDword(0);
    }
}

template<bool WithShared, bool ReverseOrder>
void AnimationCombinedBlocksRespectPrecedence() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation owner(&context, "CombinedOwner"), loaded(&context, "CombinedLoaded");
    VxVector sharedPosition(7.0f, 0.0f, 0.0f), oldPosition(-9.0f, 0.0f, 0.0f);
    owner.AddPositionKey(0.0f, &sharedPosition);
    owner.SetLength(31.0f);
    loaded.AddPositionKey(0.0f, &oldPosition);
    Chunk chunk = NewChunk(CKCID_OBJECTANIMATION);
    const CKDWORD identifiers[] = {CK_STATESAVE_OBJANIMNEWDATA,
        CK_STATESAVE_OBJANIMCONTROLLERS, CK_STATESAVE_OBJANIMSHARED};
    const int count = WithShared ? 3 : 2;
    for (int i = 0; i < count; ++i) {
        CKDWORD identifier = identifiers[ReverseOrder ? count - i - 1 : i];
        WriteAnimationStateBlock(chunk.get(), identifier, &owner, nullptr, nullptr,
                                 false, identifier == CK_STATESAVE_OBJANIMCONTROLLERS ? 3.0f : 5.0f);
    }
    LoadChunk(loaded, chunk.get());
    VxVector actual;
    Check(loaded.EvaluatePosition(0.0f, actual) &&
          Equal(actual, VxVector(WithShared ? 7.0f : 3.0f, 0.0f, 0.0f)),
          "Combined animation blocks used the wrong first-load data");
    Check(loaded.Shared() == (WithShared ? &owner : &loaded), "Combined blocks lost shared ownership");
    Check(loaded.GetLength() == (WithShared ? 31.0f : 20.0f), "Combined blocks selected the wrong length");
}

void AnimationMissingSharedOwnerCreatesEmptyData() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation owner(&context, "MissingOwnerOld"), loaded(&context, "MissingOwnerLoaded");
    VxVector oldPosition(8.0f, 0.0f, 0.0f);
    owner.AddPositionKey(0.0f, &oldPosition);
    loaded.ShareDataFrom(&owner);
    Chunk chunk = NewChunk(CKCID_OBJECTANIMATION);
    WriteAnimationStateBlock(chunk.get(), CK_STATESAVE_OBJANIMCONTROLLERS, nullptr,
                             nullptr, nullptr, false, 3.0f);
    WriteAnimationStateBlock(chunk.get(), CK_STATESAVE_OBJANIMSHARED, nullptr,
                             nullptr, nullptr, false, 0.0f);
    LoadChunk(loaded, chunk.get());
    Check(loaded.Shared() == &loaded && !loaded.GetPositionController() && loaded.GetLength() == 100.0f,
          "Missing shared owner must create empty data without falling back to the full block");
    VxVector actual;
    Check(owner.EvaluatePosition(0.0f, actual) && Equal(actual, oldPosition),
          "Missing shared reference changed the old owner's keys");
}

template<int Version, bool EmptyRotation>
void LegacyObjectTransformBlocksPreserveKeysAndReferences() {
    CKContext context(nullptr,0,0);
    RCK3dEntity entity(&context,"LegacyEntity");
    RCKObjectAnimation animation(&context,"LegacyTransforms"), reloaded(&context,"ConvertedTransforms");
    float position[2][4]={{2,1,2,3},{9,-4,5,-6}};
    float scale[2][4]={{3,2,3,4},{10,5,6,7}};
    float rotation[2][5]={{4,0,0,0,1},{11,0.5f,-0.5f,0.5f,0.5f}};
    float axis[2][6]={{5,12345,0,0.6f,0,0.8f},{12,-12345,0,0,0.8f,0.6f}};
    Chunk chunk=NewChunk(CKCID_OBJECTANIMATION,Version);
    if (Version==1) {
        chunk->WriteIdentifier(CK_STATESAVE_OBJANIMNEWDATA);
        chunk->WriteFloat(2); chunk->WriteFloat(-3); chunk->WriteFloat(4);
        for (int i=0; i<4; ++i) chunk->WriteFloat(0);
        chunk->WriteInt(0); chunk->WriteInt(0); // no Morph keys
        chunk->WriteDword(0);
        chunk->WriteObject(&entity);
        chunk->WriteFloat(23);
    }
    if (Version==0) chunk->WriteIdentifier(CK_STATESAVE_OBJANIMPOSKEYS);
    chunk->WriteDword(sizeof(position)); chunk->WriteDword(2);
    chunk->WriteBufferNoSize_LEndian(sizeof(position),position);
    if (Version==0) chunk->WriteIdentifier(CK_STATESAVE_OBJANIMSCLKEYS);
    chunk->WriteDword(sizeof(scale)); chunk->WriteDword(2);
    chunk->WriteBufferNoSize_LEndian(sizeof(scale),scale);
    if (Version==0) chunk->WriteIdentifier(CK_STATESAVE_OBJANIMROTKEYS);
    chunk->WriteDword(EmptyRotation ? 0 : sizeof(rotation)); chunk->WriteDword(EmptyRotation ? 0 : 2);
    if (!EmptyRotation) chunk->WriteBufferNoSize_LEndian(sizeof(rotation),rotation);
    chunk->WriteDword(sizeof(axis)); chunk->WriteDword(2);
    chunk->WriteBufferNoSize_LEndian(sizeof(axis),axis);
    if (Version==0) {
        chunk->WriteIdentifier(CK_STATESAVE_OBJANIMENTITY); chunk->WriteObject(&entity);
        chunk->WriteIdentifier(CK_STATESAVE_OBJANIMLENGTH); chunk->WriteFloat(23);
        chunk->WriteIdentifier(CK_STATESAVE_OBJANIMNEWDATA);
        chunk->WriteFloat(2); chunk->WriteFloat(-3); chunk->WriteFloat(4);
    }
    LoadChunk(animation,chunk.get());
    Check(animation.Get3dEntity()==reinterpret_cast<CK3dEntity *>(&entity) && animation.GetLength()==23,
          "Legacy animation lost entity or length");
    VxVector *root=static_cast<VxVector *>(animation.GetAppData());
    const bool rootMatches=root && Equal(*root,VxVector(2,-3,4));
    delete root; animation.SetAppData(nullptr);
    Check(rootMatches,"Legacy animation root-position transfer changed");
    for (int pass=0; pass<2; ++pass) {
        RCKObjectAnimation &actual=pass ? reloaded : animation;
        CKAnimController *controllers[]={actual.GetPositionController(),actual.GetScaleController(),
            actual.GetRotationController(),actual.GetScaleAxisController()};
        for (int slot=0; slot<4; ++slot) {
            if (slot==2 && EmptyRotation) {
                Check(!controllers[slot],"Empty legacy rotation should not suppress scale-axis or survive as a controller");
                continue;
            }
            Check(controllers[slot] && controllers[slot]->GetKeyCount()==2,"Legacy transform controller missing");
            if (!pass) Check(controllers[slot]->GetLength()==(Version==0 ? 0.0f : 23.0f),
                             "Legacy controller length convention changed");
            const int components=slot<2 ? 4 : 5;
            CKDWORD actualWords[11]={}, expectedWords[11]={};
            expectedWords[0]=2;
            for (int key=0; key<2; ++key) {
                float values[5]={};
                for (int i=0; i<components; ++i)
                    values[i]=slot==0 ? position[key][i] : slot==1 ? scale[key][i] :
                              slot==2 ? rotation[key][i] : axis[key][i ? i+1 : 0];
                std::memcpy(expectedWords+1+key*components,values,components*sizeof(float));
            }
            const int bytes=(1+2*components)*sizeof(CKDWORD);
            Check(controllers[slot]->DumpKeysTo(nullptr)==bytes,"Legacy transform converted key size changed");
            controllers[slot]->DumpKeysTo(actualWords);
            Check(std::memcmp(actualWords,expectedWords,bytes)==0,"Legacy transform conversion changed time or key components");
        }
        if (!pass) {
            Chunk saved(animation.Save(nullptr,CK_STATESAVE_OBJANIMALL),&DeleteCKStateChunk);
            saved->StartRead();
            Check(reloaded.Load(saved.get(),nullptr)==CK_OK,"Converted transform reload failed");
        }
    }
}

template<bool Legacy>
void AnimationBaseSettingsAndSelectiveSnapshot() {
    CKContext context(nullptr,0,0);
    RCKAnimation animation(&context,"Settings"), loaded(&context,"Snapshot");
    for (int mask=0; mask<4; ++mask) {
        animation.SetFlags(CKANIMATION_ALIGNORIENTATION);
        Chunk chunk=NewChunk(CKCID_ANIMATION,Legacy ? 0 : 10);
        chunk->WriteIdentifier(CK_STATESAVE_ANIMATIONDATA);
        const CKDWORD flags=CKANIMATION_ALIGNORIENTATION | ((mask & 1) ? CKANIMATION_LINKTOFRAMERATE : 0) |
                            ((mask & 2) ? CKANIMATION_CANBEBREAK : 0);
        if (Legacy) { chunk->WriteInt((mask & 2)!=0); chunk->WriteInt((mask & 1)!=0); }
        else chunk->WriteDword(flags);
        chunk->WriteFloat(24);
        chunk->WriteIdentifier(CK_STATESAVE_ANIMATIONLENGTH); chunk->WriteFloat(37);
        chunk->WriteIdentifier(CK_STATESAVE_ANIMATIONCURRENTSTEP); chunk->WriteFloat(0.375f);
        LoadChunk(animation,chunk.get());
        Check(animation.GetFlags()==flags && animation.GetLinkedFrameRate()==24,"Animation settings conversion changed");
        Check(animation.GetLength()==37 && animation.GetStep()==0.375f,"Animation length/step loading changed");
        Chunk saved(animation.Save(nullptr,CK_STATESAVE_ANIMATIONLENGTH),&DeleteCKStateChunk);
        saved->StartRead();
        Check(saved->SeekIdentifier(CK_STATESAVE_ANIMATIONLENGTH),"Selective animation Save omitted length");
        Check(!saved->SeekIdentifier(CK_STATESAVE_ANIMATIONDATA) &&
              !saved->SeekIdentifier(CK_STATESAVE_ANIMATIONCURRENTSTEP),"Selective animation Save included unrelated settings");
        loaded.SetFlags(flags); loaded.SetStep(0.625f);
        saved->StartRead();
        Check(loaded.Load(saved.get(),nullptr)==CK_OK,"Selective animation reload failed");
        Check(loaded.GetLength()==37 && loaded.GetFlags()==flags && loaded.GetStep()==0.625f,
              "Partial animation Load overwrote absent state");
    }
}

class ObjectAnimationParentProbe : public RCKObjectAnimation {
public:
    explicit ObjectAnimationParentProbe(CKContext *context) : RCKObjectAnimation(context,"NestedObject") {}
    RCKKeyedAnimation *ParentAnimation() const { return m_ParentKeyedAnimation; }
};

void KeyedAnimationSnapshotRestoresSubanimationAndRootTransfer() {
    CKContext context(nullptr,0,0);
    RCK3dEntity entity(&context,"RootEntity");
    ObjectAnimationParentProbe object(&context);
    RCKKeyedAnimation animation(&context,"NestedAnimation");
    Chunk sub=NewChunk(CKCID_OBJECTANIMATION,0);
    sub->WriteIdentifier(CK_STATESAVE_OBJANIMENTITY); sub->WriteObject(&entity);
    sub->WriteIdentifier(CK_STATESAVE_OBJANIMNEWDATA);
    sub->WriteFloat(2); sub->WriteFloat(-3); sub->WriteFloat(4);
    sub->WriteIdentifier(CK_STATESAVE_OBJANIMPOSKEYS);
    float position[4]={5,7,8,9};
    sub->WriteDword(sizeof(position)); sub->WriteDword(1);
    sub->WriteBufferNoSize_LEndian(sizeof(position),position);
    sub->CloseChunk();
    Chunk chunk=NewChunk(CKCID_KEYEDANIMATION);
    chunk->WriteIdentifier(CK_STATESAVE_KEYEDANIMANIMLIST);
    XObjectPointerArray objects; objects.PushBack(&object); objects.Save(chunk.get());
    chunk->WriteIdentifier(CK_STATESAVE_KEYEDANIMSUBANIMS);
    chunk->WriteDword(1); chunk->WriteObject(&object); chunk->WriteSubChunk(sub.get());
    animation.SetFlags(CKANIMATION_SUBANIMSSORTED);
    LoadChunk(animation,chunk.get());
    VxVector actual;
    Check(object.EvaluatePosition(5,actual) && Equal(actual,VxVector(7,8,9)),"Keyed snapshot did not restore nested object data");
    Check(object.ParentAnimation()==&animation,"Nested animation parent was not restored");
    Check(object.GetAppData()==nullptr && Equal(animation.GetRootVectorInternal(),VxVector(2,-3,4)),
          "Keyed snapshot did not consume root-position transfer");
    Check(animation.GetRootEntity()==reinterpret_cast<CK3dEntity *>(&entity),"Keyed snapshot did not restore root entity");
    Check(!(animation.GetFlags() & CKANIMATION_SUBANIMSSORTED),"Keyed snapshot kept stale sorting state");
}

void KeyedAnimationMergeStateAndSelectiveSnapshots() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation first(&context, "MergeStateFirst"), second(&context, "MergeStateSecond");
    RCKKeyedAnimation animation(&context, "MergeState"), loaded(&context, "MergeStateLoaded");
    animation.AddAnimation(&first);
    animation.AddAnimation(&second);
    Check(!animation.IsMerged() && animation.GetMergeFactor() == 0.5f,
          "New keyed animation API does not expose its default merge state");
    Chunk chunk = NewChunk(CKCID_KEYEDANIMATION);
    chunk->WriteIdentifier(CK_STATESAVE_KEYEDANIMMERGE);
    chunk->WriteInt(TRUE);
    chunk->WriteFloat(0.25f);
    LoadChunk(animation, chunk.get());
    CKAnimation *api = &animation;
    Check(api->IsMerged() && api->GetMergeFactor() == 0.25f,
          "Keyed animation API does not expose the loaded merge state");
    Check(first.GetMergeFactor() == 0.5f && second.GetMergeFactor() == 0.5f,
          "Keyed Load must restore its own factor without overwriting child factors");
    const float factors[] = {-0.25f, 0.0f, 0.75f, 1.25f};
    for (int i = 0; i < 4; ++i) {
        api->SetMergeFactor(factors[i]);
        Check(api->GetMergeFactor() == factors[i] && first.GetMergeFactor() == factors[i] &&
              second.GetMergeFactor() == factors[i], "Keyed factor change did not reach all subanimations");
    }
    animation.SetLength(47.0f);
    Chunk saved(animation.Save(nullptr, CK_STATESAVE_KEYEDANIMMERGE), &DeleteCKStateChunk);
    saved->StartRead();
    Check(saved->SeekIdentifier(CK_STATESAVE_KEYEDANIMMERGE) && saved->ReadInt() == TRUE &&
          saved->ReadFloat() == 1.25f, "Selective merge snapshot wrote the wrong merge fields");
    Check(!saved->SeekIdentifier(CK_STATESAVE_KEYEDANIMANIMLIST) &&
          !saved->SeekIdentifier(CK_STATESAVE_KEYEDANIMSUBANIMS), "Merge-only save included unrequested children");
    loaded.SetLength(13.0f);
    LoadChunk(loaded, saved.get());
    Check(loaded.IsMerged() && loaded.GetMergeFactor() == 1.25f && loaded.GetLength() == 13.0f,
          "Merge-only Load changed absent length or lost merge state");
    Chunk lengthOnly(animation.Save(nullptr, CK_STATESAVE_ANIMATIONLENGTH), &DeleteCKStateChunk);
    LoadChunk(loaded, lengthOnly.get());
    Check(loaded.IsMerged() && loaded.GetMergeFactor() == 1.25f && loaded.GetLength() == 47.0f,
          "Length-only Load overwrote absent merge state");
}

template<bool FileLoad>
void KeyedAnimationMultipleSnapshotsSkipMissingObjects() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation first(&context, "MultiFirst"), second(&context, "MultiSecond");
    RCKKeyedAnimation animation(&context, "MultipleSnapshots");
    VxVector firstPosition(1.0f, 2.0f, 3.0f), secondPosition(4.0f, 5.0f, 6.0f), changed(9.0f);
    first.AddPositionKey(0.0f, &firstPosition);
    second.AddPositionKey(0.0f, &secondPosition);
    Chunk firstSaved(first.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
    Chunk secondSaved(second.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
    first.ClearAll();
    second.ClearAll();
    first.AddPositionKey(0.0f, &changed);
    second.AddPositionKey(0.0f, &changed);
    Chunk chunk = NewChunk(CKCID_KEYEDANIMATION);
    chunk->WriteIdentifier(CK_STATESAVE_KEYEDANIMANIMLIST);
    XObjectPointerArray objects;
    objects.PushBack(&second);
    objects.PushBack(&first);
    objects.Save(chunk.get());
    chunk->WriteIdentifier(CK_STATESAVE_KEYEDANIMSUBANIMS);
    chunk->WriteDword(3);
    chunk->WriteObject(&first); chunk->WriteSubChunk(firstSaved.get());
    chunk->WriteObject(nullptr); chunk->WriteSubChunk(firstSaved.get());
    chunk->WriteObject(&second); chunk->WriteSubChunk(secondSaved.get());
    CKFile file(&context);
    chunk->CloseChunk();
    chunk->StartRead();
    Check(animation.Load(chunk.get(), FileLoad ? &file : nullptr) == CK_OK, "Multiple snapshot Load failed");
    Check(animation.GetAnimationCount() == 2 && animation.GetAnimation(0) == &second && animation.GetAnimation(1) == &first,
          "Multiple snapshot changed the explicit child list order");
    VxVector actual;
    Check(first.EvaluatePosition(0.0f, actual) && Equal(actual, FileLoad ? changed : firstPosition),
          "Multiple snapshot restored the wrong first child");
    Check(second.EvaluatePosition(0.0f, actual) && Equal(actual, FileLoad ? changed : secondPosition),
          "Missing snapshot object prevented the following child's restoration");
}

class CharacterStateProbe : public RCKCharacter {
public:
    explicit CharacterStateProbe(CKContext *context) : RCKCharacter(context,"SnapshotCharacter") {}
    bool HasReferences(RCKBodyPart *root, RCKKeyedAnimation *active, RCKAnimation *next, RCK3dEntity *floor) const {
        return m_RootBodyPart==root && m_ActiveAnimation==active && m_AnimDest==next && m_FloorRef==floor;
    }
};

template<int Version>
void CharacterSnapshotRestoresEmbeddedBodyAndAnimationReferences() {
    CKContext context(nullptr,0,0);
    new RCKRenderManager(&context);
    RCKBodyPart body(&context,"SnapshotBody");
    RCK3dEntity floor(&context,"Floor");
    RCKKeyedAnimation active(&context,"Active"), next(&context,"Next");
    CharacterStateProbe character(&context), reloaded(&context);
    VxVector wanted(2,3,4), changed(-7,8,-9), actual;
    body.SetPosition(&wanted);
    Chunk bodyChunk(body.Save(nullptr,CK_STATESAVE_BODYPARTALL),&DeleteCKStateChunk);
    body.SetPosition(&changed);
    Chunk chunk=NewChunk(CKCID_CHARACTER,Version);
    if (Version<5) {
        chunk->WriteIdentifier(CK_STATESAVE_CHARACTERSAVEANIMS);
        chunk->WriteDword(0); chunk->WriteObject(&active); chunk->WriteObject(&next);
        chunk->WriteIdentifier(CK_STATESAVE_CHARACTERSAVEPARTS);
        chunk->WriteDword(1); chunk->WriteObject(&body); chunk->WriteSubChunk(bodyChunk.get());
        chunk->WriteIdentifier(CK_STATESAVE_CHARACTERROOT); chunk->WriteObject(&body);
        chunk->WriteIdentifier(CK_STATESAVE_CHARACTERFLOORREF); chunk->WriteObject(&floor);
    } else {
        chunk->WriteIdentifier(CK_STATESAVE_CHARACTERBODYPARTS);
        XObjectPointerArray bodies; bodies.PushBack(&body); bodies.Save(chunk.get());
        chunk->WriteIdentifier(CK_STATESAVE_CHARACTERSAVEPARTS);
        chunk->StartSubChunkSequence(1); chunk->WriteSubChunkSequence(bodyChunk.get());
        chunk->WriteIdentifier(CK_STATESAVE_CHARACTERONLY);
        chunk->StartObjectIDSequence(4);
        chunk->WriteObjectSequence(&active); chunk->WriteObjectSequence(&next);
        chunk->WriteObjectSequence(&body); chunk->WriteObjectSequence(&floor);
    }
    LoadChunk(character,chunk.get());
    Check(character.HasReferences(&body,&active,&next,&floor),"Character snapshot lost animation/root/floor references");
    body.GetPosition(&actual);
    Check(Equal(actual,wanted),"Character snapshot did not load embedded body transform");
    if (Version>=5) {
        Check(character.GetBodyPartCount()==1,"Character snapshot lost body-part list");
        Chunk saved(character.Save(nullptr,CK_STATESAVE_CHARACTERONLY),&DeleteCKStateChunk);
        body.SetPosition(&changed);
        saved->StartRead();
        Check(reloaded.Load(saved.get(),nullptr)==CK_OK,"Character snapshot reload failed");
        Check(reloaded.HasReferences(&body,&active,&next,&floor),"Character save/reload changed references");
        body.GetPosition(&actual);
        Check(Equal(actual,wanted),"Character save/reload omitted nested body state");
    }
}

class TemporarySerializationFile {
public:
    TemporarySerializationFile() {
#ifdef _WIN32
        char directory[MAX_PATH];
        const DWORD length = GetTempPathA(MAX_PATH, directory);
        Check(length > 0 && length < MAX_PATH, "Temporary directory unavailable");
        Check(GetTempFileNameA(directory, "ckr", 0, Path) != 0, "Temporary file creation failed");
#else
        strcpy(Path, "/tmp/ckre-serialization-XXXXXX");
        const int handle = mkstemp(Path);
        Check(handle >= 0, "Temporary file creation failed");
        close(handle);
#endif
    }
    ~TemporarySerializationFile() { remove(Path); }
    char Path[1024];
private:
    TemporarySerializationFile(const TemporarySerializationFile &) = delete;
    TemporarySerializationFile &operator=(const TemporarySerializationFile &) = delete;
};

CK_INITINSTANCEFCT InitializeGridPlugin = nullptr;

CKGridManager *InitializeRuntimeGridContext(CKContext &context) {
    new RCKRenderManager(&context);
    Check(InitializeGridPlugin && InitializeGridPlugin(&context) == CK_OK, "Grids plugin initialization failed");
    CKGridManager *manager = static_cast<CKGridManager *>(context.GetManagerByGuid(GRID_MANAGER_GUID));
    Check(manager && manager->OnCKInit() == CK_OK, "Runtime GridManager initialization failed");
    return manager;
}

class GridFileWithoutManagerMarker : public CKFile {
public:
    explicit GridFileWithoutManagerMarker(CKContext *context) : CKFile(context) {}
    CKERROR LoadWithoutGridMarker(CKSTRING path) {
        CKERROR error = OpenFile(path);
        if (error != CK_OK) return error;
        error = ReadFileData(&m_Parser);
        if (error != CK_OK) return error;
        // Exercise the old-file fallback at the parsed-record boundary. All
        // objects/chunks still come from the real file reader, without ID stubs.
        for (int i = 0; i < m_ManagersData.Size(); ++i) {
            if (m_ManagersData[i].Manager == GRID_MANAGER_GUID) {
                DeleteCKStateChunk(m_ManagersData[i].data);
                m_ManagersData.RemoveAt(i);
                break;
            }
        }
        m_ReadFileDataDone = TRUE;
        return LoadFileData(nullptr);
    }
};

template<bool ExistingTypes, bool Compressed, bool OmitMarker = false>
void RuntimeGridFileRestoresTypeParameters() {
    CKContext source(nullptr, 0, 0), destination(nullptr, 0, 0);
    CKGridManager *sourceManager = InitializeRuntimeGridContext(source);
    CKGridManager *destinationManager = InitializeRuntimeGridContext(destination);
    sourceManager->RegisterType("UnusedSourceType");
    if (ExistingTypes) {
        destinationManager->RegisterType("Linker");
        destinationManager->RegisterType("Intensity");
        destinationManager->RegisterType("Hidden");
    }
    RCKGrid grid(&source, "RuntimeGrid");
    PopulateGrid(grid, *sourceManager);
    const char *names[] = {"Intensity", "Linker", "Hidden"};
    CKParameterLocal *parameters[3];
    for (int i = 0; i < 3; ++i) {
        parameters[i] = source.CreateCKParameterLocal(names[i], CKPGUID_LAYERTYPE);
        Check(parameters[i] != nullptr, "Layer-type parameter creation failed");
        const int type = sourceManager->GetTypeFromName(names[i]);
        Check(parameters[i]->SetValue(&type, sizeof(type)) == CK_OK, "Layer-type parameter setup failed");
    }
    TemporarySerializationFile temporary;
    source.SetFileWriteMode(Compressed ? CKFILE_WHOLECOMPRESSED : CKFILE_UNCOMPRESSED);
    CKFile saved(&source);
    GridFileWithoutManagerMarker loaded(&destination);
    Check(saved.StartSave(temporary.Path) == CK_OK, "Runtime Grid StartSave failed");
    saved.SaveObject(&grid);
    for (int i = 0; i < 3; ++i) saved.SaveObject(parameters[i]);
    Check(saved.EndSave() == CK_OK, "Runtime Grid EndSave failed");
    Check((OmitMarker ? loaded.LoadWithoutGridMarker(temporary.Path) : loaded.Load(temporary.Path, nullptr)) == CK_OK,
          "Runtime Grid Load failed");
    RCKGrid *newGrid = nullptr;
    int checkedParameters = 0;
    for (int i = 0; i < loaded.m_FileObjects.Size(); ++i) {
        CKFileObject &entry = loaded.m_FileObjects[i];
        if (entry.ObjectCid == CKCID_GRID) newGrid = static_cast<RCKGrid *>(entry.ObjPtr);
        if (entry.ObjectCid == CKCID_PARAMETERLOCAL) {
            CKParameterLocal *parameter = static_cast<CKParameterLocal *>(entry.ObjPtr);
            Check(parameter != nullptr, "Runtime Grid file lost a type parameter");
            int actual = -1;
            Check(parameter->GetValue(&actual) == CK_OK, "Loaded layer-type parameter unavailable");
            const int expected = destinationManager->GetTypeFromName(entry.Name);
            if (actual != expected)
                printf("Layer type %s: expected %d, got %d\n", entry.Name, expected, actual);
            Check(expected >= 2 && actual == expected, "Layer-type parameter was not remapped exactly once");
            ++checkedParameters;
        }
    }
    Check(checkedParameters == 3 && newGrid, "Runtime Grid file lost required objects");
    Check(newGrid->GetWidth() == 2 && newGrid->GetLength() == 2 && newGrid->GetLayerCount() == 3,
          "Runtime Grid dimensions/layers changed");
    for (int i = 0; i < 3; ++i) {
        CKLayer *layer = newGrid->GetLayerByIndex(i);
        Check(layer && layer->GetOwner() == newGrid->GetID(), "Runtime layer lost its owner");
        Check(layer->GetType() == destinationManager->GetTypeFromName(names[i]), "Runtime layer type changed");
        for (int j = 0; j < 4; ++j)
            Check(layer->GetSquareArray()[j].ival == grid.GetLayerByIndex(i)->GetSquareArray()[j].ival,
                  "Runtime layer cell values changed");
    }
    CheckGridDisplay(*newGrid);
}

class SerializationDataManager : public CKBaseManager {
public:
    SerializationDataManager(CKContext *context, int index)
        : CKBaseManager(context, CKGUID(0x53415645, 0x1200 + index), "Serialization data fixture"),
          Emit(true), Saved(0), Loaded(0), Value(100 + index), Received(0),
          Reference(nullptr), ReceivedReference(nullptr) {
        Check(context->RegisterNewManager(this) == CK_OK, "Fixture manager registration failed");
    }
    CKStateChunk *SaveData(CKFile *file) override {
        ++Saved;
        if (!Emit) return nullptr;
        CKStateChunk *chunk = CreateCKStateChunk(CKCID_OBJECT, file);
        chunk->StartWrite();
        chunk->WriteIdentifier(0x120000);
        for (int i = 0; i < 128; ++i) chunk->WriteInt(Value);
        chunk->WriteObject(Reference);
        chunk->CloseChunk();
        return chunk;
    }
    CKERROR LoadData(CKStateChunk *chunk, CKFile *) override {
        ++Loaded;
        Check(chunk != nullptr, "Manager received a missing chunk");
        chunk->StartRead();
        Check(chunk->SeekIdentifier(0x120000), "Manager payload identifier missing");
        Received = chunk->ReadInt();
        for (int i = 1; i < 128; ++i)
            Check(chunk->ReadInt() == Received, "Manager payload truncated or shifted");
        ReceivedReference = chunk->ReadObject(m_Context);
        return CK_OK;
    }
    bool Emit;
    int Saved, Loaded, Value, Received;
    CKObject *Reference, *ReceivedReference;
};

template<int Omitted, bool Compressed>
void FileSavePreservesSparseManagerChunks() {
    CKContext context(nullptr, 0, 0);
    SerializationDataManager *managers[3];
    for (int i = 0; i < 3; ++i) managers[i] = new SerializationDataManager(&context, i);
    // GetManager enumerates a hash table, so choose the omitted manager by its
    // actual position rather than assuming registration order.
    int position = 0;
    for (int i = 0; i < context.GetManagerCount(); ++i) {
        CKBaseManager *manager = context.GetManager(i);
        if (manager->GetGuid().d1 == 0x53415645) {
            if (position == Omitted) static_cast<SerializationDataManager *>(manager)->Emit = false;
            ++position;
        }
    }
    TemporarySerializationFile temporary;
    context.SetFileWriteMode(Compressed ? CKFILE_WHOLECOMPRESSED : CKFILE_UNCOMPRESSED);
    CKFile saved(&context);
    Check(saved.StartSave(temporary.Path) == CK_OK, "Manager StartSave failed");
    Check(saved.EndSave() == CK_OK, "Manager EndSave failed");
    // Check the file records independently before invoking the normal loader.
    // The native v8 layout is GUID, byte count, then exactly that many bytes.
    {
        VxMemoryMappedFile mapped(temporary.Path);
        Check(mapped.GetErrorType() == VxMMF_NoError, "Saved manager file unavailable");
        const int fileSize = (int)mapped.GetFileSize();
        Check(fileSize >= 64, "Saved file header truncated");
        CKDWORD header[16];
        memcpy(header, mapped.GetBase(), sizeof(header));
        Check(64 + header[7] + header[8] == (CKDWORD)fileSize, "File section sizes changed");
        const int dataSize = (int)header[9];
        const char *bytes = static_cast<const char *>(mapped.GetBase()) + 64 + header[7];
        XArray<char> unpacked;
        if (Compressed) {
            Check(header[8] < header[9], "Compressible fixture did not exercise data compression");
            char *decoded = CKUnPackData(dataSize, bytes, header[8]);
            Check(decoded != nullptr, "Saved manager data could not be unpacked");
            unpacked.Resize(dataSize);
            memcpy(unpacked.Begin(), decoded, dataSize);
            delete[] decoded;
            bytes = unpacked.Begin();
        }
        int cursor = 0;
        int found = 0;
        for (CKDWORD i = 0; i < header[10]; ++i) {
            CKGUID guid;
            Check(cursor >= 0 && cursor <= dataSize - 12, "Manager record header truncated");
            memcpy(&guid, bytes + cursor, sizeof(guid));
            int size;
            memcpy(&size, bytes + cursor + 8, sizeof(size));
            cursor += 12;
            Check(size != 0, "An empty manager record displaced a nonempty manager");
            Check(size > 0 && size <= dataSize - cursor, "Manager size prefix missing or invalid");
            if (guid.d1 == 0x53415645) {
                const int index = (int)guid.d2 - 0x1200;
                Check(index >= 0 && index < 3 && managers[index]->Emit, "Saved an omitted manager");
                ++found;
            }
            cursor += size;
        }
        Check(found == 2, "Sparse manager collection discarded a later nonempty chunk");
        Check(cursor == dataSize, "Manager records do not fill the declared file data");
    }
    CKFile loaded(&context);
    Check(loaded.Load(temporary.Path, nullptr) == CK_OK, "Saved manager file could not be loaded");
    for (int i = 0; i < 3; ++i) {
        Check(managers[i]->Saved == 1, "Manager SaveData called an unexpected number of times");
        Check(managers[i]->Loaded == (managers[i]->Emit ? 1 : 0), "Manager LoadData dispatch lost or added a record");
        if (managers[i]->Emit)
            Check(managers[i]->Received == managers[i]->Value, "Manager data did not survive the full file cycle");
    }
}

template<bool ReferencedFirst, bool Compressed>
void FullFileMaterialTextureDependencies() {
    CKContext source(nullptr, 0, 0), destination(nullptr, 0, 0);
    new RCKRenderManager(&source);
    new RCKRenderManager(&destination);
    RCKTexture texture(&source, "FullFileTexture");
    RCKMaterial material(&source, "FullFileMaterial");
    SerializationDataManager *savedManager = new SerializationDataManager(&source, 3);
    SerializationDataManager *loadedManager = new SerializationDataManager(&destination, 3);
    savedManager->Reference = &texture;
    material.SetTexture(0, &texture);
    material.SetPower(37.5f);
    // Force destination IDs to differ from source IDs.
    for (int i = 0; i < 7; ++i) destination.CreateObject(CKCID_OBJECT, nullptr);
    TemporarySerializationFile temporary;
    source.SetFileWriteMode(Compressed ? CKFILE_WHOLECOMPRESSED : CKFILE_UNCOMPRESSED);
    CKFile saved(&source), loaded(&destination);
    for (int pass = 0; pass < 2; ++pass) {
        Check(saved.StartSave(temporary.Path) == CK_OK, "Material StartSave failed");
        if (ReferencedFirst) saved.SaveObjectAsReference(&texture);
        saved.SaveObject(&material);
        Check(saved.m_FileObjects.Size() == 2, "Actual file save did not collect the texture");
        Check(saved.EndSave() == CK_OK, "Material EndSave failed");
        Check(loaded.Load(temporary.Path, nullptr) == CK_OK, "Material full-file reload failed");
        RCKMaterial *newMaterial = nullptr;
        RCKTexture *newTexture = nullptr;
        for (int i = 0; i < loaded.m_FileObjects.Size(); ++i) {
            CKFileObject &entry = loaded.m_FileObjects[i];
            Check(entry.CreatedObject != entry.Object, "Fixture did not exercise object-ID remapping");
            if (entry.ObjectCid == CKCID_MATERIAL) newMaterial = static_cast<RCKMaterial *>(entry.ObjPtr);
            if (entry.ObjectCid == CKCID_TEXTURE) newTexture = static_cast<RCKTexture *>(entry.ObjPtr);
        }
        Check(newMaterial && newTexture, "Full file lost material or texture object");
        Check(newMaterial->GetTexture() == newTexture, "Full file did not remap the material texture dependency");
        Check(newMaterial->GetPower() == material.GetPower(), "Full file changed material state");
        Check(loadedManager->Loaded == pass + 1 && loadedManager->Received == savedManager->Value,
              "Combined manager/object file lost manager state");
        Check(loadedManager->ReceivedReference == newTexture, "Manager chunk object reference was not remapped");
        material.SetPower(61.25f);
        ++savedManager->Value;
    }
}

template<bool AliasFirst, bool Compressed>
void FullFileSharedAnimationControllers() {
    CKContext source(nullptr, 0, 0), destination(nullptr, 0, 0);
    RCKObjectAnimation owner(&source, "FullOwner"), alias(&source, "FullAlias");
    VxVector wanted(2.0f, -3.0f, 4.0f);
    owner.AddPositionKey(0.0f, &wanted);
    owner.SetLength(20.0f);
    Check(alias.ShareDataFrom(&owner), "Cannot share full-file animation data");
    RCKObjectAnimation *objects[] = {AliasFirst ? &alias : &owner, AliasFirst ? &owner : &alias};
    for (int i = 0; i < 7; ++i) destination.CreateObject(CKCID_OBJECT, nullptr);
    TemporarySerializationFile temporary;
    source.SetFileWriteMode(Compressed ? CKFILE_WHOLECOMPRESSED : CKFILE_UNCOMPRESSED);
    CKFile saved(&source), loaded(&destination);
    Check(saved.StartSave(temporary.Path) == CK_OK, "Animation StartSave failed");
    saved.SaveObject(objects[0]); saved.SaveObject(objects[1]);
    Check(saved.EndSave() == CK_OK, "Animation EndSave failed");
    Check(loaded.Load(temporary.Path, nullptr) == CK_OK, "Animation full-file reload failed");
    Check(loaded.m_FileObjects.Size() == 2, "Full file lost shared animation objects");
    RCKObjectAnimation *animations[2];
    for (int i = 0; i < 2; ++i) {
        CKFileObject &entry = loaded.m_FileObjects[i];
        Check(entry.ObjectCid == CKCID_OBJECTANIMATION && entry.ObjPtr, "Full file did not create animations");
        animations[i] = static_cast<RCKObjectAnimation *>(entry.ObjPtr);
        VxVector actual;
        Check(animations[i]->EvaluatePosition(0.0f, actual) && Equal(actual, wanted), "Full file lost shared controller keys");
        Check(animations[i]->GetLength() == 20.0f, "Full file lost shared animation length");
    }
    Check(animations[0]->GetPositionController() == animations[1]->GetPositionController(),
          "Full file duplicated or disconnected shared controllers");
}

template<bool Compressed>
void FullFileKeyedAnimationCollectsChildren() {
    CKContext source(nullptr, 0, 0), destination(nullptr, 0, 0);
    RCKKeyedAnimation animation(&source, "KeyedFile");
    RCKObjectAnimation first(&source, "KeyedFileFirst"), second(&source, "KeyedFileSecond");
    VxVector firstPosition(2.0f, -3.0f, 4.0f), secondPosition(10.0f, 5.0f, 12.0f);
    first.AddPositionKey(0.0f, &firstPosition);
    second.AddPositionKey(0.0f, &secondPosition);
    animation.AddAnimation(&second);
    animation.AddAnimation(&first);
    Chunk mergeState = NewChunk(CKCID_KEYEDANIMATION);
    mergeState->WriteIdentifier(CK_STATESAVE_KEYEDANIMMERGE);
    mergeState->WriteInt(TRUE);
    mergeState->WriteFloat(0.25f);
    LoadChunk(animation, mergeState.get());
    animation.SetLength(20.0f);
    animation.SetMergeFactor(0.75f);
    for (int i = 0; i < 7; ++i) destination.CreateObject(CKCID_OBJECT, nullptr);
    TemporarySerializationFile temporary;
    source.SetFileWriteMode(Compressed ? CKFILE_WHOLECOMPRESSED : CKFILE_UNCOMPRESSED);
    CKFile saved(&source), loaded(&destination);
    Check(saved.StartSave(temporary.Path) == CK_OK, "Keyed file StartSave failed");
    saved.SaveObject(&animation); // PreSave must collect both children itself.
    Check(saved.EndSave() == CK_OK, "Keyed file EndSave failed");
    Check(loaded.Load(temporary.Path, nullptr) == CK_OK, "Keyed full-file Load failed");
    Check(loaded.m_FileObjects.Size() == 3, "Keyed PreSave failed to collect both children");
    RCKKeyedAnimation *newAnimation = nullptr;
    RCKObjectAnimation *newFirst = nullptr, *newSecond = nullptr;
    for (int i = 0; i < loaded.m_FileObjects.Size(); ++i) {
        CKFileObject &entry = loaded.m_FileObjects[i];
        Check(entry.CreatedObject != entry.Object, "Keyed file fixture did not exercise ID remapping");
        if (entry.Object == animation.GetID()) newAnimation = static_cast<RCKKeyedAnimation *>(entry.ObjPtr);
        if (entry.Object == first.GetID()) newFirst = static_cast<RCKObjectAnimation *>(entry.ObjPtr);
        if (entry.Object == second.GetID()) newSecond = static_cast<RCKObjectAnimation *>(entry.ObjPtr);
    }
    Check(newAnimation && newFirst && newSecond, "Keyed full file lost an object");
    Check(newAnimation->IsMerged() && newAnimation->GetMergeFactor() == 0.75f && newAnimation->GetLength() == 20.0f,
          "Keyed full file lost merge state or length");
    Check(newAnimation->GetAnimationCount() == 2 && newAnimation->GetAnimation(0) == newSecond &&
          newAnimation->GetAnimation(1) == newFirst, "Keyed full file lost child order or remapped references");
    VxVector actual;
    Check(newFirst->EvaluatePosition(7.0f, actual) && Equal(actual, firstPosition), "Keyed full file lost first child's keys");
    Check(newSecond->EvaluatePosition(7.0f, actual) && Equal(actual, secondPosition), "Keyed full file lost second child's keys");
    newAnimation->SetMergeFactor(0.125f);
    Check(newFirst->GetMergeFactor() == 0.125f && newSecond->GetMergeFactor() == 0.125f,
          "Reloaded keyed animation does not propagate factor changes to remapped children");
}

template<bool MergedFirst, bool Compressed>
void FullFileMergedAnimationReferences() {
    CKContext source(nullptr, 0, 0), destination(nullptr, 0, 0);
    RCKObjectAnimation *first = static_cast<RCKObjectAnimation *>(source.CreateObject(CKCID_OBJECTANIMATION, "MergeFirst"));
    RCKObjectAnimation *second = static_cast<RCKObjectAnimation *>(source.CreateObject(CKCID_OBJECTANIMATION, "MergeSecond"));
    Check(first && second, "Merged animation sources could not be created");
    VxVector firstKey(2.0f, -3.0f, 4.0f), secondKey(10.0f, 5.0f, 12.0f);
    first->AddPositionKey(0.0f, &firstKey); first->SetLength(20.0f);
    second->AddPositionKey(0.0f, &secondKey); second->SetLength(10.0f);
    RCKObjectAnimation *merged = static_cast<RCKObjectAnimation *>(first->CreateMergedAnimation(second));
    Check(merged != nullptr, "Merged animation could not be created");
    merged->SetMergeFactor(0.25f);
    for (int i = 0; i < 7; ++i) destination.CreateObject(CKCID_OBJECT, nullptr);
    TemporarySerializationFile temporary;
    source.SetFileWriteMode(Compressed ? CKFILE_WHOLECOMPRESSED : CKFILE_UNCOMPRESSED);
    CKFile saved(&source), loaded(&destination);
    Check(saved.StartSave(temporary.Path) == CK_OK, "Merged animation StartSave failed");
    if (MergedFirst) saved.SaveObject(merged);
    saved.SaveObject(first); saved.SaveObject(second);
    if (!MergedFirst) saved.SaveObject(merged);
    Check(saved.EndSave() == CK_OK, "Merged animation EndSave failed");
    Check(loaded.Load(temporary.Path, nullptr) == CK_OK, "Merged animation full-file reload failed");
    Check(loaded.m_FileObjects.Size() == 3, "Full file lost merged animation sources");
    RCKObjectAnimation *newMerged = nullptr, *newFirst = nullptr;
    for (int i = 0; i < loaded.m_FileObjects.Size(); ++i) {
        CKFileObject &entry = loaded.m_FileObjects[i];
        if (entry.Object == merged->GetID()) newMerged = static_cast<RCKObjectAnimation *>(entry.ObjPtr);
        if (entry.Object == first->GetID()) newFirst = static_cast<RCKObjectAnimation *>(entry.ObjPtr);
    }
    Check(newMerged && newFirst && newMerged->IsMerged(), "Full file lost merged state");
    Check(newMerged->GetMergeFactor() == 0.25f && newMerged->GetLength() == 20.0f,
          "Full file changed merge factor or length");
    VxVector actual;
    Check(newMerged->EvaluatePosition(7.0f, actual) && Equal(actual, VxVector(4.0f, -1.0f, 6.0f)),
          "Full file lost merged source references");
    firstKey.x = 6.0f;
    newFirst->AddPositionKey(0.0f, &firstKey);
    Check(newMerged->EvaluatePosition(7.0f, actual) && Equal(actual, VxVector(7.0f, -1.0f, 6.0f)),
          "Merged animation does not follow the reloaded source object");
}

class DependencySaveFile : public CKFile {
public:
    explicit DependencySaveFile(CKContext *context) : CKFile(context) {}
    using CKFile::SaveFindObjectIndex;
};

template<bool ReferencedFirst>
void FilePreSaveCollectsMaterialTextureAndMapsObjectIds() {
    CKContext context(nullptr,0,0);
    new RCKRenderManager(&context);
    RCKTexture texture(&context,"DependencyTexture");
    RCKMaterial material(&context,"DependencyMaterial");
    material.SetTexture(0,reinterpret_cast<CKTexture *>(&texture));
    DependencySaveFile file(&context);
    // Prepare the per-class buckets as StartSave does, without writing a file.
    // All entries and lookup mappings below must come from real SaveObject calls.
    file.m_IndexByClassId.Resize(CKGetClassCount());
    if (ReferencedFirst) {
        file.SaveObjectAsReference(&texture);
        Check(file.SaveFindObjectIndex(texture.GetID())==0,"Referenced object ID was not mapped to its file index");
    }
    file.SaveObject(&material,CK_STATESAVE_ALL);
    Check(file.m_FileObjects.Size()==2,"Material PreSave did not collect exactly its texture dependency");
    for (int i=0; i<file.m_FileObjects.Size(); ++i) {
        CKFileObject &entry=file.m_FileObjects[i];
        Check(file.SaveFindObjectIndex(entry.Object)==i,"SaveObject mapped file indices to IDs instead of IDs to indices");
        Check(entry.SaveFlags==CK_STATESAVE_ALL,"Promoting a reference did not restore full-save flags");
    }
    Check(file.m_ReferencedObjects.Size()==0,"A fully saved dependency remained reference-only");
    file.SaveObject(&material,CK_STATESAVE_ALL);
    Check(file.m_FileObjects.Size()==2,"Repeated dependency save duplicated file objects");
    Chunk chunk(material.Save(&file,CK_STATESAVE_ALL),&DeleteCKStateChunk);
    chunk->StartRead();
    Check(chunk->SeekIdentifier(CK_STATESAVE_MATDATA),"Saved material data missing");
    for (int i=0; i<5; ++i) chunk->ReadDword(); // colors and power
    for (CKFileObject &entry : file.m_FileObjects) entry.CreatedObject=entry.Object;
    Check(chunk->ReadObject(&context)==&texture,"Material texture reference was lost in the saved chunk");
}

class FontStateSpriteText : public RCKSpriteText {
public:
    explicit FontStateSpriteText(CKContext *context) : RCKSpriteText(context,"FontState") {}
    void DropFontHandle() {
        if (m_Font) VxDeleteFont(m_Font);
        m_Font=nullptr;
    }
    bool HasFontState(int size, int weight, CKBOOL italic, CKBOOL underline) const {
        return m_FontSize==size && m_FontWeight==weight && m_FontItalic==italic && m_FontUnderline==underline;
    }
};

template<bool Configured>
void SpriteTextSavePreservesFontWhenHandleIsUnavailable() {
    CKContext context(nullptr,0,0);
    new RCKRenderManager(&context);
    FontStateSpriteText source(&context), loaded(&context);
    const int size=Configured ? 23 : 12, weight=Configured ? 700 : 400;
    const CKBOOL italic=Configured ? TRUE : FALSE, underline=FALSE;
    if (Configured) source.SetFont(const_cast<char *>("Arial"),size,weight,italic,underline);
    source.DropFontHandle();
    source.SetText(const_cast<char *>("Font state"));
    source.SetTextColor(0xff123456u);
    source.SetBackgroundColor(0xffabcdefu);
    Chunk saved(source.Save(nullptr,CK_STATESAVE_ALL),&DeleteCKStateChunk);
    saved->StartRead();
    Check(saved->SeekIdentifier(CK_STATESAVE_SPRITEFONT),"SpriteText font block missing");
    char *name=nullptr;
    saved->ReadString(&name);
    const bool nameMatches=Configured ? name && std::strcmp(name,"Arial")==0 : !name || !*name;
    CKDeletePointer(name);
    Check(nameMatches,"SpriteText lost the requested font name without a handle");
    Check(saved->ReadInt()==size,"SpriteText saved an undefined font size without a handle");
    Check(saved->ReadInt()==weight,"SpriteText saved an undefined font weight without a handle");
    Check(saved->ReadInt()==italic && saved->ReadInt()==underline,"SpriteText font styles changed");
    saved->StartRead();
    Check(loaded.Load(saved.get(),nullptr)==CK_OK,"SpriteText font reload failed");
    Check(loaded.HasFontState(size,weight,italic,underline),"SpriteText font fields were read in reverse order");
    Check(loaded.GetText() && std::strcmp(loaded.GetText(),"Font state")==0,"SpriteText text changed");
    Check(loaded.GetTextColor()==0xff123456u && loaded.GetBackgroundTextColor()==0xffabcdefu,
          "SpriteText colors changed");
}

void SpriteTextLoadsFontFieldsInSaveOrder() {
    CKContext context(nullptr,0,0);
    new RCKRenderManager(&context);
    FontStateSpriteText sprite(&context);
    Chunk chunk=NewChunk(CKCID_SPRITETEXT);
    chunk->WriteIdentifier(CK_STATESAVE_SPRITEFONT);
    chunk->WriteString(const_cast<char *>("Arial"));
    chunk->WriteInt(19); chunk->WriteInt(600); chunk->WriteInt(FALSE); chunk->WriteInt(TRUE);
    LoadChunk(sprite,chunk.get());
    Check(sprite.HasFontState(19,600,FALSE,TRUE),"SpriteText did not preserve font wire field order");
}

// The legacy object loader is distinct from the modern Morph ReadKeysFrom.
// 0x10058C51 constructs zeroed eight-byte entries before copying low words.
template<int NormalFormat>
void LegacyObjectMorphNormalsUseInitializedConversionEntries() {
    const int counts[] = {1, 3, 4, 17};
    for (int count : counts) {
        CKContext context(nullptr, 0, 0);
        RCKObjectAnimation animation(&context, "LegacyMorph"), reloaded(&context, "ModernMorph");
        Chunk chunk = NewChunk(CKCID_OBJECTANIMATION, 1);
        chunk->WriteIdentifier(CK_STATESAVE_OBJANIMNEWDATA);
        for (int i=0; i<7; ++i) chunk->WriteFloat(0.0f);
        chunk->WriteInt(count);
        chunk->WriteInt(2);
        chunk->WriteDword(0);
        chunk->WriteObject(nullptr);
        chunk->WriteFloat(23.0f);
        VxVector positions[17];
        for (int key=0; key<2; ++key) {
            for (int i=0; i<count; ++i) positions[i]=VxVector(float(i),float(key),float(i+key));
            chunk->WriteFloat(float(key*10));
            chunk->WriteDword(count*sizeof(VxVector));
            chunk->WriteBufferNoSize_LEndian(count*sizeof(VxVector),positions);
        }
        for (int i=0; i<8; ++i) chunk->WriteDword(0); // empty transform controllers
        if (NormalFormat & 1) {
            chunk->WriteIdentifier(CK_STATESAVE_OBJANIMMORPHCOMP);
            for (int key=0; key<2; ++key) {
                VxCompressedVector normals[17];
                for (int i=0; i<count; ++i) {
                    normals[i].xa=short(100+key+i); normals[i].ya=short(-200-key-i);
                }
                chunk->WriteDword(count*sizeof(VxCompressedVector));
                chunk->WriteBufferNoSize_LEndian16(count*sizeof(VxCompressedVector),normals);
            }
        }
        if (NormalFormat & 2) {
            chunk->WriteIdentifier(CK_STATESAVE_OBJANIMMORPHNORMALS);
            for (int key=0; key<2; ++key) {
                CKDWORD normals[17];
                for (int i=0; i<count; ++i) normals[i]=0x77888000u+key*32+i;
                chunk->WriteDword(count*sizeof(CKDWORD));
                chunk->WriteBufferNoSize_LEndian(count*sizeof(CKDWORD),normals);
            }
        }
        LoadChunk(animation,chunk.get());
        // Check first-load values against the wire contract, then their modern save.
        for (int pass=0; pass<2; ++pass) {
            RCKObjectAnimation &actual=pass ? reloaded : animation;
            CKMorphController *controller=actual.GetMorphController();
            Check(controller && controller->GetKeyCount()==2, "Legacy Morph keys missing");
            Check(actual.GetMorphVertexCount()==count, "Legacy Morph vertex count changed");
            Check(actual.GetLength()==23.0f, "Legacy Morph animation length changed");
            for (int key=0; key<2; ++key) {
                CKMorphKey *loaded=static_cast<CKMorphKey *>(controller->GetKey(key));
                Check(loaded->TimeStep==float(key*10), "Legacy Morph key time changed");
                Check((loaded->NormArray!=nullptr)==(NormalFormat!=0), "Legacy Morph normal presence changed");
                for (int i=0; i<count; ++i) {
                    Check(Equal(loaded->PosArray[i],VxVector(float(i),float(key),float(i+key))),
                          "Legacy Morph position conversion changed");
                    if (NormalFormat) {
                        const short xa=(NormalFormat & 2) ? (2*i<count ? short(0x8000+key*32+2*i) : 0) : short(100+key+i);
                        const short ya=(NormalFormat & 2) ? (2*i+1<count ? short(0x8000+key*32+2*i+1) : 0) : short(-200-key-i);
                        Check(loaded->NormArray[i].xa==xa && loaded->NormArray[i].ya==ya,
                              "Legacy Morph normal conversion lost wire values or zero-filled tail");
                    }
                }
            }
            if (!pass) {
                Chunk saved(animation.Save(nullptr,CK_STATESAVE_OBJANIMALL),&DeleteCKStateChunk);
                saved->StartRead();
                Check(reloaded.Load(saved.get(),nullptr)==CK_OK,"Converted Morph reload failed");
            }
        }
    }
}

template<bool Normals>
void MorphClonePreservesIndependentSerializedKeys() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation sourceAnimation(&context, "CloneSource");
    WireWords original = MorphWire<Normals>();
    Chunk originalChunk = AnimationWithController(CKANIMATION_MORPH_CONTROL, original);
    LoadChunk(sourceAnimation, originalChunk.get());
    CKMorphController *source = sourceAnimation.GetMorphController();
    // Exercise both growing and shrinking the destination key array, as well
    // as cloning a loaded controller into a newly created empty destination.
    for (int previousCount : {0, 1, 3}) {
        RCKObjectAnimation destinationAnimation(&context, "CloneDestination");
        auto *destination = static_cast<RCKMorphController *>(destinationAnimation.CreateController(CKANIMATION_MORPH_CONTROL));
        destination->SetMorphVertexCount(1);
        for (int i = 0; i < previousCount; ++i) destination->AddKey(static_cast<float>(i), Normals);
        Check(destination->Clone(source), "Morph Clone failed");
        Check(destination->GetKeyCount() == 2 && destination->GetMorphVertexCount() == 2 &&
              destination->GetLength() == source->GetLength(), "Morph Clone lost controller metadata");
        for (int i = 0; i < 2; ++i) {
            auto *sourceKey = static_cast<CKMorphKey *>(source->GetKey(i));
            auto *copyKey = static_cast<CKMorphKey *>(destination->GetKey(i));
            Check(copyKey->PosArray != sourceKey->PosArray && (!Normals || copyKey->NormArray != sourceKey->NormArray),
                  "Morph Clone still aliases source key storage");
        }
        CheckSavedController(destinationAnimation, CKANIMATION_MORPH_CONTROL, original);
        auto *copyKey = static_cast<CKMorphKey *>(destination->GetKey(0));
        copyKey->PosArray[0].x += 7.0f;
        if (Normals) copyKey->NormArray[0].xa += 3;
        CheckSavedController(sourceAnimation, CKANIMATION_MORPH_CONTROL, original);
        Chunk saved(destinationAnimation.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
        RCKObjectAnimation reloaded(&context, "ReloadedClone");
        LoadChunk(reloaded, saved.get());
        Check(destination->Compare(reloaded.GetMorphController()), "Cloned Morph keys changed after Save/Load");

        RCKMorphController empty;
        empty.SetLength(11.0f);
        Check(destination->Clone(&empty) && destination->GetKeyCount() == 0 && destination->GetLength() == 11.0f,
              "Cloning an empty Morph controller did not clear the destination");
    }
}

void MorphCloneRejectsDifferentControllerType() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation animation(&context, "MorphTypeCheck");
    WireWords original = MorphWire<true>();
    Chunk chunk = AnimationWithController(CKANIMATION_MORPH_CONTROL, original);
    LoadChunk(animation, chunk.get());
    RCKLinearPositionController position;
    Check(!animation.GetMorphController()->Clone(&position), "Morph Clone accepted another controller type");
    CheckSavedController(animation, CKANIMATION_MORPH_CONTROL, original);
}

WireWords TransformControllerWire(CKANIMATION_CONTROLLER type) {
    if (type == CKANIMATION_BEZIERPOS_CONTROL || type == CKANIMATION_BEZIERSCL_CONTROL) {
        CKBezierPositionKey keys[4];
        return BezierWire(keys);
    }
    const bool rotation = (type & CKANIMATION_CONTROLLER_MASK) == CKANIMATION_CONTROLLER_ROT ||
                          (type & CKANIMATION_CONTROLLER_MASK) == CKANIMATION_CONTROLLER_SCLAXIS;
    const bool tcb = type == CKANIMATION_TCBPOS_CONTROL || type == CKANIMATION_TCBSCL_CONTROL ||
                     type == CKANIMATION_TCBROT_CONTROL || type == CKANIMATION_TCBSCLAXIS_CONTROL;
    WireWords words = {2};
    for (int i = 0; i < 2; ++i) {
        const float time = 2.0f + i * 8.0f;
        const float value[] = {1.0f + i, 2.0f, 3.0f};
        const float quaternion[] = {0.0f, 0.0f, i ? 0.6f : 0.0f, i ? 0.8f : 1.0f};
        const float tcbValues[] = {0.25f, -0.5f, 0.75f, 0.2f, 0.4f};
        AppendWire(words, &time, sizeof(time));
        AppendWire(words, rotation ? quaternion : value, rotation ? sizeof(quaternion) : sizeof(value));
        if (tcb) AppendWire(words, tcbValues, sizeof(tcbValues));
    }
    return words;
}

CKAnimController *TransformController(RCKObjectAnimation &animation, CKANIMATION_CONTROLLER type) {
    switch (type & CKANIMATION_CONTROLLER_MASK) {
    case CKANIMATION_CONTROLLER_POS: return animation.GetPositionController();
    case CKANIMATION_CONTROLLER_ROT: return animation.GetRotationController();
    case CKANIMATION_CONTROLLER_SCL: return animation.GetScaleController();
    case CKANIMATION_CONTROLLER_SCLAXIS: return animation.GetScaleAxisController();
    default: throw std::runtime_error("Unexpected transform controller type");
    }
}

template<CKANIMATION_CONTROLLER Type>
void TransformClonePreservesTypedSerializedKeys() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation sourceAnimation(&context, "TransformSource"), destinationAnimation(&context, "TransformCopy");
    WireWords words = TransformControllerWire(Type);
    Chunk sourceChunk = AnimationWithController(Type, words);
    Chunk destinationChunk = AnimationWithController(Type, words);
    LoadChunk(sourceAnimation, sourceChunk.get());
    LoadChunk(destinationAnimation, destinationChunk.get());
    CKAnimController *source = TransformController(sourceAnimation, Type);
    CKAnimController *destination = TransformController(destinationAnimation, Type);
    RCKLinearPositionController otherPosition;
    RCKLinearScaleController otherScale;
    CKAnimController *wrongType = Type == CKANIMATION_LINPOS_CONTROL ?
        static_cast<CKAnimController *>(&otherScale) : static_cast<CKAnimController *>(&otherPosition);
    wrongType->SetLength(7.0f);
    Check(!destination->Clone(wrongType), "Clone accepted a different complete controller type");
    Check(destination->GetLength() == 20.0f, "Rejected Clone changed the destination length");
    CheckSavedController(destinationAnimation, Type, words);

    destinationAnimation.SetLength(37.0f);
    Check(destination->Clone(source) && destination->GetLength() == 20.0f, "Same-type Clone did not copy controller metadata");
    Check(destination->GetKey(0) != source->GetKey(0), "Transform Clone shares key storage");
    CheckSavedController(destinationAnimation, Type, words);
    destination->GetKey(0)->SetTime(-3.0f);
    CheckSavedController(sourceAnimation, Type, words);
    Chunk saved(destinationAnimation.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
    RCKObjectAnimation reloaded(&context, "ReloadedTransformCopy");
    LoadChunk(reloaded, saved.get());
    Check(destination->Compare(TransformController(reloaded, Type)), "Cloned transform keys changed after Save/Load");
}

template<CKANIMATION_CONTROLLER Type>
void LoadedTCBUsesOriginalTimeRemapping() {
    CKContext context(nullptr, 0, 0);
    const bool rotation = Type == CKANIMATION_TCBROT_CONTROL || Type == CKANIMATION_TCBSCLAXIS_CONTROL;
    // Expected normalized times from 0x10050AB1. Include each piece of the
    // mapping, zero-sided easing, and normalization when the sum exceeds one.
    struct Sample { float from, to, time, expected; };
    const Sample samples[] = {
        {0.0f, 0.0f, 0.25f, 0.25f},
        {0.25f, 0.5f, 0.125f, 0.05f},
        {0.25f, 0.5f, 0.25f, 0.2f},
        {0.25f, 0.5f, 0.375f, 0.4f},
        {0.25f, 0.5f, 0.5f, 0.6f},
        {0.25f, 0.5f, 0.75f, 0.9f},
        {0.0f, 0.5f, 0.25f, 1.0f / 3.0f},
        {0.5f, 0.0f, 0.75f, 2.0f / 3.0f},
        {0.75f, 0.75f, 0.25f, 0.125f},
        {0.75f, 0.75f, 0.75f, 0.875f},
        {0.25f, 0.5f, 0.0f, 0.0f},
        {0.25f, 0.5f, 1.0f, 1.0f},
    };
    for (const Sample &sample : samples) {
        RCKObjectAnimation animation(&context, "TCBEase");
        WireWords words = {2};
        for (int i = 0; i < 2; ++i) {
            const float time = i ? 10.0f : 2.0f;
            const float position[] = {i ? 10.0f : 0.0f, 2.0f, 3.0f};
            const float quaternion[] = {0.0f, 0.0f, i ? 0.6f : 0.0f, i ? 0.8f : 1.0f};
            // Unused sides intentionally differ: the segment must use the
            // left key's easefrom and the right key's easeto, in that order.
            const float params[] = {0.0f, 0.0f, 0.0f, i ? sample.to : 0.9f, i ? 0.8f : sample.from};
            AppendWire(words, &time, 4);
            AppendWire(words, rotation ? quaternion : position, rotation ? 16 : 12);
            AppendWire(words, params, sizeof(params));
        }
        Chunk chunk = AnimationWithController(Type, words);
        LoadChunk(animation, chunk.get());
        CKAnimController *controller = TransformController(animation, Type);
        const float time = 2.0f + sample.time * 8.0f;
        if (rotation) {
            VxQuaternion actual, expected;
            Check(controller->Evaluate(time, &actual), "Loaded TCB rotation did not evaluate");
            // Isolate time remapping from the separately implemented tangent
            // generator: evaluate the same curve without easing at the oracle time.
            auto *first = static_cast<CKTCBRotationKey *>(controller->GetKey(0));
            auto *last = static_cast<CKTCBRotationKey *>(controller->GetKey(1));
            first->easeto = first->easefrom = last->easeto = last->easefrom = 0.0f;
            Check(controller->Evaluate(2.0f + sample.expected * 8.0f, &expected), "TCB reference evaluation failed");
            Check(std::fabs(actual.x - expected.x) < 0.00001f && std::fabs(actual.y - expected.y) < 0.00001f &&
                  std::fabs(actual.z - expected.z) < 0.00001f && std::fabs(actual.w - expected.w) < 0.00001f,
                  "TCB quaternion time remapping differs from original");
        } else {
            VxVector actual;
            Check(controller->Evaluate(time, &actual), "Loaded TCB vector did not evaluate");
            Check(std::fabs(actual.x - 10.0f * sample.expected) < 0.00001f &&
                  std::fabs(actual.y - 2.0f) < 0.00001f && std::fabs(actual.z - 3.0f) < 0.00001f,
                  "TCB vector time remapping differs from original");
        }
    }
}

void CheckLinearReference(CKAnimController *controller, const LinearReference::Fixture &fixture) {
    for (int i = 0; i < fixture.sampleCount; ++i) {
        const auto &sample = fixture.samples[i];
        float value[4] = {};
        Check(controller->Evaluate(sample.time, value), "Linear reference evaluation failed");
        for (int component = 0; component < 4; ++component) {
            if (!(std::fabs(value[component] - sample.value[component]) < 5.0e-5f)) {
                std::printf("Linear %s time=%.9g component=%d actual=%.9g expected=%.9g\n",
                            fixture.name, sample.time, component, value[component], sample.value[component]);
                Check(false, "Linear key boundary differs from original runtime");
            }
        }
    }
}

template<CKANIMATION_CONTROLLER Type>
void LoadedLinearMatchesOriginalRuntime() {
    CKContext context(nullptr, 0, 0);
    const bool rotation = Type == CKANIMATION_LINROT_CONTROL || Type == CKANIMATION_LINSCLAXIS_CONTROL;
    for (const auto &fixture : LinearReference::Fixtures) {
        if (fixture.rotation != rotation) continue;
        WireWords words = {static_cast<CKDWORD>(fixture.keyCount)};
        AppendWire(words, fixture.keys, fixture.keyCount * (rotation ? 5 : 4) * sizeof(float));
        RCKObjectAnimation animation(&context, "LinearReference"), copy(&context, "LinearReferenceCopy");
        Chunk chunk = AnimationWithController(Type, words);
        LoadChunk(animation, chunk.get());
        CKAnimController *controller = TransformController(animation, Type);
        CheckLinearReference(controller, fixture);
        CheckSavedController(animation, Type, words);
        auto *copyController = copy.CreateController(Type);
        copyController->ReadKeysFrom(words.data());
        copyController->GetKey(0)->TimeStep = -99.0f;
        Check(copyController->Clone(controller), "Linear reference Clone failed");
        Check(copyController->GetKey(0) != controller->GetKey(0), "Linear Clone aliases key storage");
        CheckLinearReference(copyController, fixture);
        CheckSavedController(copy, Type, words);
        Chunk saved(copy.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
        RCKObjectAnimation reloaded(&context, "LinearReferenceReloaded");
        LoadChunk(reloaded, saved.get());
        CheckLinearReference(TransformController(reloaded, Type), fixture);
        CheckSavedController(reloaded, Type, words);
    }
}

void CheckTCBReference(CKAnimController *controller, const TCBReference::Fixture &fixture) {
    for (int i = 0; i < fixture.sampleCount; ++i) {
        const auto &sample = fixture.samples[i];
        float actual[4] = {};
        Check(controller->Evaluate(sample.time, actual), "TCB reference evaluation failed");
        for (int component = 0; component < (fixture.rotation ? 4 : 3); ++component) {
            if (!(std::fabs(actual[component] - sample.value[component]) < 0.00005f)) {
                std::printf("TCB reference mismatch: %s time=%g component=%d actual=%.9g expected=%.9g\n",
                            fixture.name, sample.time, component, actual[component], sample.value[component]);
                Check(false, "Loaded TCB curve differs from original DLL execution");
            }
        }
    }
}

template<CKANIMATION_CONTROLLER Type>
void LoadedTCBTangentsMatchOriginalRuntime() {
    CKContext context(nullptr, 0, 0);
    const bool rotation = Type == CKANIMATION_TCBROT_CONTROL || Type == CKANIMATION_TCBSCLAXIS_CONTROL;
    for (const auto &fixture : TCBReference::Fixtures) {
        if (fixture.rotation != rotation) continue;
        WireWords words = {static_cast<CKDWORD>(fixture.keyCount)};
        AppendWire(words, fixture.keys, fixture.keyCount * (rotation ? 10 : 9) * sizeof(float));
        RCKObjectAnimation animation(&context, "TCBReference"), copy(&context, "TCBCopy");
        Chunk chunk = AnimationWithController(Type, words);
        LoadChunk(animation, chunk.get());
        CKAnimController *controller = TransformController(animation, Type);
        CheckTCBReference(controller, fixture);
        CheckSavedController(animation, Type, words);

        Chunk copyChunk = AnimationWithController(Type, words);
        LoadChunk(copy, copyChunk.get());
        CKAnimController *copyController = TransformController(copy, Type);
        if (rotation)
            static_cast<CKTCBRotationKey *>(copyController->GetKey(0))->tension = 1.0f;
        else
            static_cast<CKTCBPositionKey *>(copyController->GetKey(0))->tension = 1.0f;
        float scratch[4];
        Check(copyController->Evaluate(fixture.samples[1].time, scratch), "TCB cache setup failed");
        Check(copyController->Clone(controller), "TCB reference Clone failed");
        CheckTCBReference(copyController, fixture);

        Chunk saved(copy.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
        RCKObjectAnimation reloaded(&context, "TCBReloaded");
        LoadChunk(reloaded, saved.get());
        CheckTCBReference(TransformController(reloaded, Type), fixture);
        CheckSavedController(reloaded, Type, words);
    }
}

void CheckBezierReference(CKAnimController *controller, const BezierReference::Fixture &fixture) {
    for (int i = 0; i < fixture.sampleCount; ++i) {
        const auto &sample = fixture.samples[i];
        VxVector actual;
        Check(controller->Evaluate(sample.time, &actual), "Bezier reference evaluation failed");
        for (int component = 0; component < 3; ++component) {
            if (!(std::fabs(actual[component] - sample.value[component]) < 0.00005f)) {
                std::printf("Bezier reference mismatch: %s time=%g component=%d actual=%.9g expected=%.9g\n",
                            fixture.name, sample.time, component, actual[component], sample.value[component]);
                Check(false, "Loaded Bezier curve differs from original DLL execution");
            }
        }
    }
    for (int i = 0; i < controller->GetKeyCount(); ++i) {
        auto *key = static_cast<CKBezierPositionKey *>(controller->GetKey(i));
        for (int component = 0; component < 6; ++component) {
            const float actual = component < 3 ? key->In[component] : key->Out[component - 3];
            if (!(std::fabs(actual - fixture.tangents[i][component]) < 0.00005f)) {
                std::printf("Bezier tangent mismatch: %s key=%d component=%d actual=%.9g expected=%.9g\n",
                            fixture.name, i, component, actual, fixture.tangents[i][component]);
                Check(false, "Computed Bezier tangent differs from original DLL execution");
            }
        }
    }
}

template<CKANIMATION_CONTROLLER Type, int Group>
void LoadedBezierMatchesOriginalRuntime() {
    CKContext context(nullptr, 0, 0);
    for (const auto &fixture : BezierReference::Fixtures) {
        if (fixture.group != Group) continue;
        WireWords words(fixture.words, fixture.words + fixture.wordCount);
        WireWords expectedSaved(fixture.saved, fixture.saved + fixture.wordCount);
        RCKObjectAnimation animation(&context, "BezierReference"), copy(&context, "BezierCopy");
        Chunk chunk = AnimationWithController(Type, words);
        LoadChunk(animation, chunk.get());
        CKAnimController *controller = TransformController(animation, Type);
        CheckSavedController(animation, Type, words);
        CheckBezierReference(controller, fixture);
        CheckSavedController(animation, Type, expectedSaved);

        Chunk copyChunk = AnimationWithController(Type, words);
        LoadChunk(copy, copyChunk.get());
        CKAnimController *copyController = TransformController(copy, Type);
        static_cast<CKBezierPositionKey *>(copyController->GetKey(0))->Pos.x += 7.0f;
        VxVector scratch;
        Check(copyController->Evaluate(fixture.samples[1].time, &scratch), "Bezier cache setup failed");
        Check(copyController->Clone(controller), "Bezier reference Clone failed");
        CheckBezierReference(copyController, fixture);

        Chunk saved(copy.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
        RCKObjectAnimation reloaded(&context, "BezierReloaded");
        LoadChunk(reloaded, saved.get());
        CheckBezierReference(TransformController(reloaded, Type), fixture);
        CheckSavedController(reloaded, Type, expectedSaved);
    }
}

void CheckMorphReference(RCKObjectAnimation &animation, const MorphReference::Fixture &fixture) {
    for (int sampleIndex = 0; sampleIndex < fixture.sampleCount; ++sampleIndex) {
        const auto &sample = fixture.samples[sampleIndex];
        std::vector<float> vertices(fixture.vertexCount * fixture.stride / sizeof(float) + 8, -12345.0f);
        std::vector<VxCompressedVector> normals(fixture.vertexCount + 2);
        const CKDWORD sentinel = 0x13572468;
        for (auto &normal : normals) std::memcpy(&normal, &sentinel, sizeof(normal));
        const CKBOOL result = animation.EvaluateMorphTarget(sample.time, fixture.vertexCount,
            (fixture.outputMask & 1) ? reinterpret_cast<VxVector *>(vertices.data() + 4) : nullptr,
            fixture.stride, (fixture.outputMask & 2) ? normals.data() + 1 : nullptr);
        Check(result == sample.success, "Morph evaluation return value differs from original runtime");
        for (size_t i = 0; i < vertices.size(); ++i) {
            if (!(std::fabs(vertices[i] - sample.vertices[i]) < 5.0e-5f)) {
                std::printf("Morph %s time %.9g vertex buffer[%zu]: %.9g != %.9g\n",
                            fixture.name, sample.time, i, vertices[i], sample.vertices[i]);
                Check(false, "Morph vertex output or stride padding differs from original runtime");
            }
        }
        if (std::memcmp(normals.data(), sample.normals, normals.size() * sizeof(VxCompressedVector))) {
            std::printf("Morph %s time %.9g\n", fixture.name, sample.time);
            Check(false, "Morph packed normal output differs from original runtime");
        }
    }
}

template<int Group>
void LoadedMorphMatchesOriginalRuntime() {
    CKContext context(nullptr, 0, 0);
    for (const auto &fixture : MorphReference::Fixtures) {
        if (fixture.group != Group) continue;
        WireWords words(fixture.words, fixture.words + fixture.wordCount);
        RCKObjectAnimation animation(&context, "MorphReference"), copy(&context, "MorphReferenceCopy");
        Chunk chunk = AnimationWithController(CKANIMATION_MORPH_CONTROL, words);
        LoadChunk(animation, chunk.get());
        CheckMorphReference(animation, fixture);
        CheckSavedController(animation, CKANIMATION_MORPH_CONTROL, words);

        auto *copyController = static_cast<RCKMorphController *>(copy.CreateController(CKANIMATION_MORPH_CONTROL));
        copyController->SetMorphVertexCount(1);
        copyController->AddKey(0.0f, FALSE);
        Check(copyController->Clone(animation.GetMorphController()), "Morph reference Clone failed");
        CheckMorphReference(copy, fixture);

        Chunk saved(copy.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
        RCKObjectAnimation reloaded(&context, "MorphReferenceReloaded");
        LoadChunk(reloaded, saved.get());
        CheckMorphReference(reloaded, fixture);
        CheckSavedController(reloaded, CKANIMATION_MORPH_CONTROL, words);
    }
}

template<int FixtureIndex>
void MorphEditingMatchesOriginalRuntime() {
    const auto &fixture = MorphEditReference::Fixtures[FixtureIndex];
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation animation(&context, "MorphEditing");
    WireWords input(fixture.words, fixture.words + fixture.wordCount);
    Chunk chunk = AnimationWithController(CKANIMATION_MORPH_CONTROL, input);
    LoadChunk(animation, chunk.get());
    auto *controller = static_cast<RCKMorphController *>(animation.GetMorphController());
    for (int step = 0; step < fixture.operationCount; ++step) {
        const auto &op = fixture.operations[step];
        const auto &expected = fixture.states[step];
        std::vector<CKMorphKey> before;
        for (int i = 0; i < controller->GetKeyCount(); ++i)
            before.push_back(*static_cast<CKMorphKey *>(controller->GetKey(i)));
        std::unique_ptr<VxVector[]> positions(new VxVector[3]);
        std::unique_ptr<VxCompressedVector[]> normals(new VxCompressedVector[3]);
        std::memcpy(positions.get(), op.positions, sizeof(op.positions));
        std::memcpy(normals.get(), op.normals, sizeof(op.normals));
        CKMorphKey source;
        source.TimeStep = op.time;
        source.PosArray = (op.flags & 2) ? positions.get() : nullptr;
        source.NormArray = (op.flags & 4) ? normals.get() : nullptr;
        int result = 0, cloneAliases = 0;
        switch (op.kind) {
        case 0:
            controller->SetMorphVertexCount(op.argument);
            break;
        case 1:
            result = (op.flags & 8) ? controller->AddKey(&source) : controller->AddKey(&source, op.flags & 1);
            break;
        case 2:
            result = controller->AddKey(op.time, op.flags & 1);
            break;
        case 3:
            result = (op.flags & 8) ? controller->AddKey(controller->GetKey(op.argument)) :
                controller->AddKey(controller->GetKey(op.argument), op.flags & 1);
            break;
        case 4:
            controller->RemoveKey(op.argument);
            break;
        case 5:
            result = (op.flags & 8) ? controller->AddKey(nullptr) : controller->AddKey(nullptr, op.flags & 1);
            break;
        case 6: {
            WireWords sourceWords(controller->DumpKeysTo(nullptr) / sizeof(CKDWORD));
            controller->DumpKeysTo(sourceWords.data());
            RCKMorphController copy;
            copy.SetLength(controller->GetLength());
            copy.ReadKeysFrom(sourceWords.data());
            result = controller->Clone(&copy);
            for (int i = 0; i < controller->GetKeyCount(); ++i) {
                const auto *a = static_cast<CKMorphKey *>(controller->GetKey(i));
                const auto *b = static_cast<CKMorphKey *>(copy.GetKey(i));
                if (a->PosArray && a->PosArray == b->PosArray) cloneAliases |= 1;
                if (a->NormArray && a->NormArray == b->NormArray) cloneAliases |= 2;
            }
            break;
        }
        default:
            Check(false, "Unknown Morph editing operation");
        }
        int aliases = cloneAliases, stable = 1;
        unsigned int storage = 0;
        for (int i = 0; i < controller->GetKeyCount(); ++i) {
            const auto *key = static_cast<CKMorphKey *>(controller->GetKey(i));
            if (key->PosArray == positions.get()) aliases |= 1;
            if (key->NormArray == normals.get()) aliases |= 2;
            storage |= ((key->PosArray ? 1u : 0u) | (key->NormArray ? 2u : 0u)) << (2 * i);
            for (const auto &old : before) {
                if (op.kind != 6 && old.TimeStep == key->TimeStep &&
                    (old.PosArray != key->PosArray || old.NormArray != key->NormArray)) stable = 0;
            }
        }
        for (int i = 0; i < 3; ++i) {
            positions[i] = VxVector(-777.0f, -777.0f, -777.0f);
            const CKDWORD value = 0x24681357u;
            std::memcpy(&normals[i], &value, sizeof(value));
        }
        // The pre-fix controller took ownership of caller arrays. Relinquish
        // those test allocations before reporting the mismatch, so unwinding
        // produces a failing assertion instead of a double deletion.
        if (aliases & 1) positions.release();
        if (aliases & 2) normals.release();
        Check(aliases == expected.aliases, "Morph AddKey aliases caller-owned arrays");
        Check(result == expected.result, "Morph editing return value differs from original runtime");
        Check(controller->GetKeyCount() == expected.keyCount &&
              controller->GetMorphVertexCount() == expected.vertexCount, "Morph editing changed controller counts");
        Check(stable == expected.stable, "Morph edit replaced an existing key buffer");
        Check(storage == expected.storage, "Morph key buffer allocation differs from original runtime");

        // The original leaves new, unsupplied position components unspecified.
        // Follow the caller's editing workflow and populate them before Save.
        if (controller->GetKeyCount() > static_cast<int>(before.size()) &&
            (op.kind == 2 || (op.kind == 1 && !(op.flags & 2)))) {
            auto *key = static_cast<CKMorphKey *>(controller->GetKey(result));
            std::memcpy(key->PosArray, op.positions, controller->GetMorphVertexCount() * sizeof(VxVector));
        }
        positions.reset();
        normals.reset();
        if (expected.wordCount) {
            WireWords saved(expected.words, expected.words + expected.wordCount);
            CheckSavedController(animation, CKANIMATION_MORPH_CONTROL, saved);
            Chunk savedChunk(animation.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
            RCKObjectAnimation reloaded(&context, "MorphEditingReloaded");
            LoadChunk(reloaded, savedChunk.get());
            CheckSavedController(reloaded, CKANIMATION_MORPH_CONTROL, saved);
        }
    }
}

// The two constant source keys are taken from the native Morph fixture. The
// merged caller uses the same compressed-normal helper (CK2_3D.dll 0x10051510).
template<bool FirstNormals, bool SecondNormals>
void MergedMorphPreservesNormalOutputs() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation first(&context, "MergedMorphFirst"), second(&context, "MergedMorphSecond");
    const CKDWORD *firstKey = MorphReference::PackedNormalsWords + 3;
    const CKDWORD *secondKey = firstKey + 17;
    WireWords firstWords = {1, 4, FirstNormals ? 1u : 0u};
    WireWords secondWords = {1, 4, SecondNormals ? 1u : 0u};
    firstWords.insert(firstWords.end(), firstKey, firstKey + (FirstNormals ? 17 : 13));
    secondWords.insert(secondWords.end(), secondKey, secondKey + (SecondNormals ? 17 : 13));
    Chunk firstChunk = AnimationWithController(CKANIMATION_MORPH_CONTROL, firstWords);
    Chunk secondChunk = AnimationWithController(CKANIMATION_MORPH_CONTROL, secondWords);
    LoadChunk(first, firstChunk.get());
    LoadChunk(second, secondChunk.get());
    auto *merged = static_cast<RCKObjectAnimation *>(first.CreateMergedAnimation(&second));
    Check(merged != nullptr, "Could not create merged Morph animation");

    for (int sampleIndex : {3, 5, 6, 7, 9}) {
        const auto &sample = MorphReference::PackedNormalsSamples[sampleIndex];
        merged->SetMergeFactor((sample.time - 2.0f) / 8.0f);
        // Four vertices with 20-byte stride, plus a guard at each end.
        float vertices[28];
        for (float &value : vertices) value = -12345.0f;
        VxCompressedVector normals[6];
        CKDWORD expectedNormals[6];
        for (CKDWORD &value : expectedNormals) value = 0x13572468;
        std::memcpy(normals, expectedNormals, sizeof(normals));
        for (int i = 0; i < 4; ++i) {
            if (FirstNormals && SecondNormals) expectedNormals[i + 1] = sample.normals[i + 1];
            else if (FirstNormals) expectedNormals[i + 1] = firstKey[13 + i];
            else if (SecondNormals) expectedNormals[i + 1] = secondKey[13 + i];
        }
        Check(merged->EvaluateMorphTarget(6.0f, 4, reinterpret_cast<VxVector *>(vertices + 4), 20, normals + 1),
              "Merged Morph evaluation failed");
        for (int i = 0; i < 28; ++i) {
            const int offset = i - 4;
            const bool position = offset >= 0 && offset < 20 && offset % 5 < 3;
            const float expected = position ? sample.vertices[4 + offset / 5 * 3 + offset % 5] : -12345.0f;
            Check(std::fabs(vertices[i] - expected) < 5.0e-5f, "Merged Morph vertex output or stride padding changed");
        }
        Check(!std::memcmp(normals, expectedNormals, sizeof(normals)), "Merged Morph normal output differs from original behavior");
    }
    context.DestroyObject(merged->GetID());
}

} // namespace

int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetProcessorSpecific_FunctionsPtr();
    if (CKStartUp() != CK_OK)
        return 1;
    CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
    CKCLASSREGISTERCID(RCKBodyPart, CKCID_3DOBJECT);
    CKCLASSREGISTERCID(RCKObjectAnimation, CKCID_OBJECT);
    CKCLASSREGISTERCID(RCKAnimation, CKCID_SCENEOBJECT);
    CKCLASSREGISTERCID(RCKKeyedAnimation, CKCID_ANIMATION);
    CKCLASSREGISTERCID(RCKCharacter, CKCID_3DENTITY);
    CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
    CKCLASSREGISTERCID(RCKMesh, CKCID_BEOBJECT);
    CKCLASSREGISTERCID(RCKPatchMesh, CKCID_MESH);
    CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
    CKCLASSREGISTERCID(RCKLayer, CKCID_OBJECT);
    CKCLASSREGISTERCID(RCKGrid, CKCID_3DENTITY);
    CKCLASSREGISTERCID(RCKSpriteText, CKCID_SPRITE);
    CKBuildClassHierarchyTable();

    struct Test { const char *name; void (*run)(); };
    VxSharedLibrary gridPlugin;
    const bool runtimeGrids = argc == 3 && strcmp(argv[1], "--grid-plugin") == 0;
    if (argc != 1 && !runtimeGrids) {
        fprintf(stderr, "Usage: serialization_tests [--grid-plugin path]\n");
        CKShutdown();
        return 1;
    }
    if (runtimeGrids) {
#ifdef CK_LIB
        fprintf(stderr, "Grids plugin tests require a shared CK2 runtime; use the superproject with CK2_BUILD_STATIC=OFF.\n");
        CKShutdown();
        return 1;
#endif
        typedef CKPluginInfo *(*GetPluginInfoFunction)(int);
        GetPluginInfoFunction getPluginInfo = nullptr;
        if (gridPlugin.Load(argv[2]))
            getPluginInfo = reinterpret_cast<GetPluginInfoFunction>(gridPlugin.GetFunctionPtr("CKGetPluginInfo"));
        CKPluginInfo *info = getPluginInfo ? getPluginInfo(1) : nullptr;
        if (!info || info->m_GUID != GRID_MANAGER_GUID || !info->m_InitInstanceFct) {
            fprintf(stderr, "Cannot load the Grids manager plugin: %s\n", argv[2]);
            CKShutdown();
            gridPlugin.ReleaseLibrary();
            return 1;
        }
        InitializeGridPlugin = info->m_InitInstanceFct;
    }
    const Test gridTests[] = {
        {"Runtime Grid fresh types", RuntimeGridFileRestoresTypeParameters<false, false>},
        {"Runtime Grid existing types", RuntimeGridFileRestoresTypeParameters<true, false>},
        {"Runtime Grid compressed fresh types", RuntimeGridFileRestoresTypeParameters<false, true>},
        {"Runtime Grid compressed existing types", RuntimeGridFileRestoresTypeParameters<true, true>},
        {"Runtime Grid missing marker fresh types", RuntimeGridFileRestoresTypeParameters<false, false, true>},
        {"Runtime Grid missing marker existing types", RuntimeGridFileRestoresTypeParameters<true, false, true>},
        {"Runtime Grid compressed missing marker fresh types", RuntimeGridFileRestoresTypeParameters<false, true, true>},
        {"Runtime Grid compressed missing marker existing types", RuntimeGridFileRestoresTypeParameters<true, true, true>},
    };
    const Test tests[] = {
        {"Keyed full file collects children and merge state", FullFileKeyedAnimationCollectsChildren<false>},
        {"Compressed keyed file collects children and merge state", FullFileKeyedAnimationCollectsChildren<true>},
        {"Keyed merge queries, propagation and selective state", KeyedAnimationMergeStateAndSelectiveSnapshots},
        {"Keyed multiple snapshots with missing object", KeyedAnimationMultipleSnapshotsSkipMissingObjects<false>},
        {"Keyed file Load skips embedded snapshots", KeyedAnimationMultipleSnapshotsSkipMissingObjects<true>},
        {"Animation full block wins over snapshot", AnimationCombinedBlocksRespectPrecedence<false, false>},
        {"Animation full block wins in reverse order", AnimationCombinedBlocksRespectPrecedence<false, true>},
        {"Animation shared block wins over full/snapshot", AnimationCombinedBlocksRespectPrecedence<true, false>},
        {"Animation shared block wins in reverse order", AnimationCombinedBlocksRespectPrecedence<true, true>},
        {"Animation missing shared owner", AnimationMissingSharedOwnerCreatesEmptyData},
        {"Full file managers omit first", FileSavePreservesSparseManagerChunks<0, false>},
        {"Full file managers omit middle", FileSavePreservesSparseManagerChunks<1, false>},
        {"Full file managers omit last", FileSavePreservesSparseManagerChunks<2, false>},
        {"Compressed managers omit first", FileSavePreservesSparseManagerChunks<0, true>},
        {"Compressed managers omit middle", FileSavePreservesSparseManagerChunks<1, true>},
        {"Compressed managers omit last", FileSavePreservesSparseManagerChunks<2, true>},
        {"Full file material dependencies", FullFileMaterialTextureDependencies<false, false>},
        {"Full file promoted material dependencies", FullFileMaterialTextureDependencies<true, false>},
        {"Compressed material dependencies", FullFileMaterialTextureDependencies<false, true>},
        {"Compressed promoted material dependencies", FullFileMaterialTextureDependencies<true, true>},
        {"Full file owner before alias", FullFileSharedAnimationControllers<false, false>},
        {"Full file alias before owner", FullFileSharedAnimationControllers<true, false>},
        {"Compressed owner before alias", FullFileSharedAnimationControllers<false, true>},
        {"Compressed alias before owner", FullFileSharedAnimationControllers<true, true>},
        {"Full file merged animation first", FullFileMergedAnimationReferences<true, false>},
        {"Full file merged animation last", FullFileMergedAnimationReferences<false, false>},
        {"Compressed merged animation first", FullFileMergedAnimationReferences<true, true>},
        {"Compressed merged animation last", FullFileMergedAnimationReferences<false, true>},
        {"Skin normal wire layout and round trip", SkinNormalsUseOriginalWireLayout},
        {"Skin bone flags", SkinBoneFlagsSurviveSaveLoad},
        {"Skin save cache invalidation", SavingSkinInvalidatesCachedBonePoints},
        {"Skin replacement", LoadingSkinReplacesPreviousNormals},
        {"Animation list replacement", LoadingAnimationListReplacesPreviousAnimations},
        {"Legacy parent detachment", LegacyEntityWithoutParentDetachesPreviousParent},
        {"Modern entity precedence", ModernEntityDataTakesPrecedenceOverLegacyData},
        {"Legacy texture mipmaps", LegacyTextureReadsVideoFormatIdentifier},
        {"Legacy texture save options", LegacyTextureReadsSaveFormatIdentifier},
        {"Legacy body part joint", LegacyBodyPartUsesIntegerFlagsAndX86ShiftCounts},
        {"Sprite text base serialization", SpriteTextSaveOmitsSpriteBitmapState},
        {"Sprite text default font without handle", SpriteTextSavePreservesFontWhenHandleIsUnavailable<false>},
        {"Sprite text requested font without handle", SpriteTextSavePreservesFontWhenHandleIsUnavailable<true>},
        {"Sprite text font field order", SpriteTextLoadsFontFieldsInSaveOrder},
        {"Mesh v0 lit", LegacyMeshLayout<0, true>},
        {"Mesh v0 prelit", LegacyMeshLayout<0, false>},
        {"Mesh v1 lit", LegacyMeshLayout<1, true>},
        {"Mesh v1 prelit", LegacyMeshLayout<1, false>},
        {"Mesh v4 lit", LegacyMeshLayout<4, true>},
        {"Mesh v4 prelit", LegacyMeshLayout<4, false>},
        {"Mesh v5 lit", LegacyMeshLayout<5, true>},
        {"Mesh v5 prelit", LegacyMeshLayout<5, false>},
        {"Mesh v8 lit", LegacyMeshLayout<8, true>},
        {"Mesh v8 prelit", LegacyMeshLayout<8, false>},
        {"Mesh v8 shared colors and UVs", LegacyMeshLayout<8, false, 0x0B>},
        {"Mesh v8 generated normals", LegacyMeshLayout<8, true, 0x04>},
        {"Layer legacy parameter GUID", LayerLegacyParameterGuid},
        {"Layer loads before grid", LayerLoadsSquaresBeforeItsGrid},
        {"Layer type remapping on save", LayerSaveRegistersTypeRemapping},
        {"Layer four-byte square stride", LayerSquaresUseFourByteWireStride},
        {"Mesh preserves non-unit normals", MeshNormalSaveDecisionMatchesOriginal<true>},
        {"Mesh uses average normal error", MeshNormalSaveDecisionMatchesOriginal<false>},
        {"Animation owner saved before alias", SharedAnimationUsesPreviouslySavedOwner<false>},
        {"Animation alias saved before owner", SharedAnimationUsesPreviouslySavedOwner<true>},
        {"Legacy quad patch edge indices", LegacyPatchMeshPreservesEdgeIndices<false>},
        {"Legacy triangle patch edge sentinel", LegacyPatchMeshPreservesEdgeIndices<true>},
        {"Grid loaded before layers", GridLayerFileLoadRestoresDisplay<false>},
        {"Layers loaded before grid", GridLayerFileLoadRestoresDisplay<true>},
        {"Grid embedded layer snapshot", GridMemorySnapshotRestoresEmbeddedLayers},
        {"Morph save without normals", MorphSaveUsesGlobalNormalFlag<false>},
        {"Morph save with normals", MorphSaveUsesGlobalNormalFlag<true>},
        {"Morph load without normals", MorphLoadReadsOriginalControllerLayout<false>},
        {"Morph load with normals", MorphLoadReadsOriginalControllerLayout<true>},
        {"Empty morph header", EmptyMorphStillWritesThreeWordHeader},
        {"Bezier position save", BezierSerializationUsesConditionalTangents<false, false>},
        {"Bezier position load", BezierSerializationUsesConditionalTangents<false, true>},
        {"Bezier scale save", BezierSerializationUsesConditionalTangents<true, false>},
        {"Bezier scale load", BezierSerializationUsesConditionalTangents<true, true>},
        {"Base controller type aliases", ControllerFactoryAcceptsOriginalBaseTypes},
        {"Unsupported controller preserves keys", UnsupportedControllerPreservesExistingKeys},
        {"Patch creates saved material channel", PatchRebuildRestoresMaterialChannelState<false>},
        {"Patch refreshes retained material channel", PatchRebuildRestoresMaterialChannelState<true>},
        {"Patch SAMEUV preserves base coordinates", PatchSameUVPreservesBaseCoordinates},
        {"Animation length updates shared controllers", AnimationLengthUpdatesExistingControllers},
        {"Clear owner resets shared controllers", ClearingAnimationClearsSharedControllers<true>},
        {"Clear alias resets shared controllers", ClearingAnimationClearsSharedControllers<false>},
        {"ClearAll releases controller storage", ClearAllReleasesControllerStorage},
        {"Legacy v0 animation preserves shared owner", AnimationLoadRestoresSharedOwnership<0>},
        {"Legacy snapshot animation takes ownership", AnimationLoadRestoresSharedOwnership<1>},
        {"Modern animation takes ownership", AnimationLoadRestoresSharedOwnership<10>},
        {"Legacy v0 full transform conversion", LegacyObjectTransformBlocksPreserveKeysAndReferences<0,false>},
        {"Legacy v1 full transform conversion", LegacyObjectTransformBlocksPreserveKeysAndReferences<1,false>},
        {"Legacy v0 scale-axis without rotation", LegacyObjectTransformBlocksPreserveKeysAndReferences<0,true>},
        {"Legacy v1 scale-axis without rotation", LegacyObjectTransformBlocksPreserveKeysAndReferences<1,true>},
        {"Legacy animation settings and partial state", AnimationBaseSettingsAndSelectiveSnapshot<true>},
        {"Modern animation settings and partial state", AnimationBaseSettingsAndSelectiveSnapshot<false>},
        {"Keyed snapshot subanimation and root transfer", KeyedAnimationSnapshotRestoresSubanimationAndRootTransfer},
        {"Legacy character nested snapshot", CharacterSnapshotRestoresEmbeddedBodyAndAnimationReferences<4>},
        {"Modern character nested snapshot", CharacterSnapshotRestoresEmbeddedBodyAndAnimationReferences<10>},
        {"File PreSave dependency object indices", FilePreSaveCollectsMaterialTextureAndMapsObjectIds<false>},
        {"File PreSave promotes referenced dependency", FilePreSaveCollectsMaterialTextureAndMapsObjectIds<true>},
        {"Legacy object Morph without normals", LegacyObjectMorphNormalsUseInitializedConversionEntries<0>},
        {"Legacy object Morph compressed normals", LegacyObjectMorphNormalsUseInitializedConversionEntries<1>},
        {"Legacy object Morph converted normals", LegacyObjectMorphNormalsUseInitializedConversionEntries<2>},
        {"Legacy object Morph normal block precedence", LegacyObjectMorphNormalsUseInitializedConversionEntries<3>},
        {"Morph Clone without normals", MorphClonePreservesIndependentSerializedKeys<false>},
        {"Morph Clone with normals", MorphClonePreservesIndependentSerializedKeys<true>},
        {"Morph Clone rejects other types", MorphCloneRejectsDifferentControllerType},
        {"Linear position Clone type and keys", TransformClonePreservesTypedSerializedKeys<CKANIMATION_LINPOS_CONTROL>},
        {"Linear rotation Clone type and keys", TransformClonePreservesTypedSerializedKeys<CKANIMATION_LINROT_CONTROL>},
        {"Linear scale Clone type and keys", TransformClonePreservesTypedSerializedKeys<CKANIMATION_LINSCL_CONTROL>},
        {"Linear scale-axis Clone type and keys", TransformClonePreservesTypedSerializedKeys<CKANIMATION_LINSCLAXIS_CONTROL>},
        {"TCB position Clone type and keys", TransformClonePreservesTypedSerializedKeys<CKANIMATION_TCBPOS_CONTROL>},
        {"TCB rotation Clone type and keys", TransformClonePreservesTypedSerializedKeys<CKANIMATION_TCBROT_CONTROL>},
        {"TCB scale Clone type and keys", TransformClonePreservesTypedSerializedKeys<CKANIMATION_TCBSCL_CONTROL>},
        {"TCB scale-axis Clone type and keys", TransformClonePreservesTypedSerializedKeys<CKANIMATION_TCBSCLAXIS_CONTROL>},
        {"Bezier position Clone type and keys", TransformClonePreservesTypedSerializedKeys<CKANIMATION_BEZIERPOS_CONTROL>},
        {"Bezier scale Clone type and keys", TransformClonePreservesTypedSerializedKeys<CKANIMATION_BEZIERSCL_CONTROL>},
        {"Loaded TCB position easing", LoadedTCBUsesOriginalTimeRemapping<CKANIMATION_TCBPOS_CONTROL>},
        {"Loaded TCB rotation easing", LoadedTCBUsesOriginalTimeRemapping<CKANIMATION_TCBROT_CONTROL>},
        {"Loaded TCB scale easing", LoadedTCBUsesOriginalTimeRemapping<CKANIMATION_TCBSCL_CONTROL>},
        {"Loaded TCB scale-axis easing", LoadedTCBUsesOriginalTimeRemapping<CKANIMATION_TCBSCLAXIS_CONTROL>},
        {"TCB position original runtime tangents", LoadedTCBTangentsMatchOriginalRuntime<CKANIMATION_TCBPOS_CONTROL>},
        {"TCB rotation original runtime tangents", LoadedTCBTangentsMatchOriginalRuntime<CKANIMATION_TCBROT_CONTROL>},
        {"TCB scale original runtime tangents", LoadedTCBTangentsMatchOriginalRuntime<CKANIMATION_TCBSCL_CONTROL>},
        {"TCB scale-axis original runtime tangents", LoadedTCBTangentsMatchOriginalRuntime<CKANIMATION_TCBSCLAXIS_CONTROL>},
        {"Bezier position smooth modes", LoadedBezierMatchesOriginalRuntime<CKANIMATION_BEZIERPOS_CONTROL, 0>},
        {"Bezier position linear modes", LoadedBezierMatchesOriginalRuntime<CKANIMATION_BEZIERPOS_CONTROL, 1>},
        {"Bezier position step modes", LoadedBezierMatchesOriginalRuntime<CKANIMATION_BEZIERPOS_CONTROL, 2>},
        {"Bezier position fast modes", LoadedBezierMatchesOriginalRuntime<CKANIMATION_BEZIERPOS_CONTROL, 3>},
        {"Bezier position slow modes", LoadedBezierMatchesOriginalRuntime<CKANIMATION_BEZIERPOS_CONTROL, 4>},
        {"Bezier position explicit modes", LoadedBezierMatchesOriginalRuntime<CKANIMATION_BEZIERPOS_CONTROL, 5>},
        {"Bezier position edge cases", LoadedBezierMatchesOriginalRuntime<CKANIMATION_BEZIERPOS_CONTROL, 6>},
        {"Bezier scale smooth modes", LoadedBezierMatchesOriginalRuntime<CKANIMATION_BEZIERSCL_CONTROL, 0>},
        {"Bezier scale linear modes", LoadedBezierMatchesOriginalRuntime<CKANIMATION_BEZIERSCL_CONTROL, 1>},
        {"Bezier scale step modes", LoadedBezierMatchesOriginalRuntime<CKANIMATION_BEZIERSCL_CONTROL, 2>},
        {"Bezier scale fast modes", LoadedBezierMatchesOriginalRuntime<CKANIMATION_BEZIERSCL_CONTROL, 3>},
        {"Bezier scale slow modes", LoadedBezierMatchesOriginalRuntime<CKANIMATION_BEZIERSCL_CONTROL, 4>},
        {"Bezier scale explicit modes", LoadedBezierMatchesOriginalRuntime<CKANIMATION_BEZIERSCL_CONTROL, 5>},
        {"Bezier scale edge cases", LoadedBezierMatchesOriginalRuntime<CKANIMATION_BEZIERSCL_CONTROL, 6>},
        {"Morph packed normal interpolation", LoadedMorphMatchesOriginalRuntime<0>},
        {"Morph vertex strides and subsets", LoadedMorphMatchesOriginalRuntime<1>},
        {"Morph optional outputs", LoadedMorphMatchesOriginalRuntime<2>},
        {"Morph single and empty keys", LoadedMorphMatchesOriginalRuntime<3>},
        {"Morph zero vertex requests", LoadedMorphMatchesOriginalRuntime<4>},
        {"Merged Morph both normal sources", MergedMorphPreservesNormalOutputs<true, true>},
        {"Merged Morph first normal source", MergedMorphPreservesNormalOutputs<true, false>},
        {"Merged Morph second normal source", MergedMorphPreservesNormalOutputs<false, true>},
        {"Merged Morph without normals", MergedMorphPreservesNormalOutputs<false, false>},
        {"Morph editing copies positions", MorphEditingMatchesOriginalRuntime<0>},
        {"Morph editing copies normals without allocation flag", MorphEditingMatchesOriginalRuntime<1>},
        {"Morph editing allocates missing normals", MorphEditingMatchesOriginalRuntime<2>},
        {"Morph editing generic AddKey", MorphEditingMatchesOriginalRuntime<3>},
        {"Morph editing normal-only input", MorphEditingMatchesOriginalRuntime<4>},
        {"Morph editing empty input with normals", MorphEditingMatchesOriginalRuntime<5>},
        {"Morph editing empty input without normals", MorphEditingMatchesOriginalRuntime<6>},
        {"Morph editing time keys with normals", MorphEditingMatchesOriginalRuntime<7>},
        {"Morph editing time keys without normals", MorphEditingMatchesOriginalRuntime<8>},
        {"Morph editing duplicate source keys", MorphEditingMatchesOriginalRuntime<9>},
        {"Morph editing duplicate time keeps normals", MorphEditingMatchesOriginalRuntime<10>},
        {"Morph editing duplicate time keeps absent normals", MorphEditingMatchesOriginalRuntime<11>},
        {"Morph editing changes only count with normals", MorphEditingMatchesOriginalRuntime<12>},
        {"Morph editing changes only count without normals", MorphEditingMatchesOriginalRuntime<13>},
        {"Morph editing removes and reinserts keys", MorphEditingMatchesOriginalRuntime<14>},
        {"Morph editing null and empty keys", MorphEditingMatchesOriginalRuntime<15>},
        {"Morph editing zero vertices without normals", MorphEditingMatchesOriginalRuntime<16>},
        {"Morph editing zero vertices with normals", MorphEditingMatchesOriginalRuntime<17>},
        {"Morph Clone preserves zero-sized arrays", MorphEditingMatchesOriginalRuntime<18>},
        {"Morph Clone survives source destruction", MorphEditingMatchesOriginalRuntime<19>},
        {"Linear position original runtime boundaries", LoadedLinearMatchesOriginalRuntime<CKANIMATION_LINPOS_CONTROL>},
        {"Linear rotation original runtime boundaries", LoadedLinearMatchesOriginalRuntime<CKANIMATION_LINROT_CONTROL>},
        {"Linear scale original runtime boundaries", LoadedLinearMatchesOriginalRuntime<CKANIMATION_LINSCL_CONTROL>},
        {"Linear scale-axis original runtime boundaries", LoadedLinearMatchesOriginalRuntime<CKANIMATION_LINSCLAXIS_CONTROL>},
    };
    int failures = 0;
    const Test *selectedTests = runtimeGrids ? gridTests : tests;
    const int testCount = runtimeGrids ? sizeof(gridTests) / sizeof(gridTests[0]) : sizeof(tests) / sizeof(tests[0]);
    for (int i = 0; i < testCount; ++i) {
        const Test &test = selectedTests[i];
        std::printf("RUN: %s\n", test.name);
        try {
            test.run();
            std::printf("PASS: %s\n", test.name);
        } catch (const std::exception &error) {
            std::printf("FAIL: %s: %s\n", test.name, error.what());
            ++failures;
        }
    }
    CKShutdown();
    if (runtimeGrids) gridPlugin.ReleaseLibrary();
    return failures ? 1 : 0;
}
