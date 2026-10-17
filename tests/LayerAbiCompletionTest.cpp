#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include "CKAll.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKGrid.h"
#include "RCKLayer.h"
#include "RCKRenderManager.h"
#include "SerializationGridRegistry.h"

// Observe only exact pointers owned by CK2_3DStatic in this test executable.
// CK2.dll allocations are outside this observer's scope.
static thread_local void *s_ObservedLayerArray = nullptr;
static thread_local int s_ObservedLayerArrayDeletes = 0;

void *operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete[](void *pointer) noexcept {
    if (pointer && pointer == s_ObservedLayerArray) ++s_ObservedLayerArrayDeletes;
    ::operator delete(pointer);
}
void operator delete[](void *pointer, std::size_t) noexcept { ::operator delete[](pointer); }
void *operator new[](std::size_t bytes, const std::nothrow_t &) noexcept {
    try { return ::operator new[](bytes); } catch (...) { return nullptr; }
}
void operator delete[](void *pointer, const std::nothrow_t &) noexcept { ::operator delete[](pointer); }

extern void SetProcessorSpecific_FunctionsPtr();

namespace {
class LayerArrayWatch {
public:
    explicit LayerArrayWatch(void *pointer) { s_ObservedLayerArray = pointer; s_ObservedLayerArrayDeletes = 0; }
    ~LayerArrayWatch() { s_ObservedLayerArray = nullptr; }
    int Deletes() const { return s_ObservedLayerArrayDeletes; }
};

class LayerProbe : public RCKLayer {
public:
    LayerProbe(CKContext *context, CKSTRING name, CK_ID owner = 0) : RCKLayer(context, name, owner) {}
    int Capacity() const { return m_SquareCount; }
    CKDWORD Flags() const { return m_Flags; }
    std::size_t Offset(int index) const {
        const void *fields[] = {&m_Grid, &m_Type, &m_Format, &m_Flags, &m_SquareArray, &m_SquareCount};
        return static_cast<const unsigned char *>(fields[index]) - reinterpret_cast<const unsigned char *>(this);
    }
};

class LayerAbiCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    SerializationGridRegistry *Registry = nullptr;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKLayer, CKCID_OBJECT);
        CKCLASSREGISTERCID(RCKGrid, CKCID_3DENTITY);
        CKBuildClassHierarchyTable(); ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        new RCKRenderManager(Context);
        Registry = new SerializationGridRegistry(Context);
    }
    void TearDown() override { if (Context) CKCloseContext(Context); CKShutdown(); }
};

TEST_F(LayerAbiCompletionTest, RegisteredFactoryUsesFormalSdkBaseAndLayerVisibilityOverridesTheObjectSlot) {
    CKObject *object = Context->CreateObject(CKCID_LAYER, "Layer");
    CKLayer *layer = CKLayer::Cast(object); ASSERT_NE(nullptr, layer);
    EXPECT_EQ(CKCID_LAYER, object->GetClassID()); EXPECT_EQ(51, RCKLayer::m_ClassID);
    EXPECT_EQ(static_cast<void *>(object), static_cast<void *>(layer));
    EXPECT_TRUE((std::is_base_of<CKLayer, RCKLayer>::value));
    EXPECT_EQ(1, layer->GetType()); EXPECT_EQ(0, layer->GetFormat());
    EXPECT_EQ(0, layer->GetOwner()); EXPECT_EQ(nullptr, layer->GetSquareArray());
    EXPECT_TRUE(object->IsVisible()); object->Show(CKHIDE); EXPECT_TRUE(object->IsVisible());
    layer->SetVisible(FALSE); object->Show(CKSHOW); EXPECT_FALSE(object->IsVisible());
    layer->SetVisible(-7); EXPECT_TRUE(object->IsVisible());
    EXPECT_EQ(static_cast<int>(sizeof(RCKLayer)) + 6, object->GetMemoryOccupation());
    const CKClassDesc *descriptor = CKGetClassDesc(CKCID_LAYER); ASSERT_NE(nullptr, descriptor);
    EXPECT_EQ(CK_GENERALOPTIONS_NODUPLICATENAMECHECK, descriptor->DefaultOptions);
    EXPECT_EQ(CK_DEPENDENCIES_COPY_GRIDLAYER_DATA, descriptor->DefaultCopyDependencies);
    EXPECT_TRUE(descriptor->ToBeNotify.IsSet(CKCID_GRID));
    EXPECT_STREQ("Grid Layer", RCKLayer::GetClassName());
}

