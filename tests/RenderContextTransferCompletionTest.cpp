#include "TextureTestObserver.h"
#include "CKBitmapReader.h"
#include "CKPluginManager.h"
#include <cstdio>
#include <cstdint>
#include <type_traits>

namespace {
CKPluginInfo TransferWriterInfo;
int WriterCalls = 0;
bool WriterAccepts = true;

class TransferWriter : public CKBitmapReader {
    CKBitmapProperties Properties;
public:
    TransferWriter() {
        Properties.m_Ext = CKFileExtension("rc7");
        Properties.m_ReaderGuid = TransferWriterInfo.m_GUID;
    }
    void Release() override { delete this; }
    CKPluginInfo *GetReaderInfo() override { return &TransferWriterInfo; }
    int GetOptionsCount() override { return 0; }
    CKSTRING GetOptionDescription(int) override { return nullptr; }
    void GetBitmapDefaultProperties(CKBitmapProperties **p) override { *p = &Properties; }
    void SetBitmapDefaultProperties(CKBitmapProperties *) override {}
    int ReadFile(CKSTRING, CKBitmapProperties **) override { return 1; }
    int ReadMemory(void *, int, CKBitmapProperties **) override { return 1; }
    int ReadASynchronousFile(CKSTRING, CKBitmapProperties **) override { return 1; }
    int SaveFile(CKSTRING name, CKBitmapProperties *p) override {
        ++WriterCalls;
        if (!WriterAccepts) return 0;
        FILE *file = std::fopen(name, "wb");
        if (!file) return 0;
        const int rowBytes = p->m_Format.Width * p->m_Format.BitsPerPixel / 8;
        size_t size = 0;
        for (int row = 0; row < p->m_Format.Height; ++row)
            size += std::fwrite(static_cast<CKBYTE *>(p->m_Data) + row * p->m_Format.BytesPerLine,
                                1, rowBytes, file);
        std::fclose(file);
        return static_cast<int>(size);
    }
    int SaveMemory(void **p, CKBitmapProperties *) override { *p = nullptr; return 0; }
    void ReleaseMemory(void *p) override { delete[] static_cast<CKBYTE *>(p); }
};
int TransferWriterCount() { return 1; }
CKPluginInfo *TransferWriterGetInfo(int index) { return index == 0 ? &TransferWriterInfo : nullptr; }
CKDataReader *TransferWriterFactory(int) { return new TransferWriter; }

// A transfer endpoint with actual input/output bytes. It observes the renderer
// boundary and deliberately does not pretend to render or emulate a GPU.
class TransferBoundary : public MaterialRenderObserver {
public:
    CKBOOL Idle = TRUE, HasRegion = FALSE;
    int Reads = 0, Writes = 0, FailRead = -1;
    int ReadWidth = 2, ReadHeight = 2, ReadPitch = 12;
    int WriteResult = 19;
    CKRECT Region = {};
    VXBUFFER_TYPE Buffer = VXBUFFER_BACKBUFFER;
    VxImageDescEx Uploaded = {};
    CKBYTE ReadPixels[24] = {
        0x11,0x22,0x33,0xff,0x44,0x55,0x66,0xff,0xaa,0xaa,0xaa,0xaa,
        0x77,0x88,0x99,0xff,0xbb,0xcc,0xdd,0xff,0xee,0xee,0xee,0xee};
    CKBYTE Written[128] = {};
    CKBOOL IsIdle() const override { return Idle; }
    void Capture(const CKRECT *rect, VXBUFFER_TYPE buffer) {
        HasRegion = rect != nullptr;
        if (rect) Region = *rect;
        Buffer = buffer;
    }
    int CopyToMemoryBuffer(const CKRECT *rect, VXBUFFER_TYPE buffer, VxImageDescEx &image) override {
        Capture(rect, buffer);
        const int call = Reads++;
        if (call == FailRead) return 0;
        CKBYTE *pixels = image.Image;
        VxPixelFormat2ImageDesc(_32_ARGB8888, image);
        image.Width = ReadWidth; image.Height = ReadHeight; image.BytesPerLine = ReadPitch;
        image.Image = pixels;
        if (pixels) memcpy(pixels, ReadPixels, ReadPitch * ReadHeight);
        return ReadPitch * ReadHeight;
    }
    int CopyFromMemoryBuffer(const CKRECT *rect, VXBUFFER_TYPE buffer,
                             const VxImageDescEx &image) override {
        Capture(rect, buffer); ++Writes; Uploaded = image;
        if (!image.Image) return 0;
        const int rowBytes = image.Width * image.BitsPerPixel / 8;
        for (int row = 0; row < image.Height; ++row)
            memcpy(Written + row * rowBytes, image.Image + row * image.BytesPerLine, rowBytes);
        return WriteResult;
    }
};

class RenderContextTransferCompletionTest : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderContext *Device = nullptr;
    RCKRenderManager *Manager = nullptr;
    TransferBoundary Boundary;
    char Path[1024] = {};
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr();
        ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT);
        CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0));
        Manager = new RCKRenderManager(Context);
        Device = new RCKRenderContext(Context);
        Device->m_RasterizerContext = &Boundary;
        Device->m_Settings.m_Rect = CKRECT{11, 17, 640, 480};
        TransferWriterInfo.m_GUID = CKGUID(0x12700001, 7);
        TransferWriterInfo.m_Extension = CKFileExtension("rc7");
        TransferWriterInfo.m_Type = CKPLUGIN_BITMAP_READER;
        ASSERT_EQ(CK_OK, CKGetPluginManager()->RegisterStaticPlugin("Render context transfer fixture",
            TransferWriterCount, TransferWriterGetInfo, TransferWriterFactory));
        WriterCalls = 0; WriterAccepts = true;
        std::snprintf(Path, sizeof(Path), "%srender-context-transfer-%llu.rc7",
            ::testing::TempDir().c_str(),
            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(this)));
    }
    void TearDown() override {
        Device->m_RasterizerContext = nullptr;
        delete Device;
        CKCloseContext(Context);
        CKShutdown();
        std::remove(Path);
    }
    void ExpectFilePixels() {
        FILE *file = std::fopen(Path, "rb"); ASSERT_NE(nullptr, file);
        CKBYTE pixels[17] = {};
        const size_t size = std::fread(pixels, 1, sizeof(pixels), file);
        std::fclose(file);
        ASSERT_EQ(16u, size);
        EXPECT_EQ(0, memcmp(Boundary.ReadPixels, pixels, 8));
        EXPECT_EQ(0, memcmp(Boundary.ReadPixels + 12, pixels + 8, 8));
    }
};
}

