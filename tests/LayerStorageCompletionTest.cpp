#include <gtest/gtest.h>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKGrid.h"
#include "RCKLayer.h"
#include "SerializationGridRegistry.h"

extern void SetProcessorSpecific_FunctionsPtr();

namespace {

const CKDWORD PendingWords[] = {0x80000000u, 0x3F800001u, 0xFFFFFFFFu};

class LayerStorageCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    SerializationGridRegistry *Registry = nullptr;

    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKGrid, CKCID_3DENTITY);
        CKCLASSREGISTERCID(RCKLayer, CKCID_OBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Registry = new SerializationGridRegistry(Context);
    }

    void TearDown() override {
        if (Context)
            CKCloseContext(Context);
        CKShutdown();
    }

    RCKGrid *PendingOwner() {
        return static_cast<RCKGrid *>(Context->CreateObject(CKCID_GRID, "Pending owner"));
    }

    RCKLayer *LoadedPrefix(RCKGrid &owner, const char *name) {
        Registry->RegisterType(const_cast<char *>(name));
        RCKLayer *layer = static_cast<RCKLayer *>(Context->CreateObject(CKCID_LAYER, const_cast<char *>(name)));
        if (!layer)
            return nullptr;
        CKStateChunk *wire = CreateCKStateChunk(CKCID_LAYER, nullptr);
        wire->StartWrite();
        wire->WriteIdentifier(CK_STATESAVE_LAYERDATA);
        wire->WriteObject(&owner);
        wire->WriteInt(73); // Memory Load derives type from name, not this word.
        wire->WriteInt(0);
        wire->WriteDword(LAYER_STATE_VISIBLE);
        wire->WriteBuffer_LEndian(sizeof(PendingWords), const_cast<CKDWORD *>(PendingWords));
        wire->CloseChunk();
        wire->StartRead();
        const CKERROR result = layer->Load(wire, nullptr);
        DeleteCKStateChunk(wire);
        EXPECT_EQ(CK_OK, result);
        return layer;
    }

    void PendingCopy(CKBOOL copyCells) {
        RCKGrid *owner = PendingOwner();
        ASSERT_NE(nullptr, owner);
        ASSERT_EQ(0, owner->GetWidth());
        ASSERT_EQ(0, owner->GetLength());
        RCKLayer *source = LoadedPrefix(*owner, "Copy pending cells");
        ASSERT_NE(nullptr, source);
        CKSquare *sourceCells = source->GetSquareArray();
        ASSERT_NE(nullptr, sourceCells);
        CKDependencies dependencies;
        dependencies.m_Flags = copyCells ? CK_DEPENDENCIES_FULL : CK_DEPENDENCIES_NONE;
        RCKLayer *copy = static_cast<RCKLayer *>(Context->CopyObject(source, &dependencies));
        ASSERT_NE(nullptr, copy);
        EXPECT_NE(source, copy);
        EXPECT_EQ(owner->GetID(), copy->GetOwner());
        EXPECT_EQ(source->GetType(), copy->GetType());
        EXPECT_EQ(0, copy->GetFormat());
        EXPECT_EQ(sourceCells, source->GetSquareArray());
        EXPECT_NE(nullptr, copy->GetSquareArray());
        EXPECT_NE(sourceCells, copy->GetSquareArray());
        if (copy->GetSquareArray()) {
            for (int i = 0; i < 3; ++i)
                EXPECT_EQ(copyCells ? PendingWords[i] : 0u, copy->GetSquareArray()[i].dval);
        }
        sourceCells[0].dval = 0x13579BDFu;
        owner->SetDimensions(2, 2, 2.0f, 2.0f);
        source->FitSquareArray();
        copy->FitSquareArray();
        for (int i = 0; i < 4; ++i) {
            CKDWORD value = 0xAAAAAAAAu;
            ASSERT_TRUE(copy->GetValue2(i % 2, i / 2, &value));
            EXPECT_EQ(copyCells && i < 3 ? PendingWords[i] : 0u, value);
        }
        CKDWORD sourceValue = 0;
        ASSERT_TRUE(source->GetValue2(0, 0, &sourceValue));
        EXPECT_EQ(0x13579BDFu, sourceValue);
        EXPECT_EQ(owner->GetID(), source->GetOwner());
        EXPECT_EQ(owner->GetID(), copy->GetOwner());
        EXPECT_EQ(0, owner->GetLayerCount()); // Owner is a reference, not copied with this Layer.
    }

    void PendingSave(CKBOOL toFile) {
        RCKGrid *owner = PendingOwner();
        ASSERT_NE(nullptr, owner);
        RCKLayer *source = LoadedPrefix(*owner, "Save pending cells");
        ASSERT_NE(nullptr, source);
        ASSERT_NE(nullptr, source->GetSquareArray());
        CKFile file(Context);
        file.m_FileObjects.Resize(1);
        file.m_FileObjects[0].Object = file.m_FileObjects[0].CreatedObject = owner->GetID();
        file.m_FileObjects[0].ObjPtr = owner;
        file.m_ObjectsHashTable.Insert(owner->GetID(), 0);
        CKStateChunk *saved = source->Save(toFile ? &file : nullptr, CK_STATESAVE_LAYERDATA);
        ASSERT_NE(nullptr, saved);
        saved->StartRead();
        ASSERT_TRUE(saved->SeekIdentifier(CK_STATESAVE_LAYERDATA));
        EXPECT_EQ(owner->GetID(), saved->ReadObjectID());
        if (!toFile)
            EXPECT_EQ(source->GetType(), saved->ReadInt());
        EXPECT_EQ(0, saved->ReadInt());
        if (toFile) {
            EXPECT_EQ(3, saved->ReadInt());
            saved->ReadDword();
            saved->ReadGuid();
        }
        EXPECT_EQ(LAYER_STATE_VISIBLE, saved->ReadDword());
        void *raw = nullptr;
        const int bytes = saved->ReadBuffer(&raw);
        EXPECT_EQ(sizeof(PendingWords), bytes);
        if (raw && bytes == sizeof(PendingWords)) {
            const CKDWORD *words = static_cast<const CKDWORD *>(raw);
            for (int i = 0; i < 3; ++i)
                EXPECT_EQ(PendingWords[i], words[i]);
        }
        CKDeletePointer(raw);
        RCKLayer *restored = static_cast<RCKLayer *>(Context->CreateObject(CKCID_LAYER, "Save pending cells"));
        ASSERT_NE(nullptr, restored);
        saved->StartRead();
        EXPECT_EQ(CK_OK, restored->Load(saved, toFile ? &file : nullptr));
        DeleteCKStateChunk(saved);
        owner->SetDimensions(2, 2, 2.0f, 2.0f);
        source->FitSquareArray();
        restored->FitSquareArray();
        for (int i = 0; i < 4; ++i) {
            CKDWORD value = 0;
            ASSERT_TRUE(restored->GetValue2(i % 2, i / 2, &value));
            EXPECT_EQ(i < 3 ? PendingWords[i] : 0u, value);
        }
        EXPECT_EQ(owner->GetID(), restored->GetOwner());
        EXPECT_NE(source->GetSquareArray(), restored->GetSquareArray());
        for (int i = 0; i < 3; ++i)
            EXPECT_EQ(PendingWords[i], source->GetSquareArray()[i].dval);
    }
};

TEST_F(LayerStorageCompletionTest, CopyWithCellsPreservesPendingLoadedPrefixAndIndependentOwnerReference) {
    PendingCopy(TRUE);
}

TEST_F(LayerStorageCompletionTest, CopyWithoutCellsRetainsPendingCapacityAndZeroesItsIndependentCells) {
    PendingCopy(FALSE);
}

TEST_F(LayerStorageCompletionTest, MemorySavePreservesIndependentPendingWireUntilOwnerDimensionsArrive) {
    PendingSave(FALSE);
}

TEST_F(LayerStorageCompletionTest, FileSavePreservesIndependentPendingWireUntilOwnerDimensionsArrive) {
    PendingSave(TRUE);
}

} // namespace