TEST_F(LayerAbiCompletionTest, IndependentMemoryWireRestoresOwnerFlagsTypeAndFourByteValuesIntoHostCells) {
    RCKGrid grid(Context, "Wire owner"); grid.SetDimensions(3, 1, 3, 1);
    LayerProbe layer(Context, "Authored layer");
    const int type = Registry->RegisterType("Authored layer");
    CKDWORD words[] = {0x80000001u, 0x3f800000u, 0xfedcba98u};
    CKStateChunk chunk(CKCID_LAYER, nullptr); chunk.StartWrite(); chunk.WriteIdentifier(CK_STATESAVE_LAYERDATA);
    chunk.WriteObject(&grid); chunk.WriteInt(0x1234567); chunk.WriteInt(0); chunk.WriteDword(0xa5u);
    chunk.WriteBuffer_LEndian(sizeof(words), words); chunk.CloseChunk();
    CKObject *object = &layer; ASSERT_EQ(CK_OK, object->Load(&chunk, nullptr));
    CKLayer *sdk = &layer;
    EXPECT_EQ(grid.GetID(), sdk->GetOwner()); EXPECT_EQ(type, sdk->GetType()); EXPECT_EQ(0, sdk->GetFormat());
    EXPECT_EQ(0xa5u, layer.Flags()); EXPECT_TRUE(object->IsVisible()); EXPECT_EQ(3, layer.Capacity());
    ASSERT_NE(nullptr, sdk->GetSquareArray());
    for (int i = 0; i < 3; ++i) {
        CKDWORD actual = 0; sdk->GetValue(i, 0, &actual); EXPECT_EQ(words[i], actual) << i;
        EXPECT_EQ(words[i], sdk->GetSquareArray()[i].dval) << i;
        if (sizeof(void *) == 8) EXPECT_EQ(static_cast<std::uintptr_t>(words[i]), reinterpret_cast<std::uintptr_t>(sdk->GetSquareArray()[i].ptr)) << i;
    }
    sdk->SetVisible(FALSE); EXPECT_EQ(0xa4u, layer.Flags()); EXPECT_FALSE(object->IsVisible());
    sdk->SetVisible(TRUE); EXPECT_EQ(0xa5u, layer.Flags());
    EXPECT_EQ(static_cast<int>(sizeof(RCKLayer)) + 15, object->GetMemoryOccupation());
}

TEST_F(LayerAbiCompletionTest, PointerTransferAndOwnerRebindingKeepHostCellsUntilExplicitOwnerInitialization) {
    RCKGrid first(Context, "First owner"), second(Context, "Second owner");
    first.SetDimensions(2, 2, 2, 2); second.SetDimensions(1, 4, 1, 4);
    LayerProbe layer(Context, "Transferred", first.GetID()); CKLayer *sdk = &layer;
    CKSquare *previous = sdk->GetSquareArray(); ASSERT_NE(nullptr, previous); EXPECT_EQ(4, layer.Capacity());
    CKSquare *transferred = new CKSquare[4]();
    const std::uintptr_t token = sizeof(void *) == 8 ? static_cast<std::uintptr_t>(0x1234567887654321ull) : 0x87654321u;
    transferred[3].ptr = reinterpret_cast<void *>(token); sdk->SetSquareArray(transferred);
    EXPECT_EQ(transferred, sdk->GetSquareArray()); EXPECT_EQ(-1, layer.Capacity());
    delete[] previous; // Replacement transfers only the new pointer; the replacing caller releases the old one.
    sdk->SetOwner(second.GetID()); EXPECT_EQ(second.GetID(), sdk->GetOwner());
    layer.FitSquareArray(); EXPECT_EQ(transferred, sdk->GetSquareArray()); EXPECT_EQ(-1, layer.Capacity());
    EXPECT_EQ(token, reinterpret_cast<std::uintptr_t>(sdk->GetSquareArray()[3].ptr));
    sdk->SetOwner(0); EXPECT_EQ(0, sdk->GetOwner()); EXPECT_EQ(transferred, sdk->GetSquareArray());
    sdk->SetOwner(first.GetID()); sdk->SetType(-9); sdk->SetFormat(7); sdk->SetVisible(FALSE);
    sdk->InitOwner(second.GetID()); EXPECT_EQ(second.GetID(), sdk->GetOwner()); EXPECT_EQ(4, layer.Capacity());
    ASSERT_NE(nullptr, sdk->GetSquareArray());
    for (int i = 0; i < 4; ++i) EXPECT_EQ(nullptr, sdk->GetSquareArray()[i].ptr) << i;
    EXPECT_EQ(-9, sdk->GetType()); EXPECT_EQ(7, sdk->GetFormat()); EXPECT_FALSE(sdk->IsVisible());
    sdk->InitOwner(0); EXPECT_EQ(0, sdk->GetOwner()); EXPECT_EQ(nullptr, sdk->GetSquareArray()); EXPECT_EQ(0, layer.Capacity());
}