TEST_F(RenderContextTransferCompletionTest, DumpUsesBackendSizeMetadataAndPaddedPixelRows) {
    VxImageDescEx image = {};
    EXPECT_EQ(24, Device->DumpToMemory(nullptr, VXBUFFER_BACKBUFFER, image));
    EXPECT_EQ(2, image.Width); EXPECT_EQ(2, image.Height); EXPECT_EQ(12, image.BytesPerLine);
    EXPECT_EQ(32, image.BitsPerPixel); EXPECT_EQ(nullptr, image.Image); EXPECT_FALSE(Boundary.HasRegion);
    CKBYTE pixels[24] = {}; image.Image = pixels;
    EXPECT_EQ(24, Device->DumpToMemory(nullptr, VXBUFFER_BACKBUFFER, image));
    EXPECT_EQ(0, memcmp(pixels, Boundary.ReadPixels, sizeof(pixels))); EXPECT_EQ(2, Boundary.Reads);
}

TEST_F(RenderContextTransferCompletionTest, DumpAdaptsFractionalRectWithoutWindowClampingAndForwardsBuffer) {
    VxRect source(-1.75f, 2.75f, 701.9f, 902.9f); VxImageDescEx image = {};
    EXPECT_EQ(24, Device->DumpToMemory(&source, VXBUFFER_ZBUFFER, image));
    EXPECT_TRUE(Boundary.HasRegion); EXPECT_EQ(-1, Boundary.Region.left); EXPECT_EQ(2, Boundary.Region.top);
    EXPECT_EQ(701, Boundary.Region.right); EXPECT_EQ(902, Boundary.Region.bottom);
    EXPECT_EQ(VXBUFFER_ZBUFFER, Boundary.Buffer);
}

