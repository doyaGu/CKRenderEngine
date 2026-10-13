#ifndef TEXTURE_TEST_OBSERVER_H
#define TEXTURE_TEST_OBSERVER_H

#include "CKAll.h"
#include "RCKTexture.h"
#include "RCKRenderObject.h"
#include "RCK3dEntity.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "MaterialRenderObserver.h"

extern void SetProcessorSpecific_FunctionsPtr();

// This observer captures public API inputs, with controllable acknowledgements.
// It does not implement texture storage, sampling, rendering or GPU transfers.
class TextureTestObserver : public MaterialRenderObserver {
public:
    struct Upload {
        CKDWORD Handle = 0;
        VxImageDescEx Image;
        int Level = 0;
        CKRST_CUBEFACE Face = CKRST_CUBEFACE_XPOS;
        CKBOOL HasRegion = FALSE;
        CKRECT Region = {};
        CKBYTE Bytes[4096] = {};
    };
    CKTextureDesc Created[32];
    CKTextureDesc Resource[64];
    CKBOOL Live[64] = {};
    Upload Uploads[32];
    CKDWORD NextHandle = 1;
    int Creates = 0, Loads = 0, Deletes = 0, Binds = 0, Readbacks = 0;
    CKBOOL CreateResult = TRUE, DeleteResult = TRUE, BindResult = TRUE, ReadbackResult = TRUE;
    CKBOOL QueryResult = TRUE;
    VX_PIXELFORMAT ActualFormat = UNKNOWN_PF;
    int FailLoad = -1;
    CKReadbackCallback Pending = nullptr;
    void *PendingUser = nullptr;
    CKBOOL HasReadbackRegion = FALSE;
    CKRECT ReadbackRegion = {};
    VXBUFFER_TYPE ReadbackBuffer = VXBUFFER_BACKBUFFER;

    CKBOOL CreateTexture(const CKTextureDesc *desc, CKDWORD *handle) override {
        EXPECT_LT(Creates, 32);
        Created[Creates++] = *desc;
        if (!CreateResult) return FALSE;
        EXPECT_LT(NextHandle, 64u);
        *handle = NextHandle++;
        Resource[*handle] = *desc;
        if (ActualFormat != UNKNOWN_PF) {
            VxPixelFormat2ImageDesc(ActualFormat, Resource[*handle].Format);
            Resource[*handle].Format.Width = desc->Format.Width;
            Resource[*handle].Format.Height = desc->Format.Height;
            Resource[*handle].Flags &= ~CKRST_TEXTURE_ALPHA;
            if (Resource[*handle].Format.AlphaMask || Resource[*handle].Format.Flags >= _DXT1)
                Resource[*handle].Flags |= CKRST_TEXTURE_ALPHA;
        }
        Live[*handle] = TRUE;
        return TRUE;
    }
    CKBOOL GetTextureDesc(CKDWORD handle, CKTextureDesc *desc) const override {
        if (!QueryResult || !handle || handle >= 64 || !Live[handle]) return FALSE;
        *desc = Resource[handle]; return TRUE;
    }
    CKBOOL LoadTexture(CKDWORD handle, const VxImageDescEx &image, int level,
                       CKRST_CUBEFACE face, const CKRECT *region) override {
        EXPECT_LT(Loads, 32);
        Upload &upload = Uploads[Loads];
        upload.Handle = handle; upload.Image = image; upload.Level = level; upload.Face = face;
        upload.HasRegion = region != nullptr;
        if (region) upload.Region = *region;
        const int rowBytes = image.Width * image.BitsPerPixel / 8;
        EXPECT_LE(rowBytes * image.Height, 4096);
        for (int row = 0; row < image.Height; ++row)
            memcpy(upload.Bytes + row * rowBytes, image.Image + row * image.BytesPerLine, rowBytes);
        const int call = Loads++;
        return handle && handle < 64 && Live[handle] && call != FailLoad;
    }
    CKBOOL DeleteObject(CKRST_HANDLE handle, CKRST_OBJECTTYPE type) override {
        EXPECT_EQ(CKRST_OBJ_TEXTURE, type); ++Deletes;
        if (!DeleteResult) return FALSE;
        if (!handle || handle >= 64 || !Live[handle]) return FALSE;
        Live[handle] = FALSE; return TRUE;
    }
    CKBOOL SetTexture(CKDWORD handle, int stage) override {
        ++Binds;
        if (stage < 0 || stage >= CKRST_MAX_TEXTURE_STAGES || (handle && !BindResult)) return FALSE;
        Textures[stage] = handle; return TRUE;
    }
    CKBOOL RequestReadback(const CKRECT *rect, VXBUFFER_TYPE buffer,
                          CKReadbackCallback callback, void *user) override {
        ++Readbacks;
        if (!ReadbackResult) return FALSE;
        EXPECT_EQ(nullptr, Pending); Pending = callback; PendingUser = user;
        HasReadbackRegion = rect != nullptr;
        if (rect) ReadbackRegion = *rect;
        ReadbackBuffer = buffer; return TRUE;
    }
    void Complete(const VxImageDescEx *image, CKBOOL success = TRUE) {
        ASSERT_NE(nullptr, Pending);
        CKReadbackCallback callback = Pending; void *user = PendingUser;
        Pending = nullptr; PendingUser = nullptr;
        callback(user, HasReadbackRegion ? &ReadbackRegion : nullptr, ReadbackBuffer, image, success);
    }
};