TEST_F(LayerAbiCompletionTest, RegisteredIdentityRejectsCppCopyAndContextCloneKeepsIndependentNameAndCells) {
    EXPECT_FALSE(std::is_copy_constructible<RCKLayer>::value); EXPECT_FALSE(std::is_copy_assignable<RCKLayer>::value);
    EXPECT_FALSE(std::is_move_constructible<RCKLayer>::value); EXPECT_FALSE(std::is_move_assignable<RCKLayer>::value);
    RCKGrid grid(Context, "Shared owner"); grid.SetDimensions(2, 2, 2, 2);
    CKObject *sourceObject = Context->CreateObject(CKCID_LAYER, "Source layer");
    CKLayer *source = CKLayer::Cast(sourceObject); ASSERT_NE(nullptr, source); source->InitOwner(grid.GetID());
    source->SetType(19); source->SetVisible(FALSE); source->GetSquareArray()[3].dval = 0xfedcba98u;
    CKDependencies dependencies; dependencies.m_Flags = CK_DEPENDENCIES_FULL;
    CKObject *copyObject = Context->CopyObject(sourceObject, &dependencies); ASSERT_NE(nullptr, copyObject);
    CKLayer *copy = CKLayer::Cast(copyObject); ASSERT_NE(nullptr, copy);
    EXPECT_NE(source->GetID(), copy->GetID()); EXPECT_NE(sourceObject, copyObject);
    EXPECT_EQ(grid.GetID(), source->GetOwner()); EXPECT_EQ(grid.GetID(), copy->GetOwner());
    EXPECT_EQ(19, copy->GetType()); EXPECT_FALSE(copy->IsVisible());
    ASSERT_NE(nullptr, copy->GetSquareArray()); EXPECT_NE(source->GetSquareArray(), copy->GetSquareArray());
    EXPECT_EQ(0xfedcba98u, copy->GetSquareArray()[3].dval);
    copy->SetName("Clone layer"); copy->GetSquareArray()[3].dval = 7;
    EXPECT_STREQ("Source layer", source->GetName()); EXPECT_EQ(0xfedcba98u, source->GetSquareArray()[3].dval);
    const CK_ID copyId = copy->GetID(); delete copyObject;
    EXPECT_EQ(nullptr, Context->GetObject(copyId)); EXPECT_EQ(sourceObject, Context->GetObject(source->GetID()));
    EXPECT_STREQ("Source layer", source->GetName()); EXPECT_EQ(0xfedcba98u, source->GetSquareArray()[3].dval);
    const CK_ID sourceId = source->GetID(); ASSERT_EQ(CK_OK, Context->DestroyObject(source, CK_DESTROY_NONOTIFY));
    EXPECT_EQ(nullptr, Context->GetObject(sourceId)); EXPECT_EQ(&grid, Context->GetObject(grid.GetID()));
    LayerProbe fresh(Context, "Fresh", grid.GetID()); EXPECT_EQ(4, fresh.Capacity());
    EXPECT_EQ(nullptr, fresh.GetSquareArray()[3].ptr);
}