TEST_F(RenderContextTransferCompletionTest, BusyAndMissingReadbackDeviceDoNotAttemptSynchronousTransfers) {
    VxImageDescEx image = {}; Boundary.Idle = FALSE;
    EXPECT_EQ(0, Device->DumpToMemory(nullptr, VXBUFFER_BACKBUFFER, image));
    Device->m_RasterizerContext = nullptr;
    EXPECT_EQ(0, Device->DumpToMemory(nullptr, VXBUFFER_BACKBUFFER, image));
    EXPECT_EQ(0, Boundary.Reads);
}

TEST_F(RenderContextTransferCompletionTest, UploadUsesCurrentTargetInsteadOfCachedWindowSizeAndPreservesSourceFormat) {
    CKWORD pixels[] = {0xf800, 0x07e0, 0xdead, 0x001f, 0xffff, 0xbeef};
    VxImageDescEx image; VxPixelFormat2ImageDesc(_16_RGB565, image);
    image.Width = 2; image.Height = 2; image.BytesPerLine = 6; image.Image = reinterpret_cast<CKBYTE *>(pixels);
    EXPECT_EQ(19, Device->CopyToVideo(nullptr, VXBUFFER_BACKBUFFER, image));
    EXPECT_EQ(1, Boundary.Writes); EXPECT_FALSE(Boundary.HasRegion); EXPECT_EQ(16, Boundary.Uploaded.BitsPerPixel);
    EXPECT_EQ(6, Boundary.Uploaded.BytesPerLine); EXPECT_EQ(image.Image, Boundary.Uploaded.Image);
    const CKWORD expected[] = {0xf800, 0x07e0, 0x001f, 0xffff};
    EXPECT_EQ(0, memcmp(expected, Boundary.Written, sizeof(expected)));
}

TEST_F(RenderContextTransferCompletionTest, UploadForwardsRegionBufferAndExactBackendFailure) {
    CKDWORD pixel = 0xff123456; VxImageDescEx image; VxPixelFormat2ImageDesc(_32_ARGB8888, image);
    image.Width = image.Height = 1; image.BytesPerLine = 4; image.Image = reinterpret_cast<CKBYTE *>(&pixel);
    VxRect region(-3.5f, -2.5f, 900.5f, 700.5f); Boundary.WriteResult = 0;
    EXPECT_EQ(0, Device->CopyToVideo(&region, VXBUFFER_STENCILBUFFER, image));
    EXPECT_EQ(1, Boundary.Writes); EXPECT_EQ(VXBUFFER_STENCILBUFFER, Boundary.Buffer);
    EXPECT_EQ(-3, Boundary.Region.left); EXPECT_EQ(-2, Boundary.Region.top);
    EXPECT_EQ(900, Boundary.Region.right); EXPECT_EQ(700, Boundary.Region.bottom);
}

TEST_F(RenderContextTransferCompletionTest, UploadWithoutPixelsDoesNotInventBackendSuccess) {
    VxImageDescEx image = {}; image.Width = 640; image.Height = 480; image.BitsPerPixel = 32;
    EXPECT_EQ(0, Device->CopyToVideo(nullptr, VXBUFFER_BACKBUFFER, image)); EXPECT_EQ(1, Boundary.Writes);
    Device->m_RasterizerContext = nullptr;
    EXPECT_EQ(0, Device->CopyToVideo(nullptr, VXBUFFER_BACKBUFFER, image)); EXPECT_EQ(1, Boundary.Writes);
}

TEST_F(RenderContextTransferCompletionTest, DumpFileSelectsRegisteredWriterAndWritesActualUnpaddedPixels) {
    EXPECT_EQ(CK_OK, Device->DumpToFile(Path, nullptr, VXBUFFER_BACKBUFFER));
    EXPECT_EQ(2, Boundary.Reads); EXPECT_EQ(1, WriterCalls); ExpectFilePixels();
}