class TextureTestProbe : public RCKTexture {
public:
    explicit TextureTestProbe(CKContext *context) : RCKTexture(context) {}
    using RCKTexture::ApplyContextCopy;
    CKDWORD BitmapFlags() const { return m_BitmapFlags; }
    CKDWORD TextureFlags() const { return m_TextureFlags; }
    CKDWORD CachedLevels() const { return m_CachedMipMapCount; }
    CKRasterizerContext *SelectedContext() const { return m_RasterizerContext; }
    void BitmapFlags(CKDWORD flags) { m_BitmapFlags = flags; }
};

class TextureTestWorld : public ::testing::Test {
protected:
    CKContext *Context = nullptr;
    RCKRenderManager *Manager = nullptr;
    RCKRenderContext *Device = nullptr, *OtherDevice = nullptr;
    TextureTestObserver State, OtherState;
    CKRasterizerDriver Driver;
    void SetUp() override {
        SetProcessorSpecific_FunctionsPtr(); ASSERT_EQ(CK_OK, CKStartUp());
        CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
        CKCLASSREGISTERCID(RCK3dEntity, CKCID_RENDEROBJECT);
        CKCLASSREGISTERCID(RCKTexture, CKCID_BEOBJECT); CKBuildClassHierarchyTable();
        ASSERT_EQ(CK_OK, CKCreateContext(&Context, nullptr, 0, 0)); Manager = new RCKRenderManager(Context);
        Manager->m_DriverCount = 1; Manager->m_Drivers = new VxDriverDescEx[1]();
        Manager->m_Drivers[0].CapsUpToDate = TRUE;
        Manager->m_Drivers[0].RasterizerDriver = &Driver;
        Manager->m_Drivers[0].Caps3D.CKRasterizerSpecificCaps = CKRST_SPECIFICCAPS_CLAMPEDGEALPHA;
        VxImageDescEx alpha; VxPixelFormat2ImageDesc(_32_ARGB8888, alpha);
        Manager->m_Drivers[0].TextureFormats.PushBack(alpha);
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&State, 0));
        ASSERT_TRUE(Manager->RegisterRasterizerContext(&OtherState, 0));
        Device = new RCKRenderContext(Context); OtherDevice = new RCKRenderContext(Context);
        Device->m_RasterizerContext = &State; OtherDevice->m_RasterizerContext = &OtherState;
        Device->m_RasterizerDriver = OtherDevice->m_RasterizerDriver = &Driver;
        Device->m_DriverIndex = OtherDevice->m_DriverIndex = 0;
    }
    void TearDown() override {
        if (State.Pending) State.Complete(nullptr, FALSE);
        if (OtherState.Pending) OtherState.Complete(nullptr, FALSE);
        if (Device) { Device->m_RasterizerContext = nullptr; Device->m_RasterizerDriver = nullptr; delete Device; }
        if (OtherDevice) { OtherDevice->m_RasterizerContext = nullptr; OtherDevice->m_RasterizerDriver = nullptr; delete OtherDevice; }
        if (Context) CKCloseContext(Context); CKShutdown();
    }
    static void Pixels(RCKTexture &texture, const CKDWORD *pixels, int width, int height, int slot = 0) {
        ASSERT_TRUE(texture.CreateImage(width, height, 32, slot));
        CKBYTE *data = texture.LockSurfacePtr(slot); ASSERT_NE(nullptr, data);
        memcpy(data, pixels, width * height * 4); texture.ReleaseSurfacePtr(slot);
    }
    static VxImageDescEx Image(CKDWORD *pixels, int width, int height, int pitch = 0) {
        VxImageDescEx desc; VxPixelFormat2ImageDesc(_32_ARGB8888, desc);
        desc.Width = width; desc.Height = height; desc.BytesPerLine = pitch ? pitch : width * 4;
        desc.Image = reinterpret_cast<CKBYTE *>(pixels); return desc;
    }
    static CKDWORD Pixel(const TextureTestObserver::Upload &upload, int index) {
        CKDWORD value; memcpy(&value, upload.Bytes + index * 4, 4); return value;
    }
};

#endif