TEST_F(LayerAbiCompletionTest, ExactArrayReleaseTracksCallerReplacementInitializationAndVirtualDestruction) {
    RCKGrid first(Context, "Release owner A"), second(Context, "Release owner B");
    first.SetDimensions(2, 2, 2, 2); second.SetDimensions(4, 1, 4, 1);
    for (int transfer = 0; transfer < 2; ++transfer) {
        LayerProbe *layer = new LayerProbe(Context, "Observed layer", first.GetID());
        ASSERT_NE(nullptr, layer->GetSquareArray()); const CK_ID id = layer->GetID();
        if (transfer) {
            CKSquare *previous = layer->GetSquareArray(), *replacement = new CKSquare[4]();
            int releases;
            {
                LayerArrayWatch watch(previous); layer->SetSquareArray(replacement);
                EXPECT_EQ(0, watch.Deletes()); delete[] previous; releases = watch.Deletes();
            }
            EXPECT_EQ(1, releases);
            {
                LayerArrayWatch watch(replacement); layer->InitOwner(second.GetID()); releases = watch.Deletes();
            }
            EXPECT_EQ(1, releases); ASSERT_NE(nullptr, layer->GetSquareArray()); EXPECT_EQ(4, layer->Capacity());
            previous = layer->GetSquareArray(); replacement = new CKSquare[4]();
            {
                LayerArrayWatch watch(previous); layer->SetSquareArray(replacement);
                EXPECT_EQ(0, watch.Deletes()); delete[] previous; releases = watch.Deletes();
            }
            EXPECT_EQ(1, releases); EXPECT_EQ(-1, layer->Capacity());
        }
        int releases;
        {
            LayerArrayWatch watch(layer->GetSquareArray()); CKObject *object = layer;
            delete object; releases = watch.Deletes();
        }
        EXPECT_EQ(1, releases) << transfer; EXPECT_EQ(nullptr, Context->GetObject(id));
        EXPECT_EQ(&first, Context->GetObject(first.GetID())); EXPECT_EQ(&second, Context->GetObject(second.GetID()));
    }
}

TEST_F(LayerAbiCompletionTest, CurrentCompilerLayoutRecordsEveryOwnFieldAndPortableCapacityExtension) {
    LayerProbe layer(Context, "Layout"); const bool wide = sizeof(void *) == 8;
    const std::size_t expected[] = {wide ? 40u : 20u, wide ? 48u : 24u, wide ? 52u : 28u,
                                    wide ? 56u : 32u, wide ? 64u : 36u, wide ? 72u : 40u};
    const char *names[] = {"m_Grid", "m_Type", "m_Format", "m_Flags", "m_SquareArray", "m_SquareCount"};
    RecordProperty("RCKLayer_size", static_cast<int>(sizeof(RCKLayer)));
    RecordProperty("RCKLayer_alignment", static_cast<int>(alignof(RCKLayer)));
    RecordProperty("CKLayer_size", static_cast<int>(sizeof(CKLayer)));
    RecordProperty("CKObject_size", static_cast<int>(sizeof(CKObject)));
    RecordProperty("CKSquare_size", static_cast<int>(sizeof(CKSquare)));
    RecordProperty("CKSquare_alignment", static_cast<int>(alignof(CKSquare)));
    RecordProperty("pointer_size", static_cast<int>(sizeof(void *)));
    EXPECT_EQ(wide ? 80u : 44u, sizeof(RCKLayer)); EXPECT_EQ(wide ? 8u : 4u, alignof(RCKLayer));
    EXPECT_EQ(wide ? 40u : 20u, sizeof(CKLayer)); EXPECT_EQ(sizeof(CKObject), sizeof(CKLayer));
    EXPECT_EQ(sizeof(void *), sizeof(CKSquare)); EXPECT_EQ(alignof(void *), alignof(CKSquare));
    EXPECT_EQ(sizeof(RCKLayer), sizeof(LayerProbe));
    for (int i = 0; i < 6; ++i) { EXPECT_EQ(expected[i], layer.Offset(i)) << names[i]; RecordProperty(names[i], static_cast<int>(layer.Offset(i))); }
    EXPECT_EQ(4u, sizeof(RCKLayer::m_ClassID)); EXPECT_EQ(51, RCKLayer::m_ClassID);
    RecordProperty("m_ClassID_size", static_cast<int>(sizeof(RCKLayer::m_ClassID)));
    RecordProperty("m_ClassID_value", static_cast<int>(RCKLayer::m_ClassID));
    EXPECT_EQ(static_cast<void *>(&layer), static_cast<void *>(static_cast<CKLayer *>(&layer)));
    EXPECT_EQ(static_cast<void *>(&layer), static_cast<void *>(static_cast<CKObject *>(&layer)));
}
} // namespace