TEST_F(RenderContextTransferCompletionTest, DumpFileSupportsBuffersAcceptedByTheBackend) {
    VxRect region(1, 2, 3, 4);
    EXPECT_EQ(CK_OK, Device->DumpToFile(Path, &region, VXBUFFER_ZBUFFER));
    EXPECT_EQ(VXBUFFER_ZBUFFER, Boundary.Buffer); EXPECT_TRUE(Boundary.HasRegion);
    EXPECT_EQ(1, Boundary.Region.left); EXPECT_EQ(4, Boundary.Region.bottom); ExpectFilePixels();
}

TEST_F(RenderContextTransferCompletionTest, DumpFileReturnsNativeReadFailureWithoutInvokingWriter) {
    Boundary.FailRead = 0;
    EXPECT_EQ(CKERR_INVALIDPARAMETER, Device->DumpToFile(Path, nullptr, VXBUFFER_BACKBUFFER));
    EXPECT_EQ(1, Boundary.Reads); EXPECT_EQ(0, WriterCalls);
    Boundary.Reads = 0; Boundary.FailRead = 1;
    EXPECT_EQ(CKERR_INVALIDPARAMETER, Device->DumpToFile(Path, nullptr, VXBUFFER_BACKBUFFER));
    EXPECT_EQ(2, Boundary.Reads); EXPECT_EQ(0, WriterCalls);
}

TEST_F(RenderContextTransferCompletionTest, DumpFileReturnsNativeWriterFailureAndRejectsMissingInput) {
    WriterAccepts = false;
    EXPECT_EQ(CKERR_NOSAVEPLUGINS, Device->DumpToFile(Path, nullptr, VXBUFFER_BACKBUFFER));
    EXPECT_EQ(2, Boundary.Reads); EXPECT_EQ(1, WriterCalls);
    EXPECT_EQ(CKERR_INVALIDPARAMETER, Device->DumpToFile(nullptr, nullptr, VXBUFFER_BACKBUFFER));
    Device->m_RasterizerContext = nullptr;
    EXPECT_EQ(CKERR_INVALIDRENDERCONTEXT, Device->DumpToFile(Path, nullptr, VXBUFFER_BACKBUFFER));
}

TEST_F(RenderContextTransferCompletionTest, CpuFallbackRemovesVbufferFlagAndStaleNativeLockState) {
    Device->m_DpFlags = CKRST_DP_VBUFFER; Device->m_VertexBufferIndex = 37;
    Device->m_StartIndex = 8; Device->m_VertexBufferCount = 11;
    VxDrawPrimitiveData *data = Device->GetDrawPrimitiveStructure(
        static_cast<CKRST_DPFLAGS>(CKRST_DP_VBUFFER | CKRST_DP_TRANSFORM | CKRST_DP_LIGHT), 3);
    ASSERT_NE(nullptr, data); ASSERT_NE(nullptr, data->PositionPtr); ASSERT_NE(nullptr, data->NormalPtr);
    EXPECT_EQ(0u, data->Flags & CKRST_DP_VBUFFER); EXPECT_EQ(0u, Device->m_DpFlags);
    EXPECT_EQ(0u, Device->m_VertexBufferIndex); EXPECT_EQ(-1, Device->m_StartIndex); EXPECT_EQ(0, Device->m_VertexBufferCount);
    static_cast<VxVector *>(data->NormalPtr)[2] = VxVector(1, 2, 3);
    EXPECT_FLOAT_EQ(3, static_cast<VxVector *>(data->NormalPtr)[2].z);
}

