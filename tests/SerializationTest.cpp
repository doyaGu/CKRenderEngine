#include <cstdio>
#include <cmath>
#include <cfloat>
#include <climits>
#include <cstring>
#include <memory>
#include <new>
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
#include "CKParameterManager.h"
#include "CKGridManager.h"
#include "CKAttributeManager.h"
#include "CKObjectManager.h"
#include "CKLevel.h"
#include "CKScene.h"
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

// Test-executable-only replacements for nothrow allocation entries.
// Delegate normally; the test thread can fail a selected request.
static thread_local int s_MorphArrayAllocationFailure = -1;
static thread_local bool s_ObjectAnimationAllocationFailure = false;

void *operator new(size_t bytes, const std::nothrow_t &) noexcept {
    if (s_ObjectAnimationAllocationFailure) {
        s_ObjectAnimationAllocationFailure = false;
        return nullptr;
    }
    try {
        return ::operator new(bytes);
    } catch (...) {
        return nullptr;
    }
}

void *operator new[](size_t bytes, const std::nothrow_t &) noexcept {
    if (s_MorphArrayAllocationFailure == 0) {
        s_MorphArrayAllocationFailure = -1;
        return nullptr;
    }
    if (s_MorphArrayAllocationFailure > 0)
        --s_MorphArrayAllocationFailure;
    try {
        return ::operator new[](bytes);
    } catch (...) {
        return nullptr;
    }
}

// Pass 27 global Layer array release observer.
// Integrate at GLOBAL scope in the test executable, before its anonymous namespace.
// CK2_3DStatic arrays resolve these entries. Shared CK2.dll arrays do not;
// this observer intentionally makes no throwing-CK2 allocation assertion.
static thread_local bool s_LayerArrayReleaseObservation = false;
static thread_local void *s_LayerArrayObservedPointer = nullptr;
static thread_local size_t s_LayerArrayObservedBytes = 0;
static thread_local int s_LayerArrayObservedAllocations = 0;
static thread_local int s_LayerArrayObservedReleases = 0;

void *operator new[](size_t bytes) {
    void *pointer = ::operator new(bytes);
    if (s_LayerArrayReleaseObservation && s_LayerArrayObservedBytes == bytes) {
        s_LayerArrayObservedPointer = pointer;
        ++s_LayerArrayObservedAllocations;
    }
    return pointer;
}

void operator delete[](void *pointer) noexcept {
    if (s_LayerArrayReleaseObservation && pointer && pointer == s_LayerArrayObservedPointer)
        ++s_LayerArrayObservedReleases;
    ::operator delete(pointer);
}

void operator delete[](void *pointer, size_t) noexcept {
    ::operator delete[](pointer);
}

// End Pass 27 global Layer array release observer.

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

void LegacyBodyPartUsesIntegerFlagsAndNamedSdkAxes() {
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
    Check(body.Joint().m_Flags == (CK_IKJOINT_ACTIVE_X | CK_IKJOINT_LIMIT_Y | CK_IKJOINT_EASE_Z), "Legacy integer booleans did not restore named SDK axes");
    Check(Equal(body.Joint().m_Min, joint.minimum) && Equal(body.Joint().m_Max, joint.maximum) &&
              Equal(body.Joint().m_Damping, joint.damping), "Legacy joint vectors changed");
}

// The legacy reader copied the stored size into the 72-byte stack layout.
void LegacyBodyPartSkipsOversizedJoint() {
    CKContext context(nullptr, 0, 0);
    BodyPartForTest body(&context);
    const CKIkJoint before = body.Joint();
    CKDWORD oversized[20];
    for (CKDWORD &word : oversized) word = 1;
    Chunk chunk = NewChunk(CKCID_BODYPART, 4);
    chunk->WriteIdentifier(CK_STATESAVE_BODYPARTROTJOINT);
    chunk->WriteBuffer_LEndian(sizeof(oversized), oversized);
    LoadChunk(body, chunk.get());
    Check(body.Joint().m_Flags == before.m_Flags && Equal(body.Joint().m_Min, before.m_Min) &&
              Equal(body.Joint().m_Max, before.m_Max) && Equal(body.Joint().m_Damping, before.m_Damping),
          "Oversized legacy joint block changed the joint");
}

// The video format reader copied the stored size into a stack VxImageDescEx.
void LegacyTextureSkipsOversizedVideoFormat() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context); // Owned by CKContext.
    LegacyTexture texture(&context);
    const VX_PIXELFORMAT before = texture.GetDesiredVideoFormat();
    CKDWORD oversized[64] = {};
    oversized[0] = 64; oversized[1] = 64; oversized[2] = 32;
    Chunk chunk = NewChunk(CKCID_TEXTURE, 4);
    chunk->WriteIdentifier(CK_STATESAVE_TEXVIDEOFORMAT);
    chunk->WriteInt(1);
    chunk->WriteBuffer_LEndian(sizeof(oversized), oversized);
    LoadChunk(texture, chunk.get());
    Check(texture.mipmapRequest == 1, "Legacy mipmap request was ignored");
    Check(texture.GetDesiredVideoFormat() == before, "Oversized video format block changed the desired format");
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
template <int Version, bool Lit, CKDWORD SaveFlags = 0, bool OversizedLines = false>
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
        // An oversized block declares more indices than the one stored line.
        CKWORD indices[] = {1, 2, 3, 4};
        chunk->WriteBuffer_LEndian16(OversizedLines ? sizeof(indices) : 2 * sizeof(CKWORD), indices);
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
    if (OversizedLines)
        Check(mesh.GetLineCount() == 1 && a == 0 && b == 0, "Oversized legacy line block was copied");
    else
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

// m_Remap holds one entry per type registered at PreSave; an out-of-range
// type wrote past it.
void LayerSaveIgnoresUnregisteredTypeRemapping() {
    CKContext context(nullptr, 0, 0);
    LayerRegistry *registry = new LayerRegistry(&context);
    RCKLayer layer(&context, "Layer", 0);
    layer.SetFormat(1);
    layer.SetType(registry->GetLayerTypeCount());
    CKFile file(&context);
    Chunk saved(layer.Save(&file, CK_STATESAVE_ALL), &DeleteCKStateChunk);
    Check(registry->m_RemapCount == 1 && registry->remap[0] == 0 && registry->remap[1] == 0,
          "Layer Save remapped a type outside the registered range");
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

// The texture is capped at 256 pixels, or 128 cells. Later cells wrapped into
// the next pixel row and finally wrote past the surface.
void GridTextureDrawsOnlyCellsThatFit() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    auto *registry = new SerializationGridRegistry(&context);
    RCKGrid grid(&context, "WideGrid");
    grid.SetDimensions(200, 1, 1.0f, 1.0f);
    const int type = registry->RegisterType(const_cast<char *>("Intensity"));
    VxColor white(VxColor(1.0f, 1.0f, 1.0f).GetRGBA());
    registry->SetAssociatedColor(type, &white);
    registry->SetAssociatedParam(type, CKGUID(0, 0));
    CKLayer *layer = grid.AddLayer(type);
    Check(layer != nullptr, "Cannot create grid layer");
    layer->GetSquareArray()[0].ival = 10;
    layer->GetSquareArray()[128].ival = 200;
    grid.ConstructMeshTexture(0.5f);
    CKMesh *mesh = grid.GetCurrentMesh();
    CKTexture *texture = mesh ? mesh->GetFaceMaterial(0)->GetTexture() : nullptr;
    Check(texture && texture->GetWidth() == 256 && texture->GetHeight() == 16, "Wide grid texture size changed");
    const CKDWORD *pixels = reinterpret_cast<const CKDWORD *>(texture->LockSurfacePtr());
    Check(pixels != nullptr, "Cannot inspect wide grid pixels");
    const CKDWORD cell = 0xFF0A0A0Au;
    const bool kept = pixels[0] == cell && pixels[1] == cell && pixels[256] == cell && pixels[257] == cell;
    texture->ReleaseSurfacePtr();
    Check(kept, "Cells beyond the texture width overwrote the next pixel row");
}

// Load sizes cells from the file, but value access and Save index by the grid.
void LayerLoadFitsCellsToItsGrid() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    auto *registry = new SerializationGridRegistry(&context);
    RCKGrid grid(&context, "Grid");
    grid.SetDimensions(2, 2, 2.0f, 2.0f);
    const int type = registry->RegisterType(const_cast<char *>("Short"));
    CKLayer *layer = grid.AddLayer(type);
    CKLayer *reloaded = grid.AddLayer(registry->RegisterType(const_cast<char *>("Reloaded")));
    Check(layer && reloaded, "Cannot create grid layers");
    Chunk chunk = NewChunk(CKCID_LAYER);
    chunk->WriteIdentifier(CK_STATESAVE_LAYERDATA);
    chunk->WriteObject(&grid);
    chunk->WriteInt(type); // Runtime type index.
    chunk->WriteInt(0); // Square data format.
    chunk->WriteInt(1); // Visibility.
    CKDWORD values[] = {5, 6};
    chunk->WriteBuffer_LEndian(sizeof(values), values);
    LoadChunk(*layer, chunk.get());
    int value = -1;
    layer->GetValue(1, 1, &value);
    Check(value == 0, "Layer cells were not extended to the grid size");
    Chunk saved(layer->Save(nullptr, CK_STATESAVE_ALL), &DeleteCKStateChunk);
    LoadChunk(*reloaded, saved.get());
    const int expected[] = {5, 6, 0, 0};
    for (int i = 0; i < 4; ++i)
        Check(reloaded->GetSquareArray()[i].ival == expected[i], "Layer Save read cells beyond its loaded values");
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

// An owner switching to other data must hand its old data to a remaining
// sharer; otherwise the sharer's Save references the owner's new keys.
void ShareDataFromTransfersOwnershipOfReleasedData() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation owner(&context, "SwitchingOwner"), alias(&context, "RemainingAlias"),
        other(&context, "OtherData");
    // Ownership transfer searches the class list that CreateObject fills.
    RCKObjectAnimation *animations[] = {&owner, &alias, &other};
    for (RCKObjectAnimation *animation : animations) context.m_ObjectManager->FinishRegisterObject(animation);
    VxVector ownerPosition(9.0f, 8.0f, 7.0f), otherPosition(1.0f, 2.0f, 3.0f);
    owner.AddPositionKey(0.0f, &ownerPosition);
    other.AddPositionKey(0.0f, &otherPosition);
    Check(alias.ShareDataFrom(&owner) && alias.Shared() == &owner, "Cannot share the owner's data");
    Check(owner.ShareDataFrom(&other), "Owner cannot switch to other data");
    Check(alias.Shared() == &alias && owner.Shared() == &other, "Released data kept the switched owner");
    Check(alias.ShareDataFrom(&alias) == FALSE && owner.ShareDataFrom(&other), "Repeated sharing changed results");
    Check(owner.Shared() == &other && other.Shared() == &other, "Repeated sharing changed ownership");
    VxVector actual;
    Check(alias.EvaluatePosition(0.0f, actual) && Equal(actual, ownerPosition), "Alias lost the released data");
    Chunk saved(alias.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
    saved->StartRead();
    Check(!saved->SeekIdentifier(CK_STATESAVE_OBJANIMSHARED) && saved->SeekIdentifier(CK_STATESAVE_OBJANIMCONTROLLERS),
          "New owner saved a shared reference instead of its controllers");
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

template<int Format, int Sources>
void LoadedMergedAnimationHandlesMissingSources() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation owner(&context, "MergedOwner"), first(&context, "MergedFirst"),
        second(&context, "MergedSecond"), loaded(&context, "MergedLoaded"), reloaded(&context, "MergedReloaded");
    RCKObjectAnimation *sources[] = {&first, &second};
    for (int i = 0; i < 2; ++i) {
        VxVector position(i ? 10.0f : 2.0f, 0.0f, 0.0f);
        VxVector scale(i ? 5.0f : 1.0f, 2.0f, 3.0f);
        VxQuaternion rotation(0.0f, 0.0f, i ? 1.0f : 0.0f, i ? 0.0f : 1.0f);
        sources[i]->AddPositionKey(0.0f, &position);
        sources[i]->AddScaleKey(0.0f, &scale);
        sources[i]->AddRotationKey(0.0f, &rotation);
        sources[i]->AddScaleAxisKey(0.0f, &rotation);
        RCKMorphController *morph = static_cast<RCKMorphController *>(sources[i]->CreateController(CKANIMATION_MORPH_CONTROL));
        morph->SetMorphVertexCount(1);
        CKMorphKey key;
        key.TimeStep = 0.0f;
        key.PosArray = &position;
        key.NormArray = nullptr;
        morph->AddKey(&key, FALSE);
        sources[i]->SetLength(i ? 10.0f : 20.0f);
    }
    owner.SetLength(20.0f);
    RCKObjectAnimation *source1 = (Sources & 1) ? &first : nullptr;
    RCKObjectAnimation *source2 = (Sources & 2) ? &second : nullptr;
    Chunk chunk = NewChunk(CKCID_OBJECTANIMATION, Format == 0 ? 0 : 10);
    if (Format == 0) {
        chunk->WriteIdentifier(CK_STATESAVE_OBJANIMLENGTH);
        chunk->WriteFloat(20.0f);
        chunk->WriteIdentifier(CK_STATESAVE_OBJANIMMERGE);
        chunk->WriteFloat(0.25f);
        chunk->WriteInt(TRUE);
        chunk->WriteObject(source1);
        chunk->WriteObject(source2);
    } else {
        CKDWORD identifier = Format == 1 ? CK_STATESAVE_OBJANIMNEWDATA :
            (Format == 2 ? CK_STATESAVE_OBJANIMCONTROLLERS : CK_STATESAVE_OBJANIMSHARED);
        WriteAnimationStateBlock(chunk.get(), identifier, &owner, source1, source2, true, -99.0f);
    }
    LoadChunk(loaded, chunk.get());
    Check(loaded.IsMerged() && loaded.GetMergeFactor() == 0.25f && loaded.GetLength() == 20.0f,
          "Merged Load changed flags, factor or length");
    Chunk saved(loaded.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
    LoadChunk(reloaded, saved.get());
    RCKObjectAnimation *targets[] = {&loaded, &reloaded};
    const float factors[] = {0.0f, 0.25f, 1.0f};
    for (int targetIndex = 0; targetIndex < 2; ++targetIndex) {
        RCKObjectAnimation *target = targets[targetIndex];
        for (int i = 0; i < 3; ++i) {
            target->SetMergeFactor(factors[i]);
            bool selected = i == 0 ? source1 != nullptr : (i == 2 ? source2 != nullptr : Sources != 0);
            float x = i == 0 ? 2.0f : (i == 2 ? 10.0f : (Sources == 3 ? 4.0f : (source1 ? 2.0f : 10.0f)));
            VxVector output(123.0f);
            Check((target->EvaluatePosition(7.0f, output) != FALSE) == selected,
                  "Merged position returned the wrong missing-source result");
            if (selected) Check(Equal(output, VxVector(x, 0.0f, 0.0f)), "Merged position lost the available source");
            VxQuaternion rotation(0.5f, 0.5f, 0.5f, 0.5f);
            Check((target->EvaluateRotation(7.0f, rotation) != FALSE) == selected,
                  "Merged rotation returned the wrong missing-source result");
            float rotationFactor = Sources == 3 ? factors[i] : (source1 ? 0.0f : 1.0f);
            const float angle = 1.57079632679f * rotationFactor;
            if (selected) Check(rotation.x == 0.0f && rotation.y == 0.0f &&
                                std::fabs(rotation.z - std::sin(angle)) < 5.0e-5f &&
                                std::fabs(rotation.w - std::cos(angle)) < 5.0e-5f,
                                "Merged rotation lost the available source or blend");
            // Scale, scale-axis and mismatched/absent Morph use any available source,
            // matching their existing behavior when a source lacks that controller.
            Check((target->EvaluateScale(7.0f, output) != FALSE) == (Sources != 0), "Merged scale missing-source result changed");
            if (Sources) {
                float scaleX = Sources == 3 ? 1.0f + 4.0f * factors[i] : (source1 ? 1.0f : 5.0f);
                Check(Equal(output, VxVector(scaleX, 2.0f, 3.0f)), "Merged scale lost the available source");
            }
            Check((target->EvaluateScaleAxis(7.0f, rotation) != FALSE) == (Sources != 0), "Merged scale-axis missing-source result changed");
            if (Sources) Check(rotation.x == 0.0f && rotation.y == 0.0f &&
                               std::fabs(rotation.z - std::sin(angle)) < 5.0e-5f &&
                               std::fabs(rotation.w - std::cos(angle)) < 5.0e-5f,
                               "Merged scale-axis lost the available source or blend");
            output.Set(123.0f, 123.0f, 123.0f);
            Check((target->EvaluateMorphTarget(7.0f, 1, &output, sizeof(output), nullptr) != FALSE) == (Sources != 0),
                  "Merged Morph missing-source result changed");
            if (Sources) {
                float morphX = Sources == 3 ? 2.0f + 8.0f * factors[i] : (source1 ? 2.0f : 10.0f);
                Check(Equal(output, VxVector(morphX, 0.0f, 0.0f)), "Merged Morph lost the available source");
            } else {
                Check(Equal(output, VxVector(123.0f)), "Failed merged Morph changed the output buffer");
            }
        }
    }
}

void LoadMergedState(RCKObjectAnimation &target, RCKObjectAnimation *first,
                     RCKObjectAnimation *second, float length = 20.0f) {
    Chunk chunk = NewChunk(CKCID_OBJECTANIMATION);
    WriteAnimationStateBlock(chunk.get(), CK_STATESAVE_OBJANIMCONTROLLERS, nullptr,
                             first, second, true, -99.0f, length);
    LoadChunk(target, chunk.get());
}

void AddTimedAnimationKeys(RCKObjectAnimation &animation, float base, float span = 10.0f) {
    RCKMorphController *morph = static_cast<RCKMorphController *>(animation.CreateController(CKANIMATION_MORPH_CONTROL));
    morph->SetMorphVertexCount(1);
    for (int i = 0; i < 2; ++i) {
        VxVector value(base + 10.0f * i, 2.0f, 3.0f);
        VxQuaternion rotation(0.0f, 0.0f, i ? 1.0f : 0.0f, i ? 0.0f : 1.0f);
        animation.AddPositionKey(i ? span : 0.0f, &value);
        animation.AddScaleKey(i ? span : 0.0f, &value);
        animation.AddRotationKey(i ? span : 0.0f, &rotation);
        animation.AddScaleAxisKey(i ? span : 0.0f, &rotation);
        CKMorphKey *key = static_cast<CKMorphKey *>(morph->GetKey(morph->AddKey(i ? span : 0.0f, TRUE)));
        key->PosArray[0] = value;
        key->NormArray[0].xa = static_cast<short>(base * 100.0f);
        key->NormArray[0].ya = 300;
    }
    animation.SetLength(span);
}

class ObjectCloneProbe : public RCKObjectAnimation {
public:
    ObjectCloneProbe(CKContext *context, const char *name) : RCKObjectAnimation(context, name) {
        context->m_ObjectManager->FinishRegisterObject(this);
    }
    CKKeyframeData *Data() const { return m_KeyframeData; }
    int References() const { return m_KeyframeData ? m_KeyframeData->m_RefCount : 0; }
    void DropData() { ReleaseKeyframeData(); }
    void SetCloneState(RCK3dEntity *entity, RCKObjectAnimation *first, RCKObjectAnimation *second, CKDWORD flags) {
        m_Entity = entity;
        m_Anim1 = first; m_Anim2 = second;
        m_Flags = flags; m_MergeFactor = 0.25f; m_CurrentStep = 0.625f;
    }
    RCKObjectAnimation *First() const { return m_Anim1; }
    RCKObjectAnimation *Second() const { return m_Anim2; }
    void SetParent(RCKKeyedAnimation *parent) { m_ParentKeyedAnimation = parent; }
    RCKKeyedAnimation *Parent() const { return m_ParentKeyedAnimation; }
};

struct ObjectCloneDataSnapshot {
    CKKeyframeData *Data;
    CKObjectAnimation *Owner;
    CKAnimController *Controllers[5];
    WireWords Payloads[5];
    float Length;
};

ObjectCloneDataSnapshot SnapshotObjectCloneData(ObjectCloneProbe &animation) {
    ObjectCloneDataSnapshot snapshot;
    snapshot.Data = animation.Data(); snapshot.Owner = animation.Shared(); snapshot.Length = animation.GetLength();
    CKAnimController *controllers[] = {animation.GetPositionController(), animation.GetRotationController(),
        animation.GetScaleController(), animation.GetScaleAxisController(), animation.GetMorphController()};
    for (int i = 0; i < 5; ++i) {
        snapshot.Controllers[i] = controllers[i];
        if (!controllers[i]) continue;
        const int bytes = controllers[i]->DumpKeysTo(nullptr);
        Check(bytes > 0 && !(bytes & 3), "Object Clone fixture has invalid wire state");
        snapshot.Payloads[i].resize(bytes / 4);
        Check(controllers[i]->DumpKeysTo(snapshot.Payloads[i].data()) == bytes, "Cannot snapshot Object Clone controller");
    }
    return snapshot;
}

void CheckObjectCloneData(ObjectCloneProbe &animation, const ObjectCloneDataSnapshot &before, CKObjectAnimation *owner) {
    Check(animation.Data() == before.Data && animation.Shared() == owner && animation.GetLength() == before.Length,
          "Object Clone changed shared data/owner/length");
    CKAnimController *controllers[] = {animation.GetPositionController(), animation.GetRotationController(),
        animation.GetScaleController(), animation.GetScaleAxisController(), animation.GetMorphController()};
    for (int i = 0; i < 5; ++i) {
        Check(controllers[i] == before.Controllers[i], "Object Clone replaced a shared controller");
        if (!controllers[i]) continue;
        WireWords after(before.Payloads[i].size(), 0u);
        Check(controllers[i]->DumpKeysTo(nullptr) == static_cast<int>(after.size() * 4) &&
              controllers[i]->DumpKeysTo(after.data()) == static_cast<int>(after.size() * 4) && after == before.Payloads[i],
              "Object Clone changed source controller bytes");
    }
}

void CheckObjectCloneFields(ObjectCloneProbe &target, RCK3dEntity *entity, RCKObjectAnimation *first,
                            RCKObjectAnimation *second, CKDWORD flags, RCKKeyedAnimation *parent) {
    Check(target.Get3dEntity() == reinterpret_cast<CK3dEntity *>(entity) && target.First() == first && target.Second() == second &&
          target.GetFlags() == flags && target.GetMergeFactor() == 0.25f && target.GetCurrentStep() == 0.625f && target.Parent() == parent,
          "Object Clone lost playback fields or changed the target's parent");
}

void CheckObjectCloneReload(ObjectCloneProbe &animation, CKContext &context, float expectedPosition) {
    VxVector actual;
    Check(animation.EvaluatePosition(3.0f, actual) && std::fabs(actual.x - expectedPosition) < 5.0e-5f,
          "Object Clone lost independently expected position");
    Chunk saved(animation.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
    Check(saved != nullptr, "Object Clone Save failed");
    RCKObjectAnimation reloaded(&context, "ObjectCloneReloaded");
    LoadChunk(reloaded, saved.get());
    Check(reloaded.GetFlags() == animation.GetFlags() && reloaded.GetLength() == animation.GetLength() &&
          reloaded.Get3dEntity() == animation.Get3dEntity() && reloaded.GetMergeFactor() == (animation.IsMerged() ? 0.25f : 0.5f),
          "Object Clone Save/Load changed flags, entity, length or merged factor");
    Check(reloaded.EvaluatePosition(3.0f, actual) && std::fabs(actual.x - expectedPosition) < 5.0e-5f,
          "Object Clone Save/Load lost position semantics");
}

template<bool Merged>
void ObjectAnimationSelfClonePreservesState() {
    CKContext context(nullptr, 0, 0);
    ObjectCloneProbe animation(&context, "SelfObjectClone"), alias(&context, "SelfCloneAlias");
    RCKObjectAnimation first(&context, "FirstCloneSource"), second(&context, "SecondCloneSource");
    RCK3dEntity entity(&context, "CloneEntity"); RCKKeyedAnimation parent(&context, "CloneParent");
    AddTimedAnimationKeys(animation, 4.0f); AddTimedAnimationKeys(first, 2.0f); AddTimedAnimationKeys(second, 10.0f);
    animation.SetCloneState(&entity, &first, &second, CK_OBJECTANIMATION_IGNOREMORPH | (Merged ? CK_OBJECTANIMATION_MERGED : 0));
    animation.SetParent(&parent); alias.ShareDataFrom(&animation);
    const ObjectCloneDataSnapshot before = SnapshotObjectCloneData(animation);
    animation.Clone(&animation);
    CheckObjectCloneData(animation, before, &animation); CheckObjectCloneData(alias, before, &animation);
    Check(animation.References() == 2, "Self Object Clone changed the reference count");
    CheckObjectCloneFields(animation, &entity, &first, &second, CK_OBJECTANIMATION_IGNOREMORPH | (Merged ? CK_OBJECTANIMATION_MERGED : 0), &parent);
    CheckObjectCloneReload(animation, context, 7.0f);
}

template<int Relationship>
void ObjectAnimationClonePreservesSameDataAliases() {
    CKContext context(nullptr, 0, 0);
    ObjectCloneProbe owner(&context, "CloneOwner"), first(&context, "CloneAliasFirst"), second(&context, "CloneAliasSecond");
    AddTimedAnimationKeys(owner, 4.0f); first.ShareDataFrom(&owner); second.ShareDataFrom(&owner);
    ObjectCloneProbe &source = Relationship == 0 ? owner : first;
    ObjectCloneProbe &target = Relationship == 1 ? owner : second;
    RCKObjectAnimation mergedFirst(&context, "MergedCloneFirst"), mergedSecond(&context, "MergedCloneSecond");
    AddTimedAnimationKeys(mergedFirst, 2.0f); AddTimedAnimationKeys(mergedSecond, 10.0f);
    RCK3dEntity entity(&context, "AliasCloneEntity"); RCKKeyedAnimation parent(&context, "AliasCloneParent");
    source.SetCloneState(&entity, &mergedFirst, &mergedSecond, CK_OBJECTANIMATION_MERGED);
    target.SetCloneState(nullptr, nullptr, nullptr, CK_OBJECTANIMATION_IGNOREPOS); target.SetParent(&parent);
    const ObjectCloneDataSnapshot before = SnapshotObjectCloneData(owner);
    target.Clone(&source);
    CheckObjectCloneData(owner, before, &owner); CheckObjectCloneData(first, before, &owner); CheckObjectCloneData(second, before, &owner);
    Check(target.References() == 3, "Same-data Object Clone changed reference count");
    CheckObjectCloneFields(target, &entity, &mergedFirst, &mergedSecond, CK_OBJECTANIMATION_MERGED, &parent);
    CheckObjectCloneFields(source, &entity, &mergedFirst, &mergedSecond, CK_OBJECTANIMATION_MERGED, nullptr);
    CheckObjectCloneReload(target, context, 7.0f);
}

template<bool TargetOwnsOldData>
void ObjectAnimationClonePreservesReleasedDataAndSourceLifetime() {
    CKContext context(nullptr, 0, 0);
    ObjectCloneProbe oldOwner(&context, "CloneOldOwner"), oldAlias(&context, "CloneOldAlias"), target(&context, "CloneTarget");
    ObjectCloneProbe &destination = TargetOwnsOldData ? target : oldAlias;
    ObjectCloneProbe &retained = TargetOwnsOldData ? oldAlias : oldOwner;
    ObjectCloneProbe &initialOwner = TargetOwnsOldData ? target : oldOwner;
    AddTimedAnimationKeys(initialOwner, 40.0f);
    oldAlias.ShareDataFrom(&initialOwner);
    const ObjectCloneDataSnapshot oldData = SnapshotObjectCloneData(initialOwner);
    {
        ObjectCloneProbe source(&context, "CloneScopedSource"); AddTimedAnimationKeys(source, 4.0f);
        source.SetCloneState(nullptr, nullptr, nullptr, CK_OBJECTANIMATION_IGNOREMORPH);
        const ObjectCloneDataSnapshot sourceData = SnapshotObjectCloneData(source);
        destination.Clone(&source);
        CheckObjectCloneData(retained, oldData, &retained);
        CheckObjectCloneData(destination, sourceData, &source); CheckObjectCloneData(source, sourceData, &source);
        Check(retained.References() == 1 && source.References() == 2, "Different-data Object Clone changed reference counts");
        CheckObjectCloneReload(destination, context, 7.0f);
        CheckObjectCloneReload(retained, context, 43.0f);
    }
    Check(destination.Shared() == &destination && destination.References() == 1, "Source destruction did not transfer cloned data ownership");
    CheckObjectCloneReload(destination, context, 7.0f);
    CheckObjectCloneReload(retained, context, 43.0f);
}

void ObjectAnimationNullCloneClearsOnlyTarget() {
    CKContext context(nullptr, 0, 0);
    ObjectCloneProbe target(&context, "NullCloneTarget"), alias(&context, "NullCloneAlias");
    AddTimedAnimationKeys(target, 4.0f); alias.ShareDataFrom(&target);
    target.SetCloneState(nullptr, nullptr, nullptr, CK_OBJECTANIMATION_IGNOREPOS);
    const ObjectCloneDataSnapshot before = SnapshotObjectCloneData(alias);
    target.Clone(nullptr);
    CheckObjectCloneData(alias, before, &alias); CheckObjectCloneReload(alias, context, 7.0f);
    Check(target.Data() != before.Data && target.Shared() == &target && target.References() == 1 && alias.References() == 1 &&
          target.GetLength() == 0.0f && !target.GetPositionController() && !target.GetMorphController() && target.GetFlags() == 0 &&
          !target.Get3dEntity() && !target.First() && !target.Second() && target.GetCurrentStep() == 0.0f && target.GetMergeFactor() == 0.5f,
          "Null Object Clone did not clear only the target into owned empty data");
    Chunk saved(target.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
    RCKObjectAnimation reloaded(&context, "NullCloneReloaded"); LoadChunk(reloaded, saved.get());
    Check(reloaded.GetLength() == 0.0f && !reloaded.GetPositionController() && reloaded.Shared() == &reloaded,
          "Null Object Clone empty data did not survive Save/Load");
}

void ObjectAnimationCloneHandlesSourceWithoutData() {
    CKContext context(nullptr, 0, 0);
    ObjectCloneProbe source(&context, "MissingCloneData"), target(&context, "MissingDataTarget"), alias(&context, "MissingDataAlias");
    source.DropData(); source.SetCloneState(nullptr, nullptr, nullptr, CK_OBJECTANIMATION_IGNOREMORPH);
    AddTimedAnimationKeys(target, 4.0f); alias.ShareDataFrom(&target);
    const ObjectCloneDataSnapshot before = SnapshotObjectCloneData(alias);
    target.Clone(&source);
    CheckObjectCloneData(alias, before, &alias);
    Check(!source.Data() && target.Data() && target.Shared() == &target && target.GetLength() == 0.0f &&
          !target.GetPositionController() && target.References() == 1, "Missing-data Object Clone did not create independent empty data");
    CheckObjectCloneFields(target, nullptr, nullptr, nullptr, CK_OBJECTANIMATION_IGNOREMORPH, nullptr);
}

template<bool MissingDataSource>
void ObjectAnimationEmptyClonePreservesStateOnAllocationFailure() {
    CKContext context(nullptr, 0, 0);
    ObjectCloneProbe target(&context, "FailedCloneTarget"), alias(&context, "FailedCloneAlias"), source(&context, "FailedCloneSource");
    RCKKeyedAnimation parent(&context, "FailedCloneParent");
    AddTimedAnimationKeys(target, 4.0f); alias.ShareDataFrom(&target);
    target.SetCloneState(nullptr, nullptr, nullptr, CK_OBJECTANIMATION_IGNOREMORPH); target.SetParent(&parent);
    source.DropData();
    const ObjectCloneDataSnapshot before = SnapshotObjectCloneData(target);
    s_ObjectAnimationAllocationFailure = true;
    target.Clone(MissingDataSource ? &source : nullptr);
    const bool injected = !s_ObjectAnimationAllocationFailure;
    s_ObjectAnimationAllocationFailure = false;
    Check(injected, "Empty Object Clone did not exercise its allocation failure");
    CheckObjectCloneData(target, before, &target); CheckObjectCloneData(alias, before, &target);
    Check(target.References() == 2, "Failed empty Object Clone changed reference counts");
    CheckObjectCloneFields(target, nullptr, nullptr, nullptr, CK_OBJECTANIMATION_IGNOREMORPH, &parent);
    CheckObjectCloneReload(target, context, 7.0f);
}

template<int Case>
void MergedAnimationTimeMappingRemainsFinite() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation first(&context, "TimeFirst"), second(&context, "TimeSecond"),
        merged(&context, "TimeMerged");
    const float span = Case == 2 ? 2.0e-30f : (Case == 4 ? 10.0f : 1.0f);
    AddTimedAnimationKeys(first, 2.0f, span);
    AddTimedAnimationKeys(second, 10.0f, span);
    const float length = Case == 0 ? 0.0f : (Case == 1 ? 1.0e-40f :
        (Case == 2 ? 1.0e30f : (Case == 3 ? 20.0f : -20.0f)));
    LoadMergedState(merged, &first, &second, length);
    if (Case == 1) { first.SetLength(length); second.SetLength(length); }
    if (Case == 2) { first.SetLength(length); second.SetLength(length); }
    if (Case == 3) { first.SetLength(0.0f); second.SetLength(0.0f); }
    const float expectedX = Case == 0 || Case == 3 ? 4.0f : 9.0f;
    for (int pass = 0; pass < 2; ++pass) {
        const float time = Case == 0 ? (pass ? 7.0f : 0.0f) :
            (Case == 1 ? 0.5f : (Case == 2 ? 1.0e-30f : (Case == 3 ? 7.0f : -10.0f)));
        VxVector output(123.0f);
        Check(merged.EvaluatePosition(time, output) && std::fabs(output.x - expectedX) < 5.0e-5f,
              "Merged position received invalid or prematurely rounded source time");
        Check(merged.EvaluateScale(time, output) && std::fabs(output.x - expectedX) < 5.0e-5f,
              "Merged scale received invalid source time");
        VxQuaternion rotation;
        const float expectedZ = Case == 0 || Case == 3 ? 0.0f : std::sqrt(0.5f);
        const float expectedW = Case == 0 || Case == 3 ? 1.0f : std::sqrt(0.5f);
        Check(merged.EvaluateRotation(time, rotation) && std::fabs(rotation.z - expectedZ) < 5.0e-5f &&
              std::fabs(rotation.w - expectedW) < 5.0e-5f, "Merged rotation received invalid source time");
        Check(merged.EvaluateScaleAxis(time, rotation) && std::fabs(rotation.z - expectedZ) < 5.0e-5f &&
              std::fabs(rotation.w - expectedW) < 5.0e-5f, "Merged scale-axis received invalid source time");
        Check(merged.EvaluateMorphTarget(time, 1, &output, sizeof(output), nullptr) &&
              std::fabs(output.x - expectedX) < 5.0e-5f, "Merged Morph received invalid source time");
        Chunk saved(merged.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
        LoadChunk(merged, saved.get());
    }
}

float FloatWithBits(CKDWORD bits) {
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

void MergedAnimationRejectsNonfiniteTimeState() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation first(&context, "FiniteFirst"), second(&context, "FiniteSecond"), merged(&context, "NonfiniteMerged");
    AddTimedAnimationKeys(first, 2.0f);
    AddTimedAnimationKeys(second, 10.0f);
    const CKDWORD bits[] = {0x7f800000u, 0xff800000u, 0x7fc00000u};
    for (int field = 0; field < 4; ++field) {
        for (int i = 0; i < 3; ++i) {
            LoadMergedState(merged, &first, &second, field == 0 ? FloatWithBits(bits[i]) : 20.0f);
            first.SetLength(field == 1 ? FloatWithBits(bits[i]) : 10.0f);
            second.SetLength(field == 1 ? FloatWithBits(bits[i]) : 10.0f);
            merged.SetMergeFactor(field == 2 ? FloatWithBits(bits[i]) : 0.25f);
            const float time = field == 3 ? FloatWithBits(bits[i]) : 7.0f;
            VxVector output(123.0f);
            VxQuaternion rotation(0.5f, 0.5f, 0.5f, 0.5f);
            Check(!merged.EvaluatePosition(time, output), "Merged position accepted nonfinite time state");
            Check(!merged.EvaluateScale(time, output), "Merged scale accepted nonfinite time state");
            Check(!merged.EvaluateRotation(time, rotation), "Merged rotation accepted nonfinite time state");
            Check(!merged.EvaluateScaleAxis(time, rotation), "Merged scale-axis accepted nonfinite time state");
            output.Set(123.0f, 123.0f, 123.0f);
            Check(!merged.EvaluateMorphTarget(time, 1, &output, sizeof(output), nullptr) && Equal(output, VxVector(123.0f)),
                  "Failed nonfinite merged Morph modified the output buffer");
        }
    }
}

template<int Disabled>
void MergedMorphUsesOnlySuccessfulSources() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation first(&context, "FailMorphFirst"), second(&context, "FailMorphSecond"), merged(&context, "FailMorphMerged");
    AddTimedAnimationKeys(first, 2.0f);
    AddTimedAnimationKeys(second, 10.0f);
    first.SetFlags((Disabled & 1) ? CK_OBJECTANIMATION_IGNOREMORPH : 0);
    second.SetFlags((Disabled & 2) ? CK_OBJECTANIMATION_IGNOREMORPH : 0);
    LoadMergedState(merged, &first, &second);
    VxVector output(123.0f), expected;
    VxCompressedVector normal, expectedNormal;
    normal.xa = 111; normal.ya = 222;
    if (Disabled != 3) {
        RCKObjectAnimation &available = Disabled == 1 ? second : first;
        Check(available.EvaluateMorphTarget(3.5f, 1, &expected, sizeof(expected), &expectedNormal), "Morph control source failed");
    }
    bool result = merged.EvaluateMorphTarget(7.0f, 1, &output, sizeof(output), &normal) != FALSE;
    Check(result == (Disabled != 3), "Merged Morph ignored failed source results");
    if (result) {
        Check(Equal(output, expected) && !memcmp(&normal, &expectedNormal, sizeof(normal)), "Merged Morph blended unavailable source buffers");
    } else {
        Check(Equal(output, VxVector(123.0f)) && normal.xa == 111 && normal.ya == 222,
              "Failed merged Morph changed caller buffers");
    }
}

void MergedMorphSupportsOptionalOutputBuffers() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation first(&context, "OptionalFirst"), second(&context, "OptionalSecond"), merged(&context, "OptionalMerged");
    AddTimedAnimationKeys(first, 2.0f);
    AddTimedAnimationKeys(second, 10.0f);
    LoadMergedState(merged, &first, &second);
    VxCompressedVector expected, actual;
    VxVector position;
    Check(merged.EvaluateMorphTarget(7.0f, 1, &position, sizeof(position), &expected), "Optional Morph control evaluation failed");
    Check(merged.EvaluateMorphTarget(7.0f, 1, nullptr, 0, &actual) && !memcmp(&actual, &expected, sizeof(actual)),
          "Normals-only merged Morph failed");
    Check(merged.EvaluateMorphTarget(7.0f, 1, nullptr, 0, nullptr), "Merged Morph rejected omitted outputs");
    Check(merged.EvaluateMorphTarget(7.0f, 0, nullptr, 0, nullptr), "Merged Morph rejected a zero-vertex request");
}

void MergedMorphSubsetPreservesBufferBounds() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation first(&context, "SubsetFirst"), second(&context, "SubsetSecond"), merged(&context, "SubsetMerged");
    RCKObjectAnimation *sources[] = {&first, &second};
    for (int source = 0; source < 2; ++source) {
        RCKMorphController *morph = static_cast<RCKMorphController *>(sources[source]->CreateController(CKANIMATION_MORPH_CONTROL));
        morph->SetMorphVertexCount(3);
        CKMorphKey *key = static_cast<CKMorphKey *>(morph->GetKey(morph->AddKey(0.0f, TRUE)));
        for (int vertex = 0; vertex < 3; ++vertex) {
            key->PosArray[vertex] = VxVector((source ? 10.0f : 2.0f) + vertex, 3.0f, 4.0f);
            key->NormArray[vertex].xa = source ? 1000 : 200;
            key->NormArray[vertex].ya = 300;
        }
        sources[source]->SetLength(10.0f);
    }
    LoadMergedState(merged, &first, &second);
    struct StridedOutput { CKDWORD Before; VxVector Value; CKDWORD After; };
    StridedOutput output[3];
    VxCompressedVector normals[3];
    for (int i = 0; i < 3; ++i) {
        output[i].Before = output[i].After = 0x12345678u;
        output[i].Value.Set(123.0f, 123.0f, 123.0f);
        normals[i].xa = 111; normals[i].ya = 222;
    }
    Check(merged.EvaluateMorphTarget(7.0f, 2, &output[0].Value, sizeof(output[0]), normals), "Merged Morph subset failed");
    for (int i = 0; i < 3; ++i) {
        Check(output[i].Before == 0x12345678u && output[i].After == 0x12345678u, "Merged Morph changed stride padding");
        if (i < 2) {
            Check(Equal(output[i].Value, VxVector(4.0f + i, 3.0f, 4.0f)) && normals[i].xa == 400 && normals[i].ya == 300,
                  "Merged Morph subset changed positions or packed normals");
        } else {
            Check(Equal(output[i].Value, VxVector(123.0f)) && normals[i].xa == 111 && normals[i].ya == 222,
                  "Merged Morph overwrote the unrequested tail");
        }
    }
    StridedOutput before[3];
    VxCompressedVector normalsBefore[3];
    memcpy(before, output, sizeof(output)); memcpy(normalsBefore, normals, sizeof(normals));
    const int invalidCounts[] = {-1, 4, INT_MAX};
    for (int i = 0; i < 3; ++i) {
        Check(!merged.EvaluateMorphTarget(7.0f, invalidCounts[i], &output[0].Value, sizeof(output[0]), normals),
              "Merged Morph accepted an invalid vertex request");
        Check(!memcmp(before, output, sizeof(output)) && !memcmp(normalsBefore, normals, sizeof(normals)),
              "Invalid merged Morph request modified caller buffers");
    }
}

void MergedMorphPreservesAbsentKeyPayloads() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation first(&context, "PartialFirst"), second(&context, "PartialSecond"), merged(&context, "PartialMerged");
    AddTimedAnimationKeys(first, 2.0f); AddTimedAnimationKeys(second, 10.0f);
    CKMorphKey *key = static_cast<CKMorphKey *>(first.GetMorphController()->GetKey(1));
    delete[] key->PosArray; key->PosArray = nullptr;
    delete[] key->NormArray; key->NormArray = nullptr;
    second.SetFlags(CK_OBJECTANIMATION_IGNOREMORPH);
    LoadMergedState(merged, &first, &second);
    VxVector output(123.0f);
    VxCompressedVector normal; normal.xa = 111; normal.ya = 222;
    Check(merged.EvaluateMorphTarget(7.0f, 1, &output, sizeof(output), &normal) && Equal(output, VxVector(123.0f)) &&
          normal.xa == 111 && normal.ya == 222, "Merged Morph changed an output omitted by the available key");
}

void AddMorphRequestKeys(RCKObjectAnimation &animation, float firstTime = 0.0f, float lastTime = 10.0f) {
    RCKMorphController *morph = static_cast<RCKMorphController *>(animation.CreateController(CKANIMATION_MORPH_CONTROL));
    morph->SetMorphVertexCount(3);
    for (int i = 0; i < 2; ++i) {
        CKMorphKey *key = static_cast<CKMorphKey *>(morph->GetKey(morph->AddKey(i ? lastTime : firstTime, TRUE)));
        for (int vertex = 0; vertex < 3; ++vertex) {
            key->PosArray[vertex].Set((i ? 10.0f : 2.0f) + vertex, i ? 7.0f : 3.0f, i ? 8.0f : 4.0f);
            key->NormArray[vertex].xa = i ? 1000 : 200;
            key->NormArray[vertex].ya = 300;
        }
    }
    animation.SetLength(10.0f);
}

template<int InvalidKind>
void LoadedMorphRejectsInvalidRequests() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation source(&context, "MorphRequestSource"), loaded(&context, "MorphRequestLoaded");
    AddMorphRequestKeys(source);
    Chunk chunk(source.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
    LoadChunk(loaded, chunk.get());
    RCKObjectAnimation *targets[] = {&source, &loaded};
    const int invalidCounts[] = {4, INT_MAX, -1};
    const float invalidTimes[] = {FloatWithBits(0x7FC00000u), FloatWithBits(0x7F800000u), FloatWithBits(0xFF800000u)};
    for (int targetIndex = 0; targetIndex < 2; ++targetIndex) {
        RCKObjectAnimation &target = *targets[targetIndex];
        for (int i = 0; i < 3; ++i) {
            const int count = InvalidKind == 0 ? invalidCounts[i] : 2;
            const float time = InvalidKind == 1 ? invalidTimes[i] : 5.0f;
            CKMorphKey *first = static_cast<CKMorphKey *>(target.GetMorphController()->GetKey(0));
            if (InvalidKind == 2) first->TimeStep = invalidTimes[i];
            // Start without outputs to reproduce bad acceptance without an
            // out-of-bounds read; then verify rejected calls preserve bytes.
            Check(!target.EvaluateMorphTarget(time, count, nullptr, 0, nullptr), "Morph accepted an invalid request/key time");
            VxVector positions[3];
            VxCompressedVector normals[3];
            for (int vertex = 0; vertex < 3; ++vertex) {
                positions[vertex].Set(123.0f, 234.0f, 345.0f);
                normals[vertex].xa = 111; normals[vertex].ya = 222;
            }
            VxVector positionsBefore[3];
            VxCompressedVector normalsBefore[3];
            memcpy(positionsBefore, positions, sizeof(positions)); memcpy(normalsBefore, normals, sizeof(normals));
            Check(!target.EvaluateMorphTarget(time, count, positions, sizeof(VxVector), normals), "Morph wrote an invalid request/key time");
            Check(!memcmp(positionsBefore, positions, sizeof(positions)) && !memcmp(normalsBefore, normals, sizeof(normals)),
                  "Failed Morph request changed caller bytes");
            if (InvalidKind == 2) first->TimeStep = 0.0f;
        }
        if (InvalidKind == 2) {
            CKMorphKey *last = static_cast<CKMorphKey *>(target.GetMorphController()->GetKey(1));
            for (int i = 0; i < 4; ++i) {
                last->TimeStep = i < 3 ? invalidTimes[i] : -1.0f;
                VxVector output(123.0f);
                Check(!target.EvaluateMorphTarget(0.0f, 1, &output, sizeof(output), nullptr) && Equal(output, VxVector(123.0f)),
                      "Morph accepted a nonfinite or unordered unselected key");
            }
            last->TimeStep = 10.0f;
        }
    }
}

void LoadedMorphRetainsValidOutputContracts() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation source(&context, "MorphOutputSource"), loaded(&context, "MorphOutputLoaded");
    AddMorphRequestKeys(source);
    Chunk chunk(source.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
    LoadChunk(loaded, chunk.get());
    RCKObjectAnimation *targets[] = {&source, &loaded};
    const float times[] = {-1.0f, 0.0f, 5.0f, 10.0f, 11.0f};
    struct StridedOutput { CKDWORD Before; VxVector Value; CKDWORD After; };
    for (int targetIndex = 0; targetIndex < 2; ++targetIndex) {
        RCKObjectAnimation &target = *targets[targetIndex];
        for (int sample = 0; sample < 5; ++sample) {
            StridedOutput output[3];
            VxCompressedVector normals[3];
            for (int i = 0; i < 3; ++i) {
                output[i].Before = output[i].After = 0x12345678u;
                output[i].Value.Set(123.0f, 123.0f, 123.0f);
                normals[i].xa = 111; normals[i].ya = 222;
            }
            const float phase = sample < 2 ? 0.0f : (sample == 2 ? 0.5f : 1.0f);
            Check(target.EvaluateMorphTarget(times[sample], 2, &output[0].Value, sizeof(output[0]), normals), "Valid Morph subset failed");
            for (int i = 0; i < 3; ++i) {
                Check(output[i].Before == 0x12345678u && output[i].After == 0x12345678u, "Morph changed output stride padding");
                if (i < 2) {
                    Check(Equal(output[i].Value, VxVector(2.0f + i + 8.0f * phase, 3.0f + 4.0f * phase, 4.0f + 4.0f * phase)) &&
                          normals[i].xa == static_cast<short>(200 + 800 * phase) && normals[i].ya == 300,
                          "Morph output differs from independent linear values");
                } else {
                    Check(Equal(output[i].Value, VxVector(123.0f)) && normals[i].xa == 111 && normals[i].ya == 222,
                          "Morph changed the unrequested tail");
                }
            }
            VxCompressedVector normalsOnly[2];
            Check(target.EvaluateMorphTarget(times[sample], 2, nullptr, 0, normalsOnly) && !memcmp(normalsOnly, normals, sizeof(normalsOnly)),
                  "Normals-only Morph changed results");
            VxVector positionsOnly[2];
            Check(target.EvaluateMorphTarget(times[sample], 2, positionsOnly, sizeof(VxVector), nullptr) &&
                  Equal(positionsOnly[0], output[0].Value) && Equal(positionsOnly[1], output[1].Value), "Position-only Morph changed results");
            Check(target.EvaluateMorphTarget(times[sample], 2, nullptr, 0, nullptr), "Morph rejected omitted outputs");
            Check(target.EvaluateMorphTarget(times[sample], 0, nullptr, 0, nullptr), "Morph rejected a zero-vertex request");
        }
    }
    RCKMorphController empty;
    Check(!empty.Evaluate(5.0f, 0, nullptr, 0, nullptr), "Keyless Morph succeeded");
    empty.AddKey(0.0f, TRUE);
    Check(empty.Evaluate(5.0f, 0, nullptr, 0, nullptr) && !empty.Evaluate(5.0f, 1, nullptr, 0, nullptr),
          "Zero-sized Morph key request contract changed");
    // Duplicate stored times remain valid: the first equal key is selected,
    // and a time beyond the duplicate group selects its final key.
    CKMorphKey *last = static_cast<CKMorphKey *>(source.GetMorphController()->GetKey(1));
    last->TimeStep = 0.0f;
    Chunk repeated(source.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
    LoadChunk(loaded, repeated.get());
    VxVector value;
    Check(loaded.EvaluateMorphTarget(0.0f, 1, &value, sizeof(value), nullptr) && Equal(value, VxVector(2.0f, 3.0f, 4.0f)),
          "Morph rejected or changed the first repeated-time key");
    Check(loaded.EvaluateMorphTarget(1.0f, 1, &value, sizeof(value), nullptr) && Equal(value, VxVector(10.0f, 7.0f, 8.0f)),
          "Morph rejected or changed the final repeated-time key");
}

template<int WideCase>
void LoadedMorphInterpolationAvoidsIntermediateLoss() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation source(&context, "WideMorphSource"), loaded(&context, "WideMorphLoaded");
    const float firstTime = WideCase == 0 || WideCase == 4 ? -FLT_MAX : 0.0f;
    const float lastTime = WideCase == 1 || WideCase == 3 ? 10.0f : (WideCase == 4 ? FLT_MIN : FLT_MAX);
    AddMorphRequestKeys(source, firstTime, lastTime);
    RCKMorphController *morph = static_cast<RCKMorphController *>(source.GetMorphController());
    CKMorphKey *first = static_cast<CKMorphKey *>(morph->GetKey(0));
    CKMorphKey *last = static_cast<CKMorphKey *>(morph->GetKey(1));
    if (WideCase == 1) {
        first->PosArray[0].Set(-FLT_MAX, FLT_MAX, 3.0f);
        last->PosArray[0].Set(FLT_MAX, -FLT_MAX, 5.0f);
    } else if (WideCase == 2) {
        first->PosArray[0].Set(0.0f, 3.0f, 4.0f);
        last->PosArray[0].Set(FLT_MAX, 3.0f, 4.0f);
    } else if (WideCase == 3) {
        first->PosArray[0].Set(FLT_MAX, -FLT_MAX, FLT_MAX);
        last->PosArray[0].Set(1.0f, 2.0f, 3.0f);
    } else if (WideCase == 4) {
        first->PosArray[0].Set(FLT_MAX, 3.0f, 4.0f);
        last->PosArray[0].Set(0.0f, 3.0f, 4.0f);
    }
    Chunk chunk(source.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
    LoadChunk(loaded, chunk.get());
    RCKObjectAnimation *targets[] = {&source, &loaded};
    for (int targetIndex = 0; targetIndex < 2; ++targetIndex) {
        VxVector output(123.0f);
        VxCompressedVector normal; normal.xa = 111; normal.ya = 222;
        const float time = WideCase == 0 || WideCase == 4 ? 0.0f : (WideCase == 1 ? 5.0f : (WideCase == 3 ? 10.0f : FLT_MIN));
        Check(targets[targetIndex]->EvaluateMorphTarget(time, 1, &output, sizeof(output), &normal), "Wide finite Morph evaluation failed");
        if (WideCase == 0) {
            Check(Equal(output, VxVector(6.0f, 5.0f, 6.0f)) && normal.xa == 600 && normal.ya == 300,
                  "Morph lost a finite phase across large key times");
        } else if (WideCase == 1) {
            Check(Equal(output, VxVector(0.0f, 0.0f, 4.0f)), "Morph overflowed a finite vector interpolation");
        } else if (WideCase == 3) {
            Check(Equal(output, VxVector(1.0f, 2.0f, 3.0f)), "Morph lost the exact last key through cancellation");
        } else {
            Check(std::fabs(output.x - FLT_MIN) <= FLT_MIN * 1.0e-5f && output.y == 3.0f && output.z == 4.0f,
                  "Morph discarded a representable contribution from a tiny phase");
        }
    }
}

void CheckResizedMorphKeys(RCKMorphController &controller, int count, int retained, bool normals) {
    Check(controller.GetMorphVertexCount() == count && controller.GetKeyCount() == 2 && controller.GetLength() == 10.0f,
          "Morph resize changed controller/key counts or length");
    CKDWORD expected[45] = {};
    expected[0] = 2; expected[1] = count; expected[2] = normals ? 1 : 0;
    int word = 3;
    for (int keyIndex = 0; keyIndex < 2; ++keyIndex) {
        CKMorphKey *key = static_cast<CKMorphKey *>(controller.GetKey(keyIndex));
        const float time = keyIndex ? 10.0f : 0.0f;
        Check(key->TimeStep == time && key->PosArray && (key->NormArray != nullptr) == normals,
              "Morph resize changed key time or array presence");
        memcpy(expected + word++, &time, sizeof(time));
        for (int vertex = 0; vertex < count; ++vertex) {
            const VxVector value = vertex < retained ? VxVector((keyIndex ? 10.0f : 2.0f) + vertex,
                keyIndex ? 7.0f : 3.0f, keyIndex ? 8.0f : 4.0f) : VxVector(0.0f);
            Check(Equal(key->PosArray[vertex], value), "Morph resize lost the retained prefix or failed to clear the new tail");
            memcpy(expected + word, &value, sizeof(value)); word += 3;
        }
        if (normals) {
            for (int vertex = 0; vertex < count; ++vertex) {
                const CKDWORD packed = vertex < retained ? (300u << 16) | (keyIndex ? 1000u : 200u) : 0u;
                Check(!memcmp(key->NormArray + vertex, &packed, sizeof(packed)), "Morph resize changed normal prefix/tail bytes");
                expected[word++] = packed;
            }
        }
    }
    CKDWORD output[47];
    for (int i = 0; i < 47; ++i) output[i] = 0x12345678u;
    const int bytes = word * sizeof(CKDWORD);
    Check(controller.DumpKeysTo(nullptr) == bytes && controller.DumpKeysTo(output + 1) == bytes,
          "Resized Morph wire size changed");
    Check(output[0] == 0x12345678u && !memcmp(output + 1, expected, bytes), "Resized Morph wire data differs from independent values");
    for (int i = word + 1; i < 47; ++i) Check(output[i] == 0x12345678u, "Morph Save exceeded its declared size");
    VxVector values[5];
    VxCompressedVector packedNormals[5];
    Check(controller.Evaluate(5.0f, count, values, sizeof(VxVector), normals ? packedNormals : nullptr), "Resized Morph evaluation failed");
    for (int vertex = 0; vertex < count; ++vertex) {
        Check(Equal(values[vertex], vertex < retained ? VxVector(6.0f + vertex, 5.0f, 6.0f) : VxVector(0.0f)),
              "Resized Morph interpolated the wrong prefix/tail");
        if (normals) Check(packedNormals[vertex].xa == (vertex < retained ? 600 : 0) && packedNormals[vertex].ya == (vertex < retained ? 300 : 0),
                           "Resized Morph normal interpolation changed");
    }
}

template<bool Normals>
void LoadedMorphCountResizesStorage() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation source(&context, "MorphResizeSource"), loaded(&context, "MorphResizeLoaded");
    AddMorphRequestKeys(source);
    if (!Normals) {
        for (int i = 0; i < 2; ++i) {
            CKMorphKey *key = static_cast<CKMorphKey *>(source.GetMorphController()->GetKey(i));
            delete[] key->NormArray; key->NormArray = nullptr;
        }
    }
    Chunk initial(source.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
    LoadChunk(loaded, initial.get());
    RCKMorphController *controller = static_cast<RCKMorphController *>(loaded.GetMorphController());
    const int counts[] = {5, 2, 4, 0, 1};
    const int retained[] = {3, 2, 2, 0, 0};
    for (int step = 0; step < 5; ++step) {
        CKMorphKey *first = static_cast<CKMorphKey *>(controller->GetKey(0));
        VxVector *oldPosition = first->PosArray;
        VxCompressedVector *oldNormal = first->NormArray;
        controller->SetMorphVertexCount(counts[step]);
        Check(controller->GetKey(0) == first && first->PosArray != oldPosition && (!Normals || first->NormArray != oldNormal),
              "Morph count change retained old capacity instead of resizing payloads");
        CheckResizedMorphKeys(*controller, counts[step], retained[step], Normals);
        VxVector *samePosition = first->PosArray;
        VxCompressedVector *sameNormal = first->NormArray;
        controller->SetMorphVertexCount(counts[step]);
        Check(first->PosArray == samePosition && first->NormArray == sameNormal, "Same-count Morph update replaced payloads");
        RCKMorphController copy;
        Check(copy.Clone(controller), "Resized Morph Clone failed");
        CheckResizedMorphKeys(copy, counts[step], retained[step], Normals);
        Check(copy.GetKey(0) != first && static_cast<CKMorphKey *>(copy.GetKey(0))->PosArray != first->PosArray,
              "Resized Morph Clone aliases source storage");
        Chunk saved(loaded.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
        RCKObjectAnimation reloaded(&context, "MorphResizeReloaded");
        LoadChunk(reloaded, saved.get());
        CheckResizedMorphKeys(*static_cast<RCKMorphController *>(reloaded.GetMorphController()), counts[step], retained[step], Normals);
    }
}

void MorphCountRejectsInvalidSizes() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation animation(&context, "MorphInvalidSize");
    AddMorphRequestKeys(animation);
    RCKMorphController *controller = static_cast<RCKMorphController *>(animation.GetMorphController());
    CKMorphKey *key = static_cast<CKMorphKey *>(controller->GetKey(0));
    VxVector *positions = key->PosArray;
    VxCompressedVector *normals = key->NormArray;
    const int invalid[] = {-1, INT_MAX};
    for (int i = 0; i < 2; ++i) {
        controller->SetMorphVertexCount(invalid[i]);
        Check(controller->GetMorphVertexCount() == 3 && key->PosArray == positions && key->NormArray == normals,
              "Invalid Morph count changed state");
        CheckResizedMorphKeys(*controller, 3, 3, true);
    }
    RCKMorphController empty;
    for (int i = 0; i < 2; ++i) {
        empty.SetMorphVertexCount(invalid[i]);
        Check(empty.GetMorphVertexCount() == 0 && empty.GetKeyCount() == 0, "Empty Morph accepted an unrepresentable count");
    }
}

void MorphCountPreservesOmittedPayloads() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation animation(&context, "MorphOmittedPayloadResize");
    AddMorphRequestKeys(animation);
    RCKMorphController *controller = static_cast<RCKMorphController *>(animation.GetMorphController());
    CKMorphKey *first = static_cast<CKMorphKey *>(controller->GetKey(0));
    CKMorphKey *last = static_cast<CKMorphKey *>(controller->GetKey(1));
    delete[] first->PosArray; first->PosArray = nullptr;
    delete[] first->NormArray; first->NormArray = nullptr;
    VxVector *oldLast = last->PosArray;
    controller->SetMorphVertexCount(5);
    Check(!first->PosArray && !first->NormArray && last->PosArray != oldLast && last->NormArray,
          "Morph resize changed omitted payload presence or left old capacity");
    VxVector values[5];
    VxCompressedVector normals[5];
    for (int i = 0; i < 5; ++i) { values[i].Set(123.0f, 123.0f, 123.0f); normals[i].xa = 111; normals[i].ya = 222; }
    Check(controller->Evaluate(0.0f, 5, values, sizeof(VxVector), normals), "Omitted first-key evaluation failed");
    for (int i = 0; i < 5; ++i) Check(Equal(values[i], VxVector(123.0f)) && normals[i].xa == 111 && normals[i].ya == 222,
                                      "Resizing an omitted payload changed caller outputs");
    Check(controller->Evaluate(11.0f, 5, values, sizeof(VxVector), normals), "Resized last-key evaluation failed");
    for (int i = 0; i < 5; ++i) Check(Equal(values[i], i < 3 ? VxVector(10.0f + i, 7.0f, 8.0f) : VxVector(0.0f)) &&
                                      normals[i].xa == (i < 3 ? 1000 : 0) && normals[i].ya == (i < 3 ? 300 : 0),
                                      "Partial Morph resize lost its valid prefix/tail");
    RCKMorphController copy;
    Check(copy.Clone(controller) && !static_cast<CKMorphKey *>(copy.GetKey(0))->PosArray &&
          !static_cast<CKMorphKey *>(copy.GetKey(0))->NormArray, "Resize/Clone materialized omitted arrays");
}

void MorphCountResizeComposesWithKeyEditing() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation animation(&context, "MorphResizeThenEdit");
    AddMorphRequestKeys(animation);
    RCKMorphController *controller = static_cast<RCKMorphController *>(animation.GetMorphController());
    CKMorphKey *first = static_cast<CKMorphKey *>(controller->GetKey(0));
    VxVector *oldPosition = first->PosArray;
    controller->SetMorphVertexCount(5);
    Check(first->PosArray != oldPosition, "Morph edit fixture retained undersized storage");
    CKMorphKey source = *first;
    source.TimeStep = -5.0f;
    Check(controller->AddKey(&source, FALSE) == 0, "Morph insertion after resize lost time ordering");
    CKMorphKey *added = static_cast<CKMorphKey *>(controller->GetKey(0));
    Check(added->PosArray != source.PosArray && added->NormArray != source.NormArray && added->NormArray,
          "Morph insertion after resize lost independent normal/position storage");
    for (int i = 0; i < 5; ++i) Check(Equal(added->PosArray[i], i < 3 ? VxVector(2.0f + i, 3.0f, 4.0f) : VxVector(0.0f)) &&
                                     added->NormArray[i].xa == (i < 3 ? 200 : 0) && added->NormArray[i].ya == (i < 3 ? 300 : 0),
                                     "Morph insertion failed to copy the resized prefix/tail");
    controller->RemoveKey(1);
    controller->SetMorphVertexCount(2);
    CKDWORD expected[21] = {};
    expected[0] = 2; expected[1] = 2; expected[2] = 1;
    int word = 3;
    for (int i = 0; i < 2; ++i) {
        const float time = i ? 10.0f : -5.0f;
        memcpy(expected + word++, &time, sizeof(time));
        for (int vertex = 0; vertex < 2; ++vertex) {
            const VxVector value((i ? 10.0f : 2.0f) + vertex, i ? 7.0f : 3.0f, i ? 8.0f : 4.0f);
            memcpy(expected + word, &value, sizeof(value)); word += 3;
        }
        for (int vertex = 0; vertex < 2; ++vertex) expected[word++] = (300u << 16) | (i ? 1000u : 200u);
    }
    WireWords expectedWire(expected, expected + word);
    CheckSavedController(animation, CKANIMATION_MORPH_CONTROL, expectedWire);
    Chunk saved(animation.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
    RCKObjectAnimation reloaded(&context, "MorphResizeEditReloaded");
    LoadChunk(reloaded, saved.get());
    CheckSavedController(reloaded, CKANIMATION_MORPH_CONTROL, expectedWire);
    VxVector output[2];
    VxCompressedVector normals[2];
    Check(reloaded.EvaluateMorphTarget(-5.0f, 2, output, sizeof(VxVector), normals) && Equal(output[0], VxVector(2.0f, 3.0f, 4.0f)) &&
          Equal(output[1], VxVector(3.0f, 3.0f, 4.0f)) && normals[0].xa == 200 && normals[0].ya == 300,
          "Resize/Add/Remove/Save/Load lost the independent endpoint state");
}

void MorphCountResizePreservesStateOnAllocationFailure() {
    int failures = 0;
    bool succeeded = false;
    for (int failedAllocation = 0; failedAllocation < 16; ++failedAllocation) {
        CKContext context(nullptr, 0, 0);
        RCKObjectAnimation animation(&context, "MorphResizeAllocationFailure");
        AddMorphRequestKeys(animation);
        RCKMorphController *controller = static_cast<RCKMorphController *>(animation.GetMorphController());
        CKMorphKey *keys[2];
        VxVector *positions[2];
        VxCompressedVector *normals[2];
        for (int i = 0; i < 2; ++i) {
            keys[i] = static_cast<CKMorphKey *>(controller->GetKey(i));
            positions[i] = keys[i]->PosArray; normals[i] = keys[i]->NormArray;
        }
        s_MorphArrayAllocationFailure = failedAllocation;
        controller->SetMorphVertexCount(5);
        s_MorphArrayAllocationFailure = -1;
        if (controller->GetMorphVertexCount() == 5) {
            CheckResizedMorphKeys(*controller, 5, 3, true);
            succeeded = true;
            break;
        }
        ++failures;
        for (int i = 0; i < 2; ++i) Check(controller->GetKey(i) == keys[i] && keys[i]->PosArray == positions[i] && keys[i]->NormArray == normals[i],
                                         "Failed Morph resize partially committed replacement pointers");
        CheckResizedMorphKeys(*controller, 3, 3, true);
    }
    Check(failures > 0 && succeeded, "Morph allocation failure injection did not cover failure and success");
}

template<int InvalidKind>
void MorphReadRejectsInvalidHeaderOrTimes() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation animation(&context, "MorphInvalidRead");
    AddMorphRequestKeys(animation);
    RCKMorphController *controller = static_cast<RCKMorphController *>(animation.GetMorphController());
    CKMorphKey *first = static_cast<CKMorphKey *>(controller->GetKey(0));
    VxVector *position = first->PosArray;
    WireWords input = MorphWire<true>();
    if (InvalidKind == 0) { input[0] = 0xFFFFFFFFu; input[1] = 0; }
    if (InvalidKind == 1) { input[0] = 0; input[1] = 0xFFFFFFFFu; }
    if (InvalidKind == 2) input[3] = 0x7FC00000u;
    Check(controller->ReadKeysFrom(input.data()) == 0, "Morph reader accepted invalid header/key times");
    Check(controller->GetKey(0) == first && first->PosArray == position, "Rejected Morph read changed old key/payload addresses");
    CheckResizedMorphKeys(*controller, 3, 3, true);
}

template<int InvalidKind>
void MorphDumpRejectsInconsistentState() {
    // This test-only subtype can exercise size arithmetic without allocating
    // enormous arrays. Query rejection precedes any attempt to write output.
    struct MorphSizeProbe : RCKMorphController {
        void SetRawVertexCount(int count) { m_VertexCount = count; }
    };
    MorphSizeProbe controller;
    WireWords input = MorphWire<true>();
    Check(controller.ReadKeysFrom(input.data()) == static_cast<int>(input.size() * 4), "Morph dump fixture read failed");
    CKMorphKey *first = static_cast<CKMorphKey *>(controller.GetKey(0));
    CKMorphKey *last = static_cast<CKMorphKey *>(controller.GetKey(1));
    if (InvalidKind == 0) controller.SetRawVertexCount(INT_MAX);
    if (InvalidKind == 1) { delete[] first->PosArray; first->PosArray = nullptr; }
    if (InvalidKind == 4) first->TimeStep = FloatWithBits(0x7FC00000u);
    if (InvalidKind == 5) last->TimeStep = -1.0f;
    Check(controller.DumpKeysTo(nullptr) == 0, "Morph size query accepted inconsistent or unrepresentable state");
    CKDWORD output[40];
    for (int i = 0; i < 40; ++i) output[i] = 0x12345678u;
    Check(controller.DumpKeysTo(output) == 0, "Morph dump wrote invalid state");
    for (int i = 0; i < 40; ++i) Check(output[i] == 0x12345678u, "Rejected Morph dump changed caller bytes");
}

// Native 0x100505D9 takes normal presence from key 0 and reads later keys'
// normals. Mixed presence keeps every position and omits all normals.
template<bool FirstMissing>
void MorphSaveOmitsPartialNormals() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation animation(&context, "MorphPartialNormals");
    AddMorphRequestKeys(animation);
    RCKMorphController *controller = static_cast<RCKMorphController *>(animation.GetMorphController());
    CKMorphKey *key = static_cast<CKMorphKey *>(controller->GetKey(FirstMissing ? 0 : 1));
    delete[] key->NormArray; key->NormArray = nullptr;
    const int bytes = controller->DumpKeysTo(nullptr);
    Check(bytes == static_cast<int>((3 + 2 * (1 + 3 * 3)) * sizeof(CKDWORD)), "Partial-normal Morph size included normals");
    Chunk saved(animation.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
    Check(saved != nullptr, "Partial-normal Morph discarded the animation Save");
    RCKObjectAnimation reloaded(&context, "MorphPartialNormalsReloaded");
    LoadChunk(reloaded, saved.get());
    CheckResizedMorphKeys(*static_cast<RCKMorphController *>(reloaded.GetMorphController()), 3, 3, false);
}

template<int InvalidKind>
void LoadedMorphRespectsControllerRecordBoundaries() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation animation(&context, "MorphBadRecord");
    // The physical backing contains a complete ordinary fixture; only the
    // declared controller envelope is inconsistent, so pre-fix checks remain
    // within allocated bytes rather than serving as an out-of-bounds probe.
    WireWords input(16, 0u);
    input[0] = 1; input[1] = 3; input[2] = 1;
    Chunk chunk = NewChunk(CKCID_OBJECTANIMATION);
    chunk->WriteIdentifier(CK_STATESAVE_OBJANIMCONTROLLERS);
    for (int i = 0; i < 7; ++i) chunk->WriteFloat(0.0f);
    chunk->WriteDword(0); chunk->WriteObject(nullptr); chunk->WriteFloat(20.0f);
    chunk->WriteDword(CKANIMATION_MORPH_CONTROL);
    const CKDWORD declared = InvalidKind == 0 ? 2u : (InvalidKind == 1 ? 3u : (InvalidKind == 2 ? 0xFFFFFFFFu : static_cast<CKDWORD>(input.size())));
    chunk->WriteDword(declared);
    chunk->WriteBufferNoSize_LEndian(static_cast<int>(input.size() * 4), input.data());
    if (InvalidKind != 3) chunk->WriteDword(0);
    chunk->WriteIdentifier(0x7FFFFFFFu);
    for (int i = 0; i < 64; ++i) chunk->WriteDword(0);
    chunk->CloseChunk(); chunk->StartRead();
    Check(animation.Load(chunk.get(), nullptr) == CKERR_INVALIDFILE, "Animation accepted an invalid controller envelope");
}

template<bool Normals, bool ZeroVertices>
void MorphSelfClonePreservesOwnedState() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation animation(&context, "MorphSelfClone");
    AddMorphRequestKeys(animation);
    RCKMorphController *controller = static_cast<RCKMorphController *>(animation.GetMorphController());
    if (!Normals) {
        for (int i = 0; i < 2; ++i) {
            CKMorphKey *key = static_cast<CKMorphKey *>(controller->GetKey(i));
            delete[] key->NormArray; key->NormArray = nullptr;
        }
    }
    if (ZeroVertices) controller->SetMorphVertexCount(0);
    CKMorphKey *keys[2];
    VxVector *positions[2];
    VxCompressedVector *normals[2];
    for (int i = 0; i < 2; ++i) {
        keys[i] = static_cast<CKMorphKey *>(controller->GetKey(i));
        positions[i] = keys[i]->PosArray; normals[i] = keys[i]->NormArray;
    }
    s_MorphArrayAllocationFailure = 0;
    const CKBOOL cloned = controller->Clone(controller);
    const bool noAllocation = s_MorphArrayAllocationFailure == 0;
    s_MorphArrayAllocationFailure = -1;
    Check(cloned && noAllocation, "Morph self-Clone allocated or failed");
    for (int i = 0; i < 2; ++i) Check(controller->GetKey(i) == keys[i] && keys[i]->PosArray == positions[i] && keys[i]->NormArray == normals[i],
                                     "Morph self-Clone changed owned addresses");
    const int vertices = ZeroVertices ? 0 : 3;
    CheckResizedMorphKeys(*controller, vertices, vertices, Normals);
    Chunk saved(animation.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
    Check(saved != nullptr, "Self-cloned Morph Save failed");
    RCKObjectAnimation reloaded(&context, "SelfCloneReloaded");
    LoadChunk(reloaded, saved.get());
    CheckResizedMorphKeys(*static_cast<RCKMorphController *>(reloaded.GetMorphController()), vertices, vertices, Normals);
}

template<bool Clone>
void MorphReplacementPreservesStateOnAllocationFailure() {
    WireWords expected = MorphWire<true>();
    int failures = 0;
    bool succeeded = false;
    for (int failedAllocation = 0; failedAllocation < 16; ++failedAllocation) {
        CKContext context(nullptr, 0, 0);
        RCKObjectAnimation animation(&context, "MorphReplacementTarget"), sourceAnimation(&context, "MorphReplacementSource");
        AddMorphRequestKeys(animation);
        RCKMorphController *target = static_cast<RCKMorphController *>(animation.GetMorphController());
        RCKMorphController *source = static_cast<RCKMorphController *>(sourceAnimation.CreateController(CKANIMATION_MORPH_CONTROL));
        Check(source->ReadKeysFrom(expected.data()) == static_cast<int>(expected.size() * 4), "Morph source read failed");
        sourceAnimation.SetLength(27.0f);
        CKMorphKey *oldKey = static_cast<CKMorphKey *>(target->GetKey(0));
        VxVector *oldPosition = oldKey->PosArray;
        VxCompressedVector *oldNormal = oldKey->NormArray;
        s_MorphArrayAllocationFailure = failedAllocation;
        const int result = Clone ? target->Clone(source) : target->ReadKeysFrom(expected.data());
        s_MorphArrayAllocationFailure = -1;
        if (result) {
            Check(result == (Clone ? TRUE : static_cast<int>(expected.size() * 4)), "Replacement returned an incorrect result");
            Check(target->GetLength() == (Clone ? 27.0f : 10.0f), "Replacement changed the controller length contract");
            WireWords output(expected.size(), 0u);
            Check(target->DumpKeysTo(output.data()) == static_cast<int>(expected.size() * 4) && output == expected,
                  "Replacement did not retain independent expected bytes");
            Check(target->GetKey(0) != source->GetKey(0) &&
                  static_cast<CKMorphKey *>(target->GetKey(0))->PosArray != static_cast<CKMorphKey *>(source->GetKey(0))->PosArray,
                  "Morph replacement shared source storage");
            succeeded = true;
        } else {
            ++failures;
            Check(target->GetKey(0) == oldKey && oldKey->PosArray == oldPosition && oldKey->NormArray == oldNormal,
                  "Failed replacement changed destination pointers");
            CheckResizedMorphKeys(*target, 3, 3, true);
        }
        WireWords sourceOutput(expected.size(), 0u);
        Check(source->DumpKeysTo(sourceOutput.data()) == static_cast<int>(expected.size() * 4) && sourceOutput == expected && source->GetLength() == 27.0f,
              "Morph replacement changed source data");
        if (succeeded) break;
    }
    Check(failures == 5 && succeeded, "Replacement did not cover every header/payload allocation failure");
}

void MorphReadAndDumpAcceptUnalignedBuffers() {
    WireWords expected = MorphWire<true>();
    std::vector<unsigned char> input(expected.size() * 4 + 2, 0xCD);
    std::vector<unsigned char> output(input.size(), 0xCD);
    memcpy(input.data() + 1, expected.data(), expected.size() * 4);
    RCKMorphController controller;
    Check(controller.ReadKeysFrom(input.data() + 1) == static_cast<int>(expected.size() * 4) &&
          controller.DumpKeysTo(output.data() + 1) == static_cast<int>(expected.size() * 4), "Unaligned Morph buffer failed");
    Check(input == output && output.front() == 0xCD && output.back() == 0xCD, "Unaligned Morph bytes/guards changed");
}

template<int InvalidKind>
void LoadedMorphRejectsActualTruncation() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation animation(&context, "MorphActualTruncation");
    Chunk chunk = NewChunk(CKCID_OBJECTANIMATION);
    chunk->WriteIdentifier(CK_STATESAVE_OBJANIMCONTROLLERS);
    if (InvalidKind == 0) {
        for (int i = 0; i < 9; ++i) chunk->WriteDword(0);
    } else {
        for (int i = 0; i < 7; ++i) chunk->WriteFloat(0.0f);
        chunk->WriteDword(InvalidKind == 1 ? CK_OBJECTANIMATION_MERGED : 0);
        chunk->WriteObject(nullptr); chunk->WriteFloat(20.0f);
        if (InvalidKind == 1) {
            chunk->WriteFloat(0.5f); // both merged references are missing
        } else {
            chunk->WriteDword(CKANIMATION_MORPH_CONTROL);
            chunk->WriteDword(3);
            chunk->WriteDword(1); chunk->WriteDword(2); chunk->WriteDword(1);
            chunk->WriteDword(0); // declared header is present, payload is absent
        }
    }
    chunk->CloseChunk(); chunk->StartRead();
    Check(animation.Load(chunk.get(), nullptr) == CKERR_INVALIDFILE, "Animation accepted a physically truncated controller block");
    if (InvalidKind == 2) Check(!animation.GetMorphController(), "Rejected Morph retained a partial controller");
}

void LoadedMorphSkipsRecordPaddingAndUnknownControllers() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation animation(&context, "MorphPaddedRecord");
    WireWords words = MorphWire<true>();
    Chunk chunk = NewChunk(CKCID_OBJECTANIMATION);
    chunk->WriteIdentifier(CK_STATESAVE_OBJANIMCONTROLLERS);
    for (int i = 0; i < 7; ++i) chunk->WriteFloat(0.0f);
    chunk->WriteDword(0); chunk->WriteObject(nullptr); chunk->WriteFloat(20.0f);
    chunk->WriteDword(CKANIMATION_MORPH_CONTROL); chunk->WriteDword(static_cast<CKDWORD>(words.size() + 2));
    chunk->WriteBufferNoSize_LEndian(static_cast<int>(words.size() * 4), words.data());
    chunk->WriteDword(0xABCDEF12u); chunk->WriteDword(0x12345678u);
    chunk->WriteDword(0x7FFF0000u); chunk->WriteDword(2);
    chunk->WriteDword(0x87654321u); chunk->WriteDword(0x12345678u);
    chunk->WriteDword(CKANIMATION_LINPOS_CONTROL); chunk->WriteDword(5);
    chunk->WriteDword(1); chunk->WriteFloat(0.0f);
    chunk->WriteFloat(9.0f); chunk->WriteFloat(8.0f); chunk->WriteFloat(7.0f);
    chunk->WriteDword(0);
    LoadChunk(animation, chunk.get());
    WireWords output(words.size(), 0u);
    Check(animation.GetMorphController() && animation.GetMorphController()->DumpKeysTo(output.data()) == static_cast<int>(words.size() * 4) && output == words,
          "Padded Morph record changed keys");
    VxVector position;
    Check(animation.EvaluatePosition(5.0f, position) && Equal(position, VxVector(9.0f, 8.0f, 7.0f)),
          "Record padding/unknown controller changed the following controller");
}

void MorphSaveRejectsIncompletePayloads() {
    // Partial normals are omitted rather than rejected; see MorphSaveOmitsPartialNormals.
    for (int kind : {0, 3, 4}) {
        CKContext context(nullptr, 0, 0);
        RCKObjectAnimation animation(&context, "MorphBadSave");
        AddMorphRequestKeys(animation);
        CKMorphController *controller = animation.GetMorphController();
        CKMorphKey *first = static_cast<CKMorphKey *>(controller->GetKey(0));
        CKMorphKey *last = static_cast<CKMorphKey *>(controller->GetKey(1));
        if (kind == 0) { delete[] first->PosArray; first->PosArray = nullptr; }
        if (kind == 3) first->TimeStep = FloatWithBits(0x7FC00000u);
        if (kind == 4) last->TimeStep = -1.0f;
        Check(controller->DumpKeysTo(nullptr) == 0, "Invalid Save fixture unexpectedly serialized");
        Chunk saved(animation.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
        Check(!saved, "Save silently omitted an invalid Morph controller");
    }
}

template<int InvalidKind>
void MorphReadRejectsOverflowAndUnorderedTimes() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation animation(&context, "MorphInvalidReadSize");
    AddMorphRequestKeys(animation);
    RCKMorphController *controller = static_cast<RCKMorphController *>(animation.GetMorphController());
    WireWords input = MorphWire<true>();
    if (InvalidKind < 2) { input[0] = INT_MAX; input[1] = InvalidKind == 0 ? INT_MAX : 0; }
    if (InvalidKind == 2) { const float time = -1.0f; memcpy(input.data() + 12, &time, sizeof(time)); }
    Check(controller->ReadKeysFrom(input.data()) == 0, "Morph reader accepted overflowing size or descending times");
    CheckResizedMorphKeys(*controller, 3, 3, true);
}

template<int Topology>
void LoadedMergedAnimationTerminatesCycles() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation leaf(&context, "GraphLeaf"), root(&context, "GraphRoot"), peer(&context, "GraphPeer"), other(&context, "GraphOther");
    AddTimedAnimationKeys(leaf, 2.0f);
    if (Topology == 0) {
        LoadMergedState(root, &root, nullptr);
    } else if (Topology == 1 || Topology == 2) {
        LoadMergedState(root, &peer, Topology == 2 ? &leaf : nullptr);
        LoadMergedState(peer, &root, nullptr);
    } else {
        // A shared descendant is a valid DAG, not a cycle.
        LoadMergedState(peer, &leaf, nullptr);
        LoadMergedState(other, &leaf, nullptr);
        LoadMergedState(root, &peer, &other);
    }
    const bool available = Topology >= 2;
    for (int pass = 0; pass < 2; ++pass) {
        Check((root.HasPositionInfo() != FALSE) == available && (root.HasScaleInfo() != FALSE) == available &&
              (root.HasRotationInfo() != FALSE) == available && (root.HasScaleAxisInfo() != FALSE) == available &&
              (root.HasMorphInfo() != FALSE) == available && (root.HasMorphNormalInfo() != FALSE) == available,
              "Merged graph queries did not ignore only cyclic branches");
        Check(root.GetMorphVertexCount() == (available ? 1 : 0), "Merged graph Morph count lost available data");
        VxVector output(123.0f);
        VxQuaternion rotation;
        Check((root.EvaluatePosition(7.0f, output) != FALSE) == available, "Merged graph position result changed");
        if (available) Check(std::fabs(output.x - 5.5f) < 5.0e-5f, "Merged graph changed source time or valid DAG output");
        Check((root.EvaluateScale(7.0f, output) != FALSE) == available, "Merged graph scale result changed");
        Check((root.EvaluateRotation(7.0f, rotation) != FALSE) == available, "Merged graph rotation result changed");
        Check((root.EvaluateScaleAxis(7.0f, rotation) != FALSE) == available, "Merged graph scale-axis result changed");
        Check((root.EvaluateMorphTarget(7.0f, 1, &output, sizeof(output), nullptr) != FALSE) == available,
              "Merged graph Morph result changed");
        if (available) Check(std::fabs(output.x - 9.0f) < 5.0e-5f,
                             "Merged graph changed the legacy unscaled Morph fallback time");
        Chunk saved(root.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
        LoadChunk(root, saved.get());
    }
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

// Fixture metadata is independent of the production reader. The Bezier
// fixture covers neither, in-only, out-only and both explicit tangents.
std::vector<size_t> TransformWireTimeOffsets(CKANIMATION_CONTROLLER type, const WireWords &words) {
    std::vector<size_t> offsets;
    const bool bezier = type == CKANIMATION_BEZIERPOS_CONTROL || type == CKANIMATION_BEZIERSCL_CONTROL;
    const bool rotation = (type & CKANIMATION_CONTROLLER_MASK) == CKANIMATION_CONTROLLER_ROT ||
                          (type & CKANIMATION_CONTROLLER_MASK) == CKANIMATION_CONTROLLER_SCLAXIS;
    const bool tcb = type == CKANIMATION_TCBPOS_CONTROL || type == CKANIMATION_TCBROT_CONTROL ||
                     type == CKANIMATION_TCBSCL_CONTROL || type == CKANIMATION_TCBSCLAXIS_CONTROL;
    size_t offset = 1;
    for (CKDWORD i = 0; i < words[0]; ++i) {
        offsets.push_back(offset);
        if (bezier) {
            CKBezierKeyFlags flags;
            std::memcpy(&flags, words.data() + offset + 4, sizeof(flags));
            offset += 5;
            if (flags.GetInTangentMode() & BEZIER_KEY_TANGENTS) offset += 3;
            if (flags.GetOutTangentMode() & BEZIER_KEY_TANGENTS) offset += 3;
        } else {
            offset += rotation ? 5 : 4;
            if (tcb) offset += 5;
        }
    }
    Check(offset == words.size(), "Transform fixture has an incorrect independent layout");
    return offsets;
}

Chunk TransformRecordWithDeclaredSize(CKANIMATION_CONTROLLER type, const WireWords &words, CKDWORD declared) {
    Chunk chunk = NewChunk(CKCID_OBJECTANIMATION);
    chunk->WriteIdentifier(CK_STATESAVE_OBJANIMCONTROLLERS);
    for (int i = 0; i < 7; ++i) chunk->WriteFloat(0.0f);
    chunk->WriteDword(0); chunk->WriteObject(nullptr); chunk->WriteFloat(20.0f);
    chunk->WriteDword(type); chunk->WriteDword(declared);
    chunk->WriteBufferNoSize_LEndian(static_cast<int>(words.size() * 4), const_cast<CKDWORD *>(words.data()));
    chunk->WriteDword(0);
    // Retain a complete physical backing while varying the record's logical
    // capacity; pre-fix readers need not touch unallocated memory.
    chunk->WriteIdentifier(0x7FFFFFFFu);
    for (int i = 0; i < 64; ++i) chunk->WriteDword(0);
    chunk->CloseChunk(); chunk->StartRead();
    return chunk;
}

template<CKANIMATION_CONTROLLER Type>
void LoadedTransformRespectsEveryRecordBoundary() {
    CKContext context(nullptr, 0, 0);
    WireWords words = TransformControllerWire(Type);
    words[1] = 0; // First key's time doubles as a benign early terminator pre-fix.
    {
        RCKObjectAnimation animation(&context, "TransformHeaderOnlyRecord");
        Chunk chunk = TransformRecordWithDeclaredSize(Type, words, 1);
        Check(animation.Load(chunk.get(), nullptr) == CKERR_INVALIDFILE,
              "Transform Load accepted a header-only record with nonempty keys");
    }
    for (CKDWORD declared = 0; declared < words.size(); ++declared) {
        RCKObjectAnimation animation(&context, "TransformTruncatedRecord");
        Chunk chunk = TransformRecordWithDeclaredSize(Type, words, declared);
        Check(animation.Load(chunk.get(), nullptr) == CKERR_INVALIDFILE,
              "Transform Load accepted a record shorter than its keys or explicit tangents");
    }
    const CKDWORD invalidCounts[] = {0xFFFFFFFFu, 0x7FFFFFFFu, words[0] + 1};
    for (CKDWORD count : invalidCounts) {
        // Only reached after bounded-load rejection above succeeds.
        WireWords invalid = words; invalid[0] = count;
        RCKObjectAnimation animation(&context, "TransformInvalidCount");
        Chunk chunk = TransformRecordWithDeclaredSize(Type, invalid, static_cast<CKDWORD>(invalid.size()));
        Check(animation.Load(chunk.get(), nullptr) == CKERR_INVALIDFILE, "Transform Load accepted an invalid inner key count");
        Check(!TransformController(animation, Type), "Failed transform reader left a partial controller attached");
    }
}

template<CKANIMATION_CONTROLLER Type>
void TransformRawReadRejectsInvalidStateAtomically() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation animation(&context, "TransformInvalidRawRead");
    WireWords words = TransformControllerWire(Type);
    Chunk chunk = AnimationWithController(Type, words); LoadChunk(animation, chunk.get());
    CKAnimController *controller = TransformController(animation, Type);
    CKKey *oldKey = controller->GetKey(0);
    VxQuaternion before, after;
    Check(controller->Evaluate(5.0f, &before), "Cannot prime transform tangent cache");
    const std::vector<size_t> offsets = TransformWireTimeOffsets(Type, words);
    for (int invalidKind = 0; invalidKind < 5; ++invalidKind) {
        WireWords invalid = words;
        if (invalidKind == 0) invalid[0] = 0xFFFFFFFFu;
        if (invalidKind == 1) invalid[offsets[0]] = 0x7FC00000u;
        if (invalidKind == 2) invalid[offsets[0]] = 0x7F800000u;
        if (invalidKind == 3) invalid[offsets[1]] = 0xBF800000u;
        Check(controller->ReadKeysFrom(invalidKind == 4 ? nullptr : invalid.data()) == 0,
              "Transform raw reader accepted a negative count or invalid key time");
        Check(controller->GetKey(0) == oldKey && controller->GetKeyCount() == static_cast<int>(words[0]) &&
              controller->GetLength() == 20.0f, "Rejected transform read changed key storage or length");
        Check(controller->Evaluate(5.0f, &after) && after == before, "Rejected transform read changed cached evaluation");
        CheckSavedController(animation, Type, words);
    }
}

template<CKANIMATION_CONTROLLER Type>
void TransformRawReadPreservesStateOnAllocationFailure() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation animation(&context, "TransformFailedRawRead");
    WireWords words = TransformControllerWire(Type);
    Chunk chunk = AnimationWithController(Type, words); LoadChunk(animation, chunk.get());
    CKAnimController *controller = TransformController(animation, Type);
    CKKey *oldKey = controller->GetKey(0);
    VxQuaternion before, after;
    Check(controller->Evaluate(5.0f, &before), "Cannot prime transform cache before failed read");
    WireWords replacement = words;
    replacement[TransformWireTimeOffsets(Type, words)[0]] = 0xBF800000u;
    s_MorphArrayAllocationFailure = 0;
    const int result = controller->ReadKeysFrom(replacement.data());
    const bool injected = s_MorphArrayAllocationFailure == -1;
    s_MorphArrayAllocationFailure = -1;
    Check(!result && injected, "Transform reader ignored allocation failure");
    Check(controller->GetKey(0) == oldKey && controller->GetKeyCount() == static_cast<int>(words[0]) &&
          controller->GetLength() == 20.0f, "Failed transform read changed owned keys");
    Check(controller->Evaluate(5.0f, &after) && after == before, "Failed transform read changed cached evaluation");
    CheckSavedController(animation, Type, words);
    Check(controller->ReadKeysFrom(replacement.data()) == static_cast<int>(replacement.size() * 4),
          "Transform read did not recover after allocation failure");
    CheckSavedController(animation, Type, replacement);
}

template<CKANIMATION_CONTROLLER Type>
void TransformReadRetainsValidWireAndLengthContracts() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation animation(&context, "TransformValidRead");
    WireWords words = TransformControllerWire(Type);
    // Padding belongs to the envelope rather than the controller's wire data.
    WireWords padded = words; padded.push_back(0xAABBCCDDu); padded.push_back(0u);
    Chunk chunk = TransformRecordWithDeclaredSize(Type, padded, static_cast<CKDWORD>(padded.size()));
    Check(animation.Load(chunk.get(), nullptr) == CK_OK, "Transform Load rejected valid record padding");
    CheckSavedController(animation, Type, words);
    CKAnimController *controller = TransformController(animation, Type);
    controller->SetLength(37.0f);
    std::vector<unsigned char> unaligned(words.size() * 4 + 1);
    std::memcpy(unaligned.data() + 1, words.data(), words.size() * 4);
    Check(controller->ReadKeysFrom(unaligned.data() + 1) == static_cast<int>(words.size() * 4) &&
          controller->GetLength() == 37.0f, "Transform read changed byte count or length on unaligned input");
    WireWords output(words.size(), 0u);
    Check(controller->DumpKeysTo(output.data()) == static_cast<int>(output.size() * 4) && output == words,
          "Unaligned transform read changed original wire bytes");
    WireWords duplicates = words;
    const std::vector<size_t> offsets = TransformWireTimeOffsets(Type, words);
    duplicates[offsets[1]] = duplicates[offsets[0]];
    Check(controller->ReadKeysFrom(duplicates.data()) == static_cast<int>(duplicates.size() * 4),
          "Transform reader rejected native-compatible duplicate key times");
    Check(controller->DumpKeysTo(output.data()) == static_cast<int>(output.size() * 4) && output == duplicates,
          "Transform read changed duplicate-time wire bytes");
    CKDWORD empty = 0;
    Check(controller->ReadKeysFrom(&empty) == 4 && controller->GetKeyCount() == 0 &&
          !controller->GetKey(0) && controller->GetLength() == 37.0f, "Empty transform read changed the native length contract");
}

void LoadedTransformReportsKeyAllocationFailure() {
    const CKANIMATION_CONTROLLER types[] = {
        CKANIMATION_LINPOS_CONTROL, CKANIMATION_LINROT_CONTROL,
        CKANIMATION_LINSCL_CONTROL, CKANIMATION_LINSCLAXIS_CONTROL,
        CKANIMATION_TCBPOS_CONTROL, CKANIMATION_TCBROT_CONTROL,
        CKANIMATION_TCBSCL_CONTROL, CKANIMATION_TCBSCLAXIS_CONTROL,
        CKANIMATION_BEZIERPOS_CONTROL, CKANIMATION_BEZIERSCL_CONTROL
    };
    CKContext context(nullptr, 0, 0);
    for (CKANIMATION_CONTROLLER type : types) {
        RCKObjectAnimation animation(&context, "TransformLoadAllocationFailure");
        WireWords words = TransformControllerWire(type);
        Chunk chunk = AnimationWithController(type, words);
        chunk->CloseChunk(); chunk->StartRead();
        s_MorphArrayAllocationFailure = 0;
        const CKERROR result = animation.Load(chunk.get(), nullptr);
        const bool injected = s_MorphArrayAllocationFailure == -1;
        s_MorphArrayAllocationFailure = -1;
        Check(result == CKERR_INVALIDFILE && injected, "Transform Load did not report reader allocation failure");
        Check(!TransformController(animation, type), "Failed transform Load retained a partial controller");
    }
}

void LoadedTransformCanonicalizesSupportedBaseTypes() {
    const CKANIMATION_CONTROLLER aliases[] = {
        CKANIMATION_CONTROLLER_POS, CKANIMATION_CONTROLLER_ROT, CKANIMATION_CONTROLLER_SCL
    };
    const CKANIMATION_CONTROLLER canonical[] = {
        CKANIMATION_LINPOS_CONTROL, CKANIMATION_LINROT_CONTROL, CKANIMATION_LINSCL_CONTROL
    };
    CKContext context(nullptr, 0, 0);
    for (int i = 0; i < 3; ++i) {
        RCKObjectAnimation animation(&context, "TransformBaseTypeLoad");
        WireWords words = TransformControllerWire(canonical[i]);
        Chunk chunk = AnimationWithController(aliases[i], words);
        LoadChunk(animation, chunk.get());
        CKAnimController *controller = TransformController(animation, canonical[i]);
        Check(controller && controller->GetType() == canonical[i] && controller->GetLength() == 20.0f,
              "Transform base type did not load as its canonical controller");
        CheckSavedController(animation, canonical[i], words);
    }
    // Native factory 0x1004A922 accepts bare 1, 2, 4 and 16, but not
    // SCLAXIS mask 8. Its record is skipped; the complete axis type is required.
    RCKObjectAnimation animation(&context, "TransformUnsupportedAxisMask");
    WireWords words = TransformControllerWire(CKANIMATION_LINSCLAXIS_CONTROL);
    Chunk chunk = AnimationWithController(CKANIMATION_CONTROLLER_SCLAXIS, words);
    LoadChunk(animation, chunk.get());
    Check(!animation.GetScaleAxisController() && animation.GetLength() == 20.0f,
          "Unsupported bare scale-axis mask created a controller or changed length");
}

void LoadedTransformSkipsUnknownRecordsAndPadding() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation animation(&context, "TransformRecordProgress");
    WireWords position = TransformControllerWire(CKANIMATION_BEZIERPOS_CONTROL);
    WireWords scale = TransformControllerWire(CKANIMATION_LINSCL_CONTROL);
    Chunk chunk = NewChunk(CKCID_OBJECTANIMATION);
    chunk->WriteIdentifier(CK_STATESAVE_OBJANIMCONTROLLERS);
    for (int i = 0; i < 7; ++i) chunk->WriteFloat(0.0f);
    chunk->WriteDword(0); chunk->WriteObject(nullptr); chunk->WriteFloat(20.0f);
    chunk->WriteDword(CKANIMATION_BEZIERPOS_CONTROL);
    chunk->WriteDword(static_cast<CKDWORD>(position.size() + 2));
    chunk->WriteBufferNoSize_LEndian(static_cast<int>(position.size() * 4), position.data());
    chunk->WriteDword(0); chunk->WriteDword(CKANIMATION_LINROT_CONTROL);
    chunk->WriteDword(0x7FFF1234u); chunk->WriteDword(3);
    chunk->WriteDword(0); chunk->WriteDword(CKANIMATION_TCBROT_CONTROL); chunk->WriteDword(0xAABBCCDDu);
    chunk->WriteDword(CKANIMATION_LINSCL_CONTROL);
    chunk->WriteDword(static_cast<CKDWORD>(scale.size()));
    chunk->WriteBufferNoSize_LEndian(static_cast<int>(scale.size() * 4), scale.data());
    chunk->WriteDword(0);
    LoadChunk(animation, chunk.get());
    CKAnimController *first = animation.GetPositionController();
    CKAnimController *next = animation.GetScaleController();
    Check(first && first->GetType() == CKANIMATION_BEZIERPOS_CONTROL &&
          next && next->GetType() == CKANIMATION_LINSCL_CONTROL &&
          !animation.GetRotationController(), "Transform envelope did not skip padding and unknown records");
    WireWords output(position.size(), 0u);
    Check(first->DumpKeysTo(output.data()) == static_cast<int>(position.size() * 4) && output == position,
          "Padding changed the first transform payload");
    output.assign(scale.size(), 0u);
    Check(next->DumpKeysTo(output.data()) == static_cast<int>(scale.size() * 4) && output == scale,
          "Unknown record changed the following transform payload");
    Chunk saved(animation.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
    saved->StartRead();
    Check(saved->SeekIdentifier(CK_STATESAVE_OBJANIMCONTROLLERS), "Missing saved transform envelope");
    saved->Skip(10);
    Check(saved->ReadDword() == CKANIMATION_BEZIERPOS_CONTROL && saved->ReadDword() == position.size(),
          "Save retained position padding in the controller size");
    saved->Skip(static_cast<int>(position.size()));
    Check(saved->ReadDword() == CKANIMATION_LINSCL_CONTROL && saved->ReadDword() == scale.size(),
          "Save retained the unknown transform record");
    saved->Skip(static_cast<int>(scale.size()));
    Check(saved->ReadDword() == 0, "Saved transform envelope lost its terminator");
}

template<class Controller, CKANIMATION_CONTROLLER Type>
void TransformDumpRejectsInvalidStateBeforeWriting() {
    Controller controller;
    WireWords words = TransformControllerWire(Type);
    Check(controller.ReadKeysFrom(words.data()) == static_cast<int>(words.size() * 4), "Cannot prepare transform dump fixture");
    controller.SetLength(37.0f);
    const int count = controller.GetKeyCount();
    CKKey *first = controller.GetKey(0), *last = controller.GetKey(count - 1);
    const float firstTime = first->TimeStep, lastTime = last->TimeStep;
    struct RestoreCount {
        CKAnimController &ControllerRef;
        int Count;
        ~RestoreCount() { ControllerRef.m_NbKeys = Count; }
    } restore = {controller, count};
    for (int invalid = 0; invalid < 5; ++invalid) {
        controller.m_NbKeys = count; first->TimeStep = firstTime; last->TimeStep = lastTime;
        if (invalid == 0) controller.m_NbKeys = -1;
        if (invalid == 1) controller.m_NbKeys = INT_MAX;
        if (invalid == 2) first->TimeStep = FloatWithBits(0x7FC00000u);
        if (invalid == 3) first->TimeStep = FloatWithBits(0x7F800000u);
        if (invalid == 4) last->TimeStep = firstTime - 1.0f;
        // Query rejection precedes all writes or impossible-sized traversal.
        Check(controller.DumpKeysTo(nullptr) == 0, "Transform dump query accepted invalid count or key times");
        CKDWORD output[64];
        for (CKDWORD &word : output) word = 0xAABBCCDDu;
        Check(controller.DumpKeysTo(output) == 0, "Transform dump wrote rejected state");
        for (CKDWORD word : output) Check(word == 0xAABBCCDDu, "Rejected transform dump changed caller bytes");
        Check(controller.GetLength() == 37.0f && controller.m_NbKeys == (invalid < 2 ? (invalid ? INT_MAX : -1) : count),
              "Transform dump changed metadata");
    }
    controller.m_NbKeys = count; first->TimeStep = firstTime; last->TimeStep = lastTime;
    Check(controller.GetKey(0) == first && controller.GetKey(count - 1) == last, "Transform dump replaced key storage");
    WireWords output(words.size(), 0u);
    Check(controller.DumpKeysTo(output.data()) == static_cast<int>(words.size() * 4) && output == words,
          "Rejected transform dump corrupted valid keys");
    Controller missing;
    missing.m_NbKeys = 1;
    Check(missing.DumpKeysTo(nullptr) == 0, "Transform dump accepted absent nonempty storage");
    for (CKDWORD &word : output) word = 0xAABBCCDDu;
    Check(missing.DumpKeysTo(output.data()) == 0, "Transform dump wrote absent key storage");
    for (CKDWORD word : output) Check(word == 0xAABBCCDDu, "Missing-key dump changed caller bytes");
}

template<class Controller, CKANIMATION_CONTROLLER Type>
void TransformDumpPreservesValidWireAndOutputBounds() {
    Controller controller;
    WireWords words = TransformControllerWire(Type);
    Check(controller.ReadKeysFrom(words.data()) == static_cast<int>(words.size() * 4), "Cannot read valid dump fixture");
    controller.SetLength(37.0f);
    CKKey *first = controller.GetKey(0);
    VxQuaternion before, after;
    Check(controller.Evaluate(5.0f, &before), "Cannot prime transform dump cache");
    std::vector<unsigned char> output(words.size() * 4 + 2, 0xCD);
    const int size = controller.DumpKeysTo(nullptr);
    Check(size == static_cast<int>(words.size() * 4) && controller.DumpKeysTo(output.data() + 1) == size &&
          !std::memcmp(output.data() + 1, words.data(), words.size() * 4) && output.front() == 0xCD && output.back() == 0xCD,
          "Transform dump changed native bytes or unaligned output guards");
    Check(controller.GetKey(0) == first && controller.GetLength() == 37.0f &&
          controller.Evaluate(5.0f, &after) && before == after, "Transform dump changed playback or its cache");
    // Preserve duplicate-time and unrelated value bits accepted by Read.
    const std::vector<size_t> offsets = TransformWireTimeOffsets(Type, words);
    words[offsets[1]] = words[offsets[0]];
    words[offsets[0] + 1] = 0x7FC00000u;
    Check(controller.ReadKeysFrom(words.data()) == size && controller.DumpKeysTo(output.data() + 1) == size &&
          !std::memcmp(output.data() + 1, words.data(), words.size() * 4), "Transform dump changed valid duplicate-time/value bits");
    CKDWORD empty = 0;
    Check(controller.ReadKeysFrom(&empty) == 4, "Cannot clear transform dump fixture");
    unsigned char emptyOutput[6] = {0xCD, 0xCD, 0xCD, 0xCD, 0xCD, 0xCD};
    Check(controller.DumpKeysTo(nullptr) == 4 && controller.DumpKeysTo(emptyOutput + 1) == 4 &&
          !std::memcmp(emptyOutput + 1, &empty, 4) && emptyOutput[0] == 0xCD && emptyOutput[5] == 0xCD &&
          controller.GetLength() == 37.0f, "Empty transform dump changed output or length");
}

// Synthetic controller results stay within a four-byte physical write, even
// when the reported size is wrong. These fixtures test error propagation.
template<class Controller>
class SaveDumpResultProbe : public Controller {
public:
    SaveDumpResultProbe(CKDWORD type, int query, int result) : Query(query), Result(result), Writes(0) { this->m_Type = type; }
    int DumpKeysTo(void *buffer) override {
        if (!buffer) return Query;
        ++Writes;
        const CKDWORD empty = 0;
        std::memcpy(buffer, &empty, sizeof(empty));
        return Result;
    }
    int Query, Result, Writes;
};

class AnimationSaveProbe : public ObjectCloneProbe {
public:
    AnimationSaveProbe(CKContext *context, const char *name) : ObjectCloneProbe(context, name) {}
    void AllocateSceneBits(int index) { m_Scenes.Set(index); }
    int SceneBytes() const { return m_Scenes.GetMemoryOccupation(); }
    void ReplaceController(int slot, CKAnimController *replacement) {
        switch (slot) {
        case 0: delete m_KeyframeData->m_PositionController; m_KeyframeData->m_PositionController = replacement; break;
        case 1: delete m_KeyframeData->m_RotationController; m_KeyframeData->m_RotationController = replacement; break;
        case 2: delete m_KeyframeData->m_ScaleController; m_KeyframeData->m_ScaleController = replacement; break;
        case 3: delete m_KeyframeData->m_ScaleAxisController; m_KeyframeData->m_ScaleAxisController = replacement; break;
        case 4: delete m_KeyframeData->m_MorphController; m_KeyframeData->m_MorphController = static_cast<CKMorphController *>(replacement); break;
        }
    }
};

const CKANIMATION_CONTROLLER SaveProbeTypes[] = {
    CKANIMATION_LINPOS_CONTROL, CKANIMATION_LINROT_CONTROL, CKANIMATION_LINSCL_CONTROL,
    CKANIMATION_LINSCLAXIS_CONTROL, CKANIMATION_MORPH_CONTROL
};

template<int Slot>
void AnimationSaveRejectsControllerFailuresAndPreservesOwner() {
    const int queries[] = {0, -4, 2, 6, 4, 4, 4, INT_MAX - 3};
    const int results[] = {0, -4, 2, 6, 0, -4, 8, 4};
    for (int failure = 0; failure < 8; ++failure) {
        CKContext context(nullptr, 0, 0);
        AnimationSaveProbe owner(&context, "SaveFailureOwner"), target(&context, "SaveFailureTarget");
        AddTimedAnimationKeys(owner, 4.0f); target.ShareDataFrom(&owner);
        CKAnimController *probe;
        if (Slot == 4) probe = new SaveDumpResultProbe<RCKMorphController>(SaveProbeTypes[Slot], queries[failure], results[failure]);
        else probe = new SaveDumpResultProbe<RCKLinearPositionController>(SaveProbeTypes[Slot], queries[failure], results[failure]);
        owner.ReplaceController(Slot, probe);
        CKKeyframeData *data = owner.Data();
        CKFile file(&context); // Original owner has not been serialized.
        Chunk saved(target.Save(&file, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
        Check(!saved, "ObjectAnimation Save accepted invalid query or failed controller dump");
        Check(target.Data() == data && owner.Data() == data && target.Shared() == &owner && owner.Shared() == &owner &&
              target.References() == 2 && target.GetLength() == 10.0f,
              "Failed ObjectAnimation Save changed shared ownership or source metadata");
        if (Slot == 4) Check(static_cast<SaveDumpResultProbe<RCKMorphController> *>(probe)->Writes == (failure >= 4 && failure < 7 ? 1 : 0),
                             "Invalid Morph query was written or a valid query was not exercised");
        else Check(static_cast<SaveDumpResultProbe<RCKLinearPositionController> *>(probe)->Writes == (failure >= 4 && failure < 7 ? 1 : 0),
                   "Invalid transform query was written or a valid query was not exercised");
    }
}

void AnimationSaveRejectsAggregateControllerOverflow() {
    // Benign zero-size failure first keeps the pre-fix regression away from
    // large allocations. After that is handled, exercise arithmetic only.
    CKContext context(nullptr, 0, 0);
    AnimationSaveProbe owner(&context, "SaveAggregateOwner"), target(&context, "SaveAggregateTarget");
    AddTimedAnimationKeys(owner, 4.0f); target.ShareDataFrom(&owner);
    auto *position = new SaveDumpResultProbe<RCKLinearPositionController>(CKANIMATION_LINPOS_CONTROL, 0, 4);
    auto *rotation = new SaveDumpResultProbe<RCKLinearPositionController>(CKANIMATION_LINROT_CONTROL, 0x40000000, 4);
    owner.ReplaceController(0, position); owner.ReplaceController(1, rotation);
    CKFile file(&context);
    Chunk first(target.Save(&file, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
    Check(!first, "ObjectAnimation Save omitted an invalid controller before aggregate checks");
    position->Query = 0x40000000;
    Chunk overflow(target.Save(&file, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
    Check(!overflow && position->Writes == 0 && rotation->Writes == 0 && target.Shared() == &owner,
          "ObjectAnimation Save accepted overflowing aggregate controller bytes");
}

void AnimationSaveCommitsOwnerOnlyForFullSuccessfulData() {
    CKContext context(nullptr, 0, 0);
    AnimationSaveProbe owner(&context, "SaveCommitOwner"), target(&context, "SaveCommitTarget");
    AddTimedAnimationKeys(owner, 4.0f); target.ShareDataFrom(&owner);
    CKFile file(&context);
    Chunk saved(target.Save(&file, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
    Check(saved && target.Shared() == &target && owner.Shared() == &target && target.References() == 2,
          "Successful full Save did not commit the new shared owner");
    saved->StartRead(); Check(saved->SeekIdentifier(CK_STATESAVE_OBJANIMCONTROLLERS), "Unsaved owner did not force full data");
    // A subsequent reference to an owner already saved needs no controller dump.
    file.m_FileObjects.Resize(1);
    file.m_FileObjects[0].ObjPtr = &target;
    file.m_FileObjects[0].Data = saved.release();
    auto *invalid = new SaveDumpResultProbe<RCKLinearPositionController>(CKANIMATION_LINPOS_CONTROL, 0, 0);
    target.ReplaceController(0, invalid);
    Chunk reference(owner.Save(&file, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
    Check(reference && invalid->Writes == 0 && target.Shared() == &target && owner.Shared() == &target,
          "Shared Save dumped unused keys or changed owner");
    reference->StartRead(); Check(reference->SeekIdentifier(CK_STATESAVE_OBJANIMSHARED), "Shared Save did not keep the native reference branch");
}

void AnimationMemoryEstimateRejectsInvalidOrOverflowingControllerSizes() {
    CKContext context(nullptr, 0, 0);
    AnimationSaveProbe animation(&context, "MemoryEstimateProbe");
    const int objectOnly = animation.GetMemoryOccupation();
    auto *first = new SaveDumpResultProbe<RCKLinearPositionController>(CKANIMATION_LINPOS_CONTROL, -4, 4);
    animation.ReplaceController(0, first);
    Check(animation.GetMemoryOccupation() == objectOnly, "Invalid controller size corrupted the object memory estimate");
    auto *second = new SaveDumpResultProbe<RCKLinearPositionController>(CKANIMATION_LINROT_CONTROL, 0x40000000, 4);
    animation.ReplaceController(1, second); first->Query = 0x40000000;
    Check(animation.GetMemoryOccupation() == objectOnly, "Aggregate controller bytes overflowed the memory estimate");
    first->Query = 4; second->Query = 4;
    Check(animation.GetMemoryOccupation() == objectOnly + 8, "Valid controller memory estimate changed");
    ObjectCloneProbe alias(&context, "MemoryEstimateAlias");
    const int aliasOnly = alias.GetMemoryOccupation(); alias.ShareDataFrom(&animation);
    Check(alias.GetMemoryOccupation() == aliasOnly - static_cast<int>(sizeof(CKKeyframeData)),
          "Non-owner memory estimate included shared data or controller bytes");
}

void AnimationMemoryEstimateCountsOwnedDataAndSceneStorage() {
    CKContext context(nullptr, 0, 0);
    AnimationSaveProbe owner(&context, "MemoryOwner"), alias(&context, "MemoryAlias");
    const int ownerBase = owner.CKObject::GetMemoryOccupation() +
        static_cast<int>(sizeof(RCKObjectAnimation) - sizeof(CKObject));
    Check(owner.GetMemoryOccupation() == ownerBase + owner.SceneBytes() + static_cast<int>(sizeof(CKKeyframeData)),
          "Owner memory estimate omitted the owned keyframe data container");
    owner.AllocateSceneBits(127);
    Check(owner.SceneBytes() > 0 && owner.GetMemoryOccupation() ==
          ownerBase + owner.SceneBytes() + static_cast<int>(sizeof(CKKeyframeData)),
          "Owner memory estimate omitted dynamic scene storage");
    alias.AllocateSceneBits(64);
    alias.ShareDataFrom(&owner);
    const int aliasBase = alias.CKObject::GetMemoryOccupation() +
        static_cast<int>(sizeof(RCKObjectAnimation) - sizeof(CKObject));
    Check(alias.GetMemoryOccupation() == aliasBase + alias.SceneBytes(),
          "Shared animation double-counted the owned keyframe container or omitted its own scenes");
}

// Pass 23 controller editing regressions.
template<class Controller, class Key>
void LinearEditAllocationFailuresPreserveState() {
    Controller controller;
    Key key;
    float values[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    std::memcpy(reinterpret_cast<char *>(&key) + 4, values, sizeof(Key) - 4);
    key.TimeStep = 2.0f;
    Check(controller.AddKey(&key) == 0, "Cannot prepare Linear edit fixture");
    controller.SetLength(37.0f);
    CKKey *first = controller.GetKey(0);
    WireWords before((4 + sizeof(Key)) / 4, 0);
    Check(controller.DumpKeysTo(before.data()) == static_cast<int>(before.size() * 4), "Cannot snapshot Linear edit fixture");
    key.TimeStep = 5.0f;
    s_MorphArrayAllocationFailure = 0;
    const int added = controller.AddKey(&key);
    const bool injected = s_MorphArrayAllocationFailure == -1;
    s_MorphArrayAllocationFailure = -1;
    Check(added == -1 && injected, "Linear AddKey did not propagate allocation failure");
    Check(controller.GetKeyCount() == 1 && controller.GetKey(0) == first && controller.GetLength() == 37.0f,
          "Failed Linear AddKey changed owned state");
    WireWords after(before.size(), 0);
    Check(controller.DumpKeysTo(after.data()) == static_cast<int>(after.size() * 4) && after == before,
          "Failed Linear AddKey changed key bytes");
    Check(controller.AddKey(&key) == 1, "Cannot prepare Linear removal fixture");
    first = controller.GetKey(0);
    before.resize((4 + 2 * sizeof(Key)) / 4);
    controller.DumpKeysTo(before.data());
    s_MorphArrayAllocationFailure = 0;
    controller.RemoveKey(0);
    const bool removalInjected = s_MorphArrayAllocationFailure == -1;
    s_MorphArrayAllocationFailure = -1;
    Check(removalInjected && controller.GetKeyCount() == 2 && controller.GetKey(0) == first && controller.GetLength() == 37.0f,
          "Failed Linear RemoveKey changed owned state");
    after.resize(before.size());
    Check(controller.DumpKeysTo(after.data()) == static_cast<int>(after.size() * 4) && after == before,
          "Failed Linear RemoveKey changed key bytes");
}

template<class Controller, class Key>
void LinearEditUpdatesFirstLoadedDuplicateAndPreservesPayload() {
    Key expected[3];
    for (int i = 0; i < 3; ++i) {
        const float values[4] = {float(i + 1), float(i + 2), float(i + 3), float(i + 4)};
        std::memcpy(reinterpret_cast<char *>(&expected[i]) + 4, values, sizeof(Key) - 4);
        expected[i].TimeStep = i < 2 ? 2.0f : 10.0f;
    }
    WireWords words((4 + sizeof(expected)) / 4);
    words[0] = 3;
    std::memcpy(words.data() + 1, expected, sizeof(expected));
    Controller controller;
    Check(controller.ReadKeysFrom(words.data()) == static_cast<int>(words.size() * 4),
          "Cannot load Linear duplicate-time fixture");
    controller.SetLength(37.0f);
    CKKey *first = controller.GetKey(0), *second = controller.GetKey(1);
    Check(controller.AddKey(nullptr) == -1 && controller.GetKeyCount() == 3,
          "Null Linear AddKey changed state");
    // A key owned by this controller must update the first matching key,
    // without reallocating or modifying the later duplicate.
    s_MorphArrayAllocationFailure = 0;
    const int updated = controller.AddKey(second);
    const bool noAllocation = s_MorphArrayAllocationFailure == 0;
    s_MorphArrayAllocationFailure = -1;
    Check(updated == 0 && noAllocation && controller.GetKey(0) == first && controller.GetKey(1) == second &&
          controller.GetKeyCount() == 3 && controller.GetLength() == 37.0f,
          "Linear owned duplicate update changed storage or metadata");
    std::memcpy(reinterpret_cast<char *>(&expected[0]) + 4,
                reinterpret_cast<char *>(&expected[1]) + 4, sizeof(Key) - 4);
    std::memcpy(words.data() + 1, expected, sizeof(expected));
    WireWords output(words.size());
    Check(controller.DumpKeysTo(output.data()) == static_cast<int>(output.size() * 4) && output == words,
          "Linear duplicate update lost value fields or modified another key");
    controller.RemoveKey(0);
    Check(controller.GetKeyCount() == 2 && std::memcmp(controller.GetKey(0), &expected[1], sizeof(Key)) == 0 &&
          std::memcmp(controller.GetKey(1), &expected[2], sizeof(Key)) == 0 && controller.GetLength() == 37.0f,
          "Linear removal lost surviving duplicate payloads");
    Controller reloaded;
    output.resize((4 + 2 * sizeof(Key)) / 4);
    Check(controller.DumpKeysTo(output.data()) == static_cast<int>(output.size() * 4) &&
          reloaded.ReadKeysFrom(output.data()) == static_cast<int>(output.size() * 4) &&
          std::memcmp(reloaded.GetKey(0), &expected[1], sizeof(Key)) == 0 &&
          std::memcmp(reloaded.GetKey(1), &expected[2], sizeof(Key)) == 0,
          "Linear edited payloads changed after wire reload");
}

template<class Controller, class Key>
void LinearEditPreservesOrderingDuplicatesAndLength() {
    Controller controller;
    controller.SetLength(37.0f);
    Key key;
    float values[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    std::memcpy(reinterpret_cast<char *>(&key) + 4, values, sizeof(Key) - 4);
    key.TimeStep = 10.0f; Check(controller.AddKey(&key) == 0, "Linear initial insertion failed");
    key.TimeStep = 0.0f; Check(controller.AddKey(&key) == 0, "Linear prepend failed");
    key.TimeStep = 5.0f; Check(controller.AddKey(&key) == 1, "Linear middle insertion failed");
    Check(controller.GetKeyCount() == 3 && controller.GetKey(0)->TimeStep == 0.0f &&
          controller.GetKey(1)->TimeStep == 5.0f && controller.GetKey(2)->TimeStep == 10.0f && controller.GetLength() == 37.0f,
          "Linear insertion changed native ordering or length");
    CKKey *first = controller.GetKey(0);
    key.TimeStep = -0.0f; values[0] = 9.0f;
    std::memcpy(reinterpret_cast<char *>(&key) + 4, values, sizeof(Key) - 4);
    s_MorphArrayAllocationFailure = 0;
    const int duplicate = controller.AddKey(&key);
    const bool noAllocation = s_MorphArrayAllocationFailure == 0;
    s_MorphArrayAllocationFailure = -1;
    Check(duplicate == 0 && noAllocation && controller.GetKey(0) == first && controller.GetKeyCount() == 3,
          "Linear duplicate time allocated or replaced storage");
    CKDWORD timeBits; std::memcpy(&timeBits, &first->TimeStep, 4);
    Check(timeBits == 0 && std::memcmp(reinterpret_cast<char *>(first) + 4, values, sizeof(Key) - 4) == 0,
          "Linear duplicate changed existing time bits or lost value");
    Check(controller.AddKey(controller.GetKey(1)) == 1 && controller.GetKeyCount() == 3,
          "Linear self-key update failed");
    controller.RemoveKey(-1); controller.RemoveKey(3); controller.RemoveKey(INT_MAX);
    Check(controller.GetKeyCount() == 3 && controller.GetKey(0) == first, "Invalid Linear removal changed state");
    controller.RemoveKey(1);
    Check(controller.GetKeyCount() == 2 && controller.GetKey(0)->TimeStep == 0.0f && controller.GetKey(1)->TimeStep == 10.0f,
          "Linear middle removal changed surviving keys");
    controller.RemoveKey(0);
    Check(controller.GetKeyCount() == 1 && controller.GetKey(0)->TimeStep == 10.0f, "Linear first removal failed");
    s_MorphArrayAllocationFailure = 0;
    controller.RemoveKey(0);
    const bool emptyNoAllocation = s_MorphArrayAllocationFailure == 0;
    s_MorphArrayAllocationFailure = -1;
    Check(emptyNoAllocation && controller.GetKeyCount() == 0 && controller.GetKey(0) == nullptr && controller.GetLength() == 37.0f,
          "Linear final removal allocated or changed length");
}

// Pass 23 TCB tests; independent wire fixture and executable-only allocation faults.
template<class Controller> struct TCBEditTraits;
template<> struct TCBEditTraits<RCKTCBPositionController> { typedef CKTCBPositionKey Key; };
template<> struct TCBEditTraits<RCKTCBRotationController> { typedef CKTCBRotationKey Key; };
template<> struct TCBEditTraits<RCKTCBScaleController> { typedef CKTCBScaleKey Key; };
template<> struct TCBEditTraits<RCKTCBScaleAxisController> { typedef CKTCBScaleAxisKey Key; };

template<class Controller>
class TCBEditProbe : public Controller {
public:
    void *Keys() const { return this->m_Keys; }
    void *Tangents() const { return this->m_Tangents; }
};

template<class Controller, CKANIMATION_CONTROLLER Type>
void TCBAddAllocationFailureIsAtomic() {
    TCBEditProbe<Controller> controller;
    WireWords original = TransformControllerWire(Type);
    Check(controller.ReadKeysFrom(original.data()) == static_cast<int>(original.size()*4), "TCB fixture read failed");
    controller.SetLength(37.0f);
    VxQuaternion before, after;
    Check(controller.Evaluate(5.0f, &before), "TCB cache prime failed");
    void *keys = controller.Keys(), *cache = controller.Tangents();
    const size_t keySize = (original.size()*4-sizeof(int))/2;
    typename TCBEditTraits<Controller>::Key keyStorage;
    std::memcpy(&keyStorage, controller.GetKey(0), keySize);
    CKKey *input = &keyStorage; input->TimeStep = 6.0f;
    s_MorphArrayAllocationFailure = 0;
    int result = controller.AddKey(input);
    const bool injected = s_MorphArrayAllocationFailure == -1;
    s_MorphArrayAllocationFailure = -1;
    Check(injected && result == -1, "TCB Add did not report key allocation failure");
    Check(controller.GetKeyCount() == 2 && controller.GetLength() == 37.0f && controller.Keys() == keys &&
          controller.Tangents() == cache, "Failed TCB Add changed complete state");
    Check(controller.Evaluate(5.0f, &after) && after == before, "Failed TCB Add changed sampled output");
    WireWords actual(original.size());
    Check(controller.DumpKeysTo(actual.data()) == static_cast<int>(actual.size()*4) && actual == original,
          "Failed TCB Add changed serialized keys");
}

template<class Controller, CKANIMATION_CONTROLLER Type>
void TCBRemoveAllocationFailureIsAtomic() {
    TCBEditProbe<Controller> controller;
    WireWords original = TransformControllerWire(Type);
    Check(controller.ReadKeysFrom(original.data()) == static_cast<int>(original.size()*4), "TCB fixture read failed");
    controller.SetLength(37.0f);
    VxQuaternion before, after;
    Check(controller.Evaluate(5.0f, &before), "TCB cache prime failed");
    void *keys = controller.Keys(), *cache = controller.Tangents();
    for (int index : {0, 1}) {
        s_MorphArrayAllocationFailure = 0;
        controller.RemoveKey(index);
        const bool injected = s_MorphArrayAllocationFailure == -1;
        s_MorphArrayAllocationFailure = -1;
        Check(injected, "TCB Remove did not use failure-reporting allocation");
        Check(controller.GetKeyCount() == 2 && controller.GetLength() == 37.0f && controller.Keys() == keys &&
              controller.Tangents() == cache, "Failed TCB Remove changed complete state");
        Check(controller.Evaluate(5.0f, &after) && after == before, "Failed TCB Remove changed sampled output");
    }
    WireWords actual(original.size());
    Check(controller.DumpKeysTo(actual.data()) == static_cast<int>(actual.size()*4) && actual == original,
          "Failed TCB Remove changed serialized keys");
}

template<class Controller, CKANIMATION_CONTROLLER Type>
void TCBValidEditsPreserveNativeDuplicateParametersAndLength() {
    TCBEditProbe<Controller> controller;
    WireWords original = TransformControllerWire(Type);
    Check(controller.ReadKeysFrom(original.data()) == static_cast<int>(original.size()*4), "TCB fixture read failed");
    controller.SetLength(37.0f);
    const size_t keySize = (original.size()*4-sizeof(int))/2;
    typename TCBEditTraits<Controller>::Key keyStorage;
    std::memcpy(&keyStorage, controller.GetKey(0), keySize);
    CKKey *input = &keyStorage;
    input->TimeStep = 6.0f;
    VxQuaternion sample;
    Check(controller.Evaluate(5.0f, &sample) && controller.Tangents(), "Cannot prime TCB edit cache");
    Check(controller.AddKey(input) == 1 && controller.GetKeyCount() == 3 && controller.GetLength() == 37.0f,
          "TCB sorted insertion changed count or length");
    Check(!controller.Tangents(), "Successful TCB insertion retained stale cache");
    Check(!std::memcmp(controller.GetKey(1), &keyStorage, keySize), "TCB insertion lost complete typed key");
    Check(controller.AddKey(controller.GetKey(1)) == 1 && controller.GetKeyCount() == 3,
          "TCB equal-time self-alias replacement changed count");
    typename TCBEditTraits<Controller>::Key expectedDuplicate = *static_cast<typename TCBEditTraits<Controller>::Key *>(controller.GetKey(1));
    keyStorage.tension = -0.625f;
    keyStorage.continuity = 0.375f;
    keyStorage.bias = -0.125f;
    keyStorage.easeto = 0.75f;
    keyStorage.easefrom = 0.875f;
    const float replacedValue = 7.25f;
    std::memcpy(reinterpret_cast<unsigned char *>(&keyStorage)+sizeof(float), &replacedValue, sizeof(float));
    std::memcpy(reinterpret_cast<unsigned char *>(&expectedDuplicate)+sizeof(float), &replacedValue, sizeof(float));
    Check(controller.Evaluate(5.0f, &sample) && controller.Tangents(), "Cannot re-prime TCB replacement cache");
    s_MorphArrayAllocationFailure = 0;
    const int replaced = controller.AddKey(input);
    const bool noAllocation = s_MorphArrayAllocationFailure == 0;
    s_MorphArrayAllocationFailure = -1;
    Check(replaced == 1 && noAllocation && !std::memcmp(controller.GetKey(1), &expectedDuplicate, keySize),
          "TCB equal-time replacement allocated or changed native spline parameters");
    Check(!controller.Tangents(), "Successful TCB replacement retained stale cache");
    controller.RemoveKey(-1); controller.RemoveKey(3);
    Check(controller.GetKeyCount() == 3, "TCB invalid Remove index changed count");
    controller.RemoveKey(1);
    WireWords actual(original.size());
    Check(controller.DumpKeysTo(actual.data()) == static_cast<int>(actual.size()*4) && actual == original,
          "TCB insertion/removal did not restore wire keys");
    controller.RemoveKey(0);
    Check(controller.GetKeyCount() == 1 && controller.GetKey(0)->TimeStep == 10.0f,
          "TCB Remove changed retained key ordering");
    s_MorphArrayAllocationFailure = 0; controller.RemoveKey(0);
    const bool noFinalAllocation = s_MorphArrayAllocationFailure == 0;
    s_MorphArrayAllocationFailure = -1;
    Check(noFinalAllocation && controller.GetKeyCount() == 0 && !controller.Keys() && !controller.Tangents() &&
          controller.GetLength() == 37.0f, "TCB final Remove allocated or left keys/cache");
    input->TimeStep = FloatWithBits(0x80000000u);
    Check(controller.AddKey(input) == 0, "TCB signed-zero insertion failed");
    input->TimeStep = 0.0f;
    Check(controller.AddKey(input) == 0, "TCB equal signed-zero replacement failed");
    CKDWORD timeBits; std::memcpy(&timeBits, &controller.GetKey(0)->TimeStep, sizeof(timeBits));
    Check(timeBits == 0x80000000u, "TCB replacement changed existing equal-time bits");
}

template<class Controller, CKANIMATION_CONTROLLER Type>
void TCBTangentRebuildAllocationFailurePreservesCache() {
    TCBEditProbe<Controller> controller;
    WireWords original = TransformControllerWire(Type);
    Check(controller.ReadKeysFrom(original.data()) == static_cast<int>(original.size()*4), "TCB fixture read failed");
    controller.SetLength(37.0f);
    VxQuaternion before, after;
    Check(controller.Evaluate(5.0f, &before), "TCB cache prime failed");
    void *keys = controller.Keys(), *cache = controller.Tangents();
    s_MorphArrayAllocationFailure = 0;
    controller.ComputeTangents();
    const bool injected = s_MorphArrayAllocationFailure == -1;
    s_MorphArrayAllocationFailure = -1;
    Check(injected && controller.Tangents() == cache && controller.Keys() == keys &&
          controller.GetKeyCount() == 2 && controller.GetLength() == 37.0f,
          "Failed TCB tangent rebuild destroyed previous cache or keys");
    Check(controller.Evaluate(5.0f, &after) && after == before, "Failed tangent rebuild changed sample");
}

template<class Controller, CKANIMATION_CONTROLLER Type>
void TCBEvaluatePropagatesTangentFailureWithoutWritingOutput() {
    TCBEditProbe<Controller> controller;
    WireWords original = TransformControllerWire(Type);
    Check(controller.ReadKeysFrom(original.data()) == static_cast<int>(original.size()*4), "TCB fixture read failed");
    controller.SetLength(37.0f);
    VxQuaternion output(11,12,13,14); const VxQuaternion sentinel = output;
    s_MorphArrayAllocationFailure = 0;
    const bool evaluated = !!controller.Evaluate(5.0f, &output);
    const bool injected = s_MorphArrayAllocationFailure == -1;
    s_MorphArrayAllocationFailure = -1;
    Check(injected && !evaluated && output == sentinel && !controller.Tangents() && controller.GetKeyCount() == 2 &&
          controller.GetLength() == 37.0f, "TCB Evaluate did not propagate tangent allocation failure atomically");
    s_MorphArrayAllocationFailure = 0;
    const bool endpoint = !!controller.Evaluate(2.0f, &output);
    const bool noAllocation = s_MorphArrayAllocationFailure == 0;
    s_MorphArrayAllocationFailure = -1;
    Check(endpoint && noAllocation && !controller.Tangents(), "TCB endpoint unnecessarily requires tangent allocation");
    Check(controller.Evaluate(5.0f, &output) && controller.Tangents(), "TCB Evaluate did not recover after allocation failure");
}

template<class Controller, CKANIMATION_CONTROLLER Type>
void TCBEvaluateRetainsFinitePhaseAcrossOverflowingTimeDifference() {
    TCBEditProbe<Controller> reference, wide;
    WireWords original = TransformControllerWire(Type);
    Check(reference.ReadKeysFrom(original.data()) == static_cast<int>(original.size()*4) &&
          wide.ReadKeysFrom(original.data()) == static_cast<int>(original.size()*4), "TCB affine-time fixture read failed");
    for (int i=0; i<2; ++i) {
        typename TCBEditTraits<Controller>::Key *a = static_cast<typename TCBEditTraits<Controller>::Key *>(reference.GetKey(i));
        typename TCBEditTraits<Controller>::Key *b = static_cast<typename TCBEditTraits<Controller>::Key *>(wide.GetKey(i));
        a->easefrom = a->easeto = b->easefrom = b->easeto = 0.0f;
        b->TimeStep = i ? FLT_MAX : -FLT_MAX;
    }
    VxQuaternion expected, actual;
    Check(reference.Evaluate(6.0f, &expected) && wide.Evaluate(0.0f, &actual),
          "TCB finite affine time interval failed evaluation");
    Check(std::fabs(expected.x-actual.x) < 0.00001f && std::fabs(expected.y-actual.y) < 0.00001f &&
          std::fabs(expected.z-actual.z) < 0.00001f && std::fabs(expected.w-actual.w) < 0.00001f,
          "TCB lost midpoint phase when finite time subtraction overflowed");
}

template<class Controller>
class BezierEditProbe : public Controller {
public:
    CKBezierPositionKey *Keys() { return this->m_Keys; }
    CKBOOL TangentsComputed() const { return this->m_TangentsComputed; }
    void SetTangentsComputed(CKBOOL computed) { this->m_TangentsComputed = computed; }
};

template<class Controller, CKANIMATION_CONTROLLER Type>
void BezierAddAllocationFailurePreservesState() {
    BezierEditProbe<Controller> controller;
    WireWords original = TransformControllerWire(Type);
    Check(controller.ReadKeysFrom(original.data()) > 0, "Cannot initialize Bezier Add failure test");
    controller.SetLength(37.0f);
    VxVector sampled;
    Check(controller.Evaluate(5.0f, &sampled), "Cannot prime Bezier tangent cache");
    const int count = controller.GetKeyCount();
    CKBezierPositionKey *keys = controller.Keys();
    std::vector<CKBezierPositionKey> snapshot(keys, keys + count);
    const CKBOOL computed = controller.TangentsComputed();
    CKBezierPositionKey added = *keys; added.TimeStep = -2.0f;
    s_MorphArrayAllocationFailure = 0;
    int index = controller.AddKey(&added);
    const bool injected = s_MorphArrayAllocationFailure == -1;
    s_MorphArrayAllocationFailure = -1;
    Check(index == -1 && injected, "Bezier Add did not report injected allocation failure");
    Check(controller.GetKeyCount() == count && controller.Keys() == keys && controller.GetLength() == 37.0f &&
          controller.TangentsComputed() == computed && !std::memcmp(keys, snapshot.data(), snapshot.size()*sizeof(*keys)),
          "Failed Bezier Add changed key storage, length, or tangent cache");
}

template<class Controller, CKANIMATION_CONTROLLER Type>
void BezierRemoveAllocationFailurePreservesState() {
    BezierEditProbe<Controller> controller;
    WireWords original = TransformControllerWire(Type);
    Check(controller.ReadKeysFrom(original.data()) > 0, "Cannot initialize Bezier Remove failure test");
    controller.SetLength(37.0f);
    controller.ComputeBezierPts();
    const int count = controller.GetKeyCount();
    CKBezierPositionKey *keys = controller.Keys();
    std::vector<CKBezierPositionKey> snapshot(keys, keys + count);
    const CKBOOL computed = controller.TangentsComputed();
    s_MorphArrayAllocationFailure = 0;
    controller.RemoveKey(1);
    const bool injected = s_MorphArrayAllocationFailure == -1;
    s_MorphArrayAllocationFailure = -1;
    Check(injected && controller.GetKeyCount() == count && controller.Keys() == keys &&
          controller.GetLength() == 37.0f && controller.TangentsComputed() == computed &&
          !std::memcmp(keys, snapshot.data(), snapshot.size()*sizeof(*keys)),
          "Failed Bezier Remove changed key storage, length, or tangent cache");
}


template<class Controller, CKANIMATION_CONTROLLER Type>
void BezierLegalEditsFollowNativeValueAndTangentContract() {
    BezierEditProbe<Controller> controller;
    WireWords original = TransformControllerWire(Type);
    Check(controller.ReadKeysFrom(original.data()) > 0, "Cannot initialize native Bezier edit test");
    controller.SetLength(37.0f);
    CKBezierPositionKey added = *controller.Keys(); added.TimeStep = -2.0f;
    Check(controller.AddKey(&added) == 0 && controller.GetKeyCount() == 5 && controller.GetLength() == 37.0f,
          "Bezier insertion changed sorting, count, or length");
    Check(!std::memcmp(controller.Keys(),&added,sizeof(added)), "Bezier insertion lost fields or tangent flags");
    controller.ComputeBezierPts();
    Check(controller.TangentsComputed(), "Bezier tangent calculation did not finish");
    Check(controller.AddKey(controller.GetKey(2)) == 2 && !controller.TangentsComputed() && controller.GetKeyCount() == 5,
          "Bezier self-alias replacement changed count or retained stale tangents");
    controller.ComputeBezierPts();
    CKBezierPositionKey expected = *controller.Keys();
    CKBezierPositionKey replacement = expected;
    replacement.Pos = VxVector(7,8,9);
    replacement.In = VxVector(101,102,103); replacement.Out = VxVector(104,105,106);
    replacement.Flags.SetInTangentMode(BEZIER_KEY_STEP);
    replacement.Flags.SetOutTangentMode(BEZIER_KEY_SLOW);
    expected.Pos = replacement.Pos;
    Check(controller.AddKey(&replacement) == 0 && controller.GetKeyCount() == 5 && !controller.TangentsComputed() &&
          !std::memcmp(controller.Keys(),&expected,sizeof(expected)),
          "Native Bezier duplicate must change only position and invalidate calculation");
    CKBezierPositionKey zeroReplacement = *static_cast<CKBezierPositionKey *>(controller.GetKey(1));
    const CKBezierPositionKey oldZero = zeroReplacement;
    zeroReplacement.TimeStep = -0.0f; zeroReplacement.Pos = VxVector(3,4,5);
    Check(controller.AddKey(&zeroReplacement) == 1 && !std::signbit(controller.Keys()[1].TimeStep) &&
          !(controller.Keys()[1].Flags != oldZero.Flags) && controller.Keys()[1].In == oldZero.In &&
          controller.Keys()[1].Out == oldZero.Out, "Bezier duplicate changed existing time bits or tangent fields");
    // The shared in-place tangent helper does not allocate a second cache.
    s_MorphArrayAllocationFailure = 0;
    controller.ComputeBezierPts(); controller.ComputeBezierPts(1);
    const bool noAllocation = s_MorphArrayAllocationFailure == 0;
    s_MorphArrayAllocationFailure = -1;
    Check(noAllocation && controller.TangentsComputed(), "Native Bezier tangent calculation allocated a scratch cache");
    VxVector output;
    Check(controller.Evaluate(2.5f,&output), "Edited Bezier controller could not evaluate");
    // A targeted smoothing update preserves the global calculation flag.
    Check(controller.AddKey(controller.GetKey(2)) == 2 && !controller.TangentsComputed(), "Cannot invalidate Bezier calculation");
    controller.ComputeBezierPts(1);
    Check(!controller.TangentsComputed(), "Targeted Bezier smoothing claimed a complete tangent calculation");
    while (controller.GetKeyCount() > 0) {
        controller.ComputeBezierPts();
        controller.RemoveKey(controller.GetKeyCount()-1);
        Check(!controller.TangentsComputed() && controller.GetLength() == 37.0f,
              "Bezier removal retained stale calculation state or changed length");
    }
    Check(!controller.Keys() && !controller.GetKey(0), "Bezier final removal retained storage");
}

void MorphEditInsertionFailuresPreserveEveryOwnedArray() {
    for (int failedAllocation = 0; failedAllocation < 3; ++failedAllocation) {
        RCKMorphController controller;
        WireWords words = MorphWire<true>();
        Check(controller.ReadKeysFrom(words.data()) == static_cast<int>(words.size() * 4), "Cannot prepare Morph edit fixture");
        controller.SetLength(37.0f);
        CKMorphKey *first = static_cast<CKMorphKey *>(controller.GetKey(0));
        VxVector *positions = first->PosArray;
        VxCompressedVector *normals = first->NormArray;
        CKMorphKey source = *first; source.TimeStep = 5.0f;
        s_MorphArrayAllocationFailure = failedAllocation;
        const int inserted = controller.AddKey(&source, FALSE);
        const bool injected = s_MorphArrayAllocationFailure == -1;
        s_MorphArrayAllocationFailure = -1;
        Check(inserted == -1 && injected, "Morph insertion did not propagate a selected array allocation failure");
        Check(controller.GetKeyCount() == 2 && controller.GetKey(0) == first && first->PosArray == positions &&
              first->NormArray == normals && controller.GetLength() == 37.0f && controller.GetMorphVertexCount() == 2,
              "Failed Morph insertion changed owned pointers or metadata");
        WireWords after(words.size(), 0u);
        Check(controller.DumpKeysTo(after.data()) == static_cast<int>(words.size() * 4) && after == words,
              "Failed Morph insertion changed payload bytes");
        Check(controller.AddKey(&source, FALSE) == 1, "Morph insertion could not recover after allocation failure");
        CKMorphKey *added = static_cast<CKMorphKey *>(controller.GetKey(1));
        Check(added->PosArray != positions && added->NormArray != normals && added->NormArray &&
              Equal(added->PosArray[0], positions[0]) && added->NormArray[0].xa == normals[0].xa,
              "Morph insertion did not independently copy aliased source payloads");
    }
}

void MorphEditRemovalFailurePreservesPayloads() {
    RCKMorphController controller;
    WireWords words = MorphWire<true>();
    Check(controller.ReadKeysFrom(words.data()) == static_cast<int>(words.size() * 4), "Cannot prepare Morph removal fixture");
    controller.SetLength(37.0f);
    CKMorphKey *first = static_cast<CKMorphKey *>(controller.GetKey(0));
    VxVector *positions = first->PosArray; VxCompressedVector *normals = first->NormArray;
    s_MorphArrayAllocationFailure = 0;
    controller.RemoveKey(0);
    const bool injected = s_MorphArrayAllocationFailure == -1;
    s_MorphArrayAllocationFailure = -1;
    Check(injected && controller.GetKeyCount() == 2 && controller.GetKey(0) == first &&
          first->PosArray == positions && first->NormArray == normals && controller.GetLength() == 37.0f,
          "Failed Morph removal released payloads or changed count");
    WireWords after(words.size(), 0u);
    Check(controller.DumpKeysTo(after.data()) == static_cast<int>(words.size() * 4) && after == words,
          "Failed Morph removal changed payload bytes");
    controller.RemoveKey(0);
    Check(controller.GetKeyCount() == 1 && controller.GetKey(0)->TimeStep == 10.0f, "Morph removal recovery lost surviving key");
    s_MorphArrayAllocationFailure = 0;
    controller.RemoveKey(0);
    const bool noAllocation = s_MorphArrayAllocationFailure == 0;
    s_MorphArrayAllocationFailure = -1;
    Check(noAllocation && controller.GetKeyCount() == 0 && controller.GetKey(0) == nullptr && controller.GetLength() == 37.0f,
          "Final Morph removal allocated or changed length");
}

void MorphEditPreservesNativeDuplicateAndMixedPayloadContract() {
    RCKMorphController controller;
    WireWords words = MorphWire<true>();
    Check(controller.ReadKeysFrom(words.data()) == static_cast<int>(words.size() * 4), "Cannot prepare Morph functional edit fixture");
    controller.SetLength(37.0f);
    CKMorphKey *first = static_cast<CKMorphKey *>(controller.GetKey(0));
    CKMorphKey duplicate; duplicate.TimeStep = first->TimeStep; duplicate.PosArray = nullptr; duplicate.NormArray = nullptr;
    s_MorphArrayAllocationFailure = 0;
    const int existing = controller.AddKey(&duplicate, FALSE);
    const bool noAllocation = s_MorphArrayAllocationFailure == 0;
    s_MorphArrayAllocationFailure = -1;
    WireWords after(words.size(), 0u);
    Check(existing == 0 && noAllocation && controller.GetKey(0) == first &&
          controller.DumpKeysTo(after.data()) == static_cast<int>(words.size() * 4) && after == words,
          "Morph duplicate changed the existing key or payload presence");
    Check(controller.AddKey(controller.GetKey(1), FALSE) == 1, "Morph owned-key duplicate changed its index");
    Check(controller.AddKey(-5.0f, FALSE) == 0, "Morph prepend changed native time ordering");
    CKMorphKey *added = static_cast<CKMorphKey *>(controller.GetKey(0));
    Check(added->PosArray && !added->NormArray && Equal(added->PosArray[0], VxVector(0.0f)) &&
          controller.GetLength() == 37.0f, "Morph time-only insertion changed allocation or length semantics");
    controller.RemoveKey(-1); controller.RemoveKey(3);
    Check(controller.GetKeyCount() == 3, "Invalid Morph removal index changed state");
    controller.RemoveKey(0);
    Check(controller.DumpKeysTo(after.data()) == static_cast<int>(words.size() * 4) && after == words,
          "Morph prepend/remove changed surviving payloads");
}

template<bool TimeOnly>
void MorphEditWrapperPropagatesAllocationFailure() {
    for (int allocation = 0; allocation < 3; ++allocation) {
        RCKMorphController controller;
        controller.SetMorphVertexCount(2); controller.SetLength(37.0f);
        CKMorphKey key; key.TimeStep = 5.0f; key.PosArray = nullptr; key.NormArray = nullptr;
        s_MorphArrayAllocationFailure = allocation;
        const int inserted = TimeOnly ? controller.AddKey(5.0f, TRUE) : controller.AddKey(&key);
        const bool injected = s_MorphArrayAllocationFailure == -1;
        s_MorphArrayAllocationFailure = -1;
        Check(inserted == -1 && injected && controller.GetKeyCount() == 0 && !controller.GetKey(0) &&
              controller.GetLength() == 37.0f && controller.GetMorphVertexCount() == 2,
              "Morph insertion wrapper did not preserve state or propagate allocation failure");
        Check((TimeOnly ? controller.AddKey(5.0f, TRUE) : controller.AddKey(&key)) == 0,
              "Morph insertion wrapper failed to recover after allocation failure");
        CKMorphKey *added = static_cast<CKMorphKey *>(controller.GetKey(0));
        Check(added->PosArray && added->NormArray && Equal(added->PosArray[0], VxVector(0.0f)) &&
              added->NormArray[0].xa == 0 && added->NormArray[0].ya == 0,
              "Morph insertion wrapper lost its allocation/default payload contract");
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
void TransformSelfCloneAndFailedCopyPreserveKeys() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation sourceAnimation(&context, "TransformFailedCopySource"), animation(&context, "TransformSelfClone");
    WireWords words = TransformControllerWire(Type);
    Chunk sourceChunk = AnimationWithController(Type, words);
    Chunk chunk = AnimationWithController(Type, words);
    LoadChunk(sourceAnimation, sourceChunk.get());
    LoadChunk(animation, chunk.get());
    CKAnimController *source = TransformController(sourceAnimation, Type);
    CKAnimController *controller = TransformController(animation, Type);
    const int keyCount = controller->GetKeyCount();
    CKKey *key = controller->GetKey(0);

    s_MorphArrayAllocationFailure = 0;
    const CKBOOL cloned = controller->Clone(controller);
    const bool noAllocation = s_MorphArrayAllocationFailure == 0;
    s_MorphArrayAllocationFailure = -1;
    Check(cloned && noAllocation, "Transform self-Clone allocated or failed");
    Check(keyCount > 0 && controller->GetKeyCount() == keyCount && controller->GetKey(0) == key && controller->GetLength() == 20.0f,
          "Transform self-Clone changed owned keys");
    CheckSavedController(animation, Type, words);

    sourceAnimation.SetLength(37.0f);
    source->GetKey(0)->SetTime(-3.0f);
    s_MorphArrayAllocationFailure = 0;
    const CKBOOL failed = controller->Clone(source);
    s_MorphArrayAllocationFailure = -1;
    Check(!failed, "Transform Clone ignored a key allocation failure");
    Check(keyCount > 0 && controller->GetKeyCount() == keyCount && controller->GetKey(0) == key && controller->GetLength() == 20.0f,
          "Failed transform Clone changed the destination");
    CheckSavedController(animation, Type, words);
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

// Count-only native fixtures document the intentional difference: portable
// resizing discards truncated vertices and clears newly introduced storage.
template<int FixtureIndex>
void MorphCountEditingUsesPortableStorageContract() {
    const MorphEditReference::Fixture &fixture = MorphEditReference::Fixtures[FixtureIndex];
    const bool normals = FixtureIndex == 12;
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation animation(&context, "MorphPortableCountEditing");
    WireWords input(fixture.words, fixture.words + fixture.wordCount);
    Chunk initial = AnimationWithController(CKANIMATION_MORPH_CONTROL, input);
    LoadChunk(animation, initial.get());
    RCKMorphController *controller = static_cast<RCKMorphController *>(animation.GetMorphController());
    for (int step = 0; step < fixture.operationCount; ++step) {
        const int count = fixture.operations[step].argument;
        CKMorphKey *first = static_cast<CKMorphKey *>(controller->GetKey(0));
        VxVector *oldPositions = first->PosArray;
        controller->SetMorphVertexCount(count);
        Check(controller->GetMorphVertexCount() == fixture.states[step].vertexCount && controller->GetKeyCount() == 2 &&
              controller->GetKey(0) == first && first->PosArray != oldPositions,
              "Morph count fixture did not resize coherent storage");
        CKDWORD expected[45] = {};
        expected[0] = 2; expected[1] = count; expected[2] = normals ? 1 : 0;
        int word = 3;
        for (int keyIndex = 0; keyIndex < 2; ++keyIndex) {
            CKMorphKey *key = static_cast<CKMorphKey *>(controller->GetKey(keyIndex));
            const float time = keyIndex ? 10.0f : 2.0f;
            Check(key->TimeStep == time && key->PosArray && (key->NormArray != nullptr) == normals,
                  "Morph count fixture lost array presence or key time");
            memcpy(expected + word++, &time, sizeof(time));
            for (int vertex = 0; vertex < count; ++vertex) {
                // Only the initial 3 -> 1 step retains data. The following
                // transition through zero discards all former payload values.
                const VxVector value = step == 0 ? VxVector(keyIndex ? 11.0f : 1.0f, keyIndex ? 12.0f : 2.0f,
                    keyIndex ? 13.0f : 3.0f) : VxVector(0.0f);
                Check(Equal(key->PosArray[vertex], value), "Portable Morph count fixture retained truncated data or lost its prefix");
                memcpy(expected + word, &value, sizeof(value)); word += 3;
            }
            if (normals) {
                for (int vertex = 0; vertex < count; ++vertex) {
                    const CKDWORD packed = step == 0 ? (keyIndex ? 0x00100084u : 0x00100020u) : 0u;
                    Check(!memcmp(key->NormArray + vertex, &packed, sizeof(packed)), "Portable count fixture changed normal bytes");
                    expected[word++] = packed;
                }
            }
        }
        WireWords expectedWire(expected, expected + word);
        CheckSavedController(animation, CKANIMATION_MORPH_CONTROL, expectedWire);
        Chunk saved(animation.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
        RCKObjectAnimation reloaded(&context, "MorphCountFixtureReloaded");
        LoadChunk(reloaded, saved.get());
        CheckSavedController(reloaded, CKANIMATION_MORPH_CONTROL, expectedWire);
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

// Pass 24 shared functional regressions.
// Native Load 0x10058C51: shared references have priority and Clear only resets
// per-object state. These fixtures exercise live owners, aliases and payloads.
void SharedLoadMovesOwnerAndPreservesRemainingAlias() {
    CKContext context(nullptr, 0, 0);
    ObjectCloneProbe target(&context, "ReloadOwner"), remaining(&context, "OldAlias"),
        source(&context, "NewOwner");
    VxVector oldValue(8, 9, 10), newValue(2, 3, 4);
    target.AddPositionKey(0, &oldValue); target.SetLength(11);
    remaining.ShareDataFrom(&target);
    source.AddPositionKey(0, &newValue); source.SetLength(29);
    CKKeyframeData *oldData = target.Data();
    Chunk chunk = NewChunk(CKCID_OBJECTANIMATION);
    WriteAnimationStateBlock(chunk.get(), CK_STATESAVE_OBJANIMSHARED, &source,
                             nullptr, nullptr, false, 0);
    LoadChunk(target, chunk.get());
    VxVector actual;
    Check(target.Data() == source.Data() && source.References() == 2 && target.Shared() == &source,
          "Shared Load did not attach exactly once to the referenced data");
    Check(remaining.Data() == oldData && remaining.Shared() == &remaining && remaining.References() == 1,
          "Reloaded owner did not hand its previous data to the remaining alias");
    Check(remaining.GetLength() == 11 && remaining.EvaluatePosition(0, actual) && Equal(actual, oldValue),
          "Shared Load changed the old alias payload");
    Check(target.GetLength() == 29 && target.EvaluatePosition(0, actual) && Equal(actual, newValue),
          "Shared Load did not use the new owner payload");
}

void SharedLoadSelfReferenceKeepsDataAndResetsState() {
    CKContext context(nullptr, 0, 0);
    ObjectCloneProbe target(&context, "SelfShared");
    VxVector value(7, 8, 9); target.AddPositionKey(0, &value); target.SetLength(23);
    target.SetCloneState(nullptr, nullptr, nullptr, CK_OBJECTANIMATION_MERGED);
    CKKeyframeData *data = target.Data();
    Chunk chunk = NewChunk(CKCID_OBJECTANIMATION);
    WriteAnimationStateBlock(chunk.get(), CK_STATESAVE_OBJANIMSHARED, &target,
                             nullptr, nullptr, false, 0);
    LoadChunk(target, chunk.get());
    VxVector actual;
    Check(target.Data() == data && target.References() == 1 && target.Shared() == &target &&
          target.GetLength() == 23 && target.EvaluatePosition(0, actual) && Equal(actual, value),
          "Self shared reference replaced or duplicated existing controller data");
    Check(!target.IsMerged() && target.GetCurrentStep() == 0 && target.GetMergeFactor() == .5f,
          "Shared Load did not reset per-object playback state");
}

void SharedLoadMissingOwnerAllocationFailureKeepsData() {
    CKContext context(nullptr, 0, 0);
    ObjectCloneProbe owner(&context, "MissingSharedOwner"), target(&context, "MissingSharedAlias");
    VxVector value(3, 5, 7); owner.AddPositionKey(0, &value); owner.SetLength(31);
    target.ShareDataFrom(&owner);
    CKKeyframeData *data = target.Data();
    Chunk chunk = NewChunk(CKCID_OBJECTANIMATION);
    WriteAnimationStateBlock(chunk.get(), CK_STATESAVE_OBJANIMSHARED, nullptr,
                             nullptr, nullptr, false, 0);
    chunk->CloseChunk(); chunk->StartRead();
    s_ObjectAnimationAllocationFailure = true;
    const CKERROR result = target.Load(chunk.get(), nullptr);
    const bool injected = !s_ObjectAnimationAllocationFailure;
    s_ObjectAnimationAllocationFailure = false;
    VxVector actual;
    Check(injected && result == CKERR_OUTOFMEMORY, "Shared Load did not propagate empty-data allocation failure");
    Check(target.Data() == data && owner.Data() == data && target.References() == 2 && target.Shared() == &owner &&
          target.EvaluatePosition(0, actual) && Equal(actual, value),
          "Failed missing-owner Load detached or destroyed shared controller data");
}

template<bool Merged>
void SharedLoadRejectsTruncatedIdentifier() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation owner(&context, "ShortSharedOwner"), target(&context, "ShortSharedTarget");
    VxVector value(3, 4, 5); owner.AddPositionKey(0, &value);
    Chunk chunk = NewChunk(CKCID_OBJECTANIMATION);
    chunk->WriteIdentifier(CK_STATESAVE_OBJANIMSHARED); chunk->WriteObject(&owner);
    for (int i = 0; i < 7; ++i) chunk->WriteFloat(0);
    chunk->WriteDword(Merged ? CK_OBJECTANIMATION_MERGED : 0);
    if (Merged) {
        chunk->WriteObject(nullptr); chunk->WriteFloat(.25f); chunk->WriteObject(&owner);
        // The second merged reference is absent.
    } // Otherwise the entity reference is absent.
    WriteAnimationStateBlock(chunk.get(), CK_STATESAVE_OBJANIMCONTROLLERS, nullptr,
                             nullptr, nullptr, false, 99);
    chunk->CloseChunk(); chunk->StartRead();
    Check(target.Load(chunk.get(), nullptr) == CKERR_INVALIDFILE,
          "Shared Load consumed the next identifier as missing state fields");
    Check(target.Shared() == &target && !target.GetPositionController(),
          "Truncated shared state attached data before completing its reference fields");
}

template<bool Merged>
void SharedLoadRestoresEntityRootAndMergedReferences() {
    CKContext context(nullptr, 0, 0);
    ObjectCloneProbe owner(&context, "StateOwner"), target(&context, "StateTarget"),
        first(&context, "StateFirst"), second(&context, "StateSecond");
    RCK3dEntity entity(&context, "StateEntity");
    VxVector value(9, 2, 3); owner.AddPositionKey(0, &value); owner.SetLength(37);
    Chunk chunk = NewChunk(CKCID_OBJECTANIMATION);
    chunk->WriteIdentifier(CK_STATESAVE_OBJANIMSHARED); chunk->WriteObject(&owner);
    chunk->WriteVector(VxVector(4, -3, 2));
    for (int i = 0; i < 4; ++i) chunk->WriteFloat(50.0f + i);
    const CKDWORD flags = Merged ? CK_OBJECTANIMATION_MERGED | 0x10000u : 0x10000u;
    chunk->WriteDword(flags); chunk->WriteObject(&entity);
    if (Merged) { chunk->WriteFloat(.375f); chunk->WriteObject(&first); chunk->WriteObject(&second); }
    LoadChunk(target, chunk.get());
    VxVector *root = static_cast<VxVector *>(target.GetAppData());
    const bool rootMatches = root && Equal(*root, VxVector(4, -3, 2));
    delete root; target.SetAppData(nullptr);
    Check(rootMatches && target.Get3dEntity() == reinterpret_cast<CK3dEntity *>(&entity) && target.GetFlags() == flags,
          "Shared Load did not restore root vector, entity or independent flags");
    Check(target.Data() == owner.Data() && target.GetLength() == 37 && target.GetCurrentStep() == 0 &&
          target.First() == (Merged ? &first : nullptr) && target.Second() == (Merged ? &second : nullptr) &&
          target.GetMergeFactor() == (Merged ? .375f : .5f),
          "Shared Load changed referenced data or merged field ordering");
    Check(entity.GetObjectAnimationCount() == 0,
          "Direct Load unexpectedly added an entity animation-list association");
}

void AnimationSaveSelectiveFlagsUseNativeMask() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation animation(&context, "SelectiveSave");
    VxVector position(2, 3, 4); animation.AddPositionKey(0, &position); animation.SetLength(17);
    const CKDWORD flags[] = {0, 0x80000000u, 1, CK_STATESAVE_OBJANIMALL};
    for (int i = 0; i < 4; ++i) {
        Chunk saved(animation.Save(nullptr, flags[i]), &DeleteCKStateChunk);
        Check(saved != nullptr, "Selective Save did not produce a chunk");
        saved->StartRead();
        Check(bool(saved->SeekIdentifier(CK_STATESAVE_OBJANIMCONTROLLERS)) == (i >= 2),
              "Save did not apply the native low-27-bit animation-data mask");
        if (i >= 2) {
            RCKObjectAnimation reloaded(&context, "SelectiveReload"); saved->StartRead();
            Check(reloaded.Load(saved.get(), nullptr) == CK_OK && reloaded.GetLength() == 17,
                  "Selected animation data was not reloadable");
            VxVector actual;
            Check(reloaded.EvaluatePosition(0, actual) && Equal(actual, position),
                  "Selected Save lost key data");
        }
    }
}

void ModernAnimationLoadPropagatesControllerFactoryFailure() {
    const CKANIMATION_CONTROLLER types[] = {
        CKANIMATION_LINPOS_CONTROL, CKANIMATION_LINROT_CONTROL,
        CKANIMATION_LINSCL_CONTROL, CKANIMATION_LINSCLAXIS_CONTROL,
        CKANIMATION_TCBPOS_CONTROL, CKANIMATION_TCBROT_CONTROL,
        CKANIMATION_TCBSCL_CONTROL, CKANIMATION_TCBSCLAXIS_CONTROL,
        CKANIMATION_BEZIERPOS_CONTROL, CKANIMATION_BEZIERSCL_CONTROL,
        CKANIMATION_MORPH_CONTROL
    };
    CKContext context(nullptr, 0, 0);
    for (CKANIMATION_CONTROLLER type : types) {
        RCKObjectAnimation animation(&context, "FactoryFailureLoad");
        WireWords words = type == CKANIMATION_MORPH_CONTROL ? MorphWire<true>() : TransformControllerWire(type);
        Chunk chunk = AnimationWithController(type, words);
        chunk->CloseChunk(); chunk->StartRead();
        s_ObjectAnimationAllocationFailure = true;
        const CKERROR result = animation.Load(chunk.get(), nullptr);
        const bool injected = !s_ObjectAnimationAllocationFailure;
        s_ObjectAnimationAllocationFailure = false;
        Check(injected && result == CKERR_OUTOFMEMORY,
              "Modern Load silently skipped a supported controller whose factory allocation failed");
        Check(!animation.GetPositionController() && !animation.GetRotationController() &&
              !animation.GetScaleController() && !animation.GetScaleAxisController() && !animation.GetMorphController(),
              "Modern Load retained a failed factory controller");
    }
}

// Pass 24 playback functional regressions.
void ObjectPlaybackEmptyAndIgnoredValuesFollowNative() {
    CKContext context(nullptr,0,0);
    RCKObjectAnimation animation(&context,"EmptyPlayback");
    VxVector pos(9), scale(9); VxQuaternion rot(1,2,3,4), axis(1,2,3,4);
    Check(!animation.EvaluateScale(0,scale) && Equal(scale,VxVector(0.0f)), "Native absent scale must write zero");
    Check(!animation.EvaluatePosition(0,pos) && Equal(pos,VxVector(0.0f)), "Absent position must write zero");
    Check(!animation.EvaluateRotation(0,rot) && rot == VxQuaternion(), "Absent rotation must write identity");
    Check(!animation.EvaluateScaleAxis(0,axis) && axis == VxQuaternion(1,2,3,4), "Absent scale axis must retain output");
    VxVector position(3,4,5), scaling(2,3,4); VxQuaternion rotation(0,0,1,0);
    animation.AddPositionKey(0,&position); animation.AddScaleKey(0,&scaling);
    animation.AddRotationKey(0,&rotation); animation.AddScaleAxisKey(0,&rotation);
    animation.SetFlags(CK_OBJECTANIMATION_IGNOREPOS|CK_OBJECTANIMATION_IGNOREROT|CK_OBJECTANIMATION_IGNORESCALE|CK_OBJECTANIMATION_IGNORESCALEROT);
    scale = VxVector(9); pos = VxVector(9); axis = VxQuaternion(1,2,3,4);
    Check(!animation.EvaluateScale(0,scale) && Equal(scale,VxVector(0.0f)), "Ignored native scale must write zero");
    Check(!animation.EvaluatePosition(0,pos) && Equal(pos,VxVector(0.0f)), "Ignored position must write zero");
    Check(!animation.EvaluateRotation(0,rot) && rot == VxQuaternion(), "Ignored rotation must write identity");
    Check(!animation.EvaluateScaleAxis(0,axis) && axis == VxQuaternion(1,2,3,4), "Ignored scale axis must retain output");
}

void ObjectPlaybackDirectKeysFlagsAndLengthUsePublicAPI() {
    CKContext context(nullptr,0,0);
    RCKObjectAnimation animation(&context,"DirectPlayback");
    Check(!animation.HasPositionInfo() && !animation.HasScaleInfo() && !animation.HasRotationInfo() &&
          !animation.HasScaleAxisInfo() && !animation.HasMorphInfo() && !animation.HasMorphNormalInfo(), "Empty animation reports controllers");
    VxVector first(1,2,3), second(11,12,13), scale(2,3,4);
    VxQuaternion identity, turn(0,0,1,0);
    animation.AddPositionKey(0,&first); animation.AddPositionKey(10,&second);
    animation.AddScaleKey(0,&scale); animation.AddRotationKey(0,&identity); animation.AddRotationKey(10,&turn);
    animation.AddScaleAxisKey(0,&identity); animation.AddScaleAxisKey(10,&turn);
    animation.SetLength(10);
    Check(animation.HasPositionInfo() && animation.HasScaleInfo() && animation.HasRotationInfo() && animation.HasScaleAxisInfo(), "Added channels missing");
    Check(animation.GetLength()==10 && animation.GetPositionController()->GetLength()==10 &&
          animation.GetScaleController()->GetLength()==10 && animation.GetRotationController()->GetLength()==10 &&
          animation.GetScaleAxisController()->GetLength()==10, "SetLength did not propagate controller duration");
    VxVector pos, scl; VxQuaternion rot, axis;
    Check(animation.EvaluateKeys(0.5f,&rot,&pos,&scl,&axis) && Equal(pos,VxVector(6,7,8)) && Equal(scl,scale), "Direct EvaluateKeys changed normalized phase");
    Check(std::fabs(std::fabs(rot.z)-0.70710678f)<0.0001f && std::fabs(std::fabs(axis.z)-0.70710678f)<0.0001f, "Rotation/axis public interpolation changed");
    Check(animation.EvaluateKeys(-1,nullptr,&pos,nullptr,nullptr) && Equal(pos,first), "EvaluateKeys did not clamp negative phase");
    Check(animation.EvaluateKeys(2,nullptr,&pos,nullptr,nullptr) && Equal(pos,second), "EvaluateKeys did not clamp late phase");
    Check(animation.EvaluateKeys(0.5f,nullptr,nullptr,nullptr,nullptr), "All optional EvaluateKeys outputs should succeed");
    animation.SetFlags(0x12340000u); Check(animation.GetFlags()==0x12340000u && !animation.IsMerged(), "Flags were filtered");
    animation.SetMergeFactor(1.25f); Check(animation.GetMergeFactor()==1.25f, "Merge factor should retain unclamped value");
    animation.SetLength(20); Check(animation.EvaluatePosition(15,pos) && Equal(pos,second), "SetLength must not rescale keys");
}

void ObjectPlaybackSetStepComposesScaleAxisAndKeepsPosition() {
    CKContext context(nullptr,0,0);
    RCK3dEntity entity(&context,"ScaleAxisTarget"); RCKObjectAnimation animation(&context,"ScaleAxisPlayback");
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity)); animation.SetLength(10);
    VxVector pos(7,8,9), scale(2,3,4); VxQuaternion rot, axis(0,0,0.70710678f,0.70710678f);
    animation.AddPositionKey(0,&pos); animation.AddScaleKey(0,&scale);
    animation.AddRotationKey(0,&rot); animation.AddScaleAxisKey(0,&axis);
    Check(animation.SetStep(0.5f)==CK_OK && animation.GetCurrentStep()==0.5f, "SetStep did not apply full pose");
    const VxMatrix &m = entity.GetLocalMatrix();
    Check(std::fabs(m[0][0]-3)<0.0001f && std::fabs(m[1][1]-2)<0.0001f && std::fabs(m[2][2]-4)<0.0001f &&
          std::fabs(m[0][1])<0.0001f && std::fabs(m[1][0])<0.0001f, "SetStep discarded the animated scale axis");
    Check(Equal(VxVector(m[3][0],m[3][1],m[3][2]),pos), "Full SetStep lost translation");
    animation.Set3dEntity(nullptr);
}

void ObjectPlaybackRootCenterOffsetReachesEntity() {
    CKContext context(nullptr,0,0);
    RCK3dEntity entity(&context,"CenteredTarget"); RCKObjectAnimation object(&context,"CenteredObject");
    RCKKeyedAnimation parent(&context,"CenteredParent");
    object.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity)); object.SetLength(10);
    VxVector first(2,3,4), last(12,13,14);
    object.AddPositionKey(0,&first); object.AddPositionKey(10,&last);
    parent.AddAnimation(&object); parent.UpdateRootEntity(); parent.CenterAnimation(0);
    Check(Equal(parent.GetRootVectorInternal(),-first), "Cannot establish root centering offset");
    Check(object.SetStep(0.5f)==CK_OK, "Centered root SetStep failed");
    const VxMatrix &m=entity.GetLocalMatrix();
    Check(Equal(VxVector(m[3][0],m[3][1],m[3][2]),VxVector(5,5,5)), "Root animation ignored the keyed centering offset");
    parent.RemoveAnimation(&object); object.Set3dEntity(nullptr);
}

void ObjectPlaybackStepFrameIgnoreAndVelocityUseEntity() {
    CKContext context(nullptr,0,0);
    RCK3dEntity firstEntity(&context,"FirstPlaybackTarget"), secondEntity(&context,"SecondPlaybackTarget");
    RCKObjectAnimation animation(&context,"StepPlayback");
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&firstEntity));
    Check(animation.Get3dEntity()==reinterpret_cast<CK3dEntity *>(&firstEntity) && firstEntity.GetObjectAnimationCount()==1, "Set3dEntity missing reciprocal link");
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&firstEntity)); Check(firstEntity.GetObjectAnimationCount()==1, "Repeated entity binding duplicated membership");
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&secondEntity));
    Check(firstEntity.GetObjectAnimationCount()==0 && secondEntity.GetObjectAnimationCount()==1, "Rebinding retained old entity membership");
    VxVector first(0,0,0), last(20,10,0);
    animation.AddPositionKey(0,&first); animation.AddPositionKey(10,&last); animation.SetLength(10);
    Check(animation.SetFrame(2.5f)==CK_OK && animation.GetCurrentStep()==0.25f, "SetFrame did not map frame to phase");
    Check(Equal(VxVector(secondEntity.GetLocalMatrix()[3][0],secondEntity.GetLocalMatrix()[3][1],0),VxVector(5,2.5f,0)), "SetFrame did not move the entity");
    VxVector velocity; animation.GetVelocity(0.5f,&velocity); Check(Equal(velocity,VxVector(2,1,0)), "Forward velocity sample is incorrect");
    animation.GetVelocity(1,&velocity); Check(Equal(velocity,VxVector(2,1,0)), "Backward endpoint velocity sample is incorrect");
    secondEntity.SetMoveableFlags(secondEntity.GetMoveableFlags()|0x400);
    Check(animation.SetStep(0.75f)==CK_OK && animation.GetCurrentStep()==0.75f && secondEntity.GetLocalMatrix()[3][0]==5, "Ignored entity should retain pose but update phase");
    Check(animation.SetStep(0.75f,reinterpret_cast<CKKeyedAnimation *>(static_cast<intptr_t>(-1)))==CK_OK && secondEntity.GetLocalMatrix()[3][0]==15,
          "Force SetStep did not override entity ignore flag");
    animation.Set3dEntity(nullptr); animation.GetVelocity(0.5f,&velocity); Check(Equal(velocity,VxVector(0.0f)), "Detached animation velocity should be zero");
    animation.SetLength(0); Check(animation.SetFrame(0)==CKERR_INVALIDOPERATION, "Zero duration SetFrame must report invalid operation");
}

void ObjectPlaybackTransitionUsesProvidedStartingSet() {
    CKContext context(nullptr,0,0);
    RCKObjectAnimation in(&context,"TransitionIn"), out(&context,"TransitionOut"), target(&context,"TransitionTarget");
    VxVector first(1,2,3), last(11,12,13), scale(2,3,4); VxQuaternion rotation;
    in.AddPositionKey(0,&first); out.AddPositionKey(0,&last);
    in.AddScaleKey(0,&scale); out.AddScaleKey(0,&scale);
    in.AddRotationKey(0,&rotation); out.AddRotationKey(0,&rotation);
    in.AddScaleAxisKey(0,&rotation); out.AddScaleAxisKey(0,&rotation);
    in.SetLength(10); out.SetLength(10); target.SetLength(4);
    CKAnimKey start; start.m_Pos=VxVector(21,22,23); start.m_Scl=VxVector(5,6,7);
    start.m_Rot=VxQuaternion(0,0,1,0); start.m_SclRot=VxQuaternion(0,1,0,0);
    target.CreateTransition(4,&in,0,&out,0,FALSE,FALSE,&start);
    VxVector pos,scl; VxQuaternion rot,axis;
    Check(target.EvaluateKeys(0,&rot,&pos,&scl,&axis) && Equal(pos,start.m_Pos) && Equal(scl,start.m_Scl) &&
          rot==start.m_Rot && axis==start.m_SclRot, "Transition ignored supplied starting pose");
    Check(target.EvaluateKeys(1,&rot,&pos,&scl,&axis) && Equal(pos,last) && Equal(scl,scale) && rot==rotation && axis==rotation,
          "Transition end pose changed");
}

void ObjectPlaybackTransitionVelocityBuildsIntegratedKeys() {
    CKContext context(nullptr,0,0);
    RCK3dEntity inEntity(&context,"VelocityInEntity"),outEntity(&context,"VelocityOutEntity");
    RCKObjectAnimation in(&context,"VelocityIn"),out(&context,"VelocityOut"),target(&context,"VelocityTransition");
    in.Set3dEntity(reinterpret_cast<CK3dEntity *>(&inEntity)); out.Set3dEntity(reinterpret_cast<CK3dEntity *>(&outEntity));
    VxVector zero(0.0f), left(10,0,0), right(30,0,0);
    in.AddPositionKey(0,&zero); in.AddPositionKey(10,&left); out.AddPositionKey(0,&zero); out.AddPositionKey(10,&right);
    in.SetLength(10); out.SetLength(10); target.SetLength(4);
    target.CreateTransition(4,&in,0,&out,0,TRUE,FALSE,nullptr);
    Check(target.GetPositionController()->GetKeyCount()==5, "Velocity transition did not construct per-frame position keys");
    VxVector sampled;
    Check(target.EvaluatePosition(0,sampled) && Equal(sampled,VxVector(0.0f)), "Velocity transition did not start relative displacement at zero");
    Check(target.EvaluatePosition(4,sampled) && std::fabs(sampled.x-2.8f)<0.0001f && sampled.y==0 && sampled.z==0,
          "Velocity transition did not integrate native 0.4-scaled velocity ramp");
    in.Set3dEntity(nullptr); out.Set3dEntity(nullptr);
}

void ObjectPlaybackCheckScaleAndComparePreserveNativeDirectionality() {
    CKContext context(nullptr,0,0);
    RCKObjectAnimation first(&context,"CompareFirst"),second(&context,"CompareSecond");
    VxVector scale(2,3,4), pos(1,2,3); VxQuaternion rotation;
    first.CheckScaleKeys(scale); Check(!first.HasScaleInfo(), "Scale fixup should require a rotation controller");
    first.AddRotationKey(0,&rotation); first.CheckScaleKeys(scale);
    Check(first.HasScaleInfo() && first.GetScaleController()->GetKeyCount()==1, "Rotation fixup did not supply a scale key");
    VxVector actual; Check(first.EvaluateScale(0,actual) && Equal(actual,scale), "Scale fixup did not use provided scale");
    first.CheckScaleKeys(pos); Check(first.EvaluateScale(0,actual) && Equal(actual,scale), "Scale fixup replaced an existing key");
    Check(first.Compare(&first) && !first.Compare(nullptr), "Compare self/null contract changed");
    Check(!first.Compare(&second), "Compare missed a source controller");
    second.AddRotationKey(0,&rotation); second.AddScaleKey(0,&scale); second.AddPositionKey(0,&pos);
    Check(first.Compare(&second) && !second.Compare(&first), "Native Compare is a left-controller subset check");
    second.SetLength(5); Check(!first.Compare(&second), "Compare did not compare duration");
}

void ObjectPlaybackTransitionDontTurnUsesInverseSourceYaw() {
    CKContext context(nullptr,0,0);
    RCKObjectAnimation in(&context,"TurningIn"),out(&context,"TurningOut"),target(&context,"TurningTransition");
    VxQuaternion identity, halfTurn(0,1,0,0);
    in.AddRotationKey(0,&identity); in.AddRotationKey(10,&halfTurn); out.AddRotationKey(0,&halfTurn);
    in.SetLength(10); out.SetLength(10); target.SetLength(4);
    target.CreateTransition(4,&in,0.5f,&out,0,FALSE,TRUE,nullptr);
    VxQuaternion rot;
    Check(target.EvaluateRotation(0,rot) && std::fabs(rot.y+0.70710678f)<0.0001f && std::fabs(rot.w-0.70710678f)<0.0001f,
          "DontTurn transition did not remove the source yaw");
    Check(target.EvaluateRotation(4,rot) && rot==halfTurn, "DontTurn transition changed the destination rotation");
}

// Pass 24 legacy functional regressions.
// Independent native Load 0x10058C51 fixtures, produced through CKStateChunk.
template<int Version, bool Descending>
Chunk LegacyObjectAnimationFixture() {
    Chunk chunk = NewChunk(CKCID_OBJECTANIMATION, Version);
    if (Version >= 1) {
        chunk->WriteIdentifier(CK_STATESAVE_OBJANIMNEWDATA);
        for (int i = 0; i < 7; ++i) chunk->WriteFloat(0.0f);
        chunk->WriteInt(2); chunk->WriteInt(2);
        chunk->WriteDword(CK_OBJECTANIMATION_MERGED);
        chunk->WriteObject(nullptr); chunk->WriteFloat(23.0f);
        chunk->WriteFloat(0.25f); chunk->WriteObject(nullptr); chunk->WriteObject(nullptr);
    } else {
        chunk->WriteIdentifier(CK_STATESAVE_OBJANIMMORPHKEYS2);
        chunk->WriteInt(2); chunk->WriteInt(2);
    }
    for (int i = 0; i < 2; ++i) {
        VxVector positions[] = {VxVector(float(i + 1), 2.0f, 3.0f), VxVector(4.0f, float(i + 5), 6.0f)};
        chunk->WriteFloat(2.0f); // Native raw loading retains both duplicate Morph keys.
        chunk->WriteDword(sizeof(positions));
        chunk->WriteBufferNoSize_LEndian(sizeof(positions), positions);
    }
    const float firstTime = Descending ? 10.0f : 2.0f;
    const float lastTime = Descending ? 2.0f : 10.0f;
    CKPositionKey position[2], scale[2]; CKRotationKey rotation[2];
    float axis[12];
    for (int i = 0; i < 2; ++i) {
        position[i].TimeStep = scale[i].TimeStep = rotation[i].TimeStep = i ? lastTime : firstTime;
        position[i].Pos = VxVector(float(i + 1), 2.0f, 3.0f);
        scale[i].Pos = VxVector(4.0f, float(i + 5), 6.0f);
        rotation[i].Rot = VxQuaternion(0.0f, 0.0f, i ? 0.6f : 0.0f, i ? 0.8f : 1.0f);
        axis[i * 6] = i ? lastTime : firstTime;
        axis[i * 6 + 1] = 123.0f + i; // Native's unused float must not become a quaternion component.
        axis[i * 6 + 2] = 0.0f; axis[i * 6 + 3] = i ? 0.6f : 0.0f;
        axis[i * 6 + 4] = 0.0f; axis[i * 6 + 5] = i ? 0.8f : 1.0f;
    }
    if (Version == 0) chunk->WriteIdentifier(CK_STATESAVE_OBJANIMPOSKEYS);
    chunk->WriteDword(sizeof(position)); chunk->WriteDword(2);
    chunk->WriteBufferNoSize_LEndian(sizeof(position), position);
    if (Version >= 1) {
        chunk->WriteDword(sizeof(scale)); chunk->WriteDword(2);
        chunk->WriteBufferNoSize_LEndian(sizeof(scale), scale);
    } else chunk->WriteIdentifier(CK_STATESAVE_OBJANIMROTKEYS);
    chunk->WriteDword(sizeof(rotation)); chunk->WriteDword(2);
    chunk->WriteBufferNoSize_LEndian(sizeof(rotation), rotation);
    chunk->WriteDword(sizeof(axis)); chunk->WriteDword(2);
    chunk->WriteBufferNoSize_LEndian(sizeof(axis), axis);
    if (Version == 0) {
        chunk->WriteIdentifier(CK_STATESAVE_OBJANIMSCLKEYS);
        chunk->WriteDword(sizeof(scale)); chunk->WriteDword(2);
        chunk->WriteBufferNoSize_LEndian(sizeof(scale), scale);
        chunk->WriteIdentifier(CK_STATESAVE_OBJANIMFLAGS); chunk->WriteDword(CK_OBJECTANIMATION_MERGED);
        chunk->WriteIdentifier(CK_STATESAVE_OBJANIMENTITY); chunk->WriteObject(nullptr);
        chunk->WriteIdentifier(CK_STATESAVE_OBJANIMLENGTH); chunk->WriteFloat(23.0f);
        chunk->WriteIdentifier(CK_STATESAVE_OBJANIMMERGE);
        chunk->WriteFloat(0.25f); chunk->WriteInt(TRUE);
        chunk->WriteObject(nullptr); chunk->WriteObject(nullptr);
    }
    return chunk;
}

template<int Version, bool Descending>
void LegacyObjectAnimationLoadsAllNativeKeyLayouts() {
    CKContext context(nullptr, 0, 0);
    RCKObjectAnimation animation(&context, "LegacyAllKeys");
    Chunk chunk = LegacyObjectAnimationFixture<Version, Descending>();
    LoadChunk(animation, chunk.get());
    Check(animation.GetLength() == 23.0f && animation.IsMerged() && animation.GetMergeFactor() == 0.25f,
          "Legacy header/settings layout changed");
    CKAnimController *controllers[] = {animation.GetPositionController(), animation.GetScaleController(),
        animation.GetRotationController(), animation.GetScaleAxisController(), animation.GetMorphController()};
    for (CKAnimController *controller : controllers)
        Check(controller && controller->GetKeyCount() == 2 && controller->GetLength() == (Version == 0 ? 0.0f : 23.0f),
              "Legacy controller count or version-specific length changed");
    for (int i = 0; i < 2; ++i) {
        const float expectedTime = Descending ? (i ? 2.0f : 10.0f) : (i ? 10.0f : 2.0f);
        for (int slot = 0; slot < 4; ++slot)
            Check(controllers[slot]->GetKey(i)->TimeStep == expectedTime, "Legacy raw loading sorted the supplied key order");
        Check(Equal(static_cast<CKPositionKey *>(controllers[0]->GetKey(i))->Pos, VxVector(float(i + 1), 2.0f, 3.0f)),
              "Legacy position value changed");
        Check(Equal(static_cast<CKScaleKey *>(controllers[1]->GetKey(i))->Pos, VxVector(4.0f, float(i + 5), 6.0f)),
              "Legacy scale value changed");
        const VxQuaternion &rot = static_cast<CKRotationKey *>(controllers[2]->GetKey(i))->Rot;
        const VxQuaternion &axis = static_cast<CKScaleAxisKey *>(controllers[3]->GetKey(i))->Rot;
        Check(rot.x == 0.0f && rot.y == 0.0f && rot.z == (i ? 0.6f : 0.0f) && rot.w == (i ? 0.8f : 1.0f) &&
              axis.x == 0.0f && axis.y == (i ? 0.6f : 0.0f) && axis.z == 0.0f && axis.w == (i ? 0.8f : 1.0f),
              "Legacy 6-float scale axis conversion used the wrong wire offset");
        CKMorphKey *morph = static_cast<CKMorphKey *>(controllers[4]->GetKey(i));
        Check(static_cast<RCKMorphController *>(controllers[4])->GetMorphVertexCount() == 2 &&
              morph->TimeStep == 2.0f && morph->PosArray && !morph->NormArray &&
              Equal(morph->PosArray[0], VxVector(float(i + 1), 2.0f, 3.0f)) &&
              Equal(morph->PosArray[1], VxVector(4.0f, float(i + 5), 6.0f)),
              "Legacy raw Morph loading collapsed duplicate keys or lost payloads");
    }
    if (!Descending) {
        Chunk saved(animation.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
        Check(saved != nullptr, "Legal legacy animation could not be saved in modern format");
        RCKObjectAnimation reloaded(&context, "LegacyModernReload"); saved->StartRead();
        Check(reloaded.Load(saved.get(), nullptr) == CK_OK && reloaded.GetLength() == 23.0f &&
              reloaded.GetMorphController()->GetKeyCount() == 2 &&
              Equal(static_cast<CKMorphKey *>(reloaded.GetMorphController()->GetKey(1))->PosArray[0], VxVector(2.0f, 2.0f, 3.0f)),
              "Legacy payloads changed after modern Save/Load");
    }
}

template<int Format>
Chunk LegacyNormalOnlyChunk() {
    Chunk chunk = NewChunk(CKCID_OBJECTANIMATION, 1);
    if (Format == 0 || (Format & 1)) {
        chunk->WriteIdentifier(CK_STATESAVE_OBJANIMMORPHCOMP);
        for (int i = 0; i < 2; ++i) {
            VxCompressedVector normals[2];
            normals[0].xa = short(100 + i); normals[0].ya = short(-200 - i);
            normals[1].xa = short(300 + i); normals[1].ya = short(-400 - i);
            chunk->WriteDword(Format ? sizeof(normals) : 0);
            if (Format) chunk->WriteBufferNoSize_LEndian16(sizeof(normals), normals);
        }
    }
    if (Format & 2) {
        chunk->WriteIdentifier(CK_STATESAVE_OBJANIMMORPHNORMALS);
        for (int i = 0; i < 2; ++i) {
            CKDWORD words[] = {CKDWORD(0x11220000u + 500 + i), CKDWORD(0x33440000u + 600 + i)};
            chunk->WriteDword(sizeof(words)); chunk->WriteBufferNoSize_LEndian(sizeof(words), words);
        }
    }
    return chunk;
}

template<int Format>
void LegacyNormalOnlySnapshotUpdatesExistingMorph() {
    CKContext context(nullptr, 0, 0); ObjectCloneProbe animation(&context, "NormalOnlyOwner");
    Chunk initial = AnimationWithController(CKANIMATION_MORPH_CONTROL, MorphWire<true>());
    LoadChunk(animation, initial.get());
    CKMorphController *controller = animation.GetMorphController();
    CKKeyframeData *data = animation.Data();
    CKKey *first = controller->GetKey(0);
    VxVector *positions = static_cast<CKMorphKey *>(first)->PosArray;
    const float length = animation.GetLength();
    Chunk chunk = LegacyNormalOnlyChunk<Format>(); LoadChunk(animation, chunk.get());
    Check(animation.Data() == data && animation.GetMorphController() == controller && controller->GetKey(0) == first &&
          static_cast<CKMorphKey *>(first)->PosArray == positions && animation.GetLength() == length && animation.Shared() == &animation,
          "Normal-only partial Load reset existing keys/positions/length/owner");
    for (int i = 0; i < 2; ++i) {
        CKMorphKey *key = static_cast<CKMorphKey *>(controller->GetKey(i));
        if (Format == 0) Check(!key->NormArray, "Empty normal-only record did not clear existing normals");
        else {
            Check(key->NormArray != nullptr, "Normal-only record lost normal presence");
            const short expected0 = short((Format & 2) ? 500 + i : 100 + i);
            const short expected1 = short((Format & 2) ? 600 + i : -200 - i);
            Check(key->NormArray[0].xa == expected0 && key->NormArray[0].ya == expected1,
                  "Normal-only Load ignored the block or used the wrong legacy conversion/priority");
            Check(key->NormArray[1].xa == ((Format & 2) ? 0 : 300 + i) &&
                  key->NormArray[1].ya == ((Format & 2) ? 0 : -400 - i),
                  "Normal-only conversion did not retain native zero-filled tail");
        }
    }
}

template<int Version>
void LegacyKeyArrayAllocationFailuresReturnErrorAndRemainDestructible() {
    int failedStages = 0;
    for (int failure = 0; failure < 20; ++failure) {
        CKContext context(nullptr, 0, 0); RCKObjectAnimation animation(&context, "LegacyArrayFailure");
        Chunk chunk = LegacyObjectAnimationFixture<Version, false>(); chunk->CloseChunk(); chunk->StartRead();
        s_MorphArrayAllocationFailure = failure;
        const CKERROR error = animation.Load(chunk.get(), nullptr);
        const bool injected = s_MorphArrayAllocationFailure == -1;
        s_MorphArrayAllocationFailure = -1;
        if (!injected) {
            Check(error == CK_OK && failedStages > 0, "Legacy array Load did not use the failure-aware allocation path");
            return;
        }
        ++failedStages;
        Check(error == CKERR_OUTOFMEMORY, "Legacy array allocation failure was not propagated as OUTOFMEMORY");
        // Exiting the iteration invokes normal animation/controller destruction,
        // including every successfully read earlier payload.
    }
    Check(false, "Legacy key-array allocation probe exceeded its finite stage budget");
}

template<int Format>
void LegacyNormalAllocationFailureKeepsTheFailingKeyPayload() {
    for (int failure = 0; failure < (Format == 1 ? 1 : 2); ++failure) {
        CKContext context(nullptr, 0, 0); RCKObjectAnimation animation(&context, "NormalOnlyFailure");
        Chunk initial = AnimationWithController(CKANIMATION_MORPH_CONTROL, MorphWire<true>()); LoadChunk(animation, initial.get());
        CKMorphController *controller = animation.GetMorphController();
        CKMorphKey *key = static_cast<CKMorphKey *>(controller->GetKey(0));
        VxCompressedVector *normals = key->NormArray; VxVector *positions = key->PosArray;
        Chunk chunk = LegacyNormalOnlyChunk<Format>(); chunk->CloseChunk(); chunk->StartRead();
        s_MorphArrayAllocationFailure = failure;
        const CKERROR error = animation.Load(chunk.get(), nullptr);
        const bool injected = s_MorphArrayAllocationFailure == -1; s_MorphArrayAllocationFailure = -1;
        Check(injected && error == CKERR_OUTOFMEMORY, "Legacy normal allocation failure was not propagated");
        Check(animation.GetMorphController() == controller && controller->GetKey(0) == key && key->NormArray == normals && key->PosArray == positions,
              "Failed normal allocation replaced or released the existing failing key payload");
    }
}

template<int Version>
void LegacyScaleAxisPartialPayloadRetainsNativeIdentity() {
    CKContext context(nullptr, 0, 0); RCKObjectAnimation animation(&context, "PartialLegacyAxis");
    Chunk chunk = NewChunk(CKCID_OBJECTANIMATION, Version);
    if (Version >= 1) {
        chunk->WriteIdentifier(CK_STATESAVE_OBJANIMNEWDATA);
        for (int i = 0; i < 7; ++i) chunk->WriteFloat(0.0f);
        chunk->WriteInt(0); chunk->WriteInt(0); chunk->WriteDword(0);
        chunk->WriteObject(nullptr); chunk->WriteFloat(23.0f);
        for (int i = 0; i < 4; ++i) chunk->WriteDword(0); // empty pos/scale records
    } else chunk->WriteIdentifier(CK_STATESAVE_OBJANIMROTKEYS);
    chunk->WriteDword(0); chunk->WriteDword(0); // empty rotation record
    chunk->WriteDword(8); chunk->WriteDword(1);
    chunk->WriteFloat(3.0f); chunk->WriteFloat(123.0f);
    if (Version == 0) {
        chunk->WriteIdentifier(CK_STATESAVE_OBJANIMLENGTH); chunk->WriteFloat(23.0f);
    }
    LoadChunk(animation, chunk.get());
    CKAnimController *controller = animation.GetScaleAxisController();
    Check(controller && controller->GetKeyCount() == 1 && controller->GetKey(0)->TimeStep == 3.0f,
          "Partial axis payload lost key time/count");
    const VxQuaternion &axis = static_cast<CKScaleAxisKey *>(controller->GetKey(0))->Rot;
    Check(axis.x == 0.0f && axis.y == 0.0f && axis.z == 0.0f && axis.w == 1.0f,
          "Partial axis payload did not retain native quaternion identity construction");
}

// Pass 24 lifecycle functional regressions.
class ObjectLifecycleProbe : public ObjectCloneProbe {
public:
    ObjectLifecycleProbe(CKContext *context, const char *name) : ObjectCloneProbe(context, name) {}
    void SetReserved(CKDWORD value) { m_field_38 = value; }
    CKDWORD Reserved() const { return m_field_38; }
    void CheckLayout() const {
        const char *base = reinterpret_cast<const char *>(this);
        const size_t offsets[] = {
            static_cast<size_t>(reinterpret_cast<const char *>(&m_KeyframeData) - base),
            static_cast<size_t>(reinterpret_cast<const char *>(&m_Flags) - base),
            static_cast<size_t>(reinterpret_cast<const char *>(&m_Entity) - base),
            static_cast<size_t>(reinterpret_cast<const char *>(&m_CurrentStep) - base),
            static_cast<size_t>(reinterpret_cast<const char *>(&m_MergeFactor) - base),
            static_cast<size_t>(reinterpret_cast<const char *>(&m_Anim1) - base),
            static_cast<size_t>(reinterpret_cast<const char *>(&m_Anim2) - base),
            static_cast<size_t>(reinterpret_cast<const char *>(&m_field_38) - base),
            static_cast<size_t>(reinterpret_cast<const char *>(&m_ParentKeyedAnimation) - base)};
        const size_t native32[] = {28, 32, 36, 40, 44, 48, 52, 56, 60};
        const size_t portable64[] = {56, 64, 72, 80, 84, 88, 96, 104, 112};
        const size_t *expected = sizeof(void *) == 4 ? native32 : portable64;
        Check(sizeof(RCKObjectAnimation) == (sizeof(void *) == 4 ? 64u : 120u) &&
              alignof(RCKObjectAnimation) == sizeof(void *), "ObjectAnimation size/alignment differs from platform contract");
        for (int i = 0; i < 9; ++i) Check(offsets[i] == expected[i], "ObjectAnimation field offset differs from platform contract");
        const CKKeyframeData *data = Data();
        const char *dataBase = reinterpret_cast<const char *>(data);
        const size_t dataOffsets[] = {
            static_cast<size_t>(reinterpret_cast<const char *>(&data->m_PositionController) - dataBase),
            static_cast<size_t>(reinterpret_cast<const char *>(&data->m_ScaleController) - dataBase),
            static_cast<size_t>(reinterpret_cast<const char *>(&data->m_RotationController) - dataBase),
            static_cast<size_t>(reinterpret_cast<const char *>(&data->m_ScaleAxisController) - dataBase),
            static_cast<size_t>(reinterpret_cast<const char *>(&data->m_MorphController) - dataBase),
            static_cast<size_t>(reinterpret_cast<const char *>(&data->m_Length) - dataBase),
            static_cast<size_t>(reinterpret_cast<const char *>(&data->m_RefCount) - dataBase),
            static_cast<size_t>(reinterpret_cast<const char *>(&data->m_ObjectAnimation) - dataBase)};
        const size_t nativeData32[] = {0, 4, 8, 12, 16, 20, 24, 28};
        const size_t portableData64[] = {0, 8, 16, 24, 32, 40, 44, 48};
        const size_t *dataExpected = sizeof(void *) == 4 ? nativeData32 : portableData64;
        Check(sizeof(CKKeyframeData) == (sizeof(void *) == 4 ? 32u : 56u) && alignof(CKKeyframeData) == sizeof(void *),
              "Keyframe data size/alignment differs from platform contract");
        for (int i = 0; i < 8; ++i) Check(dataOffsets[i] == dataExpected[i], "Keyframe data offset differs from platform contract");
    }
};

class ObjectLifecycleDependencies : public CKDependenciesContext {
public:
    explicit ObjectLifecycleDependencies(CKContext *context) : CKDependenciesContext(context) {}
    void Map(CKObject &source, CKObject &destination) { m_MapID.Insert(source.GetID(), destination.GetID(), TRUE); }
};

CKAnimController *ObjectLifecycleController(RCKObjectAnimation &animation, CKANIMATION_CONTROLLER type) {
    if ((type & CKANIMATION_CONTROLLER_MASK) == CKANIMATION_CONTROLLER_MORPH)
        return animation.GetMorphController();
    return TransformController(animation, type);
}

void ObjectAnimationLifecycleDefaultsAndRegistration() {
    CKContext context(nullptr, 0, 0);
    ObjectLifecycleProbe animation(&context, "LifecycleDefaults");
    animation.CheckLayout();
    Check(animation.GetClassID() == CKCID_OBJECTANIMATION && animation.Data() && animation.Shared() == &animation &&
          animation.References() == 1 && animation.GetLength() == 100.0f && animation.GetFlags() == 0 &&
          animation.Get3dEntity() == nullptr && animation.First() == nullptr && animation.Second() == nullptr &&
          animation.GetCurrentStep() == 0.0f && animation.GetMergeFactor() == 0.5f && animation.Reserved() == 0 &&
          animation.Parent() == nullptr, "ObjectAnimation constructor differs from native defaults");
    Check(!animation.GetPositionController() && !animation.GetScaleController() && !animation.GetRotationController() &&
          !animation.GetScaleAxisController() && !animation.GetMorphController(), "Fresh animation contains controllers");
    Check(std::strcmp(RCKObjectAnimation::GetClassName(), "Object Animation") == 0,
          "ObjectAnimation class name differs from native");
    for (int mode = CK_DEPENDENCIES_COPY; mode <= CK_DEPENDENCIES_BUILD; ++mode)
        Check(RCKObjectAnimation::GetDependenciesCount(mode) == 0 && !RCKObjectAnimation::GetDependencies(0, mode),
              "ObjectAnimation unexpectedly exposes class dependency options");
    RCKObjectAnimation::Register();
    CKClassDesc *descriptor = CKGetClassDesc(CKCID_OBJECTANIMATION);
    Check(descriptor && descriptor->DefaultOptions == CK_GENERALOPTIONS_NODUPLICATENAMECHECK &&
          descriptor->Parameter == CKPGUID_OBJECTANIMATION && descriptor->ToBeNotify.IsSet(CKCID_3DENTITY) &&
          descriptor->ToBeNotify.IsSet(CKCID_OBJECTANIMATION), "ObjectAnimation registration differs from native");
    CKObjectAnimation *created = RCKObjectAnimation::CreateInstance(&context);
    Check(created && created->GetClassID() == CKCID_OBJECTANIMATION && created->GetLength() == 100.0f,
          "ObjectAnimation class creation callback failed");
    delete created;
}

template<bool All>
void ObjectAnimationClearPreservesSharedOwnershipAndReservedState() {
    CKContext context(nullptr, 0, 0);
    ObjectLifecycleProbe owner(&context, "ClearOwner"), alias(&context, "ClearAlias");
    RCKObjectAnimation first(&context, "ClearFirst"), second(&context, "ClearSecond");
    RCK3dEntity entity(&context, "ClearEntity"); RCKKeyedAnimation parent(&context, "ClearParent");
    AddTimedAnimationKeys(owner, 4.0f); alias.ShareDataFrom(&owner);
    alias.SetCloneState(&entity, &first, &second, CK_OBJECTANIMATION_MERGED);
    alias.SetParent(&parent); alias.SetReserved(0x12345678);
    const ObjectCloneDataSnapshot before = SnapshotObjectCloneData(owner);
    if (All) alias.ClearAll(); else alias.Clear();
    Check(alias.GetFlags() == 0 && !alias.Get3dEntity() && !alias.First() && !alias.Second() &&
          alias.GetCurrentStep() == 0.0f && alias.GetMergeFactor() == 0.5f && alias.Parent() == &parent &&
          alias.Reserved() == 0x12345678 && alias.Data() == before.Data && alias.Shared() == &owner &&
          owner.References() == 2, "Clear changed keyframe ownership or reserved fields");
    if (All) {
        Check(owner.GetLength() == 0 && !owner.GetPositionController() && !owner.GetRotationController() &&
              !owner.GetScaleController() && !owner.GetScaleAxisController() && !owner.GetMorphController(),
              "ClearAll did not clear controllers through the shared alias");
    } else {
        CheckObjectCloneData(owner, before, &owner); CheckObjectCloneData(alias, before, &owner);
    }
}

void ObjectAnimationResetAndLengthPropagateThroughAliases() {
    CKContext context(nullptr, 0, 0);
    ObjectLifecycleProbe owner(&context, "LengthOwner"), alias(&context, "LengthAlias");
    AddTimedAnimationKeys(owner, 4.0f); alias.ShareDataFrom(&owner);
    alias.SetKeyframeLength(27.0f);
    CKAnimController *controllers[] = {owner.GetPositionController(), owner.GetRotationController(),
        owner.GetScaleController(), owner.GetScaleAxisController(), owner.GetMorphController()};
    Check(owner.GetLength() == 27.0f && alias.GetLength() == 27.0f, "Shared keyframe length was not propagated");
    for (CKAnimController *controller : controllers)
        Check(controller && controller->GetLength() == 27.0f, "Controller did not receive keyframe length");
    alias.SetCloneState(nullptr, nullptr, nullptr, CK_OBJECTANIMATION_IGNOREPOS);
    alias.ResetKeyframeData();
    Check(owner.GetLength() == 0 && !owner.GetPositionController() && !owner.GetRotationController() &&
          !owner.GetScaleController() && !owner.GetScaleAxisController() && !owner.GetMorphController() &&
          alias.Shared() == &owner && alias.References() == 2 && alias.GetFlags() == CK_OBJECTANIMATION_IGNOREPOS &&
          alias.GetCurrentStep() == 0.625f, "ResetKeyframeData changed ownership or playback state");
}

void ObjectAnimationNullShareAllocationFailurePreservesAliases() {
    CKContext context(nullptr, 0, 0);
    ObjectLifecycleProbe owner(&context, "NullShareOwner"), alias(&context, "NullShareAlias");
    AddTimedAnimationKeys(owner, 4.0f); alias.ShareDataFrom(&owner);
    owner.SetCloneState(nullptr, nullptr, nullptr, CK_OBJECTANIMATION_IGNOREPOS);
    const ObjectCloneDataSnapshot before = SnapshotObjectCloneData(owner);
    s_ObjectAnimationAllocationFailure = true;
    const CKBOOL result = owner.ShareDataFrom(nullptr);
    s_ObjectAnimationAllocationFailure = false;
    Check(!result, "Failed ShareDataFrom(nullptr) must report allocation failure");
    CheckObjectCloneData(owner, before, &owner); CheckObjectCloneData(alias, before, &owner);
    Check(owner.References() == 2 && owner.GetFlags() == CK_OBJECTANIMATION_IGNOREPOS && owner.GetCurrentStep() == 0.625f,
          "Failed null sharing changed references or playback fields");
    Check(owner.ShareDataFrom(nullptr) && owner.Shared() == &owner && owner.References() == 1 && owner.GetLength() == 100.0f,
          "Null sharing did not restore independent native default data");
    CheckObjectCloneData(alias, before, &alias);
}

void ObjectAnimationControllerFactoryAndDeleteFollowNativeDispatch() {
    CKContext context(nullptr, 0, 0);
    ObjectLifecycleProbe owner(&context, "FactoryOwner"), alias(&context, "FactoryAlias");
    owner.SetLength(37.0f); alias.ShareDataFrom(&owner);
    const CKANIMATION_CONTROLLER types[] = {CKANIMATION_CONTROLLER_POS, CKANIMATION_CONTROLLER_ROT,
        CKANIMATION_CONTROLLER_SCL, CKANIMATION_CONTROLLER_MORPH, CKANIMATION_LINPOS_CONTROL,
        CKANIMATION_LINROT_CONTROL, CKANIMATION_LINSCL_CONTROL, CKANIMATION_LINSCLAXIS_CONTROL,
        CKANIMATION_TCBPOS_CONTROL, CKANIMATION_TCBROT_CONTROL, CKANIMATION_TCBSCL_CONTROL,
        CKANIMATION_TCBSCLAXIS_CONTROL, CKANIMATION_BEZIERPOS_CONTROL, CKANIMATION_BEZIERSCL_CONTROL,
        CKANIMATION_MORPH_CONTROL};
    for (CKANIMATION_CONTROLLER type : types) {
        CKAnimController *controller = alias.CreateController(type);
        Check(controller && controller->GetLength() == 37.0f && ObjectLifecycleController(owner, type) == controller,
              "Controller factory did not replace the shared category with native length");
    }
    const ObjectCloneDataSnapshot before = SnapshotObjectCloneData(owner);
    Check(!alias.CreateController(CKANIMATION_CONTROLLER_SCLAXIS) &&
          !alias.CreateController(static_cast<CKANIMATION_CONTROLLER>(0x12345678)),
          "Factory accepted a type absent from native dispatch");
    CheckObjectCloneData(owner, before, &owner);
    const CKANIMATION_CONTROLLER deleteTypes[] = {CKANIMATION_TCBPOS_CONTROL, CKANIMATION_TCBROT_CONTROL,
        CKANIMATION_BEZIERSCL_CONTROL, CKANIMATION_TCBSCLAXIS_CONTROL, CKANIMATION_MORPH_CONTROL};
    for (CKANIMATION_CONTROLLER type : deleteTypes)
        Check(alias.DeleteController(type) && !ObjectLifecycleController(owner, type) && alias.DeleteController(type),
              "DeleteController did not use the low-byte category or repeat successfully");
    Check(alias.DeleteController(static_cast<CKANIMATION_CONTROLLER>(0x12345678)),
          "Native DeleteController returns TRUE for unsupported categories");
}

template<CKANIMATION_CONTROLLER Type>
void ObjectAnimationFactoryAllocationFailureKeepsExistingController() {
    CKContext context(nullptr, 0, 0);
    ObjectLifecycleProbe owner(&context, "FactoryFailureOwner"), alias(&context, "FactoryFailureAlias");
    AddTimedAnimationKeys(owner, 4.0f); alias.ShareDataFrom(&owner);
    const ObjectCloneDataSnapshot before = SnapshotObjectCloneData(owner);
    s_ObjectAnimationAllocationFailure = true;
    CKAnimController *result = alias.CreateController(Type);
    s_ObjectAnimationAllocationFailure = false;
    Check(!result, "Controller factory must report nothrow allocation failure");
    CheckObjectCloneData(owner, before, &owner); CheckObjectCloneData(alias, before, &owner);
    Check(owner.References() == 2, "Failed controller replacement changed ownership");
}

template<bool Share>
void ObjectAnimationCopyUsesCharacterOptionAndCopiesNativePlaybackFields() {
    CKContext context(nullptr, 0, 0);
    ObjectLifecycleProbe source(&context, "CopySource"), destination(&context, "CopyDestination"), retained(&context, "CopyRetained");
    RCKObjectAnimation first(&context, "CopyFirst"), second(&context, "CopySecond");
    RCK3dEntity entity(&context, "CopyEntity"); RCKKeyedAnimation sourceParent(&context, "SourceParent"), targetParent(&context, "TargetParent");
    AddTimedAnimationKeys(source, 4.0f); AddTimedAnimationKeys(destination, 40.0f); retained.ShareDataFrom(&destination);
    source.SetCloneState(&entity, &first, &second, CK_OBJECTANIMATION_MERGED | CK_OBJECTANIMATION_IGNOREMORPH);
    source.SetParent(&sourceParent); source.SetReserved(17); destination.SetParent(&targetParent); destination.SetReserved(29);
    const ObjectCloneDataSnapshot oldData = SnapshotObjectCloneData(destination), sourceData = SnapshotObjectCloneData(source);
    CKDependencies options; options.Resize(CKGetClassCount()); options.m_Flags = CK_DEPENDENCIES_CUSTOM;
    for (int i = 0; i < options.Size(); ++i) options[i] = 0;
    options[CKCID_CHARACTER] = Share ? CK_DEPENDENCIES_COPY_CHARACTER_SHAREANIMS : 0;
    options[CKCID_OBJECTANIMATION] = Share ? 0 : 1; // Deliberately differs from Character: native reads CID40.
    CKDependenciesContext dependencies(&context); dependencies.StartDependencies(&options);
    Check(destination.Copy(source, dependencies) == CK_OK, "ObjectAnimation Copy failed");
    dependencies.StopDependencies();
    CheckObjectCloneFields(destination, &entity, &first, &second, source.GetFlags(), &targetParent);
    Check(destination.Reserved() == 29, "Copy changed native reserved/parent state");
    CheckObjectCloneData(source, sourceData, &source);
    if (Share) {
        CheckObjectCloneData(retained, oldData, &retained);
        CheckObjectCloneData(destination, sourceData, &source);
        Check(source.References() == 2, "Character SHAREANIMS did not share keyframe data");
    } else {
        Check(destination.Data() == oldData.Data && destination.Data() != source.Data() && destination.Shared() == &destination &&
              destination.References() == 2 && destination.GetLength() == source.GetLength(),
              "Deep Copy changed native destination data ownership or aliases");
        CKAnimController *controllers[] = {destination.GetPositionController(), destination.GetRotationController(),
            destination.GetScaleController(), destination.GetScaleAxisController(), destination.GetMorphController()};
        for (int i = 0; i < 5; ++i) {
            Check(controllers[i] && controllers[i] != sourceData.Controllers[i] &&
                  controllers[i]->GetKey(0) != sourceData.Controllers[i]->GetKey(0), "Deep Copy aliases source controllers/keys");
            WireWords words(sourceData.Payloads[i].size());
            Check(controllers[i]->DumpKeysTo(words.data()) == static_cast<int>(words.size() * 4) && words == sourceData.Payloads[i],
                  "Deep Copy changed typed controller payload");
        }
        CKMorphKey *srcKey = static_cast<CKMorphKey *>(source.GetMorphController()->GetKey(0));
        CKMorphKey *dstKey = static_cast<CKMorphKey *>(destination.GetMorphController()->GetKey(0));
        Check(srcKey->PosArray != dstKey->PosArray && srcKey->NormArray != dstKey->NormArray,
              "Deep Copy aliases Morph payload arrays");
        const ObjectCloneDataSnapshot copied = SnapshotObjectCloneData(destination);
        CheckObjectCloneData(retained, copied, &destination);
    }
}

template<bool ObjectAllocation>
void ObjectAnimationDeepCopyFailurePreservesAnimationDataAndAliases() {
    // Four transform key arrays, then Morph keys plus two position/normal pairs.
    const int attempts = ObjectAllocation ? 1 : 9;
    for (int failure = 0; failure < attempts; ++failure) {
        CKContext context(nullptr, 0, 0);
        ObjectLifecycleProbe source(&context, "FailedCopySource"), destination(&context, "FailedCopyTarget"), alias(&context, "FailedCopyAlias");
        AddTimedAnimationKeys(source, 4.0f); AddTimedAnimationKeys(destination, 40.0f); alias.ShareDataFrom(&destination);
        destination.SetCloneState(nullptr, nullptr, nullptr, CK_OBJECTANIMATION_IGNOREPOS);
        const ObjectCloneDataSnapshot before = SnapshotObjectCloneData(destination), sourceBefore = SnapshotObjectCloneData(source);
        CKDependencies options; options.Resize(CKGetClassCount()); options.m_Flags = CK_DEPENDENCIES_CUSTOM;
        for (int i = 0; i < options.Size(); ++i) options[i] = 0;
        CKDependenciesContext dependencies(&context); dependencies.StartDependencies(&options);
        if (ObjectAllocation) s_ObjectAnimationAllocationFailure = true; else s_MorphArrayAllocationFailure = failure;
        CKERROR error = destination.Copy(source, dependencies);
        s_ObjectAnimationAllocationFailure = false; s_MorphArrayAllocationFailure = -1;
        dependencies.StopDependencies();
        Check(error == CKERR_OUTOFMEMORY, "Deep Copy did not propagate allocation failure");
        CheckObjectCloneData(destination, before, &destination); CheckObjectCloneData(alias, before, &destination);
        CheckObjectCloneData(source, sourceBefore, &source);
        Check(destination.References() == 2 && destination.GetFlags() == CK_OBJECTANIMATION_IGNOREPOS &&
              destination.GetCurrentStep() == 0.625f, "Failed deep Copy changed derived playback fields or references");
    }
}

void ObjectAnimationDependenciesFinishAndRemapOnlyNativeReferences() {
    CKContext context(nullptr, 0, 0);
    ObjectLifecycleProbe animation(&context, "DependencyAnimation");
    RCKObjectAnimation first(&context, "DepFirst"), second(&context, "DepSecond"), newFirst(&context, "DepNewFirst"), newSecond(&context, "DepNewSecond");
    RCK3dEntity entity(&context, "DepEntity"), newEntity(&context, "DepNewEntity");
    RCKKeyedAnimation parent(&context, "DepParent"), newParent(&context, "DepNewParent");
    animation.SetCloneState(&entity, &first, &second, CK_OBJECTANIMATION_MERGED); animation.SetParent(&parent);
    ObjectLifecycleDependencies dependencies(&context);
    Check(animation.PrepareDependencies(dependencies) == CK_OK && dependencies.IsDependenciesHere(animation.GetID()) &&
          dependencies.m_DynamicObjects.IsEmpty(), "ObjectAnimation PrepareDependencies did not finish its native class stack");
    Check(animation.PrepareDependencies(dependencies) == CKERR_ALREADYPRESENT,
          "ObjectAnimation PrepareDependencies did not propagate base duplicate failure");
    Check(!dependencies.IsDependenciesHere(first.GetID()) && !dependencies.IsDependenciesHere(entity.GetID()),
          "ObjectAnimation unexpectedly prepared entity/source dependencies");
    dependencies.Map(first, newFirst); dependencies.Map(second, newSecond); dependencies.Map(entity, newEntity);
    dependencies.Map(parent, newParent);
    Check(animation.RemapDependencies(dependencies) == CK_OK, "ObjectAnimation RemapDependencies failed");
    CheckObjectCloneFields(animation, &newEntity, &newFirst, &newSecond, CK_OBJECTANIMATION_MERGED, &parent);
}

void ObjectAnimationPreDeletionAndObjectUseMatchNativeReferences() {
    CKContext context(nullptr, 0, 0);
    ObjectLifecycleProbe animation(&context, "DeletionAnimation");
    RCKObjectAnimation first(&context, "DeleteFirst"), second(&context, "KeepSecond"), unrelated(&context, "Unrelated");
    RCK3dEntity entity(&context, "DeleteEntity"); RCKKeyedAnimation parent(&context, "KeepParent");
    animation.SetCloneState(&entity, &first, &second, CK_OBJECTANIMATION_MERGED); animation.SetParent(&parent);
    Check(animation.IsObjectUsed(&first, CKCID_OBJECTANIMATION) && animation.IsObjectUsed(&second, CKCID_OBJECTANIMATION) &&
          animation.IsObjectUsed(&entity, CKCID_3DENTITY) && !animation.IsObjectUsed(&unrelated, CKCID_OBJECTANIMATION),
          "IsObjectUsed does not recognize native entity/source references");
    first.ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0); entity.ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0);
    parent.ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0); animation.CheckPreDeletion();
    Check(!animation.First() && animation.Second() == &second && !animation.Get3dEntity() && animation.Parent() == &parent &&
          animation.GetFlags() == CK_OBJECTANIMATION_MERGED, "PreDeletion changed references outside native deletion scope");
}

void ObjectAnimationCopyRetainsOriginalControllerFixtures() {
    CKContext context(nullptr, 0, 0);
    CKDependencies options; options.m_Flags = CK_DEPENDENCIES_NONE;
    CKDependenciesContext dependencies(&context); dependencies.StartDependencies(&options);
    const CKANIMATION_CONTROLLER linearTypes[] = {CKANIMATION_LINPOS_CONTROL, CKANIMATION_LINROT_CONTROL,
        CKANIMATION_LINSCL_CONTROL, CKANIMATION_LINSCLAXIS_CONTROL};
    const CKANIMATION_CONTROLLER tcbTypes[] = {CKANIMATION_TCBPOS_CONTROL, CKANIMATION_TCBROT_CONTROL,
        CKANIMATION_TCBSCL_CONTROL, CKANIMATION_TCBSCLAXIS_CONTROL};
    for (int category = 0; category < 4; ++category) {
        const bool rotation = category == 1 || category == 3;
        for (const LinearReference::Fixture &fixture : LinearReference::Fixtures) {
            if (fixture.rotation != rotation) continue;
            WireWords words = {static_cast<CKDWORD>(fixture.keyCount)};
            AppendWire(words, fixture.keys, fixture.keyCount * (rotation ? 5 : 4) * sizeof(float));
            ObjectLifecycleProbe source(&context, "NativeLinearCopySource"), destination(&context, "NativeLinearCopyTarget");
            source.SetLength(20.0f);
            CKAnimController *controller = source.CreateController(linearTypes[category]);
            Check(controller && controller->ReadKeysFrom(words.data()) > 0, "Cannot load original Linear Copy fixture");
            Check(destination.Copy(source, dependencies) == CK_OK, "Native Linear fixture Copy failed");
            CheckLinearReference(ObjectLifecycleController(destination, linearTypes[category]), fixture);
        }
        for (const TCBReference::Fixture &fixture : TCBReference::Fixtures) {
            if (fixture.rotation != rotation) continue;
            WireWords words = {static_cast<CKDWORD>(fixture.keyCount)};
            AppendWire(words, fixture.keys, fixture.keyCount * (rotation ? 10 : 9) * sizeof(float));
            ObjectLifecycleProbe source(&context, "NativeTCBCopySource"), destination(&context, "NativeTCBCopyTarget");
            source.SetLength(20.0f);
            CKAnimController *controller = source.CreateController(tcbTypes[category]);
            Check(controller && controller->ReadKeysFrom(words.data()) > 0, "Cannot load original TCB Copy fixture");
            Check(destination.Copy(source, dependencies) == CK_OK, "Native TCB fixture Copy failed");
            CheckTCBReference(ObjectLifecycleController(destination, tcbTypes[category]), fixture);
        }
    }
    const CKANIMATION_CONTROLLER bezierTypes[] = {CKANIMATION_BEZIERPOS_CONTROL, CKANIMATION_BEZIERSCL_CONTROL};
    for (CKANIMATION_CONTROLLER type : bezierTypes) {
        for (const BezierReference::Fixture &fixture : BezierReference::Fixtures) {
            WireWords words(fixture.words, fixture.words + fixture.wordCount);
            ObjectLifecycleProbe source(&context, "NativeBezierCopySource"), destination(&context, "NativeBezierCopyTarget");
            source.SetLength(20.0f);
            CKAnimController *controller = source.CreateController(type);
            Check(controller && controller->ReadKeysFrom(words.data()) > 0, "Cannot load original Bezier Copy fixture");
            Check(destination.Copy(source, dependencies) == CK_OK, "Native Bezier fixture Copy failed");
            CheckBezierReference(ObjectLifecycleController(destination, type), fixture);
        }
    }
    for (const MorphReference::Fixture &fixture : MorphReference::Fixtures) {
        WireWords words(fixture.words, fixture.words + fixture.wordCount);
        ObjectLifecycleProbe source(&context, "NativeMorphCopySource"), destination(&context, "NativeMorphCopyTarget");
        source.SetLength(20.0f);
        CKAnimController *controller = source.CreateController(CKANIMATION_MORPH_CONTROL);
        Check(controller && controller->ReadKeysFrom(words.data()) > 0, "Cannot load original Morph Copy fixture");
        Check(destination.Copy(source, dependencies) == CK_OK, "Native Morph fixture Copy failed");
        CheckMorphReference(destination, fixture);
    }
    dependencies.StopDependencies();
}

// Pass 24 playback-extra functional regressions.
void ObjectPlaybackPartialChannelsPreserveEntityPoseAndHierarchy() {
    CKContext context(nullptr,0,0);
    RCK3dEntity entity(&context,"PartialTarget"), child(&context,"PartialChild");
    RCKObjectAnimation animation(&context,"PartialPlayback");
    VxMatrix initial; Vx3DMatrixIdentity(initial);
    initial[0][0]=2; initial[1][1]=3; initial[2][2]=4; initial[3][0]=7; initial[3][1]=8; initial[3][2]=9;
    entity.SetLocalMatrix(initial); child.SetParent(reinterpret_cast<CK3dEntity *>(&entity),FALSE);
    animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    VxQuaternion identity; animation.AddRotationKey(0,&identity);
    Check(animation.SetStep(0)==CK_OK, "Rotation-only playback failed");
    const VxMatrix &m=entity.GetLocalMatrix();
    Check(std::fabs(m[0][0]-2)<0.0001f && std::fabs(m[1][1]-3)<0.0001f && std::fabs(m[2][2]-4)<0.0001f &&
          Equal(VxVector(m[3][0],m[3][1],m[3][2]),VxVector(7,8,9)), "Rotation-only playback lost unanimated scale/translation");
    VxVector moved(17,18,19); animation.AddPositionKey(0,&moved);
    Check(animation.SetStep(0)==CK_OK && entity.GetWorldMatrix()[3][0]==17 && child.GetWorldMatrix()[3][0]==17,
          "Playback notification did not update entity and child world transforms");
    animation.DeleteController(CKANIMATION_LINROT_CONTROL);
    animation.DeleteController(CKANIMATION_LINPOS_CONTROL);
    entity.SetLocalMatrix(initial);
    VxQuaternion axis(0,0,0.38268343f,0.92387953f); animation.AddScaleAxisKey(0,&axis);
    Check(animation.SetStep(0)==CK_OK, "Scale-axis-only playback failed");
    const VxMatrix &a=entity.GetLocalMatrix();
    Check(std::fabs(a[0][0]-2.5f)<0.0002f && std::fabs(a[1][1]-2.5f)<0.0002f && std::fabs(std::fabs(a[0][1])-0.5f)<0.0002f &&
          std::fabs(a[2][2]-4)<0.0002f && a[3][0]==7 && a[3][1]==8 && a[3][2]==9,
          "Scale-axis-only playback did not preserve missing channels through total decomposition");
    animation.Set3dEntity(nullptr); child.SetParent(nullptr,FALSE);
}

void ObjectPlaybackExclusiveBodyPartAndCharacterBatchRespectPose() {
    CKContext context(nullptr,0,0);
    RCKBodyPart body(&context,"ExclusiveBody"); RCKCharacter character(&context,"PlaybackCharacter");
    RCKKeyedAnimation exclusive(&context,"ExclusiveDriver"), other(&context,"OtherDriver");
    RCKObjectAnimation object(&context,"ExclusiveObject");
    object.Set3dEntity(reinterpret_cast<CK3dEntity *>(&body));
    VxVector pos(5,6,7); object.AddPositionKey(0,&pos);
    body.SetExclusiveAnimation(reinterpret_cast<CKAnimation *>(&exclusive));
    Check(object.SetStep(0.25f)==CK_OK && body.GetLocalMatrix()[3][0]==0 && object.GetCurrentStep()==0.25f,
          "Exclusive body part accepted unrelated playback");
    Check(object.SetStep(0.5f,reinterpret_cast<CKKeyedAnimation *>(&other))==CK_OK && body.GetLocalMatrix()[3][0]==0,
          "Exclusive body part accepted a different keyed driver");
    body.SetMoveableFlags(body.GetMoveableFlags()|0x400);
    Check(object.SetStep(0.75f,reinterpret_cast<CKKeyedAnimation *>(static_cast<intptr_t>(-1)))==CK_OK && body.GetLocalMatrix()[3][0]==0,
          "Force playback should bypass ignore but retain exclusive-animation ownership");
    body.SetMoveableFlags(body.GetMoveableFlags()&~0x400u);
    Check(object.SetStep(0.75f,reinterpret_cast<CKKeyedAnimation *>(&exclusive))==CK_OK && body.GetWorldMatrix()[3][0]==5,
          "Matching exclusive driver did not propagate pose");
    character.AddAnimation(reinterpret_cast<CKAnimation *>(&exclusive));
    Check(exclusive.GetCharacter()==reinterpret_cast<CKCharacter *>(&character), "Cannot establish actual character driver");
    pos=VxVector(15,16,17); object.AddPositionKey(0,&pos);
    Check(object.SetStep(1,reinterpret_cast<CKKeyedAnimation *>(&exclusive))==CK_OK && body.GetLocalMatrix()[3][0]==15 &&
          body.GetWorldMatrix()[3][0]==5, "Character batch playback notified before its hierarchy pass");
    body.LocalMatrixChanged(FALSE,FALSE);
    Check(body.GetWorldMatrix()[3][0]==15, "Character hierarchy notification failed to publish playback pose");
    character.RemoveAnimation(reinterpret_cast<CKAnimation *>(&exclusive)); body.SetExclusiveAnimation(nullptr); object.Set3dEntity(nullptr);
}

void ObjectPlaybackMorphSetStepPublishesVerticesAndNormals() {
    CKContext context(nullptr,0,0);
    RCKMesh mesh(&context,"PlaybackMorphMesh"); RCK3dEntity entity(&context,"PlaybackMorphEntity");
    RCKObjectAnimation animation(&context,"PlaybackMorph");
    Check(mesh.SetVertexCount(3), "Cannot allocate Morph playback mesh");
    entity.SetCurrentMesh(reinterpret_cast<CKMesh *>(&mesh)); animation.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    AddMorphRequestKeys(animation);
    Check(animation.HasMorphInfo() && animation.HasMorphNormalInfo() && animation.GetMorphVertexCount()==3, "Morph public info/count changed");
    Check(animation.SetStep(0.5f)==CK_OK && (mesh.GetFlags()&0x40000), "Morph playback did not mark mesh changes");
    CKDWORD stride=0, normalStride=0;
    CKBYTE *vertices=mesh.GetModifierVertices(&stride); CKBYTE *normals=static_cast<CKBYTE *>(mesh.GetNormalsPtr(&normalStride));
    VxCompressedVector packed; packed.xa=600; packed.ya=300; VxVector expectedNormal; expectedNormal=packed;
    for(int i=0;i<3;++i) {
        Check(Equal(*reinterpret_cast<VxVector *>(vertices+i*stride),VxVector(6.0f+i,5,6)), "Morph SetStep did not publish actual mesh positions");
        Check(Equal(*reinterpret_cast<VxVector *>(normals+i*normalStride),expectedNormal), "Morph SetStep did not publish decompressed normals");
    }
    animation.SetFlags(CK_OBJECTANIMATION_IGNOREMORPH);
    Check(animation.SetStep(1)==CK_OK && Equal(*reinterpret_cast<VxVector *>(vertices),VxVector(6,5,6)), "Ignored Morph changed mesh vertices");
    animation.SetFlags(0); entity.CreateSkin();
    Check(animation.SetStep(1)==CK_OK && Equal(*reinterpret_cast<VxVector *>(vertices),VxVector(6,5,6)), "Skinned entity accepted unskinned Morph playback path");
    animation.Set3dEntity(nullptr); entity.SetCurrentMesh(nullptr);
}

void ObjectPlaybackTransitionReusesTypedChannelsAndRebuildsTangents() {
    CKContext context(nullptr,0,0);
    RCK3dEntity entity(&context,"ReuseTargetEntity");
    RCKObjectAnimation in(&context,"ReuseIn"),out(&context,"ReuseOut"),target(&context,"ReuseTarget");
    VxVector first(2,3,4),last(12,13,14),scl(2,3,4),zero(0.0f); VxQuaternion identity;
    in.AddPositionKey(0,&first); out.AddPositionKey(0,&last);
    in.AddScaleKey(0,&scl); out.AddScaleKey(0,&scl);
    in.AddRotationKey(0,&identity); out.AddRotationKey(0,&identity);
    in.AddScaleAxisKey(0,&identity); out.AddScaleAxisKey(0,&identity);
    out.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity)); in.SetLength(10); out.SetLength(10); target.SetLength(4);
    CKAnimController *pos=target.CreateController(CKANIMATION_TCBPOS_CONTROL);
    CKTCBPositionKey p0(0,zero,0.2f,0.3f,0.4f),p1(4,zero,0.2f,0.3f,0.4f); pos->AddKey(&p0); pos->AddKey(&p1);
    CKAnimController *scale=target.CreateController(CKANIMATION_TCBSCL_CONTROL);
    CKTCBScaleKey s0(0,scl),s1(4,scl); scale->AddKey(&s0); scale->AddKey(&s1);
    CKAnimController *rot=target.CreateController(CKANIMATION_TCBROT_CONTROL);
    CKTCBRotationKey r0(0,identity),r1(4,identity); rot->AddKey(&r0); rot->AddKey(&r1);
    CKAnimController *axis=target.CreateController(CKANIMATION_TCBSCLAXIS_CONTROL);
    CKTCBScaleAxisKey a0(0,identity),a1(4,identity); axis->AddKey(&a0); axis->AddKey(&a1);
    VxVector sampled; VxQuaternion sampledRot; pos->Evaluate(2,&sampled); scale->Evaluate(2,&sampled); rot->Evaluate(2,&sampledRot); axis->Evaluate(2,&sampledRot);
    target.CreateTransition(4,&in,0,&out,0,FALSE,FALSE,nullptr);
    Check(target.GetPositionController()==pos && target.GetScaleController()==scale && target.GetRotationController()==rot && target.GetScaleAxisController()==axis,
          "Transition discarded matching typed controller interpolation");
    CKTCBPositionKey *kept=static_cast<CKTCBPositionKey *>(pos->GetKey(0));
    Check(kept->tension==0.2f && kept->continuity==0.3f && kept->bias==0.4f, "Transition changed retained TCB settings");
    Check(target.EvaluatePosition(0,sampled) && Equal(sampled,first) && target.EvaluatePosition(4,sampled) && Equal(sampled,last), "Reused controller endpoints retained old pose");
    Check(target.SetStep(0.5f)==CK_OK && entity.GetLocalMatrix()[3][0]>2 && entity.GetLocalMatrix()[3][0]<12,
          "Reused controller tangent cache did not publish transition pose");
    target.Set3dEntity(nullptr); out.Set3dEntity(nullptr);
}

void ObjectPlaybackTransitionMissingAndEmptyChannelsFollowIntersection() {
    CKContext context(nullptr,0,0);
    RCK3dEntity entity(&context,"SparseTransitionEntity");
    RCKObjectAnimation in(&context,"SparseIn"),out(&context,"SparseOut"),target(&context,"SparseTarget");
    VxVector first(1,2,3),last(11,12,13),scale(2,3,4); VxQuaternion rot;
    in.AddPositionKey(0,&first); out.AddPositionKey(0,&last); in.AddScaleKey(0,&scale);
    target.CreateController(CKANIMATION_LINPOS_CONTROL); target.AddScaleKey(0,&scale); target.AddRotationKey(0,&rot); target.AddScaleAxisKey(0,&rot);
    out.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity)); target.SetLength(4);
    target.CreateTransition(4,&in,0,&out,0,FALSE,FALSE,nullptr);
    Check(target.HasPositionInfo() && !target.HasScaleInfo() && !target.HasRotationInfo() && !target.HasScaleAxisInfo(),
          "Transition did not replace empty position/delete absent intersection channels");
    Check(target.GetPositionController()->GetKeyCount()==2 && target.SetStep(0.5f)==CK_OK && entity.GetLocalMatrix()[3][0]==6,
          "Sparse transition failed actual positional playback");
    target.Set3dEntity(nullptr); out.Set3dEntity(nullptr);
}

void ObjectPlaybackTransitionMorphReuseResetsTimelineAndMeshFallbackNormals() {
    CKContext context(nullptr,0,0);
    RCKObjectAnimation in(&context,"MorphTransitionIn"),out(&context,"MorphTransitionOut"),target(&context,"MorphTransitionTarget");
    AddMorphRequestKeys(in); AddMorphRequestKeys(out); AddMorphRequestKeys(target); target.SetLength(4);
    CKMorphController *old=target.GetMorphController();
    target.CreateTransition(4,&in,0,&out,1,FALSE,FALSE,nullptr);
    VxVector result[3]; VxCompressedVector normals[3];
    Check(target.GetMorphController()==old && target.EvaluateMorphTarget(2,3,result,sizeof(VxVector),normals) && Equal(result[0],VxVector(6,5,6)),
          "Reused Morph transition retained old timeline instead of transition duration");
    RCKObjectAnimation empty(&context,"MorphlessIn"); RCKMesh mesh(&context,"MorphFallbackMesh"); RCK3dEntity entity(&context,"MorphFallbackEntity");
    mesh.SetVertexCount(3); entity.SetCurrentMesh(reinterpret_cast<CKMesh *>(&mesh)); out.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    CKDWORD pStride=0,nStride=0; CKBYTE *p=mesh.GetModifierVertices(&pStride),*n=static_cast<CKBYTE *>(mesh.GetNormalsPtr(&nStride));
    for(int i=0;i<3;++i) { *reinterpret_cast<VxVector *>(p+i*pStride)=VxVector(20.0f+i,30,40); *reinterpret_cast<VxVector *>(n+i*nStride)=VxVector(0,1,0); }
    target.CreateTransition(4,&empty,0,&out,1,FALSE,FALSE,nullptr);
    Check(target.HasMorphNormalInfo() && target.EvaluateMorphTarget(0,3,result,sizeof(VxVector),normals) && Equal(result[0],VxVector(20,30,40)),
          "Mesh fallback transition lost source mesh position/normal channel");
    VxVector decoded; decoded=normals[0];
    Check(std::fabs(decoded.x)<0.0002f && std::fabs(decoded.z)<0.0002f && std::fabs(decoded.y-1)<0.0002f,
          "Mesh fallback transition did not preserve mesh normal direction within angular quantization");
    Check(target.SetStep(1)==CK_OK && Equal(*reinterpret_cast<VxVector *>(mesh.GetModifierVertices(&pStride)),VxVector(10,7,8)),
          "Morph transition destination did not reach actual mesh");
    target.Set3dEntity(nullptr); out.Set3dEntity(nullptr); entity.SetCurrentMesh(nullptr);
}

void ObjectPlaybackMergedPublicAPIBlendsControllersAndTracksInfo() {
    CKContext context(nullptr,0,0);
    RCK3dEntity entity(&context,"MergedTarget"); RCKObjectAnimation first(&context,"MergedFirst"),second(&context,"MergedSecond");
    AddTimedAnimationKeys(first,2,10); AddTimedAnimationKeys(second,12,20); first.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    Check(first.CreateMergedAnimation(nullptr)==nullptr, "Merged factory accepted absent second source");
    CKObjectAnimation *merged=first.CreateMergedAnimation(&second,FALSE);
    Check(merged && merged->IsMerged() && merged->GetLength()==20 && merged->Get3dEntity()==reinterpret_cast<CK3dEntity *>(&entity) &&
          entity.GetObjectAnimationCount()==2, "Merged public factory did not establish length/entity membership");
    Check(merged->HasPositionInfo() && merged->HasScaleInfo() && merged->HasRotationInfo() && merged->HasScaleAxisInfo() &&
          merged->HasMorphInfo() && merged->HasMorphNormalInfo() && merged->GetMorphVertexCount()==1, "Merged source info/count missing");
    const float factors[]={0,0.5f,1};
    for(int i=0;i<3;++i) {
        merged->SetMergeFactor(factors[i]); VxVector pos,scale,morph; VxQuaternion rot,axis; VxCompressedVector normal;
        Check(merged->GetMergeFactor()==factors[i] && merged->EvaluateKeys(0.5f,&rot,&pos,&scale,&axis) &&
              Equal(pos,VxVector(7+10*factors[i],2,3)) && Equal(scale,pos), "Merged source time normalization/blend changed");
        Check(std::fabs(std::fabs(rot.z)-0.70710678f)<0.0001f && std::fabs(std::fabs(axis.z)-0.70710678f)<0.0001f,
              "Merged rotation/axis interpolation changed");
        Check(merged->EvaluateMorphTarget(10,1,&morph,sizeof(morph),&normal) && Equal(morph,pos), "Merged Morph public path differed from transform phase");
        Check(merged->SetStep(0.5f)==CK_OK && entity.GetLocalMatrix()[3][0]==pos.x, "Merged SetStep did not reach real entity");
    }
    second.SetFlags(CK_OBJECTANIMATION_IGNOREPOS|CK_OBJECTANIMATION_IGNORESCALE|CK_OBJECTANIMATION_IGNOREROT|CK_OBJECTANIMATION_IGNORESCALEROT);
    merged->SetMergeFactor(0.5f); VxVector pos; VxQuaternion rot;
    Check(merged->HasPositionInfo() && merged->EvaluatePosition(10,pos) && Equal(pos,VxVector(7,2,3)) && merged->EvaluateRotation(10,rot),
          "Merged fallback or presence independent of ignore flags changed");
    merged->Set3dEntity(nullptr); first.Set3dEntity(nullptr); context.DestroyObject(merged);
}

void ObjectPlaybackInfoEmptyControllersAndKeyShortcutsUseContracts() {
    CKContext context(nullptr,0,0); RCKObjectAnimation animation(&context,"InfoContracts"),same(&context,"CompareChannels");
    animation.AddPositionKey(0,nullptr); animation.AddScaleKey(0,nullptr); animation.AddRotationKey(0,nullptr); animation.AddScaleAxisKey(0,nullptr);
    Check(!animation.HasPositionInfo() && !animation.HasScaleInfo() && !animation.HasRotationInfo() && !animation.HasScaleAxisInfo(), "Null shortcut materialized controllers");
    animation.CreateController(CKANIMATION_LINPOS_CONTROL); animation.CreateController(CKANIMATION_LINROT_CONTROL);
    animation.CreateController(CKANIMATION_LINSCL_CONTROL); animation.CreateController(CKANIMATION_LINSCLAXIS_CONTROL);
    CKMorphController *morph=static_cast<CKMorphController *>(animation.CreateController(CKANIMATION_MORPH_CONTROL));
    Check(animation.HasPositionInfo() && animation.HasScaleInfo() && animation.HasRotationInfo() && animation.HasScaleAxisInfo() &&
          animation.HasMorphInfo() && !animation.HasMorphNormalInfo(), "Presence checks should test controller rather than nonempty keys");
    morph->SetMorphVertexCount(1); morph->AddKey(0.0f,FALSE); morph->AddKey(10,TRUE);
    Check(!animation.HasMorphNormalInfo(), "Morph normal presence should inspect first key");
    animation.SetFlags(CK_OBJECTANIMATION_IGNOREPOS|CK_OBJECTANIMATION_IGNOREROT|CK_OBJECTANIMATION_IGNORESCALE|CK_OBJECTANIMATION_IGNORESCALEROT|CK_OBJECTANIMATION_IGNOREMORPH);
    Check(animation.HasPositionInfo() && animation.HasScaleInfo() && animation.HasRotationInfo() && animation.HasScaleAxisInfo() && animation.HasMorphInfo(),
          "Ignore flags should not change controller-presence query");
    animation.SetFlags(0); VxVector scale(2,3,4); animation.CheckScaleKeys(scale);
    Check(animation.GetScaleController()->GetKeyCount()==1, "CheckScaleKeys failed an existing empty scale controller");
    animation.ClearAll(); AddTimedAnimationKeys(animation,2); AddTimedAnimationKeys(same,2);
    Check(animation.HasMorphNormalInfo() && animation.Compare(&same), "All-channel comparison/normal info failed");
    static_cast<CKPositionKey *>(same.GetPositionController()->GetKey(0))->Pos.x+=0.1f;
    Check(!animation.Compare(&same) && animation.Compare(&same,100), "Compare vector threshold was not delegated to channel controllers");
    static_cast<CKPositionKey *>(same.GetPositionController()->GetKey(0))->Pos=
        static_cast<CKPositionKey *>(animation.GetPositionController()->GetKey(0))->Pos;
    static_cast<CKMorphKey *>(same.GetMorphController()->GetKey(0))->PosArray[0].x+=1;
    Check(!animation.Compare(&same), "Compare did not include Morph controller payload");
}

void ObjectPlaybackTransitionInPlaceMorphCapturesBeforeReplacement() {
    CKContext context(nullptr,0,0);
    RCKObjectAnimation target(&context,"InPlaceMorph"),out(&context,"InPlaceOut");
    AddMorphRequestKeys(target); AddMorphRequestKeys(out);
    target.CreateTransition(4,&target,0.5f,&out,1,FALSE,FALSE,nullptr); target.SetLength(4);
    VxVector actual[3]; VxCompressedVector normals[3];
    Check(target.EvaluateMorphTarget(0,3,actual,sizeof(VxVector),normals) && Equal(actual[0],VxVector(6,5,6)),
          "In-place Morph transition read its starting pose after replacing data");
    Check(target.EvaluateMorphTarget(4,3,actual,sizeof(VxVector),normals) && Equal(actual[0],VxVector(10,7,8)),
          "In-place Morph transition did not end at outgoing source");
}

// Pass 24 legacy-boundary functional regressions.
// Real identifier adjacency: the following block is not this record's payload.
template<int Version>
void LegacyTransformRecordsStopAtTheirIdentifier() {
    for (int stage = 0; stage < (Version ? 3 : 2); ++stage) {
        CKContext context(nullptr, 0, 0); RCKObjectAnimation animation(&context, "LegacyBoundary");
        Chunk chunk = NewChunk(CKCID_OBJECTANIMATION, Version);
        if (Version) {
            chunk->WriteIdentifier(CK_STATESAVE_OBJANIMNEWDATA);
            for (int i = 0; i < 7; ++i) chunk->WriteFloat(0.0f);
            chunk->WriteInt(0); chunk->WriteInt(0); chunk->WriteDword(0); chunk->WriteObject(nullptr);
            if (stage != 0) {
                chunk->WriteFloat(23.0f);
                chunk->WriteDword(16);
                if (stage == 2) chunk->WriteDword(1);
            }
        } else {
            chunk->WriteIdentifier(CK_STATESAVE_OBJANIMPOSKEYS);
            chunk->WriteDword(16);
            if (stage == 1) chunk->WriteDword(1);
        }
        chunk->WriteIdentifier(CK_STATESAVE_OBJANIMLENGTH);
        for (int i = 0; i < 8; ++i) chunk->WriteFloat(23.0f + i);
        chunk->CloseChunk(); chunk->StartRead();
        Check(animation.Load(chunk.get(), nullptr) == CKERR_INVALIDFILE,
              "Legacy header/position reader consumed an adjacent identifier as payload");
        Check(!animation.GetPositionController(), "Rejected legacy position record published neighboring bytes as a key");
    }
}

void LegacyV0RotationAxisStopsAtItsIdentifier() {
    CKContext context(nullptr, 0, 0); RCKObjectAnimation animation(&context, "LegacyAxisBoundary");
    Chunk chunk = NewChunk(CKCID_OBJECTANIMATION, 0);
    chunk->WriteIdentifier(CK_STATESAVE_OBJANIMROTKEYS);
    chunk->WriteDword(0); chunk->WriteDword(0);
    chunk->WriteDword(24); chunk->WriteDword(1);
    chunk->WriteIdentifier(CK_STATESAVE_OBJANIMLENGTH);
    for (int i = 0; i < 8; ++i) chunk->WriteFloat(23.0f + i);
    chunk->CloseChunk(); chunk->StartRead();
    Check(animation.Load(chunk.get(), nullptr) == CKERR_INVALIDFILE && !animation.GetScaleAxisController(),
          "Legacy axis reader consumed its next identifier as a six-float record");
}

template<int Version>
void LegacyMorphPositionsCoverDeclaredVertices() {
    CKContext context(nullptr, 0, 0); RCKObjectAnimation animation(&context, "LegacyShortPositions");
    Chunk chunk = NewChunk(CKCID_OBJECTANIMATION, Version);
    if (Version) {
        chunk->WriteIdentifier(CK_STATESAVE_OBJANIMNEWDATA);
        for (int i = 0; i < 7; ++i) chunk->WriteFloat(0.0f);
        chunk->WriteInt(2); chunk->WriteInt(1); chunk->WriteDword(0);
        chunk->WriteObject(nullptr); chunk->WriteFloat(23.0f);
    } else {
        chunk->WriteIdentifier(CK_STATESAVE_OBJANIMMORPHKEYS2);
        chunk->WriteInt(1); chunk->WriteInt(2);
    }
    VxVector position(1.0f, 2.0f, 3.0f);
    chunk->WriteFloat(2.0f); chunk->WriteDword(sizeof(position));
    chunk->WriteBufferNoSize_LEndian(sizeof(position), &position);
    if (Version) for (int i = 0; i < 8; ++i) chunk->WriteDword(0);
    chunk->CloseChunk(); chunk->StartRead();
    Check(animation.Load(chunk.get(), nullptr) == CKERR_INVALIDFILE && !animation.GetMorphController(),
          "Legacy Morph accepted a nonempty position allocation smaller than its declared vertex count");
}

template<int Format>
void LegacyNormalsCoverDeclaredVertices() {
    CKContext context(nullptr, 0, 0); RCKObjectAnimation animation(&context, "LegacyShortNormals");
    Chunk initial = AnimationWithController(CKANIMATION_MORPH_CONTROL, MorphWire<true>()); LoadChunk(animation, initial.get());
    CKMorphKey *key = static_cast<CKMorphKey *>(animation.GetMorphController()->GetKey(0));
    VxCompressedVector *old = key->NormArray;
    Chunk chunk = NewChunk(CKCID_OBJECTANIMATION, 1);
    chunk->WriteIdentifier(Format == 1 ? CK_STATESAVE_OBJANIMMORPHCOMP : CK_STATESAVE_OBJANIMMORPHNORMALS);
    chunk->WriteDword(4); chunk->WriteDword(0x12345678);
    chunk->WriteDword(0);
    chunk->CloseChunk(); chunk->StartRead();
    Check(animation.Load(chunk.get(), nullptr) == CKERR_INVALIDFILE && key->NormArray == old,
          "Legacy normals accepted an allocation smaller than the declared Morph vertex count");
}

template<int Format>
void LegacyNormalRecordsStopAtTheirIdentifier() {
    for (int stage = 0; stage < 2; ++stage) {
        CKContext context(nullptr, 0, 0); RCKObjectAnimation animation(&context, "LegacyNormalBoundary");
        Chunk initial = AnimationWithController(CKANIMATION_MORPH_CONTROL, MorphWire<true>()); LoadChunk(animation, initial.get());
        CKMorphKey *key = static_cast<CKMorphKey *>(animation.GetMorphController()->GetKey(0));
        VxCompressedVector *old = key->NormArray;
        Chunk chunk = NewChunk(CKCID_OBJECTANIMATION, 1);
        chunk->WriteIdentifier(Format == 1 ? CK_STATESAVE_OBJANIMMORPHCOMP : CK_STATESAVE_OBJANIMMORPHNORMALS);
        if (stage) chunk->WriteDword(8);
        chunk->WriteIdentifier(CK_STATESAVE_OBJANIMLENGTH);
        for (int i = 0; i < 8; ++i) chunk->WriteDword(0x12345678);
        chunk->CloseChunk(); chunk->StartRead();
        Check(animation.Load(chunk.get(), nullptr) == CKERR_INVALIDFILE && key->NormArray == old,
              "Legacy normal header/payload reader consumed an adjacent identifier");
    }
}

// Pass 24 playback-final functional regressions.
void ObjectPlaybackTransitionVelocityReusesMatchingTypedController() {
    CKContext context(nullptr,0,0);
    RCK3dEntity inEntity(&context,"ReuseVelocityIn"),outEntity(&context,"ReuseVelocityOut");
    RCKObjectAnimation in(&context,"ReuseVelocitySource"),out(&context,"ReuseVelocityDestination"),target(&context,"ReuseVelocityTarget");
    in.Set3dEntity(reinterpret_cast<CK3dEntity *>(&inEntity)); out.Set3dEntity(reinterpret_cast<CK3dEntity *>(&outEntity));
    VxVector zero(0.0f),left(10,0,0),right(30,0,0);
    in.AddPositionKey(0,&zero); in.AddPositionKey(10,&left); out.AddPositionKey(0,&zero); out.AddPositionKey(10,&right);
    in.SetLength(10); out.SetLength(10); target.SetLength(4);
    CKAnimController *controller=target.CreateController(CKANIMATION_TCBPOS_CONTROL);
    for(int i=0;i<5;++i) { CKTCBPositionKey key(static_cast<float>(i),zero,0.25f,0.3f,0.4f); controller->AddKey(&key); }
    VxVector sampled; controller->Evaluate(2.5f,&sampled);
    target.CreateTransition(4,&in,0,&out,0,TRUE,FALSE,nullptr);
    Check(target.GetPositionController()==controller && controller->GetKeyCount()==5, "Velocity transition replaced a matching typed controller");
    const float expected[]={0,0.4f,1,1.8f,2.8f};
    for(int i=0;i<5;++i) {
        CKTCBPositionKey *key=static_cast<CKTCBPositionKey *>(controller->GetKey(i));
        Check(key->tension==0.25f && key->continuity==0.3f && key->bias==0.4f && key->TimeStep==i,
              "Velocity transition changed retained per-key interpolation settings");
        Check(target.SetStep(i/4.0f)==CK_OK && std::fabs(outEntity.GetLocalMatrix()[3][0]-expected[i])<0.0002f,
              "Reused velocity transition did not publish integrated actual pose");
    }
    target.Set3dEntity(nullptr); in.Set3dEntity(nullptr); out.Set3dEntity(nullptr);
}

void ObjectPlaybackTransitionMorphUnsupportedSourcesRetainPreviousChannel() {
    for(int mode=0;mode<3;++mode) {
        CKContext context(nullptr,0,0);
        RCKObjectAnimation in(&context,"UnavailableMorphIn"),out(&context,"UnavailableMorphOut"),target(&context,"UnavailableMorphTarget");
        RCKMesh mesh(&context,"UnavailableMorphMesh"); RCK3dEntity entity(&context,"UnavailableMorphEntity");
        AddMorphRequestKeys(out); AddMorphRequestKeys(target);
        for(int k=0;k<2;++k) {
            CKMorphKey *key=static_cast<CKMorphKey *>(target.GetMorphController()->GetKey(k));
            key->PosArray[0]=VxVector(k?19.0f:9.0f,0,0);
            static_cast<CKMorphKey *>(out.GetMorphController()->GetKey(k))->PosArray[0].x+=100;
        }
        if(mode==0) AddTimedAnimationKeys(in,2); // one incoming vertex vs three outgoing vertices
        if(mode==2) { mesh.SetVertexCount(4); entity.SetCurrentMesh(reinterpret_cast<CKMesh *>(&mesh)); out.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity)); }
        CKMorphController *old=target.GetMorphController();
        target.CreateTransition(4,&in,0,&out,1,FALSE,FALSE,nullptr);
        VxVector actual;
        Check(target.GetMorphController()==old && target.EvaluateMorphTarget(5,1,&actual,sizeof(actual),nullptr) && Equal(actual,VxVector(14,0,0)),
              "Unavailable/mismatched Morph transition should retain native previous channel");
        target.Set3dEntity(nullptr); out.Set3dEntity(nullptr); entity.SetCurrentMesh(nullptr);
    }
    CKContext context(nullptr,0,0); RCKObjectAnimation in(&context,"NoOutgoingIn"),out(&context,"NoOutgoingOut"),target(&context,"NoOutgoingTarget");
    AddMorphRequestKeys(target); target.CreateTransition(4,&in,0,&out,0,FALSE,FALSE,nullptr);
    Check(!target.HasMorphInfo() && !target.GetMorphController(), "Absent outgoing Morph should delete previous transition channel");
}

void ObjectPlaybackMergedCreationFlagsAndDetachedPhaseRespectContract() {
    CKContext context(nullptr,0,0); RCKObjectAnimation first(&context,"DynamicMergedFirst"),second(&context,"DynamicMergedSecond");
    VxVector p(1,2,3); first.AddPositionKey(0,&p); second.AddPositionKey(0,&p);
    first.SetLength(10); second.SetLength(20);
    first.SetFlags(0x20000000u);
    CKObjectAnimation *dynamic=first.CreateMergedAnimation(&second,FALSE);
    Check(dynamic && dynamic->IsDynamic() && dynamic->GetLength()==20 && std::strcmp(dynamic->GetName(),first.GetName())==0,
          "Merged creation lost native source-flag dynamic option/name");
    Check(dynamic->HasPositionInfo() && !dynamic->HasScaleInfo() && !dynamic->HasRotationInfo() && !dynamic->HasScaleAxisInfo() &&
          !dynamic->HasMorphInfo() && !dynamic->HasMorphNormalInfo() && dynamic->GetMorphVertexCount()==0,
          "Merged presence queries did not preserve absent channels");
    context.DestroyObject(dynamic);
    first.SetFlags(0); CKObjectAnimation *extension=first.CreateMergedAnimation(&second,TRUE);
    Check(extension && !extension->IsDynamic(), "SDL extra Dynamic parameter should retain native source-flag semantics");
    context.DestroyObject(extension);
    Check(first.SetStep(0.375f)==CKERR_NOTINITIALIZED && first.GetCurrentStep()==0.375f,
          "Detached SetStep should record requested phase then report missing target");
}

void ObjectPlaybackDurationAndMergedMorphMaximumUseExistingControllers() {
    CKContext context(nullptr,0,0); RCKObjectAnimation first(&context,"LengthAllChannels"),second(&context,"LengthSecondChannels");
    AddTimedAnimationKeys(first,2); AddMorphRequestKeys(first); AddTimedAnimationKeys(second,12,20);
    first.SetLength(37);
    CKAnimController *controllers[]={first.GetPositionController(),first.GetRotationController(),first.GetScaleController(),first.GetScaleAxisController(),first.GetMorphController()};
    for(int i=0;i<5;++i) {
        Check(controllers[i]->GetLength()==37 && controllers[i]->GetKey(0)->TimeStep==0 && controllers[i]->GetKey(1)->TimeStep==10,
              "SetLength did not update every existing controller duration without rescaling keys");
    }
    CKObjectAnimation *merged=first.CreateMergedAnimation(&second);
    Check(merged && merged->GetLength()==37 && merged->GetMorphVertexCount()==3, "Merged Morph count did not choose maximum unequal source count");
    VxVector positions[3]; Check(merged->EvaluateMorphTarget(5,3,positions,sizeof(VxVector),nullptr) && Equal(positions[0],VxVector(6,5,6)),
          "Unequal Morph source counts did not retain native unscaled first-source time fallback");
    context.DestroyObject(merged);
}

void ObjectPlaybackMergedInPlaceTransitionCapturesLiveSourcePose() {
    CKContext context(nullptr,0,0); RCK3dEntity entity(&context,"MergedTransitionEntity");
    RCKObjectAnimation first(&context,"MergedTransitionFirst"),second(&context,"MergedTransitionSecond"),out(&context,"MergedTransitionOut");
    AddTimedAnimationKeys(first,2); AddTimedAnimationKeys(second,12); AddTimedAnimationKeys(out,22);
    out.Set3dEntity(reinterpret_cast<CK3dEntity *>(&entity));
    CKObjectAnimation *target=first.CreateMergedAnimation(&second); Check(target!=nullptr,"Cannot create merged in-place transition source");
    target->SetMergeFactor(0.5f); target->CreateTransition(4,target,0.5f,&out,1,FALSE,FALSE,nullptr); target->SetLength(4);
    VxVector pos,morph; VxCompressedVector normal;
    Check(!target->IsMerged() && target->EvaluatePosition(0,pos) && Equal(pos,VxVector(12,2,3)),
          "In-place transition retained stale merged dispatch instead of captured starting pose");
    Check(target->EvaluateMorphTarget(0,1,&morph,sizeof(morph),&normal) && Equal(morph,VxVector(12,2,3)),
          "Merged in-place Morph capture happened after clearing live sources");
    Check(target->SetStep(0.5f)==CK_OK && entity.GetLocalMatrix()[3][0]==22,
          "Merged-to-transition did not publish actual interpolated entity pose");
    target->Set3dEntity(nullptr); out.Set3dEntity(nullptr); context.DestroyObject(target);
}

// Pass 24 duration functional regressions.
template<bool ZeroDuration>
void ObjectTransitionVelocityHandlesShortDuration() {
    CKContext context(nullptr, 0, 0);
    RCK3dEntity inEntity(&context, "ShortVelocityIn"), outEntity(&context, "ShortVelocityOut");
    RCKObjectAnimation in(&context, "ShortVelocitySource"), out(&context, "ShortVelocityDestination"),
        target(&context, "ShortVelocityTransition");
    in.Set3dEntity(reinterpret_cast<CK3dEntity *>(&inEntity));
    out.Set3dEntity(reinterpret_cast<CK3dEntity *>(&outEntity));
    VxVector zero(0.0f), firstEnd(10, 0, 0), secondEnd(30, 0, 0);
    in.AddPositionKey(0, &zero); in.AddPositionKey(10, &firstEnd); in.SetLength(10);
    out.AddPositionKey(0, &zero); out.AddPositionKey(10, &secondEnd); out.SetLength(10);
    const float duration = ZeroDuration ? 0.0f : 0.5f;
    target.SetLength(duration);
    target.CreateTransition(duration, &in, 0, &out, 0, TRUE, FALSE, nullptr);
    CKAnimController *controller = target.GetPositionController();
    Check(controller && controller->GetKeyCount() == (ZeroDuration ? 1 : 2),
          "Short velocity transition did not represent its duration with finite key intervals");
    Check(controller->GetKey(0)->TimeStep == 0 &&
          controller->GetKey(controller->GetKeyCount() - 1)->TimeStep == duration,
          "Short velocity transition generated an undefined key time");
    Check(target.SetStep(.5f) == CK_OK &&
          std::fabs(outEntity.GetLocalMatrix()[3][0] - (ZeroDuration ? 0.0f : .1f)) < 0.00001f,
          "Short velocity transition did not publish its integrated midpoint pose");
    Check(target.SetStep(1) == CK_OK &&
          std::fabs(outEntity.GetLocalMatrix()[3][0] - (ZeroDuration ? 0.0f : .2f)) < 0.00001f,
          "Short velocity transition did not scale its left-endpoint velocity by duration");
    Chunk saved(target.Save(nullptr, CK_STATESAVE_OBJANIMALL), &DeleteCKStateChunk);
    Check(saved != nullptr, "Short velocity transition produced unsaveable key data");
    RCKObjectAnimation reloaded(&context, "ShortVelocityReloaded");
    LoadChunk(reloaded, saved.get());
    VxVector actual;
    Check(reloaded.EvaluatePosition(duration, actual) &&
          Equal(actual, VxVector(ZeroDuration ? 0.0f : .2f, 0, 0)),
          "Saved short velocity transition changed its final displacement");
    target.Set3dEntity(nullptr); in.Set3dEntity(nullptr); out.Set3dEntity(nullptr);
}

// Pass 25 runtime-grid functional regressions.
template<bool Compressed>
void RuntimeGridRecreationPreservesDimensionsAndFileCells() {
    CKContext source(nullptr, 0, 0), destination(nullptr, 0, 0);
    CKGridManager *manager = InitializeRuntimeGridContext(source);
    CKGridManager *loadedManager = InitializeRuntimeGridContext(destination);
    const int type = manager->RegisterType("RecreatedLayer");
    loadedManager->RegisterType("UnrelatedDestinationLayer");
    RCKGrid grid(&source, "RecreatedGrid");
    grid.SetDimensions(2, 3, 4.0f, 12.0f);
    CKLayer *oldLayer = grid.AddLayer(type);
    Check(oldLayer != nullptr, "Initial runtime layer was not created");
    const CK_ID oldID = oldLayer->GetID();
    int oldValue = 91;
    oldLayer->SetValue(1, 2, &oldValue);
    Check(grid.RemoveAllLayers() == CK_OK && grid.GetLayerCount() == 0 && !source.GetObject(oldID),
          "SDK dimension-change workflow did not remove previous layer objects");
    grid.SetDimensions(3, 1, 9.0f, 5.0f);
    grid.SetHeightValidity(7.0f);
    grid.SetGridPriority(17);
    grid.SetOrientationMode(CKGRID_YZ);
    CKLayer *layer = grid.AddLayer(type);
    Check(layer && layer->GetOwner() == grid.GetID(), "Recreated layer did not bind its grid");
    layer->SetVisible(FALSE);
    VxVector scale;
    grid.GetScale(&scale, TRUE);
    Check(std::fabs(scale.x - 3.0f) < 1e-4f && std::fabs(scale.y - 7.0f) < 1e-4f && std::fabs(scale.z - 5.0f) < 1e-4f,
          "SetDimensions changed total-size scaling or height validity");
    const CKDWORD values[] = {0x10203040u, 0x80000000u, 0xFFFFFFFFu};
    for (int x = 0; x < 3; ++x) {
        CKDWORD actual = 99;
        Check(layer->GetValue2(x, 0, &actual) && actual == 0,
              "Recreated layer kept the removed layer's cell data");
        Check(layer->SetValue2(x, 0, const_cast<CKDWORD *>(&values[x])),
              "Recreated layer rejected an in-range cell");
    }
    TemporarySerializationFile temporary;
    source.SetFileWriteMode(Compressed ? CKFILE_WHOLECOMPRESSED : CKFILE_UNCOMPRESSED);
    CKFile saved(&source), loaded(&destination);
    Check(saved.StartSave(temporary.Path) == CK_OK, "Recreated Grid StartSave failed");
    saved.SaveObject(&grid);
    Check(saved.EndSave() == CK_OK, "Recreated Grid EndSave failed");
    Check(loaded.Load(temporary.Path, nullptr) == CK_OK, "Recreated Grid file load failed");
    RCKGrid *result = nullptr;
    for (int i = 0; i < loaded.m_FileObjects.Size(); ++i)
        if (loaded.m_FileObjects[i].ObjectCid == CKCID_GRID)
            result = static_cast<RCKGrid *>(loaded.m_FileObjects[i].ObjPtr);
    Check(result && result->GetWidth() == 3 && result->GetLength() == 1 && result->GetLayerCount() == 1,
          "Recreated Grid dimensions/layer list changed in a complete file");
    Check(result->GetGridPriority() == 17 && result->GetOrientationMode() == CKGRID_YZ &&
          std::fabs(result->GetHeightValidity() - 7.0f) < 1e-4f,
          "Recreated Grid priority/orientation/height changed in a complete file");
    CKLayer *loadedLayer = result->GetLayerByName("RecreatedLayer");
    Check(loadedLayer && loadedLayer->GetOwner() == result->GetID() &&
          loadedLayer->GetType() == loadedManager->GetTypeFromName("RecreatedLayer") && !loadedLayer->IsVisible(),
          "Recreated layer owner/type was not restored by the runtime manager");
    for (int x = 0; x < 3; ++x) {
        CKDWORD actual = 0;
        Check(loadedLayer->GetValue2(x, 0, &actual) && actual == values[x],
              "Recreated layer's four-byte cell values changed in a complete file");
    }
    result->GetScale(&scale, TRUE);
    Check(std::fabs(scale.x - 3.0f) < 1e-4f && std::fabs(scale.y - 7.0f) < 1e-4f && std::fabs(scale.z - 5.0f) < 1e-4f,
          "Recreated Grid world-size scaling changed in a complete file");
}

// Pass 25 layer-wire functional regressions.
// Independent Layer identifier fixtures; native Save 0x1001A559 / Load 0x1001A742.
class Pass25LayerWireRegistry : public LayerRegistry {
public:
    explicit Pass25LayerWireRegistry(CKContext *context) : LayerRegistry(context) {
        m_Remap = wireRemap;
        m_RemapCount = 1;
        parameter = CKGUID(0x10203040, 0x50607080);
        color.Set(0.25f, 0.5f, 0.75f, 1.0f);
    }
    int GetTypeFromName(CKSTRING name) override {
        ++lookups;
        incomingName = name ? name : "";
        return foundType;
    }
    int RegisterType(CKSTRING name) override {
        ++registrations;
        registeredName = name ? name : "";
        return 2;
    }
    int GetLayerTypeCount() override { return 5; }
    int foundType = 3;
    int lookups = 0;
    int registrations = 0;
    const char *incomingName = "";
    const char *registeredName = "";
    int wireRemap[5] = {};
};

class Pass25LayerWireProbe : public RCKLayer {
public:
    Pass25LayerWireProbe(CKContext *context, CKSTRING name, CK_ID owner = 0)
        : RCKLayer(context, name, owner) {}
    void SetRawFlags(CKDWORD flags) { m_Flags = flags; }
    CKDWORD RawFlags() const { return m_Flags; }
};

void Pass25CheckLayerBuffer(CKStateChunk *chunk, const CKDWORD *values, int count) {
    void *data = nullptr;
    const int bytes = chunk->ReadBuffer(&data);
    const bool matched = bytes == count * 4 && (count == 0 ||
        (data && std::memcmp(data, values, static_cast<size_t>(count) * 4) == 0));
    CKDeletePointer(data);
    Check(matched, "Layer payload changed independent four-byte square words");
}

template<bool FileSave>
void LayerWireSaveRequiresManagerBeforeSelectivePayload() {
    CKContext context(nullptr, 0, 0);
    RCKLayer layer(&context, "NoManager", 0);
    CKFile file(&context);
    const CKDWORD flags[] = {0, CK_STATESAVE_LAYERDATA};
    for (int i = 0; i < 2; ++i) {
        Chunk saved(layer.Save(FileSave ? &file : nullptr, flags[i]), &DeleteCKStateChunk);
        Check(saved.get() == nullptr, "Native Layer Save without GridManager must return null");
    }
}

template<int Format>
void LayerWireMemorySaveUsesNativeSelectiveHeader() {
    CKContext context(nullptr, 0, 0);
    new Pass25LayerWireRegistry(&context);
    RCKGrid grid(&context, "WireGrid");
    grid.SetDimensions(2, 1, 2.0f, 1.0f);
    Pass25LayerWireProbe layer(&context, "WireLayer", grid.GetID());
    layer.SetType(3);
    layer.SetFormat(Format);
    layer.SetRawFlags(0xA5);
    const CKDWORD values[] = {0x7F800001u, 0x80000000u};
    for (int i = 0; i < 2; ++i) layer.GetSquareArray()[i].dval = values[i];
    const CKDWORD masks[] = {0, 1, 8, 16, 17, CK_STATESAVE_ALL};
    for (int i = 0; i < 6; ++i) {
        Chunk saved(layer.Save(nullptr, masks[i]), &DeleteCKStateChunk);
        Check(saved != nullptr, "Layer selective memory Save failed with manager");
        saved->StartRead();
        const bool hasData = saved->SeekIdentifier(CK_STATESAVE_LAYERDATA) != FALSE;
        Check(hasData == ((masks[i] & 16) != 0), "Layer Save changed native bit0x10 selection");
        if (!hasData) continue;
        Check(saved->SeekIdentifierAndReturnSize(CK_STATESAVE_LAYERDATA) == (Format == 0 ? 28 : 16),
              "Layer memory identifier contains unexpected metadata or cell data");
        Check(saved->ReadObjectID() == grid.GetID(), "Layer memory owner ID changed");
        Check(saved->ReadInt() == 3, "Layer memory raw type index missing");
        Check(saved->ReadInt() == Format, "Layer memory format changed");
        Check(saved->ReadDword() == 0xA5, "Layer memory raw flags changed");
        if (Format == 0) Pass25CheckLayerBuffer(saved.get(), values, 2);
    }
    if (Format == 0) {
        layer.SetOwner(0);
        Chunk detached(layer.Save(nullptr, CK_STATESAVE_LAYERDATA), &DeleteCKStateChunk);
        detached->StartRead();
        Check(detached->SeekIdentifierAndReturnSize(CK_STATESAVE_LAYERDATA) == 16,
              "Detached Layer Save must omit cells even if it retains old square storage");
        Check(detached->ReadObjectID() == 0, "Detached Layer Save retained an owner reference");
    }
}

template<int Format>
void LayerWireFileSaveOmitsTypeAndWritesVersion3Metadata() {
    CKContext context(nullptr, 0, 0);
    Pass25LayerWireRegistry *registry = new Pass25LayerWireRegistry(&context);
    RCKGrid grid(&context, "FileGrid");
    grid.SetDimensions(2, 1, 2.0f, 1.0f);
    Pass25LayerWireProbe layer(&context, "FileLayer", grid.GetID());
    layer.SetType(3);
    layer.SetFormat(Format);
    layer.SetRawFlags(0xA4);
    const CKDWORD values[] = {0x12345678u, 0xFEDCBA98u};
    for (int i = 0; i < 2; ++i) layer.GetSquareArray()[i].dval = values[i];
    CKFile file(&context);
    file.m_FileObjects.Resize(1);
    file.m_FileObjects[0].Object = file.m_FileObjects[0].CreatedObject = grid.GetID();
    file.m_FileObjects[0].ObjPtr = &grid;
    file.m_ObjectsHashTable.Insert(grid.GetID(), 0);
    const CKDWORD masks[] = {0, 1, 16};
    for (int i = 0; i < 3; ++i) {
        Chunk saved(layer.Save(&file, masks[i]), &DeleteCKStateChunk);
        Check(saved != nullptr, "Layer file Save failed with manager");
        saved->StartRead();
        Check(saved->SeekIdentifier(CK_STATESAVE_LAYERDATA), "File Save must include Layer regardless of mask");
        Check(saved->SeekIdentifierAndReturnSize(CK_STATESAVE_LAYERDATA) == (Format == 0 ? 40 : 28),
              "File Layer identifier contains memory-only type or unexpected cell payload");
        Check(saved->ReadObjectID() == grid.GetID(), "Layer file owner did not use object remapping");
        Check(saved->ReadInt() == Format, "File Layer must omit the memory-only type word");
        Check(saved->ReadInt() == 3, "Layer file metadata version must be3");
        Check(saved->ReadDword() == registry->color.GetRGBA(), "Layer file color word changed");
        Check(saved->ReadGuid() == registry->parameter, "Layer file parameter GUID changed");
        Check(saved->ReadDword() == 0xA4, "Layer file raw flags changed");
        if (Format == 0) Pass25CheckLayerBuffer(saved.get(), values, 2);
        Check(registry->wireRemap[3] == 1 && registry->m_RemapCount == 2,
              "Layer file remap must register a type only once");
    }
}

template<int Version>
void LayerWireLoadVersionedMetadataUsesNativeFieldOrder() {
    CKContext context(nullptr, 0, 0);
    Pass25LayerWireRegistry *registry = new Pass25LayerWireRegistry(&context);
    RCKGrid grid(&context, "LoadedOwner");
    grid.SetDimensions(2, 1, 2.0f, 1.0f);
    Pass25LayerWireProbe layer(&context, "IncomingLayer", grid.GetID());
    layer.SetRawFlags(0xA6);
    const CKGUID oldParam = registry->parameter;
    const CKDWORD oldColor = registry->color.GetRGBA();
    const CKGUID incomingParam(0x31415926, 0x27182818);
    const CKDWORD incomingColor = 0xD17F3F1Fu;
    const CKDWORD values[] = {0x80000000u, 0xFFFFFFFFu};
    Chunk chunk = NewChunk(CKCID_LAYER);
    chunk->WriteIdentifier(CK_STATESAVE_LAYERDATA);
    chunk->WriteObject(&grid);
    chunk->WriteInt(0);
    chunk->WriteInt(Version);
    if (Version >= 1) chunk->WriteDword(incomingColor);
    if (Version >= 3) chunk->WriteGuid(incomingParam);
    if (Version >= 2) chunk->WriteDword(0xB4);
    chunk->WriteBuffer_LEndian(sizeof(values), const_cast<CKDWORD *>(values));
    chunk->CloseChunk();
    CKFile file(&context);
    Check(layer.Load(chunk.get(), &file) == CK_OK, "Independent legacy Layer fixture failed");
    Check(layer.GetOwner() == grid.GetID() && layer.GetFormat() == 0 && layer.GetType() == 3,
          "Legacy Layer owner/type/format field order changed");
    Check(std::strcmp(registry->incomingName, "IncomingLayer") == 0 && registry->lookups == 1 &&
          registry->registrations == 0, "Legacy Layer type must be resolved from loaded object's name");
    Check(layer.RawFlags() == (Version == 0 ? 0xA6u : Version == 1 ? 1u : 0xB4u),
          "Legacy Layer version-specific flags changed");
    Check(registry->color.GetRGBA() == (Version == 0 ? oldColor : incomingColor),
          "Legacy Layer version-specific color changed");
    const CKGUID expectedParam = Version < 2 ? oldParam : Version == 2 ?
        CKGUID(0x5A5716FD, 0x44E276D7) : incomingParam;
    Check(registry->parameter == expectedParam, "Legacy Layer version-specific GUID changed");
    for (int i = 0; i < 2; ++i)
        Check(layer.GetSquareArray()[i].dval == values[i], "Legacy Layer lost raw cell values");
}

template<bool ExistingType>
void LayerWireMemoryLoadIgnoresRawTypeAndRegistersName() {
    CKContext context(nullptr, 0, 0);
    Pass25LayerWireRegistry *registry = new Pass25LayerWireRegistry(&context);
    registry->foundType = ExistingType ? 3 : 0;
    Pass25LayerWireProbe layer(&context, "IncomingMemoryLayer");
    Chunk chunk = NewChunk(CKCID_LAYER);
    chunk->WriteIdentifier(CK_STATESAVE_LAYERDATA);
    chunk->WriteObject(nullptr);
    chunk->WriteInt(0x1234);
    chunk->WriteInt(1);
    chunk->WriteDword(0x80000005u);
    LoadChunk(layer, chunk.get());
    Check(layer.GetType() == (ExistingType ? 3 : 2) && layer.GetOwner() == 0 && layer.GetFormat() == 1,
          "Memory Layer raw type must be discarded in favor of registry name mapping");
    Check(layer.RawFlags() == 0x80000005u, "Memory Layer raw flags must retain all bits");
    Check(registry->lookups == 1 && registry->registrations == (ExistingType ? 0 : 1),
          "Memory Layer name registration must happen only when lookup returns0");
    Check(std::strcmp(registry->incomingName, "IncomingMemoryLayer") == 0,
          "Memory Layer did not resolve its object name");
    if (!ExistingType) Check(std::strcmp(registry->registeredName, "IncomingMemoryLayer") == 0,
                             "Layer registered a different name");
}

void LayerWireNoIdentifierPreservesDerivedState() {
    CKContext context(nullptr, 0, 0);
    Pass25LayerWireRegistry *registry = new Pass25LayerWireRegistry(&context);
    RCKGrid grid(&context, "RetainedOwner");
    grid.SetDimensions(1, 1, 1.0f, 1.0f);
    Pass25LayerWireProbe layer(&context, "RetainedLayer", grid.GetID());
    layer.SetType(4); layer.SetFormat(0); layer.SetRawFlags(0xF4);
    layer.GetSquareArray()[0].dval = 0x12345678u;
    CKSquare *original = layer.GetSquareArray();
    Chunk chunk = NewChunk(CKCID_LAYER);
    chunk->WriteIdentifier(0x40000000u);
    chunk->WriteInt(123);
    LoadChunk(layer, chunk.get());
    Check(layer.GetOwner() == grid.GetID() && layer.GetType() == 4 && layer.GetFormat() == 0 &&
          layer.RawFlags() == 0xF4 && layer.GetSquareArray() == original && original[0].dval == 0x12345678u,
          "Absent Layer identifier must preserve all derived Layer state");
    Check(registry->lookups == 0, "Absent Layer identifier must not consult registry");
}

void LayerWireLoadManagerRequirementDependsOnIdentifier() {
    CKContext context(nullptr, 0, 0);
    RCKLayer layer(&context, "NoLoadManager", 0);
    Check(layer.Load(nullptr, nullptr) == CKERR_INVALIDPARAMETER, "Null Layer chunk error changed");
    Chunk empty = NewChunk(CKCID_LAYER);
    empty->CloseChunk();
    Check(layer.Load(empty.get(), nullptr) == CK_OK, "Absent Layer identifier should not require manager");
    Chunk payload = NewChunk(CKCID_LAYER);
    payload->WriteIdentifier(CK_STATESAVE_LAYERDATA);
    payload->WriteObject(nullptr); payload->WriteInt(1); payload->WriteInt(1); payload->WriteInt(1);
    payload->CloseChunk();
    Check(layer.Load(payload.get(), nullptr) == CKERR_NOTINITIALIZED,
          "Present Layer identifier without manager must report not initialized");
}

void LayerWireNonzeroFormatReloadDropsPreviousCells() {
    CKContext context(nullptr, 0, 0);
    new Pass25LayerWireRegistry(&context);
    RCKGrid first(&context, "FirstOwner"), second(&context, "SecondOwner");
    first.SetDimensions(2, 1, 2.0f, 1.0f);
    second.SetDimensions(1, 1, 1.0f, 1.0f);
    Pass25LayerWireProbe layer(&context, "FormatReload", first.GetID());
    layer.GetSquareArray()[0].dval = 77;
    Chunk payload = NewChunk(CKCID_LAYER);
    payload->WriteIdentifier(CK_STATESAVE_LAYERDATA);
    payload->WriteObject(&second); payload->WriteInt(99); payload->WriteInt(7); payload->WriteDword(0x82);
    LoadChunk(layer, payload.get());
    Check(layer.GetOwner() == second.GetID() && layer.GetFormat() == 7 && layer.RawFlags() == 0x82 &&
          layer.GetSquareArray() == nullptr, "Nonzero format reload must clear prior cells and replace owner/flags");
}

// Pass 25 layer functional regressions.
class LayerDependencyOwnerProbe : public RCKGrid {
public:
    explicit LayerDependencyOwnerProbe(CKContext *context) : RCKGrid(context, "LayerOwnerProbe"), Calls(0) {}
    CKERROR PrepareDependencies(CKDependenciesContext &) override { ++Calls; return CK_OK; }
    int Calls;
};

void LayerNativeDependencyDescriptions() {
    Check(std::strcmp(RCKLayer::GetClassName(), "Grid Layer") == 0, "Layer class display name differs from native Grid Layer");
    for (int mode = 0; mode <= 5; ++mode) {
        const bool copy = mode == CK_DEPENDENCIES_COPY;
        Check(RCKLayer::GetDependenciesCount(mode) == (copy ? 1 : 0), "Layer copy-data dependency count differs from native");
        const CKSTRING description = RCKLayer::GetDependencies(0, mode);
        Check(copy ? description && std::strcmp(description, "Layer Data") == 0 : !description,
              "Layer copy-data dependency description differs from native");
        Check(!RCKLayer::GetDependencies(1, mode), "Layer exposed an unsupported dependency index");
    }
}

void LayerCellsUseFourByteValuesAndNativeCoordinates() {
    CKContext context(nullptr, 0, 0); RCKGrid grid(&context, "LayerValueGrid");
    grid.SetDimensions(3, 2, 3.0f, 2.0f); RCKLayer layer(&context, "LayerValue", grid.GetID());
    Check(layer.GetClassID() == CKCID_LAYER && layer.GetType() == 1 && layer.GetFormat() == 0 && layer.IsVisible(),
          "Layer constructor metadata changed");
    CKSquare *squares = layer.GetSquareArray(); Check(squares != nullptr, "Layer constructor omitted cells");
    std::memset(squares, 0xA7, 6 * sizeof(CKSquare));
    const CKDWORD values[] = {0xF1234567u, 0x7F812345u, 0x80000000u};
    layer.SetValue(2, 1, const_cast<CKDWORD *>(&values[0]));
    Check(squares[5].dval == values[0] && squares[4].dval == 0xA7A7A7A7u, "Layer cell coordinate/host stride changed");
    for (size_t byte = sizeof(CKDWORD); byte < sizeof(CKSquare); ++byte)
        Check(reinterpret_cast<unsigned char *>(&squares[5])[byte] == 0xA7, "Layer value setter overwrote bytes beyond the SDK four-byte value");
    CKDWORD output[2] = {0, 0x12345678u}; layer.GetValue(2, 1, &output[0]);
    Check(output[0] == values[0] && output[1] == 0x12345678u, "Layer value getter overwrote its four-byte output");
    Check(layer.SetValue2(0, 0, const_cast<CKDWORD *>(&values[1])) && layer.GetValue2(0, 0, &output[0]) && output[0] == values[1],
          "Layer checked cell access changed value bits");
    Check(!layer.SetValue2(-1, 0, const_cast<CKDWORD *>(&values[2])) && !layer.SetValue2(0, 2, const_cast<CKDWORD *>(&values[2])),
          "Layer checked setter accepted coordinates outside the grid");
    output[0] = 99; Check(!layer.GetValue2(3, 0, &output[0]) && output[0] == 0 && output[1] == 0x12345678u,
          "Layer invalid checked getter did not return a four-byte zero");
    layer.SetType(-7); layer.SetFormat(19); layer.SetVisible(FALSE);
    Check(layer.GetType() == -7 && layer.GetFormat() == 19 && !layer.IsVisible(), "Layer metadata setters changed unrelated metadata");
    layer.SetVisible(TRUE); Check(layer.IsVisible(), "Layer visibility setter did not restore visible state");
    Check(layer.GetMemoryOccupation() == layer.CKObject::GetMemoryOccupation() + sizeof(RCKLayer) - sizeof(CKObject),
          "Layer memory estimate did not use the host runtime object overhead");
}

void LayerSquareArrayTransfersCurrentPointerAndKeepsReplacementContract() {
    CKContext context(nullptr, 0, 0); RCKGrid grid(&context, "LayerArrayGrid");
    grid.SetDimensions(2, 2, 2.0f, 2.0f); RCKLayer layer(&context, "LayerArray", grid.GetID());
    CKSquare *previous = layer.GetSquareArray(); CKSquare *replacement = new CKSquare[4]();
    replacement[3].ival = 91; layer.SetSquareArray(replacement);
    // Native SetSquareArray does not release the prior pointer. The caller
    // retrieves and releases it; the current replacement belongs to the layer.
    delete[] previous;
    layer.FitSquareArray(); int value = 0; layer.GetValue(1, 1, &value);
    Check(layer.GetSquareArray() == replacement && value == 91, "Layer reallocated or lost a caller-supplied current array");
    layer.SetSquareArray(replacement); Check(layer.GetSquareArray() == replacement, "Layer same-pointer assignment changed its current array");
}

void LayerOwnerAssignmentAndInitializationKeepDistinctSemantics() {
    CKContext context(nullptr, 0, 0); RCKGrid first(&context, "LayerOwnerA"), second(&context, "LayerOwnerB");
    first.SetDimensions(2, 2, 2.0f, 2.0f); second.SetDimensions(2, 2, 2.0f, 2.0f);
    RCKLayer layer(&context, "LayerOwner", first.GetID()); CKSquare *cells = layer.GetSquareArray();
    int value = 57; layer.SetValue(1, 1, &value); layer.SetOwner(second.GetID());
    int output = 0; layer.GetValue(1, 1, &output);
    Check(layer.GetOwner() == second.GetID() && layer.GetSquareArray() == cells && output == value,
          "Layer SetOwner replaced cell ownership or contents");
    layer.InitOwner(first.GetID());
    Check(layer.GetOwner() == first.GetID() && layer.GetSquareArray(), "Layer InitOwner did not create its new owner's cells");
    for (int i = 0; i < 4; ++i) Check(layer.GetSquareArray()[i].dval == 0, "New Layer cells did not retain the implementation's zero defaults");
    layer.InitOwner(0); Check(layer.GetOwner() == 0 && !layer.GetSquareArray(), "Layer InitOwner(null) did not clear its owned cells");
}

void LayerPreparesItselfWithoutAddingItsOwnerDependency() {
    CKContext context(nullptr, 0, 0); LayerDependencyOwnerProbe grid(&context);
    RCKLayer layer(&context, "LayerDependency", grid.GetID()); CKDependenciesContext dependencies(&context);
    dependencies.SetOperationMode(CK_DEPENDENCIES_DELETE);
    Check(layer.PrepareDependencies(dependencies) == CK_OK && grid.Calls == 0 &&
          dependencies.IsDependenciesHere(layer.GetID()) && !dependencies.IsDependenciesHere(grid.GetID()),
          "Preparing a Layer added its owner as a deletion dependency");
}

void LayerPreDeletionClearsOnlyAnOwnerMarkedForDeletion() {
    CKContext context(nullptr, 0, 0); RCKGrid grid(&context, "LayerDeleteOwner");
    grid.SetDimensions(2, 2, 2.0f, 2.0f); RCKLayer layer(&context, "LayerDelete", grid.GetID());
    CKSquare *cells = layer.GetSquareArray(); cells[0].ival = 44;
    layer.CheckPreDeletion(); Check(layer.GetOwner() == grid.GetID(), "Layer pre-deletion cleared a live owner");
    grid.ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0); layer.CheckPreDeletion();
    Check(!layer.GetOwner() && layer.GetSquareArray() == cells && cells[0].ival == 44,
          "Layer pre-deletion did not detach its deleted owner while retaining cells");
    grid.ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
}

void LayerDependencyRemappingChangesOnlyTheOwnerReference() {
    CKContext context(nullptr, 0, 0); RCKGrid first(&context, "LayerRemapA"), second(&context, "LayerRemapB");
    first.SetDimensions(2, 2, 2.0f, 2.0f); second.SetDimensions(2, 2, 2.0f, 2.0f);
    RCKLayer layer(&context, "LayerRemap", first.GetID()); CKSquare *cells = layer.GetSquareArray(); cells[3].ival = 38;
    ObjectLifecycleDependencies dependencies(&context); dependencies.Map(first, second);
    Check(layer.RemapDependencies(dependencies) == CK_OK && layer.GetOwner() == second.GetID() &&
          layer.GetSquareArray() == cells && cells[3].ival == 38,
          "Layer RemapDependencies changed cell ownership/data instead of only remapping its owner");
}

void LayerConstructorAllocationFailureLeavesUsableMetadata() {
    CKContext context(nullptr, 0, 0); RCKGrid grid(&context, "LayerCtorFailureGrid");
    grid.SetDimensions(2, 2, 2.0f, 2.0f); s_MorphArrayAllocationFailure = 0;
    RCKLayer layer(&context, "LayerCtorFailure", grid.GetID());
    const bool injected = s_MorphArrayAllocationFailure == -1; s_MorphArrayAllocationFailure = -1;
    Check(injected && layer.GetOwner() == grid.GetID() && !layer.GetSquareArray() && layer.GetType() == 1 && layer.IsVisible(),
          "Layer constructor did not retain valid metadata after cell allocation failure");
}

void LayerInitOwnerAllocationFailureKeepsExistingOwnerAndCells() {
    CKContext context(nullptr, 0, 0); RCKGrid first(&context, "LayerInitA"), second(&context, "LayerInitB");
    first.SetDimensions(2, 2, 2.0f, 2.0f); second.SetDimensions(3, 2, 3.0f, 2.0f);
    RCKLayer layer(&context, "LayerInitFailure", first.GetID()); CKSquare *cells = layer.GetSquareArray(); cells[3].ival = 72;
    s_MorphArrayAllocationFailure = 0; layer.InitOwner(second.GetID());
    const bool injected = s_MorphArrayAllocationFailure == -1; s_MorphArrayAllocationFailure = -1;
    Check(injected && layer.GetOwner() == first.GetID() && layer.GetSquareArray() == cells && cells[3].ival == 72,
          "Layer InitOwner allocation failure changed the existing owner or cells");
}

template<bool CopyData>
void LayerCopyPreservesNativeCellDependencyChoice() {
    CKContext context(nullptr, 0, 0); RCKGrid grid(&context, "LayerCopyGrid");
    grid.SetDimensions(2, 2, 2.0f, 2.0f); RCKLayer source(&context, "LayerCopySource", grid.GetID()), target(&context, "LayerCopyTarget", 0);
    source.SetType(9); source.SetFormat(11); source.SetVisible(FALSE);
    for (int i = 0; i < 4; ++i) source.GetSquareArray()[i].ival = 10 + i;
    CKDependencies options; options.m_Flags = CopyData ? CK_DEPENDENCIES_FULL : CK_DEPENDENCIES_NONE;
    CKDependenciesContext dependencies(&context); dependencies.StartDependencies(&options);
    Check(target.Copy(source, dependencies) == CK_OK, "Layer Copy failed for a valid owner/cell array");
    dependencies.StopDependencies();
    Check(target.GetOwner() == grid.GetID() && target.GetType() == 9 && target.GetFormat() == 11 && !target.IsVisible() &&
          target.GetSquareArray() && target.GetSquareArray() != source.GetSquareArray(), "Layer Copy lost metadata or shared source cell ownership");
    for (int i = 0; i < 4; ++i) Check(target.GetSquareArray()[i].ival == (CopyData ? 10 + i : 0), "Layer Copy ignored its cell data dependency option");
}

void LayerCopyToSelfKeepsOwnedCells() {
    CKContext context(nullptr, 0, 0); RCKGrid grid(&context, "LayerSelfGrid");
    grid.SetDimensions(2, 2, 2.0f, 2.0f); RCKLayer layer(&context, "LayerSelf", grid.GetID());
    CKSquare *cells = layer.GetSquareArray(); cells[3].ival = 61; CKDependenciesContext dependencies(&context);
    Check(layer.Copy(layer, dependencies) == CK_OK && layer.GetSquareArray() == cells && cells[3].ival == 61,
          "Layer self Copy discarded the owned cell array or its contents");
}

void LayerCopyAllocationFailureKeepsExistingLayerState() {
    CKContext context(nullptr, 0, 0); RCKGrid first(&context, "LayerCopyA"), second(&context, "LayerCopyB");
    first.SetDimensions(2, 2, 2.0f, 2.0f); second.SetDimensions(3, 2, 3.0f, 2.0f);
    RCKLayer target(&context, "LayerCopyFailure", first.GetID()), source(&context, "LayerCopyNew", second.GetID());
    target.SetType(5); target.SetVisible(FALSE); source.SetType(12);
    CKSquare *cells = target.GetSquareArray(); cells[3].ival = 82;
    CKDependenciesContext dependencies(&context); s_MorphArrayAllocationFailure = 0;
    const CKERROR result = target.Copy(source, dependencies);
    const bool injected = s_MorphArrayAllocationFailure == -1; s_MorphArrayAllocationFailure = -1;
    Check(injected && result == CKERR_OUTOFMEMORY && target.GetOwner() == first.GetID() && target.GetType() == 5 &&
          !target.IsVisible() && target.GetSquareArray() == cells && cells[3].ival == 82,
          "Layer Copy allocation failure changed metadata/cells or was not reported");
}

void LayerFactoryAllocationFailureReturnsNull() {
    CKContext context(nullptr, 0, 0); s_ObjectAnimationAllocationFailure = true;
    CKLayer *layer = RCKLayer::CreateInstance(&context);
    const bool injected = !s_ObjectAnimationAllocationFailure; s_ObjectAnimationAllocationFailure = false;
    delete layer; Check(injected && !layer, "Layer instance factory did not propagate object allocation failure");
}

// Pass 25 grid functional regressions.
bool GridPoseNear(const VxVector &a, const VxVector &b) {
    return std::fabs(a.x - b.x) < 0.0001f && std::fabs(a.y - b.y) < 0.0001f && std::fabs(a.z - b.z) < 0.0001f;
}

void GridDimensionsCoordinateAndStateContract() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    RCKGrid grid(&context, "GeometryGrid");
    Check(grid.GetClassID() == CKCID_GRID && std::strcmp(RCKGrid::GetClassName(), "Grid") == 0,
          "Grid class metadata changed");
    Check(grid.GetWidth() == 0 && grid.GetLength() == 0 && grid.GetGridPriority() == 0 &&
          grid.GetOrientationMode() == CKGRID_FREE && std::fabs(grid.GetHeightValidity() - 10.0f) < 0.0001f,
          "Grid constructor defaults changed");
    grid.SetDimensions(6, 4, 12.0f, 20.0f);
    grid.SetHeightValidity(8.0f);
    VxVector scale;
    grid.GetScale(&scale, TRUE);
    Check(GridPoseNear(scale, VxVector(2.0f, 8.0f, 5.0f)) && grid.GetWidth() == 6 && grid.GetLength() == 4,
          "Grid dimensions no longer represent total world size");
    VxVector translation(3.0f, 4.0f, 5.0f);
    grid.SetPosition(&translation);
    VxVector center;
    grid.Get3dPosFrom2dCoords(&center, 2, 1);
    Check(GridPoseNear(center, VxVector(8.0f, 4.0f, 12.5f)), "Grid cell center transform changed");
    center.y += 4.0f;
    int x = -1, y = -1;
    Check(std::fabs(grid.Get2dCoordsFrom3dPos(&center, &x, &y) - 0.5f) < 0.0001f && x == 2 && y == 1,
          "Grid inverse coordinate or normalized height changed");
    VxVector local(-0.75f, 0.25f, -1.75f), world;
    grid.Transform(&world, &local);
    Check(std::fabs(grid.Get2dCoordsFrom3dPos(&world, &x, &y) - 0.25f) < 0.0001f && x == 0 && y == -1,
          "Grid coordinate conversion changed truncation towards zero");
    grid.UpdateBox(TRUE);
    Check(GridPoseNear(grid.m_LocalBoundingBox.Min, VxVector(0.0f)) &&
          GridPoseNear(grid.m_LocalBoundingBox.Max, VxVector(6.0f, 1.0f, 4.0f)) &&
          GridPoseNear(grid.m_WorldBoundingBox.Min, translation) &&
          GridPoseNear(grid.m_WorldBoundingBox.Max, VxVector(15.0f, 12.0f, 25.0f)),
          "Grid bounds do not follow dimensions and entity pose");
    grid.SetGridPriority(13);
    grid.SetOrientationMode(CKGRID_YZ);
    Check(grid.GetGridPriority() == 13 && grid.GetOrientationMode() == CKGRID_YZ,
          "Grid priority/orientation setters lost state");
    grid.SetDimensions(0, 4, 0.0f, 16.0f);
    grid.GetScale(&scale, TRUE);
    Check(GridPoseNear(scale, VxVector(2.0f, 8.0f, 4.0f)),
          "An empty grid axis corrupted the remaining entity scale");
}

void GridCopyDependencyMetadataContract() {
    Check(RCKGrid::GetDependenciesCount(CK_DEPENDENCIES_COPY) == 1 &&
          RCKGrid::GetDependenciesCount(CK_DEPENDENCIES_DELETE) == 0 &&
          RCKGrid::GetDependenciesCount(CK_DEPENDENCIES_BUILD) == 0,
          "Grid dependency metadata omitted its copy Layers option");
    Check(RCKGrid::GetDependencies(0, CK_DEPENDENCIES_COPY) &&
          std::strcmp(RCKGrid::GetDependencies(0, CK_DEPENDENCIES_COPY), "Layers") == 0 &&
          RCKGrid::GetDependencies(1, CK_DEPENDENCIES_COPY) == nullptr &&
          RCKGrid::GetDependencies(0, CK_DEPENDENCIES_DELETE) == nullptr,
          "Grid dependency option name/mode changed");
}

void GridLayerRemovalDestroysEveryObject() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    auto *registry = new SerializationGridRegistry(&context);
    auto *grid = static_cast<RCKGrid *>(context.CreateObject(CKCID_GRID, "RemovalGrid", CK_OBJECTCREATION_NONAMECHECK));
    Check(grid != nullptr, "Cannot create registered grid");
    const CK_ID gridID = grid->GetID();
    PopulateGrid(*grid, *registry);
    CK_ID layers[3];
    for (int i = 0; i < 3; ++i) layers[i] = grid->GetLayerByIndex(i)->GetID();
    Check(grid->RemoveAllLayers() == CK_OK && context.GetObject(gridID) == grid && grid->GetLayerCount() == 0,
          "Removing layers deleted the owner grid or retained links");
    for (int i = 0; i < 3; ++i)
        Check(context.GetObject(layers[i]) == nullptr, "RemoveAllLayers skipped an object after deletion notification");
    const int type = registry->GetTypeFromName(const_cast<char *>("Intensity"));
    CKLayer *layer = grid->AddLayerByName(const_cast<char *>("Intensity"));
    Check(layer && grid->GetLayer(type) == layer && grid->GetLayerByName(const_cast<char *>("Intensity")) == layer &&
          grid->GetLayerByIndex(0) == layer && grid->GetLayerByIndex(-1) == nullptr &&
          grid->AddLayer(type) == nullptr && grid->AddLayer(type, 1) == nullptr,
          "Grid layer lookup/duplicate/format contract changed");
    const CK_ID layerID = layer->GetID();
    Check(grid->RemoveLayer(type) == CK_OK && context.GetObject(gridID) == grid &&
          context.GetObject(layerID) == nullptr && grid->GetLayerCount() == 0,
          "RemoveLayer failed to detach and destroy the layer");
    Check(grid->RemoveLayerByName(const_cast<char *>("Intensity")) == CK_OK,
          "Recognized layer-name removal lost its original idempotent result");
    Check(grid->RemoveLayerByName(const_cast<char *>("Unregistered")) != CK_OK,
          "Unregistered layer-name removal unexpectedly succeeded");
}

void GridLegalResizeRebuildsDisplayGeometry() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    auto *registry = new SerializationGridRegistry(&context);
    auto *grid = static_cast<RCKGrid *>(context.CreateObject(CKCID_GRID, "ResizeGrid", CK_OBJECTCREATION_NONAMECHECK));
    Check(grid != nullptr, "Cannot create resize grid");
    grid->SetDimensions(2, 2, 2.0f, 2.0f);
    const int type = registry->RegisterType(const_cast<char *>("ResizeIntensity"));
    VxColor white(1.0f, 1.0f, 1.0f);
    registry->SetAssociatedColor(type, &white);
    CKLayer *layer = grid->AddLayer(type);
    Check(layer != nullptr, "Cannot create first resize layer");
    layer->GetSquareArray()[0].ival = 10;
    grid->ConstructMeshTexture(0.5f);
    Check(grid->GetCurrentMesh() != nullptr, "Cannot construct initial display mesh");
    const CK_ID oldMesh = grid->GetCurrentMesh()->GetID();
    grid->RemoveAllLayers();
    grid->SetDimensions(9, 1, 18.0f, 5.0f);
    layer = grid->AddLayer(type);
    Check(layer != nullptr, "Cannot recreate layer after supported resize workflow");
    layer->GetSquareArray()[8].ival = 31;
    grid->ConstructMeshTexture(0.5f);
    CKMesh *mesh = grid->GetCurrentMesh();
    VxVector vertex;
    mesh->GetVertexPosition(2, &vertex);
    Check(Equal(vertex, VxVector(9.0f, 1.0f, 1.0f)), "Grid resize reused stale display geometry");
    float u = 0.0f, v = 0.0f;
    mesh->GetVertexTextureCoordinates(2, &u, &v, -1);
    CKTexture *texture = mesh->GetFaceMaterial(0)->GetTexture();
    Check(u == 0.5625f && v == 0.125f && texture && texture->GetWidth() == 32 && texture->GetHeight() == 16,
          "Grid resize reused stale display UV or texture dimensions");
    const CKDWORD *pixels = reinterpret_cast<const CKDWORD *>(texture->LockSurfacePtr());
    Check(pixels != nullptr, "Cannot inspect resized grid texture");
    const bool cell = pixels[16] == 0xFF1F1F1Fu && pixels[17] == 0xFF1F1F1Fu &&
                      pixels[48] == 0xFF1F1F1Fu && pixels[49] == 0xFF1F1F1Fu;
    texture->ReleaseSurfacePtr();
    Check(cell && context.GetObject(oldMesh) == nullptr, "Grid resize retained old display resources or cell pixels");
    grid->Show(CKHIDE);
    Check(!grid->IsVisible() && grid->GetCurrentMesh() == nullptr, "Grid hide retained display geometry");
    grid->Show(CKSHOW);
    Check(grid->IsVisible() && grid->GetCurrentMesh() != nullptr, "Grid show failed to restore display geometry");
    grid->GetCurrentMesh()->GetVertexPosition(2, &vertex);
    Check(Equal(vertex, VxVector(9.0f, 1.0f, 1.0f)), "Grid show restored outdated dimensions");
}

void GridCopyRemapsLayersAndOwnsDisplayResources() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    auto *registry = new SerializationGridRegistry(&context);
    auto *source = static_cast<RCKGrid *>(context.CreateObject(CKCID_GRID, "CopyGrid", CK_OBJECTCREATION_NONAMECHECK));
    Check(source != nullptr, "Cannot create copy source grid");
    PopulateGrid(*source, *registry);
    source->SetGridPriority(7); source->SetOrientationMode(CKGRID_XY);
    source->ConstructMeshTexture(0.5f);
    CKMesh *sourceMesh = source->GetCurrentMesh();
    auto *copy = static_cast<RCKGrid *>(context.CopyObject(source));
    Check(copy && copy != source && copy->GetClassID() == CKCID_GRID && copy->GetLayerCount() == 3 &&
          copy->GetWidth() == 2 && copy->GetLength() == 2 && copy->GetGridPriority() == 7 &&
          copy->GetOrientationMode() == CKGRID_XY, "Grid copy lost fields or default layer dependencies");
    Check(source->GetCurrentMesh() == sourceMesh && copy->GetCurrentMesh() && copy->GetCurrentMesh() != sourceMesh,
          "Grid copy shared private display geometry or changed source mesh");
    for (int i = 0; i < 3; ++i) {
        CKLayer *from = source->GetLayerByIndex(i), *to = copy->GetLayerByIndex(i);
        Check(from && to && from != to && to->GetOwner() == copy->GetID() &&
              from->GetOwner() == source->GetID() && to->GetType() == from->GetType() &&
              to->IsVisible() == from->IsVisible(), "Copied layer owner/type/visibility was not remapped");
        for (int cell = 0; cell < 4; ++cell)
            Check(to->GetSquareArray()[cell].ival == from->GetSquareArray()[cell].ival, "Grid copy lost cell payload");
        Check(copy->IsObjectUsed(to, CKCID_LAYER), "Grid does not report its copied layer dependency");
    }
    CheckGridDisplay(*copy);
    CKDependencies noDependencies;
    noDependencies.m_Flags = CK_DEPENDENCIES_NONE;
    auto *without = static_cast<RCKGrid *>(context.CopyObject(source, &noDependencies));
    Check(without && without->GetLayerCount() == 0 && without->GetWidth() == 2 && without->GetLength() == 2 &&
          source->GetLayerCount() == 3 && source->GetCurrentMesh() == sourceMesh,
          "Grid no-dependency copy retained layer links or damaged source");
    CK_ID copiedLayerIDs[3];
    for (int i = 0; i < 3; ++i) copiedLayerIDs[i] = copy->GetLayerByIndex(i)->GetID();
    const CK_ID copiedMesh = copy->GetCurrentMesh()->GetID();
    const CK_ID copiedMaterial = copy->GetCurrentMesh()->GetFaceMaterial(0)->GetID();
    const CK_ID copiedTexture = copy->GetCurrentMesh()->GetFaceMaterial(0)->GetTexture()->GetID();
    context.DestroyObject(copy);
    for (int i = 0; i < 3; ++i)
        Check(context.GetObject(copiedLayerIDs[i]) == nullptr, "Deleting grid left copied layer alive");
    Check(context.GetObject(copiedMesh) == nullptr && context.GetObject(copiedMaterial) == nullptr &&
          context.GetObject(copiedTexture) == nullptr && source->GetCurrentMesh() == sourceMesh &&
          source->GetLayerCount() == 3, "Grid deletion leaked private display resources or damaged source");
}

class GridClassificationRegistry : public SerializationGridRegistry {
public:
    explicit GridClassificationRegistry(CKContext *context)
        : SerializationGridRegistry(context), category(-1) {}
    int GetGridClassificationCategory() override { return category; }
    int category;
};

void GridClassificationUsesAttributeCategoryAndNativeResults() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    auto *registry = new GridClassificationRegistry(&context);
    CKAttributeManager *attributes = context.GetAttributeManager();
    const int shared = attributes->RegisterNewAttributeType("Pass25GridShared", CKPGUID_BOOL, CKCID_3DENTITY);
    const int foreign = attributes->RegisterNewAttributeType("Pass25GridForeign", CKPGUID_BOOL, CKCID_3DENTITY);
    Check(shared >= 0 && foreign >= 0 && attributes->GetAttributeCompatibleClassId(shared) == CKCID_3DENTITY &&
          attributes->GetAttributeCompatibleClassId(foreign) == CKCID_3DENTITY,
          "Cannot register classification attributes for 3D entities");
    attributes->SetAttributeCategory(shared, "Pass25GridClassification");
    attributes->SetAttributeCategory(foreign, "Pass25OtherClassification");
    registry->category = attributes->GetAttributeCategoryIndex(shared);
    RCKGrid grid(&context, "ClassificationGrid");
    RCK3dEntity entity(&context, "ClassifiedEntity");
    Check(!grid.HasCompatibleClass(reinterpret_cast<CK3dEntity *>(&entity)), "Empty classification matched");
    Check(grid.AddClassification(foreign) == CK_OK && entity.SetAttribute(foreign),
          "Cannot attach foreign classification attributes");
    Check(grid.AddClassification(foreign) == CKERR_NOTFOUND,
          "Duplicate indexed classification returned the wrong native result");
    Check(!grid.HasCompatibleClass(reinterpret_cast<CK3dEntity *>(&entity)),
          "Grid matched a shared attribute outside its classification category");
    Check(grid.AddClassificationByName("Pass25GridShared") == CK_OK && entity.SetAttribute(shared) &&
          grid.HasCompatibleClass(reinterpret_cast<CK3dEntity *>(&entity)),
          "Grid classification by name did not match the entity's shared category");
    Check(grid.AddClassificationByName("Pass25GridShared") == CKERR_NOTFOUND,
          "Duplicate named classification returned the wrong native result");
    Check(grid.RemoveClassification(shared) == CK_OK && !grid.HasCompatibleClass(reinterpret_cast<CK3dEntity *>(&entity)),
          "Indexed classification removal left a compatibility match");
    Check(grid.RemoveClassification(shared) == CKERR_NOTFOUND,
          "Absent indexed classification returned the wrong native result");
    Check(grid.AddClassification(shared) == CK_OK && grid.RemoveClassificationByName("Pass25GridShared") == CK_OK &&
          grid.RemoveClassificationByName("Pass25GridShared") == CKERR_NOTFOUND &&
          grid.AddClassificationByName("Pass25GridUnknown") == CKERR_NOTFOUND &&
          grid.RemoveClassificationByName("Pass25GridUnknown") == CKERR_NOTFOUND,
          "Classification name lookup/removal returned the wrong native result");
}

// Pass 26 memory functional regressions.
class SceneMemoryProbe : public CKSceneObject {
public:
    SceneMemoryProbe(CKContext *context, CKSTRING name) : CKSceneObject(context, name) {}
    void SetSceneBit(int index) { m_Scenes.Set(index); }
    void ClearSceneBits() { m_Scenes.Clear(); }
    int SceneBufferBytes() const { return m_Scenes.Size() / 8; }
};

void SceneMemoryIncludesObjectBodyAndOwnedName() {
    CKContext context(nullptr, 0, 0);
    SceneMemoryProbe object(&context, nullptr);
    Check(object.GetMemoryOccupation() == static_cast<int>(sizeof(CKSceneObject)) + object.SceneBufferBytes(),
          "Scene memory omitted the object body");
    object.SetName("Pass26SceneOwnedName");
    Check(object.GetMemoryOccupation() == static_cast<int>(sizeof(CKSceneObject) + std::strlen(object.GetName()) + 1) + object.SceneBufferBytes(),
          "Scene memory omitted the owned name or counted the bit-array header twice");
    object.SetName("Pass26SceneSharedName", TRUE);
    Check(object.GetMemoryOccupation() == static_cast<int>(sizeof(CKSceneObject)) + object.SceneBufferBytes(),
          "Scene memory counted caller-owned shared name storage");
}

void SceneMemoryCountsAllocatedBitsAfterClear() {
    CKContext context(nullptr, 0, 0);
    SceneMemoryProbe object(&context, nullptr);
    const int indices[] = {0, 31, 32, 127, 1024};
    for (int index : indices) {
        object.SetSceneBit(index);
        Check(object.SceneBufferBytes() >= (index / 32 + 1) * 4 && object.GetMemoryOccupation() ==
              static_cast<int>(sizeof(CKSceneObject)) + object.SceneBufferBytes(),
              "Scene memory did not account for allocated scene-bit capacity");
    }
    const int allocated = object.SceneBufferBytes();
    object.ClearSceneBits();
    Check(object.GetSceneInCount() == 0 && object.SceneBufferBytes() == allocated &&
          object.GetMemoryOccupation() == static_cast<int>(sizeof(CKSceneObject)) + allocated,
          "Scene memory confused active scene count with retained allocation");
}

class GridMemoryProbe : public RCKGrid {
public:
    GridMemoryProbe(CKContext *context, CKSTRING name) : RCKGrid(context, name) {}
    void SetSceneBit(int index) { m_Scenes.Set(index); }
    int KnownOwnedBytes() const {
        return m_Scenes.Size() / 8 + m_Groups.Size() / 8 + m_Attributes.GetMemoryOccupation(FALSE) +
               m_Meshes.GetMemoryOccupation(FALSE) + m_Children.GetMemoryOccupation(FALSE) +
               m_Layers.GetMemoryOccupation(FALSE);
    }
};

void GridMemoryIncludesInheritedSceneStorageAndLayerCells() {
    CKContext context(nullptr, 0, 0);
    auto *registry = new SerializationGridRegistry(&context);
    GridMemoryProbe grid(&context, "Pass26GridMemory");
    PopulateGrid(grid, *registry);
    const int body = static_cast<int>(sizeof(RCKGrid) + std::strlen(grid.GetName()) + 1);
    Check(grid.GetMemoryOccupation() == body + grid.KnownOwnedBytes() + 4 * 2 * 2 * 3,
          "Grid memory omitted inherited object fields or layer cells");
    grid.SetSceneBit(127);
    Check(grid.GetMemoryOccupation() == body + grid.KnownOwnedBytes() + 4 * 2 * 2 * 3,
          "Grid memory omitted inherited scene-bit storage");
    grid.RemoveAllLayers();
    Check(grid.GetMemoryOccupation() == body + grid.KnownOwnedBytes(),
          "Grid memory retained removed layer cell bytes");
}

// Pass 26 grid-contract functional regressions.
// Pass 26: Grid public contracts. Root adds CKLevel.h and CKScene.h at file scope.
void GridRegistrationDescriptorMatchesNativeContract() {
    RCKGrid::Register();
    CKClassDesc *descriptor = CKGetClassDesc(CKCID_GRID);
    Check(descriptor && descriptor->Parameter == CKPGUID_3DENTITY &&
          descriptor->ToBeNotify.IsSet(CKCID_LAYER) && descriptor->DefaultCopyDependencies == 1,
          "Grid registration lost its native parameter, Layer notification or copy mask");
    Check(descriptor->NameFct && std::strcmp(descriptor->NameFct(), "Grid") == 0 &&
          descriptor->DependsCountFct && descriptor->DependsCountFct(CK_DEPENDENCIES_COPY) == 1 &&
          descriptor->DependsFct && std::strcmp(descriptor->DependsFct(0, CK_DEPENDENCIES_COPY), "Layers") == 0,
          "Registered Grid callbacks do not expose their native dependency metadata");
}

template<bool Notify>
void GridExternalLayerDeletionPrunesRegisteredLinks() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    SerializationGridRegistry *registry = new SerializationGridRegistry(&context);
    RCKGrid *grid = static_cast<RCKGrid *>(context.CreateObject(CKCID_GRID, "ExternalDeleteGrid", CK_OBJECTCREATION_NONAMECHECK));
    Check(grid != nullptr, "Could not create Grid for external notification");
    grid->SetDimensions(2, 1, 2.0f, 1.0f);
    const int firstType = registry->RegisterType("ExternalDeleteA");
    const int secondType = registry->RegisterType("ExternalDeleteB");
    CKLayer *first = grid->AddLayer(firstType, 0);
    CKLayer *second = grid->AddLayer(secondType, 0);
    Check(first && second && grid->GetLayerCount() == 2, "Could not populate Grid for external deletion");
    const CK_ID removed = first->GetID();
    const CK_ID survivor = second->GetID();
    Check(context.DestroyObject(first, Notify ? 0 : CK_DESTROY_NONOTIFY) == CK_OK && !context.GetObject(removed),
          "External Layer deletion did not remove the requested object");
    if (!Notify) {
        Check(grid->GetLayerCount() == 2 && grid->GetLayerByIndex(0) == nullptr,
              "NONOTIFY unexpectedly ran Grid's deletion callback");
        grid->CheckPostDeletion();
    }
    Check(grid->GetLayerCount() == 1 && grid->GetLayerByIndex(0) == context.GetObject(survivor) &&
          grid->GetLayer(firstType) == nullptr && grid->GetLayer(secondType) == second,
          "Grid external notification/manual callback failed to prune only the deleted Layer");
    grid->CheckPostDeletion();
    Check(grid->GetLayerCount() == 1, "Grid CheckPostDeletion was not idempotent");
}

void GridActivityUsesOnlyTheCurrentSceneFlags() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    RCKGrid grid(&context, "ActivityGrid");
    Check(!grid.IsActive(), "Grid was active without a current level/scene");
    CKLevel *level = static_cast<CKLevel *>(context.CreateObject(CKCID_LEVEL, "ActivityLevel", CK_OBJECTCREATION_NONAMECHECK));
    CKScene *first = static_cast<CKScene *>(context.CreateObject(CKCID_SCENE, "ActivityFirst", CK_OBJECTCREATION_NONAMECHECK));
    CKScene *second = static_cast<CKScene *>(context.CreateObject(CKCID_SCENE, "ActivitySecond", CK_OBJECTCREATION_NONAMECHECK));
    Check(level && first && second, "Could not create activity scene/level objects");
    first->UseEnvironmentSettings(FALSE); second->UseEnvironmentSettings(FALSE);
    level->AddScene(first); level->AddScene(second); context.SetCurrentLevel(level);
    first->AddObject(&grid); second->AddObject(&grid);
    first->SetObjectFlags(&grid, CK_SCENEOBJECT_START_ACTIVATE);
    second->SetObjectFlags(&grid, CK_SCENEOBJECT_START_DEACTIVATE);
    Check(level->LaunchScene(first, CK_SCENEOBJECTACTIVITY_SCENEDEFAULT, CK_SCENEOBJECTRESET_DONOTHING) == CK_OK &&
          grid.IsActive(), "Grid did not follow active flags in the launched first scene");
    first->ModifyObjectFlags(&grid, 0, CK_SCENEOBJECT_ACTIVE);
    Check(!grid.IsActive(), "Grid remained active after current-scene deactivation");
    first->ModifyObjectFlags(&grid, CK_SCENEOBJECT_ACTIVE, 0);
    Check(grid.IsActive(), "Grid did not follow current-scene activation");
    Check(level->LaunchScene(second, CK_SCENEOBJECTACTIVITY_SCENEDEFAULT, CK_SCENEOBJECTRESET_DONOTHING) == CK_OK &&
          !grid.IsActive(), "Grid activity leaked from a previous scene");
    first->SetObjectFlags(&grid, CK_SCENEOBJECT_ACTIVE);
    Check(!grid.IsActive(), "Non-current scene flags changed Grid activity");
    second->ModifyObjectFlags(&grid, CK_SCENEOBJECT_ACTIVE, 0);
    Check(grid.IsActive(), "Second scene activation did not activate Grid");
    second->RemoveObject(&grid);
    Check(!grid.IsActive(), "Grid absent from current scene remained active");
    context.SetCurrentLevel(nullptr);
    Check(!grid.IsActive(), "Grid remained active after detaching the current level");
}

void GridCoordinateQueriesFollowRotatedCellCentersAndHeight() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    RCKGrid grid(&context, "RotatedGrid");
    grid.SetDimensions(5, 4, 10.0f, 12.0f); grid.SetHeightValidity(4.0f);
    VxMatrix pose; pose.SetIdentity();
    pose[0][0] = 0.0f; pose[0][2] = -2.0f;
    pose[1][1] = 4.0f;
    pose[2][0] = 3.0f; pose[2][2] = 0.0f;
    pose[3][0] = 7.0f; pose[3][1] = -3.0f; pose[3][2] = 11.0f;
    grid.SetWorldMatrix(pose);
    VxVector center;
    grid.Get3dPosFrom2dCoords(&center, 1, 2);
    Check(GridPoseNear(center, VxVector(14.5f, -3.0f, 8.0f)),
          "Grid rotated cell center no longer uses x/z half-cell offsets");
    const VxVector point(2.5f, -1.0f, 15.5f);
    int x = 99, z = 99;
    const float height = grid.Get2dCoordsFrom3dPos(&point, &x, &z);
    Check(x == -2 && z == -1 && std::fabs(height - 0.5f) < 0.0001f,
          "Grid rotated inverse query did not truncate local negative coordinates or normalize height");
}

void GridLocalOnlyBoxUpdatePreservesTheWorldCache() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    RCKGrid grid(&context, "LocalBoundsGrid");
    grid.SetDimensions(3, 2, 6.0f, 8.0f);
    grid.m_WorldBoundingBox.Min.Set(-10.0f, -11.0f, -12.0f);
    grid.m_WorldBoundingBox.Max.Set(10.0f, 11.0f, 12.0f);
    grid.m_MoveableFlags &= ~4u;
    grid.UpdateBox(FALSE);
    Check(GridPoseNear(grid.m_LocalBoundingBox.Min, VxVector(0.0f)) &&
          GridPoseNear(grid.m_LocalBoundingBox.Max, VxVector(3.0f, 1.0f, 2.0f)) &&
          GridPoseNear(grid.m_WorldBoundingBox.Min, VxVector(-10.0f, -11.0f, -12.0f)) &&
          GridPoseNear(grid.m_WorldBoundingBox.Max, VxVector(10.0f, 11.0f, 12.0f)) &&
          (grid.m_MoveableFlags & 4u) != 0,
          "Grid local-only UpdateBox changed world bounds or failed to mark local bounds current");
}

void GridObjectUsageDelegatesMeshAndAnimationQueries() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    RCKGrid grid(&context, "UsageGrid");
    RCKMesh mesh(&context, "UsedMesh"), unrelatedMesh(&context, "OtherMesh");
    RCKObjectAnimation animation(&context, "UsedAnimation"), otherAnimation(&context, "OtherAnimation");
    grid.SetCurrentMesh(&mesh, TRUE); grid.AddObjectAnimation(&animation);
    Check(grid.IsObjectUsed(&mesh, CKCID_MESH) && !grid.IsObjectUsed(&unrelatedMesh, CKCID_MESH) &&
          grid.IsObjectUsed(&animation, CKCID_OBJECTANIMATION) && !grid.IsObjectUsed(&otherAnimation, CKCID_OBJECTANIMATION) &&
          !grid.IsObjectUsed(&animation, CKCID_ANIMATION),
          "Grid did not delegate mesh/animation ownership queries to its entity base");
    CKObject object(&context, "UnusedObject");
    Check(!grid.IsObjectUsed(&object, CKCID_OBJECT), "Grid claimed an unrelated base object dependency");
}

template<bool CopyLayers>
void GridPreparationSelectsLayersWithoutOwnerRecursion() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    SerializationGridRegistry *registry = new SerializationGridRegistry(&context);
    RCKGrid grid(&context, "PrepareGrid"); grid.SetDimensions(2, 1, 2.0f, 1.0f);
    const int type = registry->RegisterType("PrepareLayer");
    CKLayer *layer = grid.AddLayer(type, 0); Check(layer, "Could not create preparation Layer");
    RCKMesh mesh(&context, "PrepareMesh"); grid.SetCurrentMesh(&mesh, TRUE);
    CKDependencies options; options.Resize(64); options.Fill(0); options.m_Flags = CK_DEPENDENCIES_CUSTOM;
    options[CKCID_GRID] = CopyLayers ? 1 : 0;
    CKDependenciesContext dependencies(&context); dependencies.SetOperationMode(CK_DEPENDENCIES_COPY);
    dependencies.StartDependencies(&options);
    Check(grid.PrepareDependencies(dependencies) == CK_OK && dependencies.IsDependenciesHere(grid.GetID()) &&
          static_cast<bool>(dependencies.IsDependenciesHere(layer->GetID())) == CopyLayers &&
          grid.GetCurrentMesh() == nullptr && !dependencies.IsDependenciesHere(mesh.GetID()),
          "Grid copy preparation did not honor Layers mask/current mesh exclusion");
    dependencies.StopDependencies();
    grid.SetCurrentMesh(&mesh, TRUE);
}

void GridPreparationPropagatesTheBaseSaveFailure() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    RCKGrid grid(&context, "NoSaveGrid"); grid.ModifyObjectFlags(CK_OBJECT_NOTTOBESAVED, 0);
    CKDependenciesContext dependencies(&context); dependencies.SetOperationMode(CK_DEPENDENCIES_SAVE);
    Check(grid.PrepareDependencies(dependencies) == CKERR_INCOMPATIBLEPARAMETERS &&
          !dependencies.IsDependenciesHere(grid.GetID()) && dependencies.m_DynamicObjects.IsEmpty(),
          "Grid PrepareDependencies swallowed the base's do-not-save error or left unfinished state");
}

// Pass 26 layer functional regressions.
class LayerArrayAllocationFailureScope {
public:
    LayerArrayAllocationFailureScope() { s_MorphArrayAllocationFailure = 0; }
    ~LayerArrayAllocationFailureScope() { s_MorphArrayAllocationFailure = -1; }
};

Chunk LayerPreparedCellFixture(CKObject *grid, int count) {
    Chunk chunk = NewChunk(CKCID_LAYER);
    chunk->WriteIdentifier(CK_STATESAVE_LAYERDATA); chunk->WriteObject(grid);
    chunk->WriteInt(1); chunk->WriteInt(0); chunk->WriteInt(LAYER_STATE_VISIBLE);
    CKDWORD values[] = {21, 34, 55, 89};
    chunk->WriteBuffer_LEndian(count * sizeof(CKDWORD), values);
    chunk->CloseChunk(); chunk->StartRead(); return chunk;
}

template<int WireCount>
void LayerLoadCellAllocationFailureRetainsCellsAndCanRetry() {
    CKContext context(nullptr, 0, 0); new LayerRegistry(&context);
    RCKGrid grid(&context, "PreparedLayerGrid"); grid.SetDimensions(2, 2, 2.0f, 2.0f);
    RCKLayer layer(&context, "PreparedLayer", grid.GetID());
    CKSquare *old = layer.GetSquareArray(); old[0].ival = 144; old[3].ival = 233;
    Chunk chunk = LayerPreparedCellFixture(&grid, WireCount);
    LayerArrayAllocationFailureScope allocationFailure;
    const CKERROR result = layer.Load(chunk.get(), nullptr);
    const bool injected = s_MorphArrayAllocationFailure == -1; s_MorphArrayAllocationFailure = -1;
    Check(injected && result == CKERR_OUTOFMEMORY,
          "Layer cell preparation allocation failure was ignored or returned success");
    Check(layer.GetSquareArray() == old && old[0].ival == 144 && old[3].ival == 233,
          "Failed Layer cell preparation discarded its existing cell allocation");
    chunk->StartRead(); Check(layer.Load(chunk.get(), nullptr) == CK_OK, "Layer cell loading could not retry after allocation failure");
    const int expected[] = {21, 34, WireCount == 4 ? 55 : 0, WireCount == 4 ? 89 : 0};
    for (int i = 0; i < 4; ++i) Check(layer.GetSquareArray()[i].ival == expected[i], "Layer cell retry lost prefix/zero-extension semantics");
    int value = -1; layer.GetValue(1, 1, &value);
    Check(value == expected[3], "Successful Layer Load did not allocate enough cells for its known owner");
}

template<bool FileSave>
void LayerSaveCellPreparationAllocationFailureReturnsNull() {
    CKContext context(nullptr, 0, 0); auto *registry = new LayerRegistry(&context);
    RCKGrid grid(&context, "LayerSavePreparationGrid"); grid.SetDimensions(2, 2, 2.0f, 2.0f);
    RCKLayer layer(&context, "LayerSavePreparation", grid.GetID());
    CKSquare *cells = layer.GetSquareArray(); cells[0].ival = 144; cells[3].ival = 233;
    CKFile file(&context); LayerArrayAllocationFailureScope allocationFailure;
    Chunk saved(layer.Save(FileSave ? &file : nullptr, CK_STATESAVE_ALL), &DeleteCKStateChunk);
    const bool injected = s_MorphArrayAllocationFailure == -1; s_MorphArrayAllocationFailure = -1;
    Check(injected && !saved, "Layer Save ignored cell wire-buffer allocation failure");
    Check(layer.GetSquareArray() == cells && cells[0].ival == 144 && cells[3].ival == 233,
          "Layer Save failure changed source cell ownership or values");
    if (FileSave) Check(registry->remap[1] == 0 && registry->m_RemapCount == 1, "Failed Layer Save registered a successful type remap");
    Chunk retry(layer.Save(FileSave ? &file : nullptr, CK_STATESAVE_ALL), &DeleteCKStateChunk);
    Check(retry != nullptr, "Layer Save could not retry after cell wire-buffer allocation failure");
    RCKLayer reloaded(&context, "LayerSaveRetry", 0); retry->StartRead();
    Check(reloaded.Load(retry.get(), FileSave ? &file : nullptr) == CK_OK && reloaded.GetSquareArray() &&
          reloaded.GetSquareArray()[0].ival == 144 && reloaded.GetSquareArray()[3].ival == 233,
          "Layer Save retry did not retain source cells");
}

void LayerExplicitFitAllocationFailureKeepsOwnedPrefixAndRetries() {
    CKContext context(nullptr, 0, 0); new LayerRegistry(&context);
    RCKGrid grid(&context, "LayerFitPreparedGrid"); grid.SetDimensions(2, 2, 2.0f, 2.0f);
    RCKLayer layer(&context, "LayerFitPrepared", 0);
    Chunk chunk = LayerPreparedCellFixture(nullptr, 2); LoadChunk(layer, chunk.get());
    CKSquare *cells = layer.GetSquareArray(); layer.SetOwner(grid.GetID());
    LayerArrayAllocationFailureScope allocationFailure; layer.FitSquareArray();
    const bool injected = s_MorphArrayAllocationFailure == -1; s_MorphArrayAllocationFailure = -1;
    Check(injected && layer.GetSquareArray() == cells && cells[0].ival == 21 && cells[1].ival == 34,
          "Failed explicit Layer Fit discarded the owned prefix");
    layer.FitSquareArray(); int last = -1; layer.GetValue(1, 1, &last);
    Check(layer.GetSquareArray()[0].ival == 21 && layer.GetSquareArray()[1].ival == 34 && last == 0,
          "Layer Fit retry lost its owned prefix or zero extension");
}

void LayerRegisterAppliesNativeDescriptorFields() {
    RCKLayer::Register(); const CKClassDesc *descriptor = CKGetClassDesc(CKCID_LAYER);
    Check(descriptor && descriptor->DefaultOptions == CK_GENERALOPTIONS_NODUPLICATENAMECHECK &&
          descriptor->DefaultCopyDependencies == CK_DEPENDENCIES_COPY_GRIDLAYER_DATA && descriptor->ToBeNotify.IsSet(CKCID_GRID),
          "Layer registration did not apply native grid notification/options/copy-data dependency");
}

void LayerContextDestructionKeepsOwnerAndHandlesTransferredCells() {
    CKContext context(nullptr, 0, 0); RCKGrid grid(&context, "LayerDestructionGrid"); grid.SetDimensions(2, 2, 2.0f, 2.0f);
    CKLayer *layer = new RCKLayer(&context, "LayerDestruction", grid.GetID());
    context.m_ObjectManager->FinishRegisterObject(layer);
    CKSquare *previous = layer->GetSquareArray(); CKSquare *transferred = new CKSquare[4](); transferred[3].ival = 377;
    layer->SetSquareArray(transferred); delete[] previous;
    const CK_ID id = layer->GetID();
    Check(context.DestroyObject(layer, CK_DESTROY_NONOTIFY) == CK_OK && !context.GetObject(id) && context.GetObject(grid.GetID()) == &grid,
          "Destroying a Layer failed to unregister it or destroyed its referenced owner");
    RCKLayer fresh(&context, "LayerDestructionFresh", grid.GetID()); int value = -1; fresh.GetValue(1, 1, &value);
    Check(value == 0, "Layer destruction left the owner unusable for a fresh cell allocation");
}

// Pass 26 grid-reload functional regressions.
void GridReloadRebuildsDisplayForLoadedDimensions() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    auto *registry = new SerializationGridRegistry(&context);
    RCKGrid source(&context, "Pass26ReloadSource"), target(&context, "Pass26ReloadTarget");
    source.SetDimensions(9, 1, 18.0f, 5.0f);
    Chunk snapshot(source.Save(nullptr, CK_STATESAVE_ALL), &DeleteCKStateChunk);
    Check(snapshot != nullptr, "Could not save Grid reload fixture");
    target.SetDimensions(2, 2, 2.0f, 2.0f);
    const int type = registry->RegisterType("Pass26ReloadCells");
    VxColor white(1.0f, 1.0f, 1.0f);
    registry->SetAssociatedColor(type, &white);
    Check(target.AddLayer(type) != nullptr, "Could not create the previous Grid layer");
    target.ConstructMeshTexture(0.5f);
    CKMesh *oldMesh = target.GetCurrentMesh();
    Check(oldMesh && oldMesh->GetFaceMaterial(0) && oldMesh->GetFaceMaterial(2) &&
          oldMesh->GetFaceMaterial(0)->GetTexture(), "Could not prepare cached Grid display");
    const CK_ID old[] = {oldMesh->GetID(), oldMesh->GetFaceMaterial(0)->GetID(),
                        oldMesh->GetFaceMaterial(2)->GetID(), oldMesh->GetFaceMaterial(0)->GetTexture()->GetID()};
    // Follow the SDK workflow: remove cells before changing dimensions and
    // recreate them after the incoming Grid fields supply the new size.
    target.RemoveAllLayers();
    snapshot->StartRead();
    const CKERROR loaded = target.Load(snapshot.get(), nullptr);
    Check(loaded == CK_OK && target.GetWidth() == 9 && target.GetLength() == 1,
          "Grid reload did not restore saved dimensions");
    for (CK_ID id : old)
        Check(context.GetObject(id) == nullptr, "Grid reload retained stale display resources");
    CKLayer *layer = target.AddLayer(type);
    Check(layer != nullptr, "Could not recreate cells after Grid reload");
    CKDWORD value = 31;
    layer->SetValue(8, 0, &value);
    target.PostLoad();
    CKMesh *mesh = target.GetCurrentMesh();
    Check(mesh && mesh->GetFaceMaterial(0), "Grid PostLoad did not rebuild the loaded display");
    VxVector vertex;
    mesh->GetVertexPosition(2, &vertex);
    float u = 0.0f, v = 0.0f;
    mesh->GetVertexTextureCoordinates(2, &u, &v, -1);
    CKTexture *texture = mesh->GetFaceMaterial(0)->GetTexture();
    Check(Equal(vertex, VxVector(9.0f, 1.0f, 1.0f)) && u == 0.5625f && v == 0.125f &&
          texture && texture->GetWidth() == 32 && texture->GetHeight() == 16,
          "Grid PostLoad reused geometry, UV or texture sizes from the old dimensions");
    const CKDWORD *pixels = reinterpret_cast<const CKDWORD *>(texture->LockSurfacePtr());
    Check(pixels != nullptr, "Could not inspect reloaded Grid display pixels");
    const bool finalCell = pixels[16] == 0xFF1F1F1Fu && pixels[17] == 0xFF1F1F1Fu &&
                           pixels[48] == 0xFF1F1F1Fu && pixels[49] == 0xFF1F1F1Fu;
    texture->ReleaseSurfacePtr();
    Check(finalCell, "Grid PostLoad did not paint the last loaded cell");
}

// Pass 26 grid functional regressions.
template<bool ByName>
void GridAddLayerRejectsUncommittedOwnerAndCanRetry() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    auto *registry = new SerializationGridRegistry(&context);
    auto *grid = static_cast<RCKGrid *>(context.CreateObject(CKCID_GRID, "AddFailureGrid", CK_OBJECTCREATION_NONAMECHECK));
    Check(grid != nullptr, "Cannot create AddLayer failure grid");
    grid->SetDimensions(2, 3, 4.0f, 9.0f);
    const int type = registry->RegisterType("Pass26Cells");
    const int before = context.GetObjectsCountByClassID(CKCID_LAYER);
    s_MorphArrayAllocationFailure = 0;
    CKLayer *failed = ByName ? grid->AddLayerByName("Pass26Cells") : grid->AddLayer(type);
    const bool injected = s_MorphArrayAllocationFailure == -1;
    s_MorphArrayAllocationFailure = -1;
    Check(injected && failed == nullptr && grid->GetLayerCount() == 0 &&
          context.GetObjectsCountByClassID(CKCID_LAYER) == before,
          "Grid returned or leaked a Layer whose cell allocation never committed its owner");
    CKLayer *layer = ByName ? grid->AddLayerByName("Pass26Cells") : grid->AddLayer(type);
    Check(layer && layer->GetOwner() == grid->GetID() && grid->GetLayerCount() == 1,
          "Grid AddLayer could not retry after owner allocation failure");
    CKDWORD value = 37, output = 0;
    Check(layer->SetValue2(1, 2, &value) && layer->GetValue2(1, 2, &output) && output == value,
          "Retried Layer did not have usable cells for all grid dimensions");
    grid->RemoveAllLayers();
    grid->SetDimensions(0, 3, 0.0f, 9.0f);
    layer = ByName ? grid->AddLayerByName("Pass26Cells") : grid->AddLayer(type);
    Check(layer && layer->GetOwner() == grid->GetID() && layer->GetSquareArray() == nullptr,
          "Owner validation rejected a successful zero-cell Layer");
}

CKMesh *Pass26CallerMesh(CKContext &context, RCKGrid &grid) {
    CKMesh *mesh = static_cast<CKMesh *>(context.CreateObject(CKCID_MESH, "CallerMesh", CK_OBJECTCREATION_NONAMECHECK));
    Check(mesh != nullptr, "Cannot create caller mesh");
    grid.SetCurrentMesh(mesh, TRUE);
    return mesh;
}

void Pass26CheckRetryDisplay(RCKGrid &grid, CKMesh *caller) {
    grid.ConstructMeshTexture(0.5f);
    CKMesh *mesh = grid.GetCurrentMesh();
    Check(mesh && mesh != caller && mesh->GetVertexCount() == 12 && mesh->GetFaceCount() == 10,
          "Grid retry reused an incomplete display mesh");
    CKMaterial *material = mesh->GetFaceMaterial(0);
    CKTexture *texture = material ? material->GetTexture() : nullptr;
    Check(texture && texture->GetWidth() == 16 && texture->GetHeight() == 16,
          "Grid retry omitted its display texture");
    const CKDWORD *pixels = reinterpret_cast<const CKDWORD *>(texture->LockSurfacePtr());
    Check(pixels != nullptr, "Grid retry omitted the CPU surface");
    const bool expected = pixels[0] == 0xFF0B0B0Bu && pixels[1] == 0xFF0B0B0Bu &&
                          pixels[16] == 0xFF0B0B0Bu && pixels[17] == 0xFF0B0B0Bu;
    texture->ReleaseSurfacePtr();
    Check(expected, "Retried grid display did not render actual Layer data");
}

void Pass26PopulateOneLayer(RCKGrid &grid, SerializationGridRegistry &registry) {
    const int type = registry.RegisterType("Pass26DisplayCells");
    VxColor white(1.0f, 1.0f, 1.0f);
    registry.SetAssociatedColor(type, &white);
    CKLayer *layer = grid.AddLayer(type);
    Check(layer != nullptr, "Cannot create display Layer");
    CKDWORD value = 11;
    layer->SetValue(0, 0, &value);
}

void GridDisplayWithoutManagerLeavesNoPartialResourcesAndCanRetry() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    auto *grid = static_cast<RCKGrid *>(context.CreateObject(CKCID_GRID, "NoManagerDisplay", CK_OBJECTCREATION_NONAMECHECK));
    Check(grid != nullptr, "Cannot create display grid without manager");
    grid->SetDimensions(1, 1, 2.0f, 3.0f);
    CKMesh *caller = Pass26CallerMesh(context, *grid);
    const int before = context.GetObjectCount();
    grid->ConstructMeshTexture(0.5f);
    Check(context.GetObjectCount() == before && grid->GetCurrentMesh() == caller && grid->GetMeshCount() == 1,
          "Missing GridManager left partial display resources or changed the caller mesh");
    auto *registry = new SerializationGridRegistry(&context);
    Pass26PopulateOneLayer(*grid, *registry);
    Pass26CheckRetryDisplay(*grid, caller);
    Check(context.GetObject(caller->GetID()) == caller, "Display retry destroyed the caller-owned mesh");
}

class Pass26FactoryOverride {
public:
    Pass26FactoryOverride(CK_CLASSID cid, CKCLASSCREATIONFCT replacement)
        : descriptor(CKGetClassDesc(cid)), previous(descriptor ? descriptor->CreationFct : nullptr) {
        Check(descriptor && previous, "Cannot override the registered fixture factory");
        descriptor->CreationFct = replacement;
    }
    ~Pass26FactoryOverride() { descriptor->CreationFct = previous; }
private:
    CKClassDesc *descriptor;
    CKCLASSCREATIONFCT previous;
};

template<bool CreateFailure>
class Pass26UnavailableTexture : public RCKTexture {
public:
    explicit Pass26UnavailableTexture(CKContext *context) : RCKTexture(context) {}
    CKBOOL Create(int width, int height, int bpp, int slot) override {
        if (CreateFailure) return FALSE;
        const CKBOOL created = RCKTexture::Create(width, height, bpp, slot);
        if (created) ReleaseAllSlots();
        return created;
    }
};

template<bool CreateFailure>
CKObject *Pass26CreateUnavailableTexture(CKContext *context) {
    return new Pass26UnavailableTexture<CreateFailure>(context);
}

int Pass26LiveObjectCount(CKContext &context) {
    int count = 0;
    const int lastID = context.GetObjectCount();
    for (int id = 1; id <= lastID; ++id)
        if (context.GetObject(id)) ++count;
    return count;
}

template<bool CreateFailure>
void GridDisplaySurfaceFailureCleansResourcesAndCanRetry() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    auto *registry = new SerializationGridRegistry(&context);
    auto *grid = static_cast<RCKGrid *>(context.CreateObject(CKCID_GRID, "SurfaceFailureDisplay", CK_OBJECTCREATION_NONAMECHECK));
    Check(grid != nullptr, "Cannot create surface failure grid");
    grid->SetDimensions(1, 1, 2.0f, 3.0f);
    Pass26PopulateOneLayer(*grid, *registry);
    CKMesh *caller = Pass26CallerMesh(context, *grid);
    const CK_ID callerID = caller->GetID();
    const int before = Pass26LiveObjectCount(context);
    const int beforeMesh = context.GetObjectsCountByClassID(CKCID_MESH);
    const int beforeMaterial = context.GetObjectsCountByClassID(CKCID_MATERIAL);
    const int beforeTexture = context.GetObjectsCountByClassID(CKCID_TEXTURE);
    {
        Pass26FactoryOverride replacement(CKCID_TEXTURE, Pass26CreateUnavailableTexture<CreateFailure>);
        grid->ConstructMeshTexture(0.5f);
    }
    Check(Pass26LiveObjectCount(context) == before && grid->GetCurrentMesh() == caller &&
          context.GetObject(callerID) == caller && grid->GetMeshCount() == 1 &&
          context.GetObjectsCountByClassID(CKCID_MESH) == beforeMesh &&
          context.GetObjectsCountByClassID(CKCID_MATERIAL) == beforeMaterial &&
          context.GetObjectsCountByClassID(CKCID_TEXTURE) == beforeTexture,
          "Failed texture creation or unavailable surface retained partial resources/current mesh");
    Pass26CheckRetryDisplay(*grid, caller);
    Check(context.GetObject(caller->GetID()) == caller, "Surface failure cleanup destroyed caller-owned geometry");
}

void GridDisplayLevelMembershipCacheAndDeletionContract() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    auto *registry = new SerializationGridRegistry(&context);
    CKLevel *level = static_cast<CKLevel *>(context.CreateObject(CKCID_LEVEL, "DisplayLevel", CK_OBJECTCREATION_NONAMECHECK));
    Check(level && level->GetLevelScene(), "Cannot create display fixture level/scene");
    context.SetCurrentLevel(level);
    auto *grid = static_cast<RCKGrid *>(context.CreateObject(CKCID_GRID, "LevelDisplayGrid", CK_OBJECTCREATION_NONAMECHECK));
    Check(grid != nullptr, "Cannot create level display grid");
    grid->SetDimensions(1, 1, 2.0f, 3.0f);
    Pass26PopulateOneLayer(*grid, *registry);
    CKMesh *caller = Pass26CallerMesh(context, *grid);
    const CK_ID callerID = caller->GetID();
    Check(level->AddObject(caller) == CK_OK, "Cannot place unrelated caller mesh in level");
    grid->ConstructMeshTexture(0.25f);
    CKMesh *mesh = grid->GetCurrentMesh();
    Check(mesh && mesh != caller, "Grid did not create its display mesh");
    CKObject *resources[] = {mesh, mesh->GetFaceMaterial(0), mesh->GetFaceMaterial(2),
                            mesh->GetFaceMaterial(0)->GetTexture()};
    CK_ID ids[4];
    for (int i = 0; i < 4; ++i) {
        Check(resources[i] && static_cast<CKSceneObject *>(resources[i])->IsInScene(level->GetLevelScene()) &&
              (resources[i]->GetObjectFlags() & 0x23) == 0x23,
              "Generated display resource was not privately attached to the current level");
        ids[i] = resources[i]->GetID();
    }
    const int count = context.GetObjectCount();
    grid->SetCurrentMesh(caller);
    grid->ConstructMeshTexture(0.9f);
    Check(grid->GetCurrentMesh() == mesh && context.GetObjectCount() == count &&
          (mesh->GetVertexColor(0) >> 24) == 63,
          "Cached display call duplicated resources or changed native cached opacity");
    grid->DestroyMeshTexture();
    for (int i = 0; i < 4; ++i)
        Check(context.GetObject(ids[i]) == nullptr, "DestroyMeshTexture left generated resource alive");
    Check(grid->GetCurrentMesh() == nullptr && grid->GetMeshCount() == 1 &&
          context.GetObject(callerID) == caller && caller->IsInScene(level->GetLevelScene()),
          "DestroyMeshTexture deleted caller geometry or unrelated level membership");
}

void GridLoadPropagatesLayerFirstFitFailureAndCanRetry() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    auto *registry = new SerializationGridRegistry(&context);
    const int type = registry->RegisterType("Pass26PendingCells");
    RCKGrid grid(&context, "PendingGrid");
    CKLayer *layer = static_cast<CKLayer *>(context.CreateObject(CKCID_LAYER, "Pass26PendingCells", CK_OBJECTCREATION_NONAMECHECK));
    Check(layer != nullptr, "Cannot create pending Layer");
    Chunk layerData = NewChunk(CKCID_LAYER);
    layerData->WriteIdentifier(CK_STATESAVE_LAYERDATA);
    layerData->WriteObject(&grid); layerData->WriteInt(type); layerData->WriteInt(0); layerData->WriteInt(1);
    CKDWORD prefix[] = {5, 6};
    layerData->WriteBuffer_LEndian(sizeof(prefix), prefix);
    LoadChunk(*layer, layerData.get());
    CKSquare *old = layer->GetSquareArray();
    Check(old && old[0].dval == 5 && old[1].dval == 6 && layer->GetOwner() == grid.GetID(),
          "Layer-first fixture did not retain pending owner/payload");
    Chunk gridData = NewChunk(CKCID_GRID);
    gridData->WriteIdentifier(CK_STATESAVE_GRIDDATA);
    gridData->WriteInt(2); gridData->WriteInt(2); gridData->WriteInt(0); gridData->WriteInt(0); gridData->WriteDword(CKGRID_FREE);
    gridData->WriteInt(1);
    XObjectArray layers; layers.PushBack(layer->GetID()); layers.Save(gridData.get(), &context);
    gridData->CloseChunk(); gridData->StartRead();
    CKFile file(&context);
    s_MorphArrayAllocationFailure = 0;
    const CKERROR failed = grid.Load(gridData.get(), &file);
    const bool injected = s_MorphArrayAllocationFailure == -1;
    s_MorphArrayAllocationFailure = -1;
    Check(injected && failed == CKERR_OUTOFMEMORY && layer->GetSquareArray() == old && old[0].dval == 5 && old[1].dval == 6,
          "Grid.Load reported success after pending Layer capacity allocation failed");
    gridData->StartRead();
    Check(grid.Load(gridData.get(), &file) == CK_OK && grid.GetLayerCount() == 1,
          "Grid.Load could not retry pending Layer sizing");
    CKDWORD value = 99;
    Check(layer->GetValue2(0, 0, &value) && value == 5 && layer->GetValue2(1, 0, &value) && value == 6 &&
          layer->GetValue2(1, 1, &value) && value == 0,
          "Grid.Load retry lost prefix or failed to create usable zero tail");
}

void GridEmbeddedSavePropagatesLayerAllocationFailureAndCanRetry() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    auto *registry = new SerializationGridRegistry(&context);
    RCKGrid grid(&context, "EmbeddedSaveGrid");
    grid.SetDimensions(1, 1, 2.0f, 3.0f);
    Pass26PopulateOneLayer(grid, *registry);
    s_MorphArrayAllocationFailure = 0;
    Chunk failed(grid.Save(nullptr, CK_STATESAVE_ALL), &DeleteCKStateChunk);
    const bool injected = s_MorphArrayAllocationFailure == -1;
    s_MorphArrayAllocationFailure = -1;
    Check(injected && failed.get() == nullptr, "Grid.Save accepted a failed embedded Layer snapshot");
    Chunk saved(grid.Save(nullptr, CK_STATESAVE_ALL), &DeleteCKStateChunk);
    Check(saved.get() != nullptr, "Grid.Save could not retry embedded Layer snapshot");
    CKDWORD changed = 77;
    grid.GetLayerByIndex(0)->SetValue(0, 0, &changed);
    LoadChunk(grid, saved.get());
    grid.GetLayerByIndex(0)->GetValue(0, 0, &changed);
    Check(changed == 11, "Retried Grid snapshot did not contain usable embedded Layer values");
}

void GridEmbeddedLoadPropagatesMissingManagerAndCanRetry() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    RCKGrid grid(&context, "EmbeddedLoadGrid");
    grid.SetDimensions(1, 1, 1.0f, 1.0f);
    CKLayer *layer = static_cast<CKLayer *>(context.CreateObject(CKCID_LAYER, "Pass26EmbeddedCells", CK_OBJECTCREATION_NONAMECHECK));
    Check(layer != nullptr, "Cannot create embedded Load Layer");
    layer->InitOwner(grid.GetID());
    Chunk layerData = NewChunk(CKCID_LAYER);
    layerData->WriteIdentifier(CK_STATESAVE_LAYERDATA);
    layerData->WriteObject(&grid); layerData->WriteInt(1); layerData->WriteInt(0); layerData->WriteInt(1);
    CKDWORD cell = 29; layerData->WriteBuffer_LEndian(sizeof(cell), &cell); layerData->CloseChunk();
    Chunk data = NewChunk(CKCID_GRID);
    data->WriteIdentifier(CK_STATESAVE_GRIDDATA);
    data->WriteInt(1); data->WriteInt(1); data->WriteInt(0); data->WriteInt(0); data->WriteDword(CKGRID_FREE);
    XObjectArray layers; layers.PushBack(layer->GetID()); layers.Save(data.get(), &context);
    data->WriteSubChunk(layerData.get()); data->CloseChunk(); data->StartRead();
    Check(grid.Load(data.get(), nullptr) == CKERR_NOTINITIALIZED,
          "Grid.Load ignored a real embedded Layer missing-manager failure");
    new SerializationGridRegistry(&context);
    data->StartRead();
    Check(grid.Load(data.get(), nullptr) == CK_OK && grid.GetLayerCount() == 1,
          "Grid.Load could not retry after registering its manager");
    CKDWORD actual = 0;
    Check(layer->GetValue2(0, 0, &actual) && actual == cell && layer->GetOwner() == grid.GetID(),
          "Retried embedded Grid.Load did not restore actual cell/owner");
}


// Pass 26 chunk functional regressions.
void ChunkObjectIdArrayReadsReplaceNonemptyCacheWithEmpty() {
    CKContext context(nullptr, 0, 0);
    CKObject first(&context, "Pass26FirstArrayObject"), second(&context, "Pass26SecondArrayObject");
    Chunk chunk = NewChunk(CKCID_OBJECT);
    chunk->WriteIdentifier(0x10203040u);
    chunk->StartObjectIDSequence(2);
    chunk->WriteObjectIDSequence(first.GetID()); chunk->WriteObjectIDSequence(second.GetID());
    chunk->StartObjectIDSequence(0);
    chunk->CloseChunk(); chunk->StartRead();
    Check(chunk->SeekIdentifier(0x10203040u), "Could not read object-ID array fixture");
    const XObjectArray &nonempty = chunk->ReadXObjectArray();
    Check(nonempty.Size() == 2 && nonempty[0] == first.GetID() && nonempty[1] == second.GetID(),
          "Object-ID reader did not return the nonempty sequence");
    Check(chunk->ReadXObjectArray().Size() == 0,
          "Empty object-ID sequence returned the preceding array's cached IDs");
    Check(chunk->ReadXObjectArray().Size() == 0,
          "Object-ID reader returned stale IDs at the end of its chunk");
    chunk->StartRead(); chunk->SeekIdentifier(0x10203040u);
    Check(chunk->ReadXObjectArray(&context).Size() == 2, "Pointer-array reader did not resolve valid object IDs");
    Check(chunk->ReadXObjectArray().Size() == 0,
          "ID-array reader confused the distinct pointer-array cache with an empty sequence");
}

// Pass 27 display functional regressions.
static CKCLASSCREATIONFCT s_Pass27DisplayFactory = nullptr;
static int s_Pass27DisplayFactoryRemaining = -1;

CKObject *Pass27DisplayFactoryFailure(CKContext *context) {
    if (s_Pass27DisplayFactoryRemaining == 0) {
        s_Pass27DisplayFactoryRemaining = -1;
        return nullptr;
    }
    if (s_Pass27DisplayFactoryRemaining > 0) --s_Pass27DisplayFactoryRemaining;
    return s_Pass27DisplayFactory(context);
}

class Pass27DisplayFactoryScope {
public:
    Pass27DisplayFactoryScope(CK_CLASSID cid, int index)
        : descriptor(CKGetClassDesc(cid)), previous(descriptor ? descriptor->CreationFct : nullptr) {
        Check(descriptor && previous, "Cannot override display class factory");
        s_Pass27DisplayFactory = previous;
        s_Pass27DisplayFactoryRemaining = index;
        descriptor->CreationFct = Pass27DisplayFactoryFailure;
    }
    ~Pass27DisplayFactoryScope() {
        descriptor->CreationFct = previous;
        s_Pass27DisplayFactory = nullptr;
        s_Pass27DisplayFactoryRemaining = -1;
    }
private:
    CKClassDesc *descriptor;
    CKCLASSCREATIONFCT previous;
};

template<int Stage>
void GridDisplayFactoryNullPreservesCallerAndRetries() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    auto *registry = new SerializationGridRegistry(&context);
    auto *grid = static_cast<RCKGrid *>(context.CreateObject(CKCID_GRID, "Pass27FactoryGrid", CK_OBJECTCREATION_NONAMECHECK));
    Check(grid != nullptr, "Cannot create display factory fixture Grid");
    grid->SetDimensions(1, 1, 2.0f, 3.0f);
    Pass26PopulateOneLayer(*grid, *registry);
    CKMesh *caller = Pass26CallerMesh(context, *grid);
    const CK_ID callerID = caller->GetID();
    const int before = Pass26LiveObjectCount(context);
    const CK_CLASSID cid = Stage == 0 ? CKCID_MESH : (Stage == 3 ? CKCID_TEXTURE : CKCID_MATERIAL);
    bool injected = false;
    {
        Pass27DisplayFactoryScope replacement(cid, Stage == 2 ? 1 : 0);
        grid->ConstructMeshTexture(0.5f);
        injected = s_Pass27DisplayFactoryRemaining == -1;
    }
    Check(injected, "Display fixture did not reach the selected null factory");
    Check(Pass26LiveObjectCount(context) == before && context.GetObject(callerID) == caller &&
          grid->GetCurrentMesh() == caller && grid->GetMeshCount() == 1,
          "Null display factory leaked generated resources or changed caller geometry");
    Pass26CheckRetryDisplay(*grid, caller);
    Check(context.GetObject(callerID) == caller, "Display retry deleted caller geometry");
}

template<int Allocation>
void GridDisplayScratchAllocationFailurePreservesCallerAndRetries() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    auto *registry = new SerializationGridRegistry(&context);
    auto *grid = static_cast<RCKGrid *>(context.CreateObject(CKCID_GRID, "Pass27ScratchGrid", CK_OBJECTCREATION_NONAMECHECK));
    Check(grid != nullptr, "Cannot create display scratch fixture Grid");
    grid->SetDimensions(1, 1, 2.0f, 3.0f);
    Pass26PopulateOneLayer(*grid, *registry);
    CKMesh *caller = Pass26CallerMesh(context, *grid);
    const CK_ID callerID = caller->GetID();
    const int before = Pass26LiveObjectCount(context);
    s_MorphArrayAllocationFailure = Allocation;
    grid->ConstructMeshTexture(0.5f);
    const bool injected = s_MorphArrayAllocationFailure == -1;
    s_MorphArrayAllocationFailure = -1;
    Check(injected && Pass26LiveObjectCount(context) == before &&
          context.GetObject(callerID) == caller && grid->GetCurrentMesh() == caller && grid->GetMeshCount() == 1,
          "Display scratch allocation failure leaked resources or changed caller geometry");
    Pass26CheckRetryDisplay(*grid, caller);
    Check(context.GetObject(callerID) == caller, "Display scratch retry deleted caller geometry");
}

class Pass27DisplayClearObserver : public CKBaseManager {
public:
    explicit Pass27DisplayClearObserver(CKContext *context)
        : CKBaseManager(context, CKGUID(0x73270001, 0x17270001), "Pass27DisplayClearObserver"), context(context) {
        context->RegisterNewManager(this);
    }
    CKDWORD GetValidFunctionsMask() override { return CKMANAGER_FUNC_PreClearAll; }
    CKERROR PreClearAll() override {
        if (!grid) return CK_OK;
        called = true;
        const int before = Pass26LiveObjectCount(*context);
        CKMesh *mesh = grid->GetCurrentMesh();
        bool present = context->IsInClearAll() != FALSE;
        for (int i = 0; i < 5; ++i) present = present && context->GetObject(ids[i]) != nullptr;
        grid->DestroyMeshTexture();
        grid->DestroyMeshTexture();
        intact = present && Pass26LiveObjectCount(*context) == before && grid->GetCurrentMesh() == mesh;
        for (int i = 0; i < 5; ++i) intact = intact && context->GetObject(ids[i]) != nullptr;
        grid = nullptr;
        return CK_OK;
    }
    CKContext *context;
    RCKGrid *grid = nullptr;
    CK_ID ids[5] = {};
    bool called = false;
    bool intact = false;
};

void GridDisplayDestroyDefersToContextDuringClearAll() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    auto *registry = new SerializationGridRegistry(&context);
    auto *observer = new Pass27DisplayClearObserver(&context);
    auto *grid = static_cast<RCKGrid *>(context.CreateObject(CKCID_GRID, "Pass27ClearGrid", CK_OBJECTCREATION_NONAMECHECK));
    Check(grid != nullptr, "Cannot create ClearAll display Grid");
    grid->SetDimensions(1, 1, 2.0f, 3.0f);
    Pass26PopulateOneLayer(*grid, *registry);
    grid->ConstructMeshTexture(0.5f);
    CKMesh *mesh = grid->GetCurrentMesh();
    Check(mesh && mesh->GetFaceMaterial(0) && mesh->GetFaceMaterial(2) && mesh->GetFaceMaterial(0)->GetTexture(),
          "ClearAll fixture lacks generated display graph");
    observer->grid = grid;
    observer->ids[0] = grid->GetID();
    observer->ids[1] = mesh->GetID();
    observer->ids[2] = mesh->GetFaceMaterial(0)->GetID();
    observer->ids[3] = mesh->GetFaceMaterial(2)->GetID();
    observer->ids[4] = mesh->GetFaceMaterial(0)->GetTexture()->GetID();
    Check(context.ClearAll() == CK_OK && observer->called && observer->intact,
          "DestroyMeshTexture destroyed resources early during actual manager PreClearAll");
    for (int i = 0; i < 5; ++i)
        Check(context.GetObject(observer->ids[i]) == nullptr, "Context ClearAll failed to delete a display resource");
    Check(Pass26LiveObjectCount(context) == 0 && !context.IsInClearAll(),
          "Display ClearAll left live objects or failed to complete context cleanup");
}

void GridDisplaySignedCellRgbUsesNativeAddition() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    auto *registry = new SerializationGridRegistry(&context);
    auto *grid = static_cast<RCKGrid *>(context.CreateObject(CKCID_GRID, "Pass27SignedGrid", CK_OBJECTCREATION_NONAMECHECK));
    Check(grid != nullptr, "Cannot create signed-cell display Grid");
    grid->SetDimensions(4, 1, 8.0f, 3.0f);
    const int type = registry->RegisterType("Pass27SignedCells");
    VxColor white(1.0f, 1.0f, 1.0f);
    registry->SetAssociatedColor(type, &white);
    CKLayer *layer = grid->AddLayer(type);
    Check(layer != nullptr, "Cannot create signed-cell Layer");
    const int values[] = {-1, -2, 0, 11};
    const CKDWORD expected[] = {0xFFFEFEFFu, 0xFFFDFDFEu, 0xFF000000u, 0xFF0B0B0Bu};
    for (int x = 0; x < 4; ++x)
        layer->SetValue(x, 0, const_cast<int *>(&values[x]));
    grid->ConstructMeshTexture(0.5f);
    CKMesh *mesh = grid->GetCurrentMesh();
    CKMaterial *material = mesh ? mesh->GetFaceMaterial(0) : nullptr;
    CKTexture *texture = material ? material->GetTexture() : nullptr;
    Check(texture && texture->GetWidth() == 16 && texture->GetHeight() == 16,
          "Signed-cell display did not create its real texture");
    const CKDWORD *pixels = reinterpret_cast<const CKDWORD *>(texture->LockSurfacePtr());
    Check(pixels != nullptr, "Signed-cell texture has no CPU pixels");
    bool matched = true;
    for (int x = 0; x < 4; ++x) {
        const int index = x * 2;
        matched = matched && pixels[index] == expected[x] && pixels[index + 1] == expected[x] &&
                  pixels[index + 16] == expected[x] && pixels[index + 17] == expected[x];
    }
    texture->ReleaseSurfacePtr();
    Check(matched, "Signed Layer color components must use native modular addition rather than bitwise OR");
}

template<bool MissingMain>
void GridDisplayDestroyHandlesEditedMaterialSlots() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    auto *registry = new SerializationGridRegistry(&context);
    auto *grid = static_cast<RCKGrid *>(context.CreateObject(CKCID_GRID, "Pass27EditedDisplayGrid", CK_OBJECTCREATION_NONAMECHECK));
    Check(grid != nullptr, "Cannot create edited-material display Grid");
    grid->SetDimensions(1, 1, 2.0f, 3.0f);
    Pass26PopulateOneLayer(*grid, *registry);
    CKMesh *caller = Pass26CallerMesh(context, *grid);
    const CK_ID callerID = caller->GetID();
    grid->ConstructMeshTexture(0.5f);
    CKMesh *mesh = grid->GetCurrentMesh();
    CKMaterial *main = mesh ? mesh->GetFaceMaterial(0) : nullptr;
    CKMaterial *border = mesh ? mesh->GetFaceMaterial(2) : nullptr;
    CKTexture *texture = main ? main->GetTexture() : nullptr;
    Check(mesh && main && border && texture, "Edited-material fixture lacks display resources");
    const CK_ID meshID = mesh->GetID(), mainID = main->GetID(), borderID = border->GetID(), textureID = texture->GetID();
    if (MissingMain) {
        mesh->SetFaceMaterial(0, nullptr);
        mesh->SetFaceMaterial(1, nullptr);
    } else {
        for (int face = 2; face < 10; ++face) mesh->SetFaceMaterial(face, main);
    }
    grid->DestroyMeshTexture();
    Check(context.GetObject(meshID) == nullptr && grid->GetCurrentMesh() == nullptr &&
          context.GetObject(callerID) == caller && grid->GetMeshCount() == 1,
          "Edited-material display teardown changed caller mesh or kept generated mesh");
    if (MissingMain) {
        Check(context.GetObject(borderID) == nullptr && context.GetObject(mainID) == main &&
              context.GetObject(textureID) == texture,
              "Missing slot zero kept reachable border material or deleted detached resources");
    } else {
        Check(context.GetObject(mainID) == nullptr && context.GetObject(textureID) == nullptr &&
              context.GetObject(borderID) == border,
              "Aliased material slots deleted the same material twice or deleted detached border material");
    }
}

// Pass 27 grid functional regressions.
// Pass27: actual Grid API contracts; no shared test source edits.
class Pass27GridRegistry : public SerializationGridRegistry {
public:
    explicit Pass27GridRegistry(CKContext *context) : SerializationGridRegistry(context), category(-1) {}
    CKSTRING GetTypeName(int type) override {
        if (type <= 0 || type >= GetLayerTypeCount()) return nullptr;
        return SerializationGridRegistry::GetTypeName(type);
    }
    int GetGridClassificationCategory() override { return category; }
    int category;
};

void GridFactoryAppliesConstructorStateAndGridAttribute() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    CKAttributeManager *attributes = context.GetAttributeManager();
    int gridAttribute = attributes->GetAttributeTypeByName("Grid");
    if (!attributes->IsAttributeIndexValid(gridAttribute))
        gridAttribute = attributes->RegisterNewAttributeType("Grid", CKPGUID_BOOL, CKCID_3DENTITY);
    Check(attributes->IsAttributeIndexValid(gridAttribute), "Cannot register the native Grid classification attribute");
    CKGrid *created = RCKGrid::CreateInstance(&context);
    Check(created != nullptr, "Grid factory did not construct a Grid");
    RCKGrid *grid = reinterpret_cast<RCKGrid *>(created);
    VxVector scale;
    grid->GetScale(&scale, TRUE);
    Check(grid->GetClassID() == CKCID_GRID && grid->GetWidth() == 0 && grid->GetLength() == 0 &&
          grid->GetGridPriority() == 0 && grid->GetOrientationMode() == CKGRID_FREE &&
          grid->GetLayerCount() == 0 && grid->GetCurrentMesh() == nullptr &&
          GridPoseNear(scale, VxVector(1.0f, 10.0f, 1.0f)) && grid->HasAttribute(gridAttribute),
          "Grid factory/constructor omitted defaults or registered Grid attribute");
    const CK_ID id = grid->GetID();
    Check(context.GetObject(id) == grid, "Direct Grid factory did not register its object");
    context.DestroyObject(grid, CK_DESTROY_NONOTIFY);
    Check(context.GetObject(id) == nullptr, "Grid factory object could not be destroyed normally");
}

template<bool ByName>
void GridLayerOperationsWithoutManagerKeepState() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    RCKGrid grid(&context, "NoGridManager");
    grid.SetDimensions(2, 1, 2.0f, 1.0f);
    RCK3dEntity entity(&context, "NoManagerEntity");
    const int before = context.GetObjectsCountByClassID(CKCID_LAYER);
    CKLayer *added = ByName ? grid.AddLayerByName("Unregistered") : grid.AddLayer(1);
    Check(added == nullptr && grid.GetLayerByName("Unregistered") == nullptr &&
          grid.RemoveLayer(1) == CKERR_NOTFOUND && !grid.HasCompatibleClass(reinterpret_cast<CK3dEntity *>(&entity)) &&
          grid.GetLayerCount() == 0 && context.GetObjectsCountByClassID(CKCID_LAYER) == before,
          "Missing GridManager changed layers or native query/removal results");
}

template<bool ByName>
void GridLayerRejectionsAndNamedLookupFollowRegistry() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    auto *registry = new Pass27GridRegistry(&context);
    RCKGrid grid(&context, "NamedLayers");
    grid.SetDimensions(2, 1, 2.0f, 1.0f);
    const int type = registry->RegisterType("Pass27KnownCells");
    const int before = context.GetObjectsCountByClassID(CKCID_LAYER);
    Check(grid.AddLayer(0) == nullptr && grid.AddLayer(9999) == nullptr &&
          grid.AddLayerByName("Pass27UnknownCells") == nullptr &&
          grid.GetLayerByName("Pass27UnknownCells") == nullptr && grid.RemoveLayer(type) == CKERR_NOTFOUND,
          "Unregistered or absent Layer was accepted");
    Check((ByName ? grid.AddLayerByName("Pass27KnownCells", 1) : grid.AddLayer(type, 1)) == nullptr &&
          grid.GetLayerCount() == 0 && context.GetObjectsCountByClassID(CKCID_LAYER) == before,
          "Unsupported Layer format changed owned state");
    CKLayer *layer = ByName ? grid.AddLayerByName("Pass27KnownCells") : grid.AddLayer(type);
    Check(layer && layer->GetOwner() == grid.GetID() && layer->GetType() == type && layer->GetFormat() == 0 &&
          std::strcmp(layer->GetName(), "Pass27KnownCells") == 0 &&
          grid.GetLayer(type) == layer && grid.GetLayerByName("Pass27KnownCells") == layer,
          "Registered Layer lost name/type/format/owner or named lookup");
    Check(grid.AddLayer(type) == nullptr && grid.AddLayerByName("Pass27KnownCells") == nullptr &&
          grid.GetLayerCount() == 1, "Duplicate named/indexed Layer changed owned state");
    const CK_ID id = layer->GetID();
    Check(grid.RemoveLayer(type) == CK_OK && context.GetObject(id) == nullptr &&
          grid.GetLayerByName("Pass27KnownCells") == nullptr && grid.RemoveLayer(type) == CKERR_NOTFOUND,
          "Layer removal did not destroy the matched Layer or retain its absent result");
}

void GridNamedClassificationUsesRegisteredCategoryAndNativeErrors() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    auto *registry = new Pass27GridRegistry(&context);
    CKAttributeManager *attributes = context.GetAttributeManager();
    const int a = attributes->RegisterNewAttributeType("Pass27SharedA", CKPGUID_BOOL, CKCID_3DENTITY);
    const int b = attributes->RegisterNewAttributeType("Pass27SharedB", CKPGUID_BOOL, CKCID_3DENTITY);
    const int foreign = attributes->RegisterNewAttributeType("Pass27Foreign", CKPGUID_BOOL, CKCID_3DENTITY);
    attributes->SetAttributeCategory(a, "Pass27GridCategory");
    attributes->SetAttributeCategory(b, "Pass27GridCategory");
    attributes->SetAttributeCategory(foreign, "Pass27OtherCategory");
    registry->category = attributes->GetAttributeCategoryIndex(a);
    RCKGrid grid(&context, "NamedClassifications");
    RCK3dEntity entity(&context, "MultiAttributeEntity");
    Check(!grid.HasCompatibleClass(reinterpret_cast<CK3dEntity *>(&entity)) && !grid.HasCompatibleClass(nullptr),
          "Empty/null entity matched Grid classifications");
    Check(grid.AddClassificationByName("Pass27SharedA") == CK_OK &&
          grid.AddClassificationByName("Pass27SharedA") == CKERR_NOTFOUND &&
          grid.AddClassificationByName("Pass27Foreign") == CK_OK && entity.SetAttribute(foreign) &&
          entity.SetAttribute(b) && !grid.HasCompatibleClass(reinterpret_cast<CK3dEntity *>(&entity)),
          "Shared unrelated-category or different-category-member attributes matched");
    Check(entity.SetAttribute(a) && grid.HasCompatibleClass(reinterpret_cast<CK3dEntity *>(&entity)),
          "Reverse attribute scan omitted a real shared classification");
    Check(grid.RemoveClassificationByName("Pass27SharedA") == CK_OK &&
          grid.RemoveClassificationByName("Pass27SharedA") == CKERR_NOTFOUND &&
          !grid.HasCompatibleClass(reinterpret_cast<CK3dEntity *>(&entity)) &&
          grid.AddClassificationByName("Pass27Unknown") == CKERR_NOTFOUND &&
          grid.RemoveClassificationByName("Pass27Unknown") == CKERR_NOTFOUND,
          "Named classification removal or unknown-name result differs from original");
}

template<bool Current>
void GridPreSaveCollectsLayersAndOnlyCurrentMesh() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    auto *registry = new Pass27GridRegistry(&context);
    RCKGrid grid(&context, "PreSaveGrid");
    grid.SetDimensions(1, 1, 1.0f, 1.0f);
    CKLayer *layer = grid.AddLayer(registry->RegisterType("Pass27PreSaveCells"));
    Check(layer != nullptr, "Cannot create PreSave Layer");
    CKMesh *first = static_cast<CKMesh *>(context.CreateObject(CKCID_MESH, "CurrentCandidate"));
    CKMesh *other = static_cast<CKMesh *>(context.CreateObject(CKCID_MESH, "DiscardedCandidate"));
    Check(first && other, "Cannot create PreSave mesh candidates");
    grid.AddMesh(first); grid.AddMesh(other); grid.SetCurrentMesh(Current ? first : nullptr, TRUE);
    TemporarySerializationFile temporary;
    CKFile file(&context);
    const CKDWORD flags = CK_STATESAVE_GRIDONLY;
    Check(file.StartSave(temporary.Path) == CK_OK, "Cannot initialize the native CKFile save job");
    // The test registry advertises no automatic manager callbacks.
    Check(registry->PreSave() == CK_OK, "Cannot initialize the test registry's file type map");
    file.SaveObject(&grid, flags); // Dispatch the real PreSave callback inside its required save job.
    Check(grid.GetMeshCount() == (Current ? 1 : 0) && grid.GetCurrentMesh() == (Current ? first : nullptr),
          "PreSave did not collapse the mesh list to its current mesh");
    bool hasLayer = false, hasFirst = false, hasOther = false;
    for (CKFileObject &entry : file.m_FileObjects) {
        if (entry.Object == layer->GetID()) { hasLayer = true; Check(entry.SaveFlags == flags, "Layer PreSave flags changed"); }
        if (entry.Object == first->GetID()) hasFirst = true;
        if (entry.Object == other->GetID()) hasOther = true;
    }
    Check(hasLayer && hasFirst == Current && !hasOther &&
          context.GetObject(other->GetID()) == other, "PreSave dependency collection omitted Layer/current mesh or destroyed caller meshes");
    Check(file.EndSave() == CK_OK, "Grid PreSave dependency queue could not complete its real file save");
}

template<bool Visible>
void GridPostLoadBuildsOnlyVisibleDisplay() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    auto *registry = new Pass27GridRegistry(&context);
    RCKGrid grid(&context, "PostLoadGrid");
    grid.SetDimensions(1, 1, 1.0f, 1.0f);
    if (!Visible) grid.Show(CKHIDE);
    CKLayer *layer = grid.AddLayer(registry->RegisterType("Pass27PostLoadCells"));
    Check(layer != nullptr, "Cannot create PostLoad Layer");
    const int before = Pass26LiveObjectCount(context);
    grid.PostLoad();
    Check(grid.IsVisible() == Visible, "PostLoad changed saved visibility");
    if (Visible) {
        CKMesh *mesh = grid.GetCurrentMesh();
        Check(mesh && mesh->GetVertexCount() == 12 && (mesh->GetVertexColor(0) >> 24) == 127,
              "Visible PostLoad omitted native 0.5-opacity display");
    } else {
        Check(grid.GetCurrentMesh() == nullptr && grid.GetMeshCount() == 0 && Pass26LiveObjectCount(context) == before,
              "Hidden PostLoad allocated display resources");
    }
}

void GridSaveMasksAndEmptyLayerWireMatchOriginalLayout() {
    CKContext context(nullptr, 0, 0);
    RCKGrid grid(&context, "SelectiveGrid");
    grid.SetDimensions(3, 2, 6.0f, 8.0f); grid.SetGridPriority(-7); grid.SetOrientationMode(CKGRID_XZ);
    const CKDWORD masks[] = {0, CK_STATESAVE_3DENTITYONLY, 0x00400000u, 0x00800000u,
                            0x01000000u, 0x02000000u, 0x04000000u, 0x08000000u};
    for (CKDWORD flags : masks) {
        Chunk saved(grid.Save(nullptr, flags), &DeleteCKStateChunk);
        Check(saved.get() != nullptr, "Selective Grid Save returned no chunk");
        saved->StartRead();
        const bool included = (flags & CK_STATESAVE_GRIDONLY) != 0;
        Check((saved->SeekIdentifier(CK_STATESAVE_GRIDDATA) != FALSE) == included,
              "Grid mask changed base-only versus complete Grid block selection");
        if (!included) continue;
        Check(saved->SeekIdentifierAndReturnSize(CK_STATESAVE_GRIDDATA) == 24,
              "Standalone empty Grid block has the wrong native size");
        Check(saved->ReadInt() == 3 && saved->ReadInt() == 2 && saved->ReadInt() == 0 &&
              saved->ReadInt() == -7 && saved->ReadDword() == CKGRID_XZ && saved->ReadXObjectArray().Size() == 0,
              "Standalone Grid fields/reserved word/empty Layer sequence changed");
    }
    CKFile file(&context);
    Chunk saved(grid.Save(&file, 0), &DeleteCKStateChunk); saved->StartRead();
    Check(saved->SeekIdentifierAndReturnSize(CK_STATESAVE_GRIDDATA) == 28 &&
          saved->ReadInt() == 3 && saved->ReadInt() == 2 && saved->ReadInt() == 0 &&
          saved->ReadInt() == -7 && saved->ReadDword() == CKGRID_XZ && saved->ReadInt() == 1 &&
          saved->ReadXObjectArray().Size() == 0, "File Grid block omitted its native marker or ignored unconditional file inclusion");
}

template<bool FileMode>
void GridLoadIndependentEmptyLayerWireAndAbsentBlock() {
    CKContext context(nullptr, 0, 0);
    RCKGrid grid(&context, "IndependentLoad");
    grid.SetDimensions(2, 1, 2.0f, 1.0f); grid.SetGridPriority(6); grid.SetOrientationMode(CKGRID_YZ);
    Chunk missing = NewChunk(CKCID_GRID);
    missing->WriteIdentifier(0x76543210u); missing->WriteInt(17); missing->CloseChunk(); missing->StartRead();
    CKFile file(&context);
    CKFile *mode = FileMode ? &file : nullptr;
    Check(grid.Load(nullptr, mode) == CKERR_INVALIDPARAMETER && grid.Load(missing.get(), mode) == CK_OK &&
          grid.GetWidth() == 2 && grid.GetLength() == 1 && grid.GetGridPriority() == 6 &&
          grid.GetOrientationMode() == CKGRID_YZ, "Null/absent Grid block changed saved Grid fields");
    Chunk wire = NewChunk(CKCID_GRID);
    wire->WriteIdentifier(CK_STATESAVE_GRIDDATA);
    wire->WriteInt(4); wire->WriteInt(3); wire->WriteInt(0x11223344);
    wire->WriteInt(-9); wire->WriteDword(CKGRID_XY);
    if (FileMode) wire->WriteInt(1);
    wire->StartObjectIDSequence(0); wire->CloseChunk(); wire->StartRead();
    Check(grid.Load(wire.get(), mode) == CK_OK && grid.GetWidth() == 4 && grid.GetLength() == 3 &&
          grid.GetGridPriority() == -9 && grid.GetOrientationMode() == CKGRID_XY && grid.GetLayerCount() == 0,
          "Independent file/memory Grid wire did not restore native fields or empty Layers");
}

class Pass27GridMemoryProbe : public RCKGrid {
public:
    explicit Pass27GridMemoryProbe(CKContext *context) : RCKGrid(context, "MemoryDelegation") {}
    int LayerArrayBytes() const { return m_Layers.GetMemoryOccupation(FALSE); }
};

void GridMemoryAddsOwnedLayerStorageToEntityEstimate() {
    CKContext context(nullptr, 0, 0);
    new RCKRenderManager(&context);
    auto *registry = new Pass27GridRegistry(&context);
    Pass27GridMemoryProbe grid(&context);
    grid.SetDimensions(3, 2, 3.0f, 2.0f);
    CKMesh *mesh = static_cast<CKMesh *>(context.CreateObject(CKCID_MESH, "MemoryMesh"));
    Check(mesh != nullptr, "Cannot create Grid memory mesh"); grid.AddMesh(mesh);
    const int fields = static_cast<int>(sizeof(XObjectArray) + 4 * sizeof(CKDWORD) + sizeof(CKMesh *));
    Check(grid.GetMemoryOccupation() == grid.RCK3dEntity::GetMemoryOccupation() + fields + grid.LayerArrayBytes(),
          "Empty Grid memory did not delegate existing entity/mesh allocations");
    CKLayer *a = grid.AddLayer(registry->RegisterType("MemoryA"));
    CKLayer *b = grid.AddLayer(registry->RegisterType("MemoryB"));
    Check(a && b && grid.GetMemoryOccupation() == grid.RCK3dEntity::GetMemoryOccupation() + fields +
          grid.LayerArrayBytes() + 2 * 3 * 2 * 4, "Grid memory did not count four bytes for each owned Layer cell");
    grid.RemoveAllLayers();
    Check(grid.GetMemoryOccupation() == grid.RCK3dEntity::GetMemoryOccupation() + fields + grid.LayerArrayBytes() &&
          context.GetObject(mesh->GetID()) == mesh, "Layer removal confused retained array capacity with live cell storage");
}

// Pass 27 layer functional regressions.
// Pass27 observer requires pass27-layer-allocation-hooks.inc at global scope.
class LayerArrayReleaseObservationScope {
public:
    explicit LayerArrayReleaseObservationScope(void *pointer, size_t bytes = 0) {
        s_LayerArrayObservedPointer = pointer;
        s_LayerArrayObservedBytes = bytes;
        s_LayerArrayObservedAllocations = 0;
        s_LayerArrayObservedReleases = 0;
        s_LayerArrayReleaseObservation = true;
    }
    ~LayerArrayReleaseObservationScope() {
        s_LayerArrayReleaseObservation = false;
        s_LayerArrayObservedPointer = nullptr;
        s_LayerArrayObservedBytes = 0;
    }
};

template<bool Transferred>
void LayerDestructorReleasesCurrentArrayExactlyOnce() {
    CKContext context(nullptr, 0, 0);
    RCKGrid grid(&context, "ObservedLayerOwner");
    grid.SetDimensions(2, 2, 2.0f, 2.0f);
    RCKLayer *layer = new RCKLayer(&context, "ObservedLayer", grid.GetID());
    CKSquare *cells = layer->GetSquareArray();
    Check(cells != nullptr, "Observed Layer did not allocate its initial cells");
    if (Transferred) {
        CKSquare *replacement = new CKSquare[4]();
        replacement[3].dval = 0xA1B2C3D4u;
        {
            LayerArrayReleaseObservationScope previous(cells);
            layer->SetSquareArray(replacement);
            Check(s_LayerArrayObservedReleases == 0 && layer->GetSquareArray() == replacement,
                  "SetSquareArray freed the previous allocation instead of transferring only the current pointer");
            delete[] cells;
            Check(s_LayerArrayObservedReleases == 1,
                  "The replacing caller could not release the previous Layer allocation exactly once");
        }
        cells = replacement;
    }
    const CK_ID layerID = layer->GetID();
    LayerArrayReleaseObservationScope current(cells);
    delete layer;
    Check(s_LayerArrayObservedReleases == 1 && !context.GetObject(layerID) && context.GetObject(grid.GetID()) == &grid,
          "Layer destruction did not release its current cell pointer once while retaining the owner");
    RCKLayer fresh(&context, "ObservedLayerFresh", grid.GetID());
    CKDWORD last = 1; fresh.GetValue(1, 1, &last);
    Check(last == 0, "Layer owner was not usable after independently observed cell release");
}

template<bool FileSave>
void LayerSaveReleasesPackedScratchAndMatchesIndependentWire() {
    CKContext context(nullptr, 0, 0);
    auto *registry = new LayerRegistry(&context);
    registry->color.Set(0.25f, 0.5f, 0.75f, 1.0f);
    registry->parameter = CKGUID(0x10203040, 0x50607080);
    RCKGrid grid(&context, "ObservedScratchGrid"); grid.SetDimensions(32, 32, 32.0f, 32.0f);
    RCKLayer layer(&context, "ObservedScratchLayer", grid.GetID());
    CKDWORD expected[1024];
    for (int i = 0; i < 1024; ++i) {
        expected[i] = 0x80000000u | static_cast<CKDWORD>(i * 257 + 3);
        layer.GetSquareArray()[i].dval = expected[i];
    }
    CKFile file(&context);
    file.m_FileObjects.Resize(1);
    file.m_FileObjects[0].Object = file.m_FileObjects[0].CreatedObject = grid.GetID();
    file.m_FileObjects[0].ObjPtr = &grid;
    file.m_ObjectsHashTable.Insert(grid.GetID(), 0);
    LayerArrayReleaseObservationScope scratch(nullptr, sizeof(expected));
    Chunk saved(layer.Save(FileSave ? &file : nullptr, CK_STATESAVE_ALL), &DeleteCKStateChunk);
    Check(saved && s_LayerArrayObservedAllocations == 1 && s_LayerArrayObservedReleases == 1,
          "Successful Layer Save did not independently allocate/release its packed cell scratch exactly once");
    saved->StartRead();
    Check(saved->SeekIdentifierAndReturnSize(CK_STATESAVE_LAYERDATA) == (FileSave ? 28 : 16) + 4 + sizeof(expected),
          "Layer word payload/header size differed from independent native wire layout");
    Check(saved->ReadObjectID() == grid.GetID(), "Layer wire owner changed");
    if (!FileSave) Check(saved->ReadInt() == 1, "Layer memory type word changed");
    Check(saved->ReadInt() == 0, "Layer wire format changed");
    if (FileSave) {
        Check(saved->ReadInt() == 3 && saved->ReadDword() == registry->color.GetRGBA() && saved->ReadGuid() == registry->parameter,
              "Layer file version/color/GUID header changed");
    }
    Check(saved->ReadDword() == 1, "Layer wire visible flag changed");
    void *raw = nullptr;
    const int bytes = saved->ReadBuffer(&raw);
    const bool matched = bytes == sizeof(expected) && raw && std::memcmp(raw, expected, sizeof(expected)) == 0;
    CKDeletePointer(raw);
    Check(matched, "Layer packed cells changed four-byte stride or independent word values");
    for (int i = 0; i < 1024; ++i) Check(layer.GetSquareArray()[i].dval == expected[i], "Layer Save changed source cells");
    if (FileSave) Check(registry->remap[1] == 1 && registry->m_RemapCount == 2, "Successful Layer file Save did not commit its one type remap");
}

void LayerFitNoActionPreservesPointersWithoutAllocation() {
    CKContext context(nullptr, 0, 0);
    RCKGrid zero(&context, "ZeroFitGrid");
    RCKGrid sized(&context, "SizedFitGrid"); sized.SetDimensions(2, 1, 2.0f, 1.0f);
    RCKLayer layer(&context, "NoActionFitLayer", sized.GetID());
    CKSquare *owned = layer.GetSquareArray(); owned[0].dval = 17; owned[1].dval = 29;
    LayerArrayAllocationFailureScope allocation;
    layer.FitSquareArray(); // Already fitted.
    layer.SetOwner(0); layer.FitSquareArray(); // No owner.
    layer.SetOwner(zero.GetID()); layer.FitSquareArray(); // Owner dimensions pending.
    layer.SetOwner(sized.GetID()); layer.SetFormat(7); layer.FitSquareArray(); // Format does not store cells.
    Check(s_MorphArrayAllocationFailure == 0 && layer.GetSquareArray() == owned && owned[0].dval == 17 && owned[1].dval == 29,
          "A no-action owned Fit branch allocated or changed its cells");
    layer.SetFormat(0);
    CKSquare *transferred = new CKSquare[2](); transferred[0].dval = 41;
    layer.SetSquareArray(transferred); delete[] owned; layer.FitSquareArray();
    Check(s_MorphArrayAllocationFailure == 0 && layer.GetSquareArray() == transferred && transferred[0].dval == 41,
          "Fit resized an unknown-capacity transferred pointer");
}

void LayerFitRejectsUnrepresentableOwnerCountWithoutAllocation() {
    CKContext context(nullptr, 0, 0); new LayerRegistry(&context);
    RCKGrid grid(&context, "InvalidCountFitGrid");
    RCKLayer layer(&context, "InvalidCountFitLayer", 0);
    Chunk cells = LayerPreparedCellFixture(nullptr, 2); LoadChunk(layer, cells.get());
    CKSquare *previous = layer.GetSquareArray();
    grid.SetDimensions(INT_MAX, 2, 2.0f, 2.0f); layer.SetOwner(grid.GetID());
    LayerArrayAllocationFailureScope allocation;
    layer.FitSquareArray();
    Check(s_MorphArrayAllocationFailure == 0 && layer.GetSquareArray() == previous && previous[0].dval == 21 && previous[1].dval == 34,
          "Public Fit changed cells or attempted an unrepresentable allocation");
    Chunk data = NewChunk(CKCID_GRID);
    data->WriteIdentifier(CK_STATESAVE_GRIDDATA);
    data->WriteInt(INT_MAX); data->WriteInt(2); data->WriteInt(0); data->WriteInt(0); data->WriteDword(CKGRID_FREE); data->WriteInt(1);
    XObjectArray links; links.PushBack(layer.GetID()); links.Save(data.get(), &context);
    data->CloseChunk(); data->StartRead(); CKFile file(&context);
    Check(grid.Load(data.get(), &file) == CKERR_INVALIDPARAMETER && s_MorphArrayAllocationFailure == 0 && layer.GetSquareArray() == previous,
          "Checked Layer Fit did not propagate unrepresentable count through Grid Load");
    grid.SetDimensions(2, 1, 2.0f, 1.0f);
    layer.FitSquareArray(); CKDWORD last = 0; layer.GetValue(1, 0, &last);
    Check(s_MorphArrayAllocationFailure == 0 && last == 34, "Fitted cells were unusable after representable owner dimensions were restored");
}

template<bool FileLoad>
void LayerLoadsIndependentWordsAndReleasesPreviousCells() {
    CKContext context(nullptr, 0, 0); auto *registry = new LayerRegistry(&context);
    RCKGrid grid(&context, "IndependentLoadGrid"); grid.SetDimensions(2, 2, 2.0f, 2.0f);
    RCKLayer layer(&context, "IndependentLoadLayer", grid.GetID());
    CKSquare *previous = layer.GetSquareArray(); previous[0].dval = 999;
    const CKDWORD words[] = {0x12345678u, 0xFEDCBA98u, 0x80000000u, 0x7FFFFFFFu};
    const CKGUID parameter(0xA1B2C3D4, 0x55667788);
    Chunk data = NewChunk(CKCID_LAYER);
    data->WriteIdentifier(CK_STATESAVE_LAYERDATA); data->WriteObject(&grid);
    if (!FileLoad) data->WriteInt(73); // Native Load derives type from name instead.
    data->WriteInt(0);
    if (FileLoad) { data->WriteInt(3); data->WriteDword(0xFF336699u); data->WriteGuid(parameter); }
    data->WriteDword(0xA4); data->WriteBuffer_LEndian(sizeof(words), const_cast<CKDWORD *>(words));
    data->CloseChunk(); data->StartRead(); CKFile file(&context);
    LayerArrayReleaseObservationScope release(previous);
    Check(layer.Load(data.get(), FileLoad ? &file : nullptr) == CK_OK && s_LayerArrayObservedReleases == 1,
          "Independent Layer Load did not replace/release previous owned cells exactly once");
    Check(layer.GetType() == 1 && layer.GetFormat() == 0 && !layer.IsVisible() && layer.GetOwner() == grid.GetID(),
          "Independent Layer Load changed native type/name/format/flag/owner semantics");
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
        CKDWORD value = 0; layer.GetValue(x, y, &value);
        Check(value == words[y * 2 + x], "Independent Layer wire values changed row-major four-byte semantics");
    }
    if (FileLoad) Check(registry->parameter == parameter && registry->color.GetRGBA() == 0xFF336699u,
                        "Independent Layer file metadata changed");
}

// Pass 28 CKFile functional regressions.

static CKGUID Pass28FileParameterGuid(int variant) {
    return CKGUID(0x50323846u, 0x100u + variant);
}

static void Pass28RegisterFileParameterType(CKContext &context, int variant) {
    CKParameterTypeDesc description;
    description.Guid = Pass28FileParameterGuid(variant);
    description.TypeName = variant ? "Pass28 Other Integer" : "Pass28 Reference Integer";
    description.DefaultSize = sizeof(int);
    Check(context.GetParameterManager()->RegisterParameterType(&description) == CK_OK,
          "Pass28 parameter type registration failed");
}

class Pass28ReferenceMappingManager : public CKBaseManager {
public:
    explicit Pass28ReferenceMappingManager(CKContext *context)
        : CKBaseManager(context, CKGUID(0x50323846u, 0x200u), "Pass28 reference mapping"),
          SourceId(0), ActualId(0), Loaded(0), InSession(FALSE) {
        Check(context->RegisterNewManager(this) == CK_OK, "Pass28 mapping manager registration failed");
    }
    CKStateChunk *SaveData(CKFile *file) override {
        CKStateChunk *chunk = CreateCKStateChunk(CKCID_OBJECT, file);
        chunk->StartWrite();
        chunk->WriteIdentifier(0x50323846u);
        // Deliberately a raw source ID: RealId must translate it during LoadData.
        chunk->WriteDword(SourceId);
        chunk->CloseChunk();
        return chunk;
    }
    CKERROR LoadData(CKStateChunk *chunk, CKFile *) override {
        Check(chunk != nullptr, "Pass28 mapping manager chunk missing");
        chunk->StartRead();
        Check(chunk->SeekIdentifier(0x50323846u), "Pass28 mapping manager payload missing");
        SourceId = chunk->ReadDword();
        CKObjectManager *objects = static_cast<CKObjectManager *>(m_Context->GetManagerByGuid(OBJECT_MANAGER_GUID));
        Check(objects != nullptr, "Pass28 load callback object manager missing");
        InSession = objects->InLoadSession();
        ActualId = objects->RealId(SourceId);
        ++Loaded;
        return CK_OK;
    }
    CK_ID SourceId, ActualId;
    int Loaded;
    CKBOOL InSession;
};

template<bool Compressed, int MatchMode>
void FileReferenceHeaderRestoresSourceIdAndExistingParameter() {
    CKContext source(nullptr, 0, 0), destination(nullptr, 0, 0);
    Pass28RegisterFileParameterType(source, 0);
    Pass28RegisterFileParameterType(destination, 0);
    Pass28RegisterFileParameterType(destination, 1);
    for (int i = 0; i < 7; ++i) destination.CreateObject(CKCID_OBJECT, nullptr);
    CKParameterLocal *parameter = source.CreateCKParameterLocal("Pass28 Shared Parameter", Pass28FileParameterGuid(0));
    CKParameterLocal *existing = destination.CreateCKParameterLocal(
        MatchMode == 1 ? "Pass28 Different Name" : "Pass28 Shared Parameter", Pass28FileParameterGuid(MatchMode == 2 ? 1 : 0));
    Check(parameter && existing, "Pass28 reference parameter creation failed");
    const int sourceValue = 317, existingValue = 941;
    Check(parameter->SetValue(&sourceValue, sizeof(sourceValue)) == CK_OK &&
          existing->SetValue(&existingValue, sizeof(existingValue)) == CK_OK, "Pass28 reference value setup failed");
    Check(parameter->GetID() != existing->GetID(), "Pass28 reference fixture requires distinct object IDs");
    const CK_ID sourceId = parameter->GetID(), destinationId = existing->GetID();
    Pass28ReferenceMappingManager *savedManager = new Pass28ReferenceMappingManager(&source);
    Pass28ReferenceMappingManager *loadedManager = new Pass28ReferenceMappingManager(&destination);
    savedManager->SourceId = sourceId;
    TemporarySerializationFile temporary;
    source.SetFileWriteMode(Compressed ? CKFILE_WHOLECOMPRESSED : CKFILE_UNCOMPRESSED);
    DependencySaveFile saved(&source);
    CKFile loaded(&destination);
    Check(saved.StartSave(temporary.Path) == CK_OK, "Pass28 reference StartSave failed");
    saved.SaveObjectAsReference(parameter);
    saved.SaveObjectAsReference(parameter);
    saved.SaveObjectAsReference(nullptr);
    Check(saved.m_FileObjects.Size() == 1 && saved.m_ReferencedObjects.Size() == 1 &&
          saved.SaveFindObjectIndex(sourceId) == 0 && saved.m_FileObjects[0].SaveFlags == 0,
          "Pass28 duplicate reference changed its queue or ID mapping");
    Check(saved.EndSave() == CK_OK, "Pass28 reference EndSave failed");
    Check(!(parameter->GetObjectFlags() & CK_OBJECT_ONLYFORFILEREFERENCE), "Pass28 reference marker escaped EndSave");
    Check(loaded.OpenFile(temporary.Path) == CK_OK && loaded.m_FileObjects.Size() == 1,
          "Pass28 reference file header unavailable");
    Check(loaded.m_FileObjects[0].Object == (sourceId | 0x80000000u),
          "Pass28 reference header must encode the source ID with the highest bit");
    Check(loaded.LoadFileData(nullptr) == CK_OK, "Pass28 reference file load failed");
    CKFileObject &entry = loaded.m_FileObjects[0];
    Check(entry.ObjPtr == (MatchMode == 0 ? existing : nullptr) &&
          entry.CreatedObject == (MatchMode == 0 ? destinationId : 0),
          "Pass28 reference did not use exact parameter name and type matching");
    int actual = 0;
    Check(existing->GetValue(&actual) == CK_OK && actual == existingValue,
          "Pass28 reference load overwrote the existing parameter value");
    Check(loadedManager->Loaded == 1 && loadedManager->InSession && loadedManager->SourceId == sourceId,
          "Pass28 reference mapping callback did not run inside the real load session");
    Check(loadedManager->ActualId == (MatchMode == 0 ? destinationId : 0),
          "Pass28 reference lost the source-ID mapping used by manager load callbacks");
    Check(entry.Object == sourceId, "Pass28 reference source ID was decoded by arithmetic negation");
}

template<bool Compressed>
void FileSaveQueuePreservesPromotionFlagsDependenciesAndJobReset() {
    CKContext source(nullptr, 0, 0), destination(nullptr, 0, 0);
    new RCKRenderManager(&source);
    new RCKRenderManager(&destination);
    RCKTexture texture(&source, "Pass28 Promoted Texture");
    RCKMaterial material(&source, "Pass28 Promoted Material");
    material.SetTexture(0, &texture);
    material.SetPower(23.75f);
    for (int i = 0; i < 7; ++i) destination.CreateObject(CKCID_OBJECT, nullptr);
    TemporarySerializationFile temporary;
    source.SetFileWriteMode(Compressed ? CKFILE_WHOLECOMPRESSED : CKFILE_UNCOMPRESSED);
    DependencySaveFile saved(&source);
    CKFile loaded(&destination);
    for (int job = 0; job < 2; ++job) {
        Check(saved.StartSave(temporary.Path) == CK_OK, "Pass28 promotion StartSave failed");
        Check(saved.m_FileObjects.IsEmpty() && saved.m_ReferencedObjects.IsEmpty(), "Pass28 save job retained prior entries");
        saved.SaveObjectAsReference(&texture);
        saved.SaveObject(&material, CK_STATESAVE_ALL);
        saved.SaveObject(&material, 0);
        saved.SaveObjectAsReference(&material);
        saved.SaveObjectAsReference(&texture);
        Check(saved.m_FileObjects.Size() == 2 && saved.m_ReferencedObjects.IsEmpty(),
              "Pass28 promotion left a reference or duplicate dependency");
        Check(!saved.m_AlreadyReferencedMask.IsSet(texture.GetID()) && saved.m_AlreadySavedMask.IsSet(texture.GetID()),
              "Pass28 promotion did not transfer reference state into saved state");
        Check(saved.SaveFindObjectIndex(texture.GetID()) == 0 && saved.SaveFindObjectIndex(material.GetID()) == 1 &&
              saved.SaveFindObjectIndex(0x7fffffffu) == -1, "Pass28 save index does not map source IDs to queue positions");
        for (int i = 0; i < 2; ++i) {
            CKFileObject &entry = saved.m_FileObjects[i];
            Check(entry.SaveFlags == CK_STATESAVE_ALL && saved.m_IndexByClassId[entry.ObjectCid].Size() == 1 &&
                  saved.m_IndexByClassId[entry.ObjectCid][0] == i, "Pass28 promotion flags or per-class index changed");
        }
        Check(saved.EndSave() == CK_OK && loaded.Load(temporary.Path, nullptr) == CK_OK, "Pass28 promoted file cycle failed");
        RCKTexture *newTexture = nullptr;
        RCKMaterial *newMaterial = nullptr;
        for (int i = 0; i < loaded.m_FileObjects.Size(); ++i) {
            CKFileObject &entry = loaded.m_FileObjects[i];
            Check(!(entry.Object & 0x80000000u) && entry.ObjPtr, "Pass28 promoted dependency remained reference-only on wire");
            if (entry.ObjectCid == CKCID_TEXTURE) newTexture = static_cast<RCKTexture *>(entry.ObjPtr);
            if (entry.ObjectCid == CKCID_MATERIAL) newMaterial = static_cast<RCKMaterial *>(entry.ObjPtr);
        }
        Check(newTexture && newMaterial && newMaterial->GetTexture() == newTexture &&
              newMaterial->GetPower() == material.GetPower(), "Pass28 promoted file lost dependency remapping or material fields");
        material.SetPower(51.5f);
    }
}

void FileSaveQueueExcludesDynamicAndNotSavedObjects() {
    CKContext source(nullptr, 0, 0), destination(nullptr, 0, 0);
    CKObject *dynamic = source.CreateObject(CKCID_OBJECT, "Pass28 Dynamic", CK_OBJECTCREATION_DYNAMIC);
    CKObject *excluded = source.CreateObject(CKCID_OBJECT, "Pass28 Excluded");
    CKObject *accepted = source.CreateObject(CKCID_OBJECT, "Pass28 Accepted");
    Check(dynamic && excluded && accepted && dynamic->IsDynamic(), "Pass28 exclusion fixture setup failed");
    excluded->ModifyObjectFlags(CK_OBJECT_NOTTOBESAVED, 0);
    TemporarySerializationFile temporary;
    DependencySaveFile saved(&source);
    CKFile loaded(&destination);
    Check(saved.StartSave(temporary.Path) == CK_OK, "Pass28 exclusion StartSave failed");
    saved.SaveObjectAsReference(dynamic); saved.SaveObjectAsReference(excluded); saved.SaveObjectAsReference(nullptr);
    Check(saved.m_FileObjects.IsEmpty() && saved.m_ReferencedObjects.IsEmpty() &&
          !saved.m_AlreadyReferencedMask.IsSet(dynamic->GetID()) && !saved.m_AlreadyReferencedMask.IsSet(excluded->GetID()),
          "Pass28 excluded reference mutated queue state");
    saved.SaveObject(dynamic); saved.SaveObject(excluded); saved.SaveObject(nullptr);
    Check(saved.m_FileObjects.IsEmpty() && saved.m_AlreadySavedMask.IsSet(dynamic->GetID()) &&
          saved.m_AlreadySavedMask.IsSet(excluded->GetID()), "Pass28 excluded save differs from native saved-mask ordering");
    saved.SaveObject(accepted); saved.SaveObject(accepted, 0);
    Check(saved.m_FileObjects.Size() == 1 && saved.SaveFindObjectIndex(accepted->GetID()) == 0 &&
          saved.m_SaveIDMax == accepted->GetID(), "Pass28 accepted save queue or max ID incorrect");
    Check(saved.EndSave() == CK_OK && loaded.Load(temporary.Path, nullptr) == CK_OK && loaded.m_FileObjects.Size() == 1,
          "Pass28 exclusion file cycle failed");
    Check(loaded.m_FileObjects[0].ObjPtr && std::strcmp(loaded.m_FileObjects[0].ObjPtr->GetName(), "Pass28 Accepted") == 0,
          "Pass28 full file created an excluded object or lost the accepted object");
}

class Pass28PreSaveObject : public CKObject {
public:
    Pass28PreSaveObject(CKContext *context, CKObject *dependency)
        : CKObject(context, "Pass28 Before Callback"), Dependency(dependency), PreSaved(0), Saved(0),
          PreFlags(0), SavedFlags(0), OwnIndex(0) {}
    void PreSave(CKFile *file, CKDWORD flags) override {
        ++PreSaved;
        PreFlags = flags;
        OwnIndex = static_cast<DependencySaveFile *>(file)->SaveFindObjectIndex(GetID());
        file->SaveObject(this, 0); // Native saved-mask ordering stops recursion.
        file->SaveObject(Dependency, flags);
        SetName("Pass28 After Callback");
    }
    CKStateChunk *Save(CKFile *file, CKDWORD flags) override {
        ++Saved;
        SavedFlags = flags;
        CKStateChunk *chunk = CKObject::Save(file, flags);
        chunk->WriteIdentifier(0x50323847u);
        chunk->WriteDword(flags);
        chunk->CloseChunk();
        return chunk;
    }
    CKObject *Dependency;
    int PreSaved, Saved;
    CKDWORD PreFlags, SavedFlags;
    int OwnIndex;
};

void FileSaveNonLevelPreSaveRunsBeforeEntryAndKeepsFirstFlags() {
    CKContext source(nullptr, 0, 0), destination(nullptr, 0, 0);
    CKObject dependency(&source, "Pass28 Callback Dependency");
    Pass28PreSaveObject object(&source, &dependency);
    object.Show(CKHIDE);
    const CKDWORD firstFlags = 0x12340000u;
    TemporarySerializationFile temporary;
    DependencySaveFile saved(&source);
    CKFile loaded(&destination);
    Check(saved.StartSave(temporary.Path) == CK_OK, "Pass28 callback StartSave failed");
    saved.SaveObject(&object, firstFlags);
    saved.SaveObject(&object, CK_STATESAVE_ALL);
    Check(object.PreSaved == 1 && object.OwnIndex == -1 && object.PreFlags == firstFlags,
          "Pass28 non-Level PreSave order or duplicate callback flags changed");
    Check(saved.m_FileObjects.Size() == 2 && saved.SaveFindObjectIndex(dependency.GetID()) == 0 &&
          saved.SaveFindObjectIndex(object.GetID()) == 1 && saved.m_FileObjects[1].SaveFlags == firstFlags &&
          std::strcmp(saved.m_FileObjects[1].Name, "Pass28 After Callback") == 0,
          "Pass28 non-Level callback dependency/name was not captured after PreSave");
    Check(saved.EndSave() == CK_OK && object.Saved == 1 && object.SavedFlags == firstFlags,
          "Pass28 EndSave did not deliver the first accepted save flags exactly once");
    Check(loaded.OpenFile(temporary.Path) == CK_OK && loaded.LoadFileData(nullptr) == CK_OK,
          "Pass28 callback full-file load failed");
    CKObject *newObject = destination.GetObjectByName("Pass28 After Callback");
    Check(newObject && newObject->IsVisible() == CKHIDE && destination.GetObjectByName("Pass28 Callback Dependency"),
          "Pass28 callback file did not preserve its post-callback name, visibility and dependency");
}

void FileSaveLevelEntryExistsBeforePreSaveAndSceneReferencePromotes() {
    CKContext source(nullptr, 0, 0), destination(nullptr, 0, 0);
    new RCKRenderManager(&source);
    new RCKRenderManager(&destination);
    CKLevel *level = static_cast<CKLevel *>(source.CreateObject(CKCID_LEVEL, "Pass28 Level"));
    CKScene *scene = static_cast<CKScene *>(source.CreateObject(CKCID_SCENE, "Pass28 Additional Scene"));
    RCKMaterial *entity = static_cast<RCKMaterial *>(source.CreateObject(CKCID_MATERIAL, "Pass28 Scene Material"));
    Check(level && scene && entity && level->AddScene(scene) == CK_OK, "Pass28 Level scene fixture setup failed");
    entity->Show(CKHIDE);
    entity->SetPower(13.5f);
    scene->AddObjectToScene(entity, FALSE);
    Check(scene->IsObjectHere(entity), "Pass28 Level dependency not attached to its Scene");
    TemporarySerializationFile temporary;
    DependencySaveFile saved(&source);
    CKFile loaded(&destination);
    Check(saved.StartSave(temporary.Path) == CK_OK && !saved.m_SceneSaved, "Pass28 Level StartSave state incorrect");
    saved.SaveObjectAsReference(scene);
    Check(saved.m_SceneSaved && saved.m_ReferencedObjects.Size() == 1 &&
          saved.m_FileObjects[0].SaveFlags == 0, "Pass28 Scene reference did not set scene-save state");
    saved.SaveObject(level);
    saved.SaveObject(level);
    // The Scene already occupies slot zero. Its new entity dependency must be
    // appended after the Level entry, showing the Level-specific callback order.
    Check(saved.SaveFindObjectIndex(level->GetID()) == 1 && saved.SaveFindObjectIndex(entity->GetID()) == 2 &&
          saved.m_FileObjects.Size() == 3 && saved.m_SceneSaved &&
          saved.m_ReferencedObjects.IsEmpty() && !saved.m_AlreadyReferencedMask.IsSet(scene->GetID()),
          "Pass28 Level entry order or recursive Scene reference promotion changed");
    Check(saved.EndSave() == CK_OK && loaded.Load(temporary.Path, nullptr) == CK_OK,
          "Pass28 Level/Scene full-file cycle failed");
    CKLevel *newLevel = nullptr;
    CKScene *newScene = nullptr;
    for (int i = 0; i < loaded.m_FileObjects.Size(); ++i) {
        CKFileObject &entry = loaded.m_FileObjects[i];
        if (entry.ObjectCid == CKCID_LEVEL) newLevel = static_cast<CKLevel *>(entry.ObjPtr);
        if (entry.ObjectCid == CKCID_SCENE && entry.Name && std::strcmp(entry.Name, "Pass28 Additional Scene") == 0)
            newScene = static_cast<CKScene *>(entry.ObjPtr);
    }
    CKObject *newEntity = destination.GetObjectByName("Pass28 Scene Material");
    Check(newLevel && newScene && newLevel->GetSceneCount() == 1 && newLevel->GetScene(0) == newScene && newLevel->GetLevelScene() &&
          newEntity && newEntity->IsVisible() == CKHIDE && newScene->IsObjectHere(newEntity) &&
          static_cast<RCKMaterial *>(newEntity)->GetPower() == 13.5f,
          "Pass28 Level saved dependencies did not remap its scenes and default scene");
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
        {"Runtime Grid SDK recreation uncompressed file", RuntimeGridRecreationPreservesDimensionsAndFileCells<false>},
        {"Runtime Grid SDK recreation compressed file", RuntimeGridRecreationPreservesDimensionsAndFileCells<true>},
    };
    const Test tests[] = {




        {"Object transition velocity handles zero duration", ObjectTransitionVelocityHandlesShortDuration<true>},
        {"Object transition velocity handles subframe duration", ObjectTransitionVelocityHandlesShortDuration<false>},




        {"Object playback existing duration and merged Morph maximum", ObjectPlaybackDurationAndMergedMorphMaximumUseExistingControllers},
        {"Object playback merged in-place transition source capture", ObjectPlaybackMergedInPlaceTransitionCapturesLiveSourcePose},
        {"Object playback transition typed velocity reuse", ObjectPlaybackTransitionVelocityReusesMatchingTypedController},
        {"Object playback transition unsupported Morph sources", ObjectPlaybackTransitionMorphUnsupportedSourcesRetainPreviousChannel},
        {"Object playback native merged creation flags detached phase", ObjectPlaybackMergedCreationFlagsAndDetachedPhaseRespectContract},

        {"Legacy snapshot stops at identifier", &LegacyTransformRecordsStopAtTheirIdentifier<1>},
        {"Legacy v0 position stops at identifier", &LegacyTransformRecordsStopAtTheirIdentifier<0>},
        {"Legacy v0 axis stops at identifier", &LegacyV0RotationAxisStopsAtItsIdentifier},
        {"Legacy snapshot Morph positions cover declared vertices", &LegacyMorphPositionsCoverDeclaredVertices<1>},
        {"Legacy v0 Morph positions cover declared vertices", &LegacyMorphPositionsCoverDeclaredVertices<0>},
        {"Legacy compressed normals cover declared vertices", &LegacyNormalsCoverDeclaredVertices<1>},
        {"Legacy converted normals cover declared vertices", &LegacyNormalsCoverDeclaredVertices<2>},
        {"Legacy compressed normals stop at identifier", &LegacyNormalRecordsStopAtTheirIdentifier<1>},
        {"Legacy converted normals stop at identifier", &LegacyNormalRecordsStopAtTheirIdentifier<2>},
        {"Object playback empty info key shortcuts comparison", ObjectPlaybackInfoEmptyControllersAndKeyShortcutsUseContracts},
        {"Object playback transition in-place Morph", ObjectPlaybackTransitionInPlaceMorphCapturesBeforeReplacement},

        {"Object playback partial pose and child hierarchy", ObjectPlaybackPartialChannelsPreserveEntityPoseAndHierarchy},
        {"Object playback exclusive body part and character batch", ObjectPlaybackExclusiveBodyPartAndCharacterBatchRespectPose},
        {"Object playback Morph mesh vertices and normals", ObjectPlaybackMorphSetStepPublishesVerticesAndNormals},
        {"Object playback transition typed controller reuse", ObjectPlaybackTransitionReusesTypedChannelsAndRebuildsTangents},
        {"Object playback transition sparse and empty channels", ObjectPlaybackTransitionMissingAndEmptyChannelsFollowIntersection},
        {"Object playback transition Morph timeline and mesh fallback", ObjectPlaybackTransitionMorphReuseResetsTimelineAndMeshFallbackNormals},
        {"Object playback merged public factory and channels", ObjectPlaybackMergedPublicAPIBlendsControllersAndTracksInfo},


        {"ObjectAnimation Copy original controller fixtures", ObjectAnimationCopyRetainsOriginalControllerFixtures},
        {"Object playback transition removes source yaw", ObjectPlaybackTransitionDontTurnUsesInverseSourceYaw},
        {"Modern ObjectAnimation Load propagates factory allocation failure", ModernAnimationLoadPropagatesControllerFactoryFailure},

        {"Legacy snapshot partial axis uses native identity", LegacyScaleAxisPartialPayloadRetainsNativeIdentity<1>},
        {"Legacy v0 partial axis uses native identity", LegacyScaleAxisPartialPayloadRetainsNativeIdentity<0>},
        {"ObjectAnimation lifecycle defaults and registration", ObjectAnimationLifecycleDefaultsAndRegistration},
        {"ObjectAnimation Clear shared data and reserved state", ObjectAnimationClearPreservesSharedOwnershipAndReservedState<false>},
        {"ObjectAnimation ClearAll shared data and reserved state", ObjectAnimationClearPreservesSharedOwnershipAndReservedState<true>},
        {"ObjectAnimation Reset and shared length", ObjectAnimationResetAndLengthPropagateThroughAliases},
        {"ObjectAnimation null sharing allocation failure", ObjectAnimationNullShareAllocationFailurePreservesAliases},
        {"ObjectAnimation native factory and Delete dispatch", ObjectAnimationControllerFactoryAndDeleteFollowNativeDispatch},
        {"ObjectAnimation LINPOS factory allocation failure", ObjectAnimationFactoryAllocationFailureKeepsExistingController<CKANIMATION_LINPOS_CONTROL>},
        {"ObjectAnimation LINROT factory allocation failure", ObjectAnimationFactoryAllocationFailureKeepsExistingController<CKANIMATION_LINROT_CONTROL>},
        {"ObjectAnimation LINSCL factory allocation failure", ObjectAnimationFactoryAllocationFailureKeepsExistingController<CKANIMATION_LINSCL_CONTROL>},
        {"ObjectAnimation LINAXIS factory allocation failure", ObjectAnimationFactoryAllocationFailureKeepsExistingController<CKANIMATION_LINSCLAXIS_CONTROL>},
        {"ObjectAnimation TCBPOS factory allocation failure", ObjectAnimationFactoryAllocationFailureKeepsExistingController<CKANIMATION_TCBPOS_CONTROL>},
        {"ObjectAnimation TCBROT factory allocation failure", ObjectAnimationFactoryAllocationFailureKeepsExistingController<CKANIMATION_TCBROT_CONTROL>},
        {"ObjectAnimation TCBSCL factory allocation failure", ObjectAnimationFactoryAllocationFailureKeepsExistingController<CKANIMATION_TCBSCL_CONTROL>},
        {"ObjectAnimation TCBAXIS factory allocation failure", ObjectAnimationFactoryAllocationFailureKeepsExistingController<CKANIMATION_TCBSCLAXIS_CONTROL>},
        {"ObjectAnimation BEZIERPOS factory allocation failure", ObjectAnimationFactoryAllocationFailureKeepsExistingController<CKANIMATION_BEZIERPOS_CONTROL>},
        {"ObjectAnimation BEZIERSCL factory allocation failure", ObjectAnimationFactoryAllocationFailureKeepsExistingController<CKANIMATION_BEZIERSCL_CONTROL>},
        {"ObjectAnimation MORPH factory allocation failure", ObjectAnimationFactoryAllocationFailureKeepsExistingController<CKANIMATION_MORPH_CONTROL>},
        {"ObjectAnimation Copy Character SHAREANIMS fields", ObjectAnimationCopyUsesCharacterOptionAndCopiesNativePlaybackFields<true>},
        {"ObjectAnimation Copy independent typed data fields", ObjectAnimationCopyUsesCharacterOptionAndCopiesNativePlaybackFields<false>},
        {"ObjectAnimation Copy data allocation failure", ObjectAnimationDeepCopyFailurePreservesAnimationDataAndAliases<true>},
        {"ObjectAnimation Copy transform and Morph allocation failures", ObjectAnimationDeepCopyFailurePreservesAnimationDataAndAliases<false>},
        {"ObjectAnimation dependency finish and native remapping", ObjectAnimationDependenciesFinishAndRemapOnlyNativeReferences},
        {"ObjectAnimation PreDeletion and object references", ObjectAnimationPreDeletionAndObjectUseMatchNativeReferences},
        {"Legacy snapshot all native key layouts", LegacyObjectAnimationLoadsAllNativeKeyLayouts<1, false>},
        {"Legacy v0 all native key layouts", LegacyObjectAnimationLoadsAllNativeKeyLayouts<0, false>},
        {"Legacy snapshot preserves supplied raw key order", LegacyObjectAnimationLoadsAllNativeKeyLayouts<1, true>},
        {"Legacy v0 preserves supplied raw key order", LegacyObjectAnimationLoadsAllNativeKeyLayouts<0, true>},
        {"Legacy normal-only empty clears payload", LegacyNormalOnlySnapshotUpdatesExistingMorph<0>},
        {"Legacy normal-only compressed payload", LegacyNormalOnlySnapshotUpdatesExistingMorph<1>},
        {"Legacy normal-only converted payload", LegacyNormalOnlySnapshotUpdatesExistingMorph<2>},
        {"Legacy normal-only converted precedence", LegacyNormalOnlySnapshotUpdatesExistingMorph<3>},
        {"Legacy snapshot array allocation stages", LegacyKeyArrayAllocationFailuresReturnErrorAndRemainDestructible<1>},
        {"Legacy v0 array allocation stages", LegacyKeyArrayAllocationFailuresReturnErrorAndRemainDestructible<0>},
        {"Legacy compressed normal allocation retains payload", LegacyNormalAllocationFailureKeepsTheFailingKeyPayload<1>},
        {"Legacy converted normal allocation retains payload", LegacyNormalAllocationFailureKeepsTheFailingKeyPayload<2>},
        {"Object playback native empty and ignored values", ObjectPlaybackEmptyAndIgnoredValuesFollowNative},
        {"Object playback direct keys flags and duration", ObjectPlaybackDirectKeysFlagsAndLengthUsePublicAPI},
        {"Object playback animated scale axis", ObjectPlaybackSetStepComposesScaleAxisAndKeepsPosition},
        {"Object playback keyed root centering", ObjectPlaybackRootCenterOffsetReachesEntity},
        {"Object playback frame phase velocity and binding", ObjectPlaybackStepFrameIgnoreAndVelocityUseEntity},
        {"Object playback transition starting pose", ObjectPlaybackTransitionUsesProvidedStartingSet},
        {"Object playback transition integrated velocity", ObjectPlaybackTransitionVelocityBuildsIntegratedKeys},
        {"Object playback scale fixup and directed comparison", ObjectPlaybackCheckScaleAndComparePreserveNativeDirectionality},
        {"Shared reload transfers owner and preserves old alias", SharedLoadMovesOwnerAndPreservesRemainingAlias},
        {"Shared self reference preserves keys and clears state", SharedLoadSelfReferenceKeepsDataAndResetsState},
        {"Shared missing-owner Load propagates allocation failure", SharedLoadMissingOwnerAllocationFailureKeepsData},
        {"Shared Load rejects short entity header", SharedLoadRejectsTruncatedIdentifier<false>},
        {"Shared Load rejects short merged references", SharedLoadRejectsTruncatedIdentifier<true>},
        {"Shared Load restores root and entity", SharedLoadRestoresEntityRootAndMergedReferences<false>},
        {"Shared Load restores root entity and merged references", SharedLoadRestoresEntityRootAndMergedReferences<true>},
        {"ObjectAnimation Save applies native selective mask", AnimationSaveSelectiveFlagsUseNativeMask},
        {"LINPOS rejects invalid dump", TransformDumpRejectsInvalidStateBeforeWriting<RCKLinearPositionController, CKANIMATION_LINPOS_CONTROL>},
        {"LINROT rejects invalid dump", TransformDumpRejectsInvalidStateBeforeWriting<RCKLinearRotationController, CKANIMATION_LINROT_CONTROL>},
        {"LINSCL rejects invalid dump", TransformDumpRejectsInvalidStateBeforeWriting<RCKLinearScaleController, CKANIMATION_LINSCL_CONTROL>},
        {"LINSCLAXIS rejects invalid dump", TransformDumpRejectsInvalidStateBeforeWriting<RCKLinearScaleAxisController, CKANIMATION_LINSCLAXIS_CONTROL>},
        {"TCBPOS rejects invalid dump", TransformDumpRejectsInvalidStateBeforeWriting<RCKTCBPositionController, CKANIMATION_TCBPOS_CONTROL>},
        {"TCBROT rejects invalid dump", TransformDumpRejectsInvalidStateBeforeWriting<RCKTCBRotationController, CKANIMATION_TCBROT_CONTROL>},
        {"TCBSCL rejects invalid dump", TransformDumpRejectsInvalidStateBeforeWriting<RCKTCBScaleController, CKANIMATION_TCBSCL_CONTROL>},
        {"TCBSCLAXIS rejects invalid dump", TransformDumpRejectsInvalidStateBeforeWriting<RCKTCBScaleAxisController, CKANIMATION_TCBSCLAXIS_CONTROL>},
        {"BEZIERPOS rejects invalid dump", TransformDumpRejectsInvalidStateBeforeWriting<RCKBezierPositionController, CKANIMATION_BEZIERPOS_CONTROL>},
        {"BEZIERSCL rejects invalid dump", TransformDumpRejectsInvalidStateBeforeWriting<RCKBezierScaleController, CKANIMATION_BEZIERSCL_CONTROL>},
        {"LINPOS retains valid dump", TransformDumpPreservesValidWireAndOutputBounds<RCKLinearPositionController, CKANIMATION_LINPOS_CONTROL>},
        {"LINROT retains valid dump", TransformDumpPreservesValidWireAndOutputBounds<RCKLinearRotationController, CKANIMATION_LINROT_CONTROL>},
        {"LINSCL retains valid dump", TransformDumpPreservesValidWireAndOutputBounds<RCKLinearScaleController, CKANIMATION_LINSCL_CONTROL>},
        {"LINSCLAXIS retains valid dump", TransformDumpPreservesValidWireAndOutputBounds<RCKLinearScaleAxisController, CKANIMATION_LINSCLAXIS_CONTROL>},
        {"TCBPOS retains valid dump", TransformDumpPreservesValidWireAndOutputBounds<RCKTCBPositionController, CKANIMATION_TCBPOS_CONTROL>},
        {"TCBROT retains valid dump", TransformDumpPreservesValidWireAndOutputBounds<RCKTCBRotationController, CKANIMATION_TCBROT_CONTROL>},
        {"TCBSCL retains valid dump", TransformDumpPreservesValidWireAndOutputBounds<RCKTCBScaleController, CKANIMATION_TCBSCL_CONTROL>},
        {"TCBSCLAXIS retains valid dump", TransformDumpPreservesValidWireAndOutputBounds<RCKTCBScaleAxisController, CKANIMATION_TCBSCLAXIS_CONTROL>},
        {"BEZIERPOS retains valid dump", TransformDumpPreservesValidWireAndOutputBounds<RCKBezierPositionController, CKANIMATION_BEZIERPOS_CONTROL>},
        {"BEZIERSCL retains valid dump", TransformDumpPreservesValidWireAndOutputBounds<RCKBezierScaleController, CKANIMATION_BEZIERSCL_CONTROL>},
        {"Animation Save rejects slot 0 failure without changing owner", AnimationSaveRejectsControllerFailuresAndPreservesOwner<0>},
        {"Animation Save rejects slot 1 failure without changing owner", AnimationSaveRejectsControllerFailuresAndPreservesOwner<1>},
        {"Animation Save rejects slot 2 failure without changing owner", AnimationSaveRejectsControllerFailuresAndPreservesOwner<2>},
        {"Animation Save rejects slot 3 failure without changing owner", AnimationSaveRejectsControllerFailuresAndPreservesOwner<3>},
        {"Animation Save rejects slot 4 failure without changing owner", AnimationSaveRejectsControllerFailuresAndPreservesOwner<4>},
        {"Animation Save rejects aggregate controller overflow", AnimationSaveRejectsAggregateControllerOverflow},
        {"Animation Save commits ownership only for successful full data", AnimationSaveCommitsOwnerOnlyForFullSuccessfulData},
        {"Animation memory estimate rejects invalid or overflowing sizes", AnimationMemoryEstimateRejectsInvalidOrOverflowingControllerSizes},
        {"Animation memory estimate counts owned data and scene storage", AnimationMemoryEstimateCountsOwnedDataAndSceneStorage},
        {"Linear position edit allocation failures", LinearEditAllocationFailuresPreserveState<RCKLinearPositionController, CKPositionKey>},
        {"Linear rotation edit allocation failures", LinearEditAllocationFailuresPreserveState<RCKLinearRotationController, CKRotationKey>},
        {"Linear scale edit allocation failures", LinearEditAllocationFailuresPreserveState<RCKLinearScaleController, CKScaleKey>},
        {"Linear scale-axis edit allocation failures", LinearEditAllocationFailuresPreserveState<RCKLinearScaleAxisController, CKScaleAxisKey>},
        {"Linear position edit loaded duplicates and payloads", LinearEditUpdatesFirstLoadedDuplicateAndPreservesPayload<RCKLinearPositionController, CKPositionKey>},
        {"Linear rotation edit loaded duplicates and payloads", LinearEditUpdatesFirstLoadedDuplicateAndPreservesPayload<RCKLinearRotationController, CKRotationKey>},
        {"Linear scale edit loaded duplicates and payloads", LinearEditUpdatesFirstLoadedDuplicateAndPreservesPayload<RCKLinearScaleController, CKScaleKey>},
        {"Linear scale-axis edit loaded duplicates and payloads", LinearEditUpdatesFirstLoadedDuplicateAndPreservesPayload<RCKLinearScaleAxisController, CKScaleAxisKey>},
        {"Linear position edit ordering duplicates and length", LinearEditPreservesOrderingDuplicatesAndLength<RCKLinearPositionController, CKPositionKey>},
        {"Linear rotation edit ordering duplicates and length", LinearEditPreservesOrderingDuplicatesAndLength<RCKLinearRotationController, CKRotationKey>},
        {"Linear scale edit ordering duplicates and length", LinearEditPreservesOrderingDuplicatesAndLength<RCKLinearScaleController, CKScaleKey>},
        {"Linear scale-axis edit ordering duplicates and length", LinearEditPreservesOrderingDuplicatesAndLength<RCKLinearScaleAxisController, CKScaleAxisKey>},

        {"TCBPOS Add allocation atomicity", TCBAddAllocationFailureIsAtomic<RCKTCBPositionController, CKANIMATION_TCBPOS_CONTROL>},
        {"TCBPOS Remove allocation atomicity", TCBRemoveAllocationFailureIsAtomic<RCKTCBPositionController, CKANIMATION_TCBPOS_CONTROL>},
        {"TCBPOS native duplicate parameters and length", TCBValidEditsPreserveNativeDuplicateParametersAndLength<RCKTCBPositionController, CKANIMATION_TCBPOS_CONTROL>},
        {"TCBPOS tangent rebuild allocation atomicity", TCBTangentRebuildAllocationFailurePreservesCache<RCKTCBPositionController, CKANIMATION_TCBPOS_CONTROL>},
        {"TCBPOS Evaluate tangent failure and endpoints", TCBEvaluatePropagatesTangentFailureWithoutWritingOutput<RCKTCBPositionController, CKANIMATION_TCBPOS_CONTROL>},
        {"TCBPOS finite overflowing time phase", TCBEvaluateRetainsFinitePhaseAcrossOverflowingTimeDifference<RCKTCBPositionController, CKANIMATION_TCBPOS_CONTROL>},
        {"TCBROT Add allocation atomicity", TCBAddAllocationFailureIsAtomic<RCKTCBRotationController, CKANIMATION_TCBROT_CONTROL>},
        {"TCBROT Remove allocation atomicity", TCBRemoveAllocationFailureIsAtomic<RCKTCBRotationController, CKANIMATION_TCBROT_CONTROL>},
        {"TCBROT native duplicate parameters and length", TCBValidEditsPreserveNativeDuplicateParametersAndLength<RCKTCBRotationController, CKANIMATION_TCBROT_CONTROL>},
        {"TCBROT tangent rebuild allocation atomicity", TCBTangentRebuildAllocationFailurePreservesCache<RCKTCBRotationController, CKANIMATION_TCBROT_CONTROL>},
        {"TCBROT Evaluate tangent failure and endpoints", TCBEvaluatePropagatesTangentFailureWithoutWritingOutput<RCKTCBRotationController, CKANIMATION_TCBROT_CONTROL>},
        {"TCBROT finite overflowing time phase", TCBEvaluateRetainsFinitePhaseAcrossOverflowingTimeDifference<RCKTCBRotationController, CKANIMATION_TCBROT_CONTROL>},
        {"TCBSCL Add allocation atomicity", TCBAddAllocationFailureIsAtomic<RCKTCBScaleController, CKANIMATION_TCBSCL_CONTROL>},
        {"TCBSCL Remove allocation atomicity", TCBRemoveAllocationFailureIsAtomic<RCKTCBScaleController, CKANIMATION_TCBSCL_CONTROL>},
        {"TCBSCL native duplicate parameters and length", TCBValidEditsPreserveNativeDuplicateParametersAndLength<RCKTCBScaleController, CKANIMATION_TCBSCL_CONTROL>},
        {"TCBSCL tangent rebuild allocation atomicity", TCBTangentRebuildAllocationFailurePreservesCache<RCKTCBScaleController, CKANIMATION_TCBSCL_CONTROL>},
        {"TCBSCL Evaluate tangent failure and endpoints", TCBEvaluatePropagatesTangentFailureWithoutWritingOutput<RCKTCBScaleController, CKANIMATION_TCBSCL_CONTROL>},
        {"TCBSCL finite overflowing time phase", TCBEvaluateRetainsFinitePhaseAcrossOverflowingTimeDifference<RCKTCBScaleController, CKANIMATION_TCBSCL_CONTROL>},
        {"TCBSCLAXIS Add allocation atomicity", TCBAddAllocationFailureIsAtomic<RCKTCBScaleAxisController, CKANIMATION_TCBSCLAXIS_CONTROL>},
        {"TCBSCLAXIS Remove allocation atomicity", TCBRemoveAllocationFailureIsAtomic<RCKTCBScaleAxisController, CKANIMATION_TCBSCLAXIS_CONTROL>},
        {"TCBSCLAXIS native duplicate parameters and length", TCBValidEditsPreserveNativeDuplicateParametersAndLength<RCKTCBScaleAxisController, CKANIMATION_TCBSCLAXIS_CONTROL>},
        {"TCBSCLAXIS tangent rebuild allocation atomicity", TCBTangentRebuildAllocationFailurePreservesCache<RCKTCBScaleAxisController, CKANIMATION_TCBSCLAXIS_CONTROL>},
        {"TCBSCLAXIS Evaluate tangent failure and endpoints", TCBEvaluatePropagatesTangentFailureWithoutWritingOutput<RCKTCBScaleAxisController, CKANIMATION_TCBSCLAXIS_CONTROL>},
        {"TCBSCLAXIS finite overflowing time phase", TCBEvaluateRetainsFinitePhaseAcrossOverflowingTimeDifference<RCKTCBScaleAxisController, CKANIMATION_TCBSCLAXIS_CONTROL>},
        {"BEZIERPOS Add preserves OOM state", BezierAddAllocationFailurePreservesState<RCKBezierPositionController, CKANIMATION_BEZIERPOS_CONTROL>},
        {"BEZIERSCL Add preserves OOM state", BezierAddAllocationFailurePreservesState<RCKBezierScaleController, CKANIMATION_BEZIERSCL_CONTROL>},
        {"BEZIERPOS Remove preserves OOM state", BezierRemoveAllocationFailurePreservesState<RCKBezierPositionController, CKANIMATION_BEZIERPOS_CONTROL>},
        {"BEZIERSCL Remove preserves OOM state", BezierRemoveAllocationFailurePreservesState<RCKBezierScaleController, CKANIMATION_BEZIERSCL_CONTROL>},
        {"BEZIERPOS native duplicate and tangent contract", BezierLegalEditsFollowNativeValueAndTangentContract<RCKBezierPositionController, CKANIMATION_BEZIERPOS_CONTROL>},
        {"BEZIERSCL native duplicate and tangent contract", BezierLegalEditsFollowNativeValueAndTangentContract<RCKBezierScaleController, CKANIMATION_BEZIERSCL_CONTROL>},
        {"Morph edit insertion failures preserve owned arrays", MorphEditInsertionFailuresPreserveEveryOwnedArray},
        {"Morph edit removal failure preserves payloads", MorphEditRemovalFailurePreservesPayloads},
        {"Morph native duplicate and mixed payload contract", MorphEditPreservesNativeDuplicateAndMixedPayloadContract},
        {"Morph time-only insertion propagates allocation failure", MorphEditWrapperPropagatesAllocationFailure<true>},
        {"Morph generic insertion propagates allocation failure", MorphEditWrapperPropagatesAllocationFailure<false>},

        {"LINPOS bounded record", LoadedTransformRespectsEveryRecordBoundary<CKANIMATION_LINPOS_CONTROL>},
        {"LINROT bounded record", LoadedTransformRespectsEveryRecordBoundary<CKANIMATION_LINROT_CONTROL>},
        {"LINSCL bounded record", LoadedTransformRespectsEveryRecordBoundary<CKANIMATION_LINSCL_CONTROL>},
        {"LINSCLAXIS bounded record", LoadedTransformRespectsEveryRecordBoundary<CKANIMATION_LINSCLAXIS_CONTROL>},
        {"TCBPOS bounded record", LoadedTransformRespectsEveryRecordBoundary<CKANIMATION_TCBPOS_CONTROL>},
        {"TCBROT bounded record", LoadedTransformRespectsEveryRecordBoundary<CKANIMATION_TCBROT_CONTROL>},
        {"TCBSCL bounded record", LoadedTransformRespectsEveryRecordBoundary<CKANIMATION_TCBSCL_CONTROL>},
        {"TCBSCLAXIS bounded record", LoadedTransformRespectsEveryRecordBoundary<CKANIMATION_TCBSCLAXIS_CONTROL>},
        {"BEZIERPOS bounded record", LoadedTransformRespectsEveryRecordBoundary<CKANIMATION_BEZIERPOS_CONTROL>},
        {"BEZIERSCL bounded record", LoadedTransformRespectsEveryRecordBoundary<CKANIMATION_BEZIERSCL_CONTROL>},
        {"LINPOS invalid raw read", TransformRawReadRejectsInvalidStateAtomically<CKANIMATION_LINPOS_CONTROL>},
        {"LINROT invalid raw read", TransformRawReadRejectsInvalidStateAtomically<CKANIMATION_LINROT_CONTROL>},
        {"LINSCL invalid raw read", TransformRawReadRejectsInvalidStateAtomically<CKANIMATION_LINSCL_CONTROL>},
        {"LINSCLAXIS invalid raw read", TransformRawReadRejectsInvalidStateAtomically<CKANIMATION_LINSCLAXIS_CONTROL>},
        {"TCBPOS invalid raw read", TransformRawReadRejectsInvalidStateAtomically<CKANIMATION_TCBPOS_CONTROL>},
        {"TCBROT invalid raw read", TransformRawReadRejectsInvalidStateAtomically<CKANIMATION_TCBROT_CONTROL>},
        {"TCBSCL invalid raw read", TransformRawReadRejectsInvalidStateAtomically<CKANIMATION_TCBSCL_CONTROL>},
        {"TCBSCLAXIS invalid raw read", TransformRawReadRejectsInvalidStateAtomically<CKANIMATION_TCBSCLAXIS_CONTROL>},
        {"BEZIERPOS invalid raw read", TransformRawReadRejectsInvalidStateAtomically<CKANIMATION_BEZIERPOS_CONTROL>},
        {"BEZIERSCL invalid raw read", TransformRawReadRejectsInvalidStateAtomically<CKANIMATION_BEZIERSCL_CONTROL>},
        {"LINPOS read allocation failure", TransformRawReadPreservesStateOnAllocationFailure<CKANIMATION_LINPOS_CONTROL>},
        {"LINROT read allocation failure", TransformRawReadPreservesStateOnAllocationFailure<CKANIMATION_LINROT_CONTROL>},
        {"LINSCL read allocation failure", TransformRawReadPreservesStateOnAllocationFailure<CKANIMATION_LINSCL_CONTROL>},
        {"LINSCLAXIS read allocation failure", TransformRawReadPreservesStateOnAllocationFailure<CKANIMATION_LINSCLAXIS_CONTROL>},
        {"TCBPOS read allocation failure", TransformRawReadPreservesStateOnAllocationFailure<CKANIMATION_TCBPOS_CONTROL>},
        {"TCBROT read allocation failure", TransformRawReadPreservesStateOnAllocationFailure<CKANIMATION_TCBROT_CONTROL>},
        {"TCBSCL read allocation failure", TransformRawReadPreservesStateOnAllocationFailure<CKANIMATION_TCBSCL_CONTROL>},
        {"TCBSCLAXIS read allocation failure", TransformRawReadPreservesStateOnAllocationFailure<CKANIMATION_TCBSCLAXIS_CONTROL>},
        {"BEZIERPOS read allocation failure", TransformRawReadPreservesStateOnAllocationFailure<CKANIMATION_BEZIERPOS_CONTROL>},
        {"BEZIERSCL read allocation failure", TransformRawReadPreservesStateOnAllocationFailure<CKANIMATION_BEZIERSCL_CONTROL>},
        {"LINPOS valid read contracts", TransformReadRetainsValidWireAndLengthContracts<CKANIMATION_LINPOS_CONTROL>},
        {"LINROT valid read contracts", TransformReadRetainsValidWireAndLengthContracts<CKANIMATION_LINROT_CONTROL>},
        {"LINSCL valid read contracts", TransformReadRetainsValidWireAndLengthContracts<CKANIMATION_LINSCL_CONTROL>},
        {"LINSCLAXIS valid read contracts", TransformReadRetainsValidWireAndLengthContracts<CKANIMATION_LINSCLAXIS_CONTROL>},
        {"TCBPOS valid read contracts", TransformReadRetainsValidWireAndLengthContracts<CKANIMATION_TCBPOS_CONTROL>},
        {"TCBROT valid read contracts", TransformReadRetainsValidWireAndLengthContracts<CKANIMATION_TCBROT_CONTROL>},
        {"TCBSCL valid read contracts", TransformReadRetainsValidWireAndLengthContracts<CKANIMATION_TCBSCL_CONTROL>},
        {"TCBSCLAXIS valid read contracts", TransformReadRetainsValidWireAndLengthContracts<CKANIMATION_TCBSCLAXIS_CONTROL>},
        {"BEZIERPOS valid read contracts", TransformReadRetainsValidWireAndLengthContracts<CKANIMATION_BEZIERPOS_CONTROL>},
        {"BEZIERSCL valid read contracts", TransformReadRetainsValidWireAndLengthContracts<CKANIMATION_BEZIERSCL_CONTROL>},
        {"Transform Load reports key allocation failure", LoadedTransformReportsKeyAllocationFailure},
        {"Transform Load canonicalizes supported base types", LoadedTransformCanonicalizesSupportedBaseTypes},
        {"Transform Load skips unknown records and padding", LoadedTransformSkipsUnknownRecordsAndPadding},
        {"Object self-Clone retains state and controllers", ObjectAnimationSelfClonePreservesState<false>},
        {"Merged Object self-Clone retains playback and controllers", ObjectAnimationSelfClonePreservesState<true>},
        {"Object Clone from owner preserves all shared keys", ObjectAnimationClonePreservesSameDataAliases<0>},
        {"Object owner Clone from alias preserves all shared keys", ObjectAnimationClonePreservesSameDataAliases<1>},
        {"Object Clone between aliases preserves all shared keys", ObjectAnimationClonePreservesSameDataAliases<2>},
        {"Object owner Clone transfers retained data ownership", ObjectAnimationClonePreservesReleasedDataAndSourceLifetime<true>},
        {"Object alias Clone preserves old owner data", ObjectAnimationClonePreservesReleasedDataAndSourceLifetime<false>},
        {"Null Object Clone clears only its target", ObjectAnimationNullCloneClearsOnlyTarget},
        {"Object Clone tolerates a source with no keyframe data", ObjectAnimationCloneHandlesSourceWithoutData},
        {"Null Object Clone allocation failure preserves all state", ObjectAnimationEmptyClonePreservesStateOnAllocationFailure<false>},
        {"Missing-data Object Clone allocation failure preserves all state", ObjectAnimationEmptyClonePreservesStateOnAllocationFailure<true>},
        {"Morph self-Clone retains normal-bearing keys", MorphSelfClonePreservesOwnedState<true, false>},
        {"Morph self-Clone retains position-only keys", MorphSelfClonePreservesOwnedState<false, false>},
        {"Morph self-Clone retains zero-sized normal presence", MorphSelfClonePreservesOwnedState<true, true>},
        {"Morph self-Clone retains zero-sized absent normals", MorphSelfClonePreservesOwnedState<false, true>},
        {"Morph Clone allocation failures preserve destination/source", MorphReplacementPreservesStateOnAllocationFailure<true>},
        {"Morph Read allocation failures preserve destination/source", MorphReplacementPreservesStateOnAllocationFailure<false>},
        {"Morph read/dump retain bytes in unaligned buffers", MorphReadAndDumpAcceptUnalignedBuffers},
        {"Loaded Morph rejects truncated fixed controller header", LoadedMorphRejectsActualTruncation<0>},
        {"Loaded Morph rejects truncated merged references", LoadedMorphRejectsActualTruncation<1>},
        {"Loaded Morph rejects absent declared key payload", LoadedMorphRejectsActualTruncation<2>},
        {"Loaded Morph skips valid padding/unknown records", LoadedMorphSkipsRecordPaddingAndUnknownControllers},
        {"Morph Save reports incomplete payload rejection", MorphSaveRejectsIncompletePayloads},
        {"Morph read rejects huge payload sizes", MorphReadRejectsOverflowAndUnorderedTimes<0>},
        {"Morph read rejects huge zero-vertex key counts", MorphReadRejectsOverflowAndUnorderedTimes<1>},
        {"Morph read rejects descending key times", MorphReadRejectsOverflowAndUnorderedTimes<2>},
        {"Morph read rejects negative key count atomically", MorphReadRejectsInvalidHeaderOrTimes<0>},
        {"Morph read rejects negative vertex count atomically", MorphReadRejectsInvalidHeaderOrTimes<1>},
        {"Morph read rejects nonfinite key times atomically", MorphReadRejectsInvalidHeaderOrTimes<2>},
        {"Morph dump rejects byte-size overflow", MorphDumpRejectsInconsistentState<0>},
        {"Morph dump rejects missing positions", MorphDumpRejectsInconsistentState<1>},
        {"Morph Save omits normals missing from the first key", MorphSaveOmitsPartialNormals<true>},
        {"Morph Save omits normals missing from a later key", MorphSaveOmitsPartialNormals<false>},
        {"Morph dump rejects nonfinite key times", MorphDumpRejectsInconsistentState<4>},
        {"Morph dump rejects descending key times", MorphDumpRejectsInconsistentState<5>},
        {"Loaded Morph rejects incomplete header envelope", LoadedMorphRespectsControllerRecordBoundaries<0>},
        {"Loaded Morph rejects payload outside declared envelope", LoadedMorphRespectsControllerRecordBoundaries<1>},
        {"Loaded Morph rejects oversized record envelope", LoadedMorphRespectsControllerRecordBoundaries<2>},
        {"Loaded Morph rejects missing terminator at identifier boundary", LoadedMorphRespectsControllerRecordBoundaries<3>},
        {"Loaded Morph resize keeps normals and coherent storage", LoadedMorphCountResizesStorage<true>},
        {"Loaded Morph resize keeps absent normals and coherent storage", LoadedMorphCountResizesStorage<false>},
        {"Morph resize rejects negative/unrepresentable counts", MorphCountRejectsInvalidSizes},
        {"Morph resize preserves omitted payloads", MorphCountPreservesOmittedPayloads},
        {"Morph resize composes with insertion/removal and Save/Load", MorphCountResizeComposesWithKeyEditing},
        {"Morph resize allocation failures preserve complete state", MorphCountResizePreservesStateOnAllocationFailure},
        {"Loaded Morph rejects excessive/negative counts", LoadedMorphRejectsInvalidRequests<0>},
        {"Loaded Morph rejects nonfinite requested times", LoadedMorphRejectsInvalidRequests<1>},
        {"Loaded Morph rejects nonfinite/unordered key times", LoadedMorphRejectsInvalidRequests<2>},
        {"Loaded Morph valid subset/optional output contracts", LoadedMorphRetainsValidOutputContracts},
        {"Loaded Morph large finite key-time interval", LoadedMorphInterpolationAvoidsIntermediateLoss<0>},
        {"Loaded Morph finite vector interpolation avoids overflow", LoadedMorphInterpolationAvoidsIntermediateLoss<1>},
        {"Loaded Morph retains tiny-phase contribution", LoadedMorphInterpolationAvoidsIntermediateLoss<2>},
        {"Loaded Morph retains exact endpoint across large values", LoadedMorphInterpolationAvoidsIntermediateLoss<3>},
        {"Loaded Morph retains near-last tiny-phase contribution", LoadedMorphInterpolationAvoidsIntermediateLoss<4>},
        {"Zero-length merged animation freezes at source time zero", MergedAnimationTimeMappingRemainsFinite<0>},
        {"Merged time mapping avoids intermediate overflow", MergedAnimationTimeMappingRemainsFinite<1>},
        {"Merged time mapping avoids intermediate underflow", MergedAnimationTimeMappingRemainsFinite<2>},
        {"Zero-duration merged sources stay at time zero", MergedAnimationTimeMappingRemainsFinite<3>},
        {"Merged time mapping retains signed finite durations", MergedAnimationTimeMappingRemainsFinite<4>},
        {"Merged animation rejects nonfinite time state", MergedAnimationRejectsNonfiniteTimeState},
        {"Merged Morph first source failure", MergedMorphUsesOnlySuccessfulSources<1>},
        {"Merged Morph second source failure", MergedMorphUsesOnlySuccessfulSources<2>},
        {"Merged Morph both source failures", MergedMorphUsesOnlySuccessfulSources<3>},
        {"Merged Morph optional output buffers", MergedMorphSupportsOptionalOutputBuffers},
        {"Merged Morph subset and invalid count bounds", MergedMorphSubsetPreservesBufferBounds},
        {"Merged Morph absent key payloads preserve outputs", MergedMorphPreservesAbsentKeyPayloads},
        {"Merged self-reference terminates", LoadedMergedAnimationTerminatesCycles<0>},
        {"Merged two-object cycle terminates", LoadedMergedAnimationTerminatesCycles<1>},
        {"Merged cyclic branch retains available source", LoadedMergedAnimationTerminatesCycles<2>},
        {"Merged shared descendant remains a valid DAG", LoadedMergedAnimationTerminatesCycles<3>},
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
        {"Legacy merged no sources", LoadedMergedAnimationHandlesMissingSources<0, 0>},
        {"Legacy merged first source", LoadedMergedAnimationHandlesMissingSources<0, 1>},
        {"Legacy merged second source", LoadedMergedAnimationHandlesMissingSources<0, 2>},
        {"Legacy merged both sources", LoadedMergedAnimationHandlesMissingSources<0, 3>},
        {"Snapshot merged no sources", LoadedMergedAnimationHandlesMissingSources<1, 0>},
        {"Snapshot merged first source", LoadedMergedAnimationHandlesMissingSources<1, 1>},
        {"Snapshot merged second source", LoadedMergedAnimationHandlesMissingSources<1, 2>},
        {"Snapshot merged both sources", LoadedMergedAnimationHandlesMissingSources<1, 3>},
        {"Controller merged no sources", LoadedMergedAnimationHandlesMissingSources<2, 0>},
        {"Controller merged first source", LoadedMergedAnimationHandlesMissingSources<2, 1>},
        {"Controller merged second source", LoadedMergedAnimationHandlesMissingSources<2, 2>},
        {"Controller merged both sources", LoadedMergedAnimationHandlesMissingSources<2, 3>},
        {"Shared merged no sources", LoadedMergedAnimationHandlesMissingSources<3, 0>},
        {"Shared merged first source", LoadedMergedAnimationHandlesMissingSources<3, 1>},
        {"Shared merged second source", LoadedMergedAnimationHandlesMissingSources<3, 2>},
        {"Shared merged both sources", LoadedMergedAnimationHandlesMissingSources<3, 3>},
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
        {"Legacy body part joint", LegacyBodyPartUsesIntegerFlagsAndNamedSdkAxes},
        {"Sprite text base serialization", SpriteTextSaveOmitsSpriteBitmapState},
        {"Sprite text default font without handle", SpriteTextSavePreservesFontWhenHandleIsUnavailable<false>},
        {"Sprite text requested font without handle", SpriteTextSavePreservesFontWhenHandleIsUnavailable<true>},
        {"Sprite text font field order", SpriteTextLoadsFontFieldsInSaveOrder},
        {"Mesh v0 lit", LegacyMeshLayout<0, true>},
        {"Mesh v0 prelit", LegacyMeshLayout<0, false>},
        {"Mesh v1 lit", LegacyMeshLayout<1, true>},
        {"Mesh v1 skips oversized line block", LegacyMeshLayout<1, true, 0, true>},
        {"Legacy body part skips oversized joint block", LegacyBodyPartSkipsOversizedJoint},
        {"Legacy texture skips oversized video format block", LegacyTextureSkipsOversizedVideoFormat},
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
        {"Grid texture draws only cells that fit", GridTextureDrawsOnlyCellsThatFit},
        {"Layer Load fits cells to its grid", LayerLoadFitsCellsToItsGrid},
        {"Layer Save ignores unregistered type remapping", LayerSaveIgnoresUnregisteredTypeRemapping},
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
        {"ShareDataFrom transfers ownership of released data", ShareDataFromTransfersOwnershipOfReleasedData},
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
        {"Linear position self-Clone and failed copy preserve keys", TransformSelfCloneAndFailedCopyPreserveKeys<CKANIMATION_LINPOS_CONTROL>},
        {"Linear rotation self-Clone and failed copy preserve keys", TransformSelfCloneAndFailedCopyPreserveKeys<CKANIMATION_LINROT_CONTROL>},
        {"Linear scale self-Clone and failed copy preserve keys", TransformSelfCloneAndFailedCopyPreserveKeys<CKANIMATION_LINSCL_CONTROL>},
        {"Linear scale-axis self-Clone and failed copy preserve keys", TransformSelfCloneAndFailedCopyPreserveKeys<CKANIMATION_LINSCLAXIS_CONTROL>},
        {"TCB position self-Clone and failed copy preserve keys", TransformSelfCloneAndFailedCopyPreserveKeys<CKANIMATION_TCBPOS_CONTROL>},
        {"TCB rotation self-Clone and failed copy preserve keys", TransformSelfCloneAndFailedCopyPreserveKeys<CKANIMATION_TCBROT_CONTROL>},
        {"TCB scale self-Clone and failed copy preserve keys", TransformSelfCloneAndFailedCopyPreserveKeys<CKANIMATION_TCBSCL_CONTROL>},
        {"TCB scale-axis self-Clone and failed copy preserve keys", TransformSelfCloneAndFailedCopyPreserveKeys<CKANIMATION_TCBSCLAXIS_CONTROL>},
        {"Bezier position self-Clone and failed copy preserve keys", TransformSelfCloneAndFailedCopyPreserveKeys<CKANIMATION_BEZIERPOS_CONTROL>},
        {"Bezier scale self-Clone and failed copy preserve keys", TransformSelfCloneAndFailedCopyPreserveKeys<CKANIMATION_BEZIERSCL_CONTROL>},
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
        {"Morph count editing uses safe storage with normals", MorphCountEditingUsesPortableStorageContract<12>},
        {"Morph count editing uses safe storage without normals", MorphCountEditingUsesPortableStorageContract<13>},
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
        {"Layer memory Save requires manager", LayerWireSaveRequiresManagerBeforeSelectivePayload<false>},
        {"Layer file Save requires manager", LayerWireSaveRequiresManagerBeforeSelectivePayload<true>},
        {"Layer memory selective square wire", LayerWireMemorySaveUsesNativeSelectiveHeader<0>},
        {"Layer memory selective nonzero format wire", LayerWireMemorySaveUsesNativeSelectiveHeader<7>},
        {"Layer file version3 square wire", LayerWireFileSaveOmitsTypeAndWritesVersion3Metadata<0>},
        {"Layer file version3 nonzero format wire", LayerWireFileSaveOmitsTypeAndWritesVersion3Metadata<7>},
        {"Layer Load metadata version0", LayerWireLoadVersionedMetadataUsesNativeFieldOrder<0>},
        {"Layer Load metadata version1", LayerWireLoadVersionedMetadataUsesNativeFieldOrder<1>},
        {"Layer Load metadata version2", LayerWireLoadVersionedMetadataUsesNativeFieldOrder<2>},
        {"Layer Load metadata version3", LayerWireLoadVersionedMetadataUsesNativeFieldOrder<3>},
        {"Layer Load metadata forward version4", LayerWireLoadVersionedMetadataUsesNativeFieldOrder<4>},
        {"Layer memory existing named type", LayerWireMemoryLoadIgnoresRawTypeAndRegistersName<true>},
        {"Layer memory unregistered named type", LayerWireMemoryLoadIgnoresRawTypeAndRegistersName<false>},
        {"Layer absent identifier preserves state", LayerWireNoIdentifierPreservesDerivedState},
        {"Layer Load manager requirement", LayerWireLoadManagerRequirementDependsOnIdentifier},
        {"Layer nonzero format clears old cells", LayerWireNonzeroFormatReloadDropsPreviousCells},
        {"Layer native dependency descriptions", &LayerNativeDependencyDescriptions},
        {"Layer cells preserve four-byte value semantics", &LayerCellsUseFourByteValuesAndNativeCoordinates},
        {"Layer SetSquareArray transfers current pointer", &LayerSquareArrayTransfersCurrentPointerAndKeepsReplacementContract},
        {"Layer SetOwner and InitOwner have distinct semantics", &LayerOwnerAssignmentAndInitializationKeepDistinctSemantics},
        {"Layer dependency preparation excludes owner", &LayerPreparesItselfWithoutAddingItsOwnerDependency},
        {"Layer pre-deletion detaches deleted owner", &LayerPreDeletionClearsOnlyAnOwnerMarkedForDeletion},
        {"Layer dependency remapping preserves cell array", &LayerDependencyRemappingChangesOnlyTheOwnerReference},
        {"Layer constructor array allocation failure", &LayerConstructorAllocationFailureLeavesUsableMetadata},
        {"Layer InitOwner allocation failure preserves cells", &LayerInitOwnerAllocationFailureKeepsExistingOwnerAndCells},
        {"Layer Copy includes cell data dependency", &LayerCopyPreservesNativeCellDependencyChoice<true>},
        {"Layer Copy excludes cell data dependency", &LayerCopyPreservesNativeCellDependencyChoice<false>},
        {"Layer Copy to self keeps owned cells", &LayerCopyToSelfKeepsOwnedCells},
        {"Layer Copy allocation failure preserves cells", &LayerCopyAllocationFailureKeepsExistingLayerState},
        {"Layer factory allocation failure returns null", &LayerFactoryAllocationFailureReturnsNull},
        {"Grid dimensions coordinate and state contract", GridDimensionsCoordinateAndStateContract},
        {"Grid copy dependency metadata contract", GridCopyDependencyMetadataContract},
        {"Grid layer removal destroys every object", GridLayerRemovalDestroysEveryObject},
        {"Grid legal resize rebuilds display geometry", GridLegalResizeRebuildsDisplayGeometry},
        {"Grid copy remaps layers and owns display resources", GridCopyRemapsLayersAndOwnsDisplayResources},
        {"Grid classification uses attribute category and native results", GridClassificationUsesAttributeCategoryAndNativeResults},
        {"Scene memory includes object body and owned name", SceneMemoryIncludesObjectBodyAndOwnedName},
        {"Scene memory counts allocated bits after clear", SceneMemoryCountsAllocatedBitsAfterClear},
        {"Grid memory includes inherited scene storage and layer cells", GridMemoryIncludesInheritedSceneStorageAndLayerCells},
        {"Grid registration descriptor native contract", GridRegistrationDescriptorMatchesNativeContract},
        {"Grid external Layer deletion registered notification", GridExternalLayerDeletionPrunesRegisteredLinks<true>},
        {"Grid manual callback after NONOTIFY Layer deletion", GridExternalLayerDeletionPrunesRegisteredLinks<false>},
        {"Grid activity follows current scene transitions", GridActivityUsesOnlyTheCurrentSceneFlags},
        {"Grid rotated coordinate and height queries", GridCoordinateQueriesFollowRotatedCellCentersAndHeight},
        {"Grid local-only bounding box update", GridLocalOnlyBoxUpdatePreservesTheWorldCache},
        {"Grid dependency usage delegates mesh animation", GridObjectUsageDelegatesMeshAndAnimationQueries},
        {"Grid copy preparation excludes Layers", GridPreparationSelectsLayersWithoutOwnerRecursion<false>},
        {"Grid copy preparation includes Layers", GridPreparationSelectsLayersWithoutOwnerRecursion<true>},
        {"Grid preparation propagates do-not-save failure", GridPreparationPropagatesTheBaseSaveFailure},
        {"Layer full cell Load allocation failure keeps cells", &LayerLoadCellAllocationFailureRetainsCellsAndCanRetry<4>},
        {"Layer short cell Load allocation failure keeps cells", &LayerLoadCellAllocationFailureRetainsCellsAndCanRetry<2>},
        {"Layer memory Save cell allocation failure", &LayerSaveCellPreparationAllocationFailureReturnsNull<false>},
        {"Layer file Save cell allocation failure", &LayerSaveCellPreparationAllocationFailureReturnsNull<true>},
        {"Layer explicit Fit failure keeps prefix and retries", &LayerExplicitFitAllocationFailureKeepsOwnedPrefixAndRetries},
        {"Layer registration native descriptor fields", &LayerRegisterAppliesNativeDescriptorFields},
        {"Layer context destruction retains owner", &LayerContextDestructionKeepsOwnerAndHandlesTransferredCells},
        {"Grid reload rebuilds display for loaded dimensions", GridReloadRebuildsDisplayForLoadedDimensions},
        {"Grid AddLayer owner allocation failure and retry", GridAddLayerRejectsUncommittedOwnerAndCanRetry<false>},
        {"Grid AddLayerByName owner allocation failure and retry", GridAddLayerRejectsUncommittedOwnerAndCanRetry<true>},
        {"Grid display missing manager cleanup and retry", GridDisplayWithoutManagerLeavesNoPartialResourcesAndCanRetry},
        {"Grid display texture Create failure cleanup and retry", GridDisplaySurfaceFailureCleansResourcesAndCanRetry<true>},
        {"Grid display unavailable surface cleanup and retry", GridDisplaySurfaceFailureCleansResourcesAndCanRetry<false>},
        {"Grid display level membership cache and deletion", GridDisplayLevelMembershipCacheAndDeletionContract},
        {"Grid Load Layer-first Fit failure and retry", GridLoadPropagatesLayerFirstFitFailureAndCanRetry},
        {"Grid embedded Save allocation failure and retry", GridEmbeddedSavePropagatesLayerAllocationFailureAndCanRetry},
        {"Grid embedded Load manager failure and retry", GridEmbeddedLoadPropagatesMissingManagerAndCanRetry},
        {"Chunk object-ID array clears cached IDs on empty input", ChunkObjectIdArrayReadsReplaceNonemptyCacheWithEmpty},
        {"Grid display null mesh factory cleanup and retry", GridDisplayFactoryNullPreservesCallerAndRetries<0>},
        {"Grid display null main material factory cleanup and retry", GridDisplayFactoryNullPreservesCallerAndRetries<1>},
        {"Grid display null border material factory cleanup and retry", GridDisplayFactoryNullPreservesCallerAndRetries<2>},
        {"Grid display null texture factory cleanup and retry", GridDisplayFactoryNullPreservesCallerAndRetries<3>},
        {"Grid display Layer pointer scratch allocation failure and retry", GridDisplayScratchAllocationFailurePreservesCallerAndRetries<0>},
        {"Grid display Layer color scratch allocation failure and retry", GridDisplayScratchAllocationFailurePreservesCallerAndRetries<1>},
        {"Grid display destroy during actual context ClearAll", GridDisplayDestroyDefersToContextDuringClearAll},
        {"Grid signed Layer cell RGB native addition", GridDisplaySignedCellRgbUsesNativeAddition},
        {"Grid display missing main material teardown", GridDisplayDestroyHandlesEditedMaterialSlots<true>},
        {"Grid display aliased material teardown", GridDisplayDestroyHandlesEditedMaterialSlots<false>},
        {"Grid factory defaults and registered attribute", GridFactoryAppliesConstructorStateAndGridAttribute},
        {"Grid indexed Layer absent manager", GridLayerOperationsWithoutManagerKeepState<false>},
        {"Grid named Layer absent manager", GridLayerOperationsWithoutManagerKeepState<true>},
        {"Grid indexed Layer registry rejection and lookup", GridLayerRejectionsAndNamedLookupFollowRegistry<false>},
        {"Grid named Layer registry rejection and lookup", GridLayerRejectionsAndNamedLookupFollowRegistry<true>},
        {"Grid named classifications and native errors", GridNamedClassificationUsesRegisteredCategoryAndNativeErrors},
        {"Grid PreSave current mesh and layers", GridPreSaveCollectsLayersAndOnlyCurrentMesh<true>},
        {"Grid PreSave no current mesh and layers", GridPreSaveCollectsLayersAndOnlyCurrentMesh<false>},
        {"Grid visible PostLoad display", GridPostLoadBuildsOnlyVisibleDisplay<true>},
        {"Grid hidden PostLoad display", GridPostLoadBuildsOnlyVisibleDisplay<false>},
        {"Grid Save masks and independent field layout", GridSaveMasksAndEmptyLayerWireMatchOriginalLayout},
        {"Grid memory Load independent and absent fields", GridLoadIndependentEmptyLayerWireAndAbsentBlock<false>},
        {"Grid file Load independent and absent fields", GridLoadIndependentEmptyLayerWireAndAbsentBlock<true>},
        {"Grid memory entity delegation and owned cells", GridMemoryAddsOwnedLayerStorageToEntityEstimate},
        {"Layer internal array destruction releases once", &LayerDestructorReleasesCurrentArrayExactlyOnce<false>},
        {"Layer transferred array destruction releases once", &LayerDestructorReleasesCurrentArrayExactlyOnce<true>},
        {"Layer memory Save packed scratch release and independent wire", &LayerSaveReleasesPackedScratchAndMatchesIndependentWire<false>},
        {"Layer file Save packed scratch release and independent wire", &LayerSaveReleasesPackedScratchAndMatchesIndependentWire<true>},
        {"Layer Fit no-action states keep cells without allocation", &LayerFitNoActionPreservesPointersWithoutAllocation},
        {"Layer Fit unrepresentable owner count returns a checked error", &LayerFitRejectsUnrepresentableOwnerCountWithoutAllocation},
        {"Layer memory Load independent words and previous-cell release", &LayerLoadsIndependentWordsAndReleasesPreviousCells<false>},
        {"Layer file Load independent words and previous-cell release", &LayerLoadsIndependentWordsAndReleasesPreviousCells<true>},
        {"File reference source ID and existing parameter", FileReferenceHeaderRestoresSourceIdAndExistingParameter<false, 0>},
        {"Compressed file reference source ID and existing parameter", FileReferenceHeaderRestoresSourceIdAndExistingParameter<true, 0>},
        {"File reference different parameter name", FileReferenceHeaderRestoresSourceIdAndExistingParameter<false, 1>},
        {"File reference different parameter type", FileReferenceHeaderRestoresSourceIdAndExistingParameter<false, 2>},
        {"File save queue promotion flags dependencies and reset", FileSaveQueuePreservesPromotionFlagsDependenciesAndJobReset<false>},
        {"Compressed file save queue promotion flags dependencies and reset", FileSaveQueuePreservesPromotionFlagsDependenciesAndJobReset<true>},
        {"File save queue excludes dynamic and not saved objects", FileSaveQueueExcludesDynamicAndNotSavedObjects},

        {"File non-Level PreSave entry ordering and first flags", FileSaveNonLevelPreSaveRunsBeforeEntryAndKeepsFirstFlags},
        {"File Level PreSave entry ordering and Scene reference promotion", FileSaveLevelEntryExistsBeforePreSaveAndSceneReferencePromotes},
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