TEST_F(RenderContextTransferCompletionTest, DrawStreamsAllocateAlignedIndependentStorageThroughEveryTextureStageAndTween) {
    UserDrawPrimitiveDataClass storage;
    const CKRST_DPFLAGS flags = static_cast<CKRST_DPFLAGS>(
        CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR | CKRST_DP_TWEEN | CKRST_DP_STAGES7);
    VxDrawPrimitiveData *data = storage.GetStructure(flags, 5);
    ASSERT_NE(nullptr, data); EXPECT_EQ(5, data->VertexCount); EXPECT_EQ(CKDWORD(flags), data->Flags);
    void *streams[] = {data->PositionPtr, data->NormalPtr, data->ColorPtr, data->SpecularColorPtr,
        data->TexCoordPtr, data->TweenPositionPtr, data->TweenNormalPtr};
    for (void *stream : streams) {
        ASSERT_NE(nullptr, stream); EXPECT_EQ(0u, reinterpret_cast<std::uintptr_t>(stream) & 15u);
    }
    EXPECT_EQ(sizeof(VxVector4), data->PositionStride); EXPECT_EQ(sizeof(VxVector), data->NormalStride);
    EXPECT_EQ(sizeof(VxVector), data->TweenPositionStride); EXPECT_EQ(sizeof(VxVector), data->TweenNormalStride);
    for (int stage = 0; stage < 8; ++stage) {
        void *stream = stage ? data->TexCoordPtrs[stage - 1] : data->TexCoordPtr;
        ASSERT_NE(nullptr, stream); EXPECT_EQ(0u, reinterpret_cast<std::uintptr_t>(stream) & 15u);
        const unsigned stride = stage ? data->TexCoordStrides[stage - 1] : data->TexCoordStride;
        EXPECT_EQ(sizeof(Vx2DVector), stride);
        static_cast<Vx2DVector *>(stream)[4] = Vx2DVector(float(stage), float(stage + 10));
    }
    for (int stage = 0; stage < 8; ++stage) {
        void *stream = stage ? data->TexCoordPtrs[stage - 1] : data->TexCoordPtr;
        EXPECT_FLOAT_EQ(float(stage + 10), static_cast<Vx2DVector *>(stream)[4].y);
    }
}

TEST_F(RenderContextTransferCompletionTest, ReusingAndGrowingDrawStreamsPreservesAllocationAndMasksOptionalPointers) {
    UserDrawPrimitiveDataClass storage;
    VxDrawPrimitiveData *data = storage.GetStructure(CKRST_DP_TRANSFORM, 3);
    void *position = data->PositionPtr; ASSERT_NE(nullptr, position);
    EXPECT_EQ(nullptr, data->NormalPtr); EXPECT_EQ(nullptr, data->ColorPtr); EXPECT_EQ(nullptr, data->SpecularColorPtr);
    EXPECT_EQ(nullptr, data->TweenPositionPtr); EXPECT_EQ(nullptr, data->TweenNormalPtr);
    data = storage.GetStructure(static_cast<CKRST_DPFLAGS>(CKRST_DP_DIFFUSE | CKRST_DP_SPECULAR), 2);
    EXPECT_EQ(position, data->PositionPtr); EXPECT_EQ(3, storage.m_MaxVertexCount);
    ASSERT_NE(nullptr, data->ColorPtr); ASSERT_NE(nullptr, data->SpecularColorPtr);
    EXPECT_EQ(0xff000000u, static_cast<CKDWORD *>(data->SpecularColorPtr)[1]);
    data = storage.GetStructure(static_cast<CKRST_DPFLAGS>(CKRST_DP_TRANSFORM | CKRST_DP_SPECULAR | CKRST_DP_TWEEN), 9);
    EXPECT_EQ(9, storage.m_MaxVertexCount); EXPECT_EQ(9, data->VertexCount);
    ASSERT_NE(nullptr, data->TweenPositionPtr); EXPECT_EQ(nullptr, data->TweenNormalPtr);
    static_cast<VxVector *>(data->TweenPositionPtr)[8] = VxVector(4, 5, 6);
    EXPECT_FLOAT_EQ(6, static_cast<VxVector *>(data->TweenPositionPtr)[8].z);
    EXPECT_EQ(0u, static_cast<CKDWORD *>(data->SpecularColorPtr)[8]);
}

TEST_F(RenderContextTransferCompletionTest, ClearedDrawStorageCanBeReacquiredAtThePreviousVertexCount) {
    UserDrawPrimitiveDataClass storage;
    ASSERT_NE(nullptr, storage.GetStructure(CKRST_DP_TRANSFORM, 7)->PositionPtr);
    storage.ClearStructure();
    EXPECT_EQ(nullptr, storage.PositionPtr); EXPECT_EQ(0, storage.m_MaxVertexCount);
    storage.ClearStructure();
    VxDrawPrimitiveData *data = storage.GetStructure(CKRST_DP_TRANSFORM, 7);
    ASSERT_NE(nullptr, data->PositionPtr); EXPECT_EQ(7, storage.m_MaxVertexCount);
    static_cast<VxVector4 *>(data->PositionPtr)[6] = VxVector4(2, 3, 4, 1);
    EXPECT_FLOAT_EQ(4, static_cast<VxVector4 *>(data->PositionPtr)[6].z);
}

TEST_F(RenderContextTransferCompletionTest, IndexScratchReusesCapacityAndPublicWrapperReturnsWritableIndices) {
    EXPECT_EQ(nullptr, Device->GetDrawPrimitiveIndices(0));
    CKWORD *indices = Device->GetDrawPrimitiveIndices(6); ASSERT_NE(nullptr, indices);
    indices[5] = 1234;
    EXPECT_EQ(indices, Device->GetDrawPrimitiveIndices(2)); EXPECT_EQ(1234, indices[5]);
    CKWORD *grown = Device->GetDrawPrimitiveIndices(15); ASSERT_NE(nullptr, grown);
    grown[14] = 4321; EXPECT_EQ(4321, Device->GetDrawPrimitiveIndices(15)[14]);
}

TEST_F(RenderContextTransferCompletionTest, SubmissionSnapshotKeepsSimpleExtentAndExtendedStageAndTweenData) {
    VxDrawPrimitiveData destination; memset(&destination, 0xab, sizeof(destination));
    UserDrawPrimitiveDataClass::CopySubmissionData(destination, nullptr);
    EXPECT_EQ(nullptr, destination.PositionPtr); EXPECT_EQ(nullptr, destination.TweenPositionPtr);
    VxVector position(1, 2, 3); VxDrawPrimitiveDataSimple simple = {};
    simple.VertexCount = 1; simple.Flags = CKRST_DP_TRANSFORM;
    simple.PositionPtr = &position; simple.PositionStride = sizeof(position);
    UserDrawPrimitiveDataClass::CopySubmissionData(destination, reinterpret_cast<VxDrawPrimitiveData *>(&simple));
    EXPECT_EQ(&position, destination.PositionPtr); EXPECT_EQ(nullptr, destination.TexCoordPtrs[6]);
    EXPECT_EQ(nullptr, destination.TweenPositionPtr); EXPECT_EQ(1, destination.VertexCount);
    Vx2DVector uv(4, 5); VxVector tween(6, 7, 8); VxDrawPrimitiveData extended = {};
    extended.Flags = CKRST_DP_TRANSFORM | CKRST_DP_STAGES7 | CKRST_DP_TWEEN;
    extended.TexCoordPtrs[6] = &uv; extended.TexCoordStrides[6] = sizeof(uv);
    extended.TweenPositionPtr = &tween; extended.TweenPositionStride = sizeof(tween);
    UserDrawPrimitiveDataClass::CopySubmissionData(destination, &extended);
    EXPECT_EQ(&uv, destination.TexCoordPtrs[6]); EXPECT_EQ(sizeof(uv), destination.TexCoordStrides[6]);
    EXPECT_EQ(&tween, destination.TweenPositionPtr); EXPECT_EQ(sizeof(tween), destination.TweenPositionStride);
    EXPECT_FALSE(std::is_copy_constructible<UserDrawPrimitiveDataClass>::value);
    EXPECT_FALSE(std::is_copy_assignable<UserDrawPrimitiveDataClass>::value);
}
