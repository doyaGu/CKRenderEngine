#include "CKDX9Rasterizer.h"
#include "TestTriangleMultiset.h"

#if defined(_WIN32)
#include <windows.h>
#endif
#include <string.h>

namespace {

#if defined(_WIN32)
HWND CreateTestWindow()
{
    HINSTANCE instance = GetModuleHandleA(NULL);
    const char *className = "CKDX9RasterizerTestWindow";

    WNDCLASSA wc = {};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = instance;
    wc.lpszClassName = className;
    RegisterClassA(&wc);

    return CreateWindowExA(0, className, "CKDX9 test", WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT, 64, 64,
                           NULL, NULL, instance, NULL);
}

class GuardedDX9Rasterizer : public CKDX9Rasterizer
{
public:
    GuardedDX9Rasterizer()
        : m_Buffer(NULL), m_BufferBytes(0), m_GuardOffset(0), m_GuardSize(64)
    {
    }

    ~GuardedDX9Rasterizer()
    {
        delete[] m_Buffer;
    }

    CKBYTE *AllocateObjects(int size) override
    {
        delete[] m_Buffer;
        m_GuardOffset = size * sizeof(XDWORD);
        m_BufferBytes = m_GuardOffset + 128 * 128 * 4 + m_GuardSize;
        m_Buffer = new CKBYTE[m_BufferBytes];
        memset(m_Buffer, 0, m_BufferBytes);
        memset(m_Buffer + m_GuardOffset, 0xA5, m_GuardSize);
        return m_Buffer;
    }

    bool GuardIntact() const
    {
        if (!m_Buffer)
            return false;

        for (size_t i = 0; i < m_GuardSize; ++i)
        {
            if (m_Buffer[m_GuardOffset + i] != 0xA5)
                return false;
        }
        return true;
    }

private:
    CKBYTE *m_Buffer;
    size_t m_BufferBytes;
    size_t m_GuardOffset;
    const size_t m_GuardSize;
};

class FakeFormatD3D9 : public IDirect3D9
{
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void **) { return E_NOINTERFACE; }
    ULONG STDMETHODCALLTYPE AddRef() { return 1; }
    ULONG STDMETHODCALLTYPE Release() { return 1; }
    HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void *) { return D3DERR_INVALIDCALL; }
    UINT STDMETHODCALLTYPE GetAdapterCount() { return 1; }
    HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT, DWORD, D3DADAPTER_IDENTIFIER9 *) { return D3DERR_INVALIDCALL; }
    UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT, D3DFORMAT) { return 0; }
    HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT, D3DFORMAT, UINT, D3DDISPLAYMODE *) { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT, D3DDISPLAYMODE *) { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, BOOL) { return D3DERR_INVALIDCALL; }

    HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT, D3DDEVTYPE, D3DFORMAT, DWORD Usage,
                                                D3DRESOURCETYPE RType, D3DFORMAT CheckFormat)
    {
        if ((Usage & D3DUSAGE_RENDERTARGET) == 0)
            return D3DERR_NOTAVAILABLE;

        if (RType == D3DRTYPE_TEXTURE && CheckFormat == D3DFMT_A8R8G8B8)
            return D3D_OK;

        if (RType == D3DRTYPE_TEXTURE && CheckFormat == D3DFMT_X8R8G8B8)
            return D3D_OK;

        if (RType == D3DRTYPE_CUBETEXTURE && CheckFormat == D3DFMT_X8R8G8B8)
            return D3D_OK;

        return D3DERR_NOTAVAILABLE;
    }

    HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT, D3DDEVTYPE, D3DFORMAT, BOOL,
                                                         D3DMULTISAMPLE_TYPE, DWORD *) { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, D3DFORMAT) { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT) { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT, D3DDEVTYPE, D3DCAPS9 *) { return D3DERR_INVALIDCALL; }
    HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT) { return NULL; }
    HRESULT STDMETHODCALLTYPE CreateDevice(UINT, D3DDEVTYPE, HWND, DWORD,
                                           D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **) { return D3DERR_INVALIDCALL; }
};
#endif

void UnknownD3DFormatMapsToUnknownPixelFormat()
{
    TestCheck(D3DFormatToVxPixelFormat(D3DFMT_UNKNOWN) == UNKNOWN_PF,
              "D3DFMT_UNKNOWN must not map to a concrete VX pixel format");
}

void Argb8888MapsToD3DFormat()
{
    TestCheck(VxPixelFormatToD3DFormat(_32_ARGB8888) == D3DFMT_A8R8G8B8,
              "_32_ARGB8888 should map to D3DFMT_A8R8G8B8");
}

void A8R8G8B8TextureDescCarriesRgbAlphaAndBpp()
{
    CKTextureDesc desc;
    D3DFormatToTextureDesc(D3DFMT_A8R8G8B8, &desc);

    TestCheck((desc.Flags & CKRST_TEXTURE_VALID) != 0,
              "A8R8G8B8 texture desc should be marked valid");
    TestCheck((desc.Flags & CKRST_TEXTURE_RGB) != 0,
              "A8R8G8B8 texture desc should be marked RGB");
    TestCheck((desc.Flags & CKRST_TEXTURE_ALPHA) != 0,
              "A8R8G8B8 texture desc should be marked alpha");
    TestCheck(desc.Format.BitsPerPixel == 32,
              "A8R8G8B8 texture desc should be 32 bpp");
}

void UnsupportedD3DFormatDoesNotCreateValidTextureDesc()
{
    CKTextureDesc desc;
    D3DFormatToTextureDesc(D3DFMT_A8, &desc);

    TestCheck((desc.Flags & CKRST_TEXTURE_VALID) == 0,
              "Unsupported D3D formats should not create valid texture descs");
    TestCheck(desc.Format.BitsPerPixel == 0,
              "Unsupported D3D formats should leave texture desc format empty");
}

void SetLightRejectsNullData()
{
    CKDX9Rasterizer rasterizer;
    CKDX9RasterizerDriver driver(&rasterizer);
    CKDX9RasterizerContext context(&driver);

    TestCheck(context.SetLight(0, NULL) == FALSE,
              "SetLight should reject null light data");
}

void SetLightRejectsOutOfRangeIndex()
{
    CKDX9Rasterizer rasterizer;
    CKDX9RasterizerDriver driver(&rasterizer);
    CKDX9RasterizerContext context(&driver);
    CKLightData data = {};

    TestCheck(context.SetLight(RST_MAX_LIGHT, &data) == FALSE,
              "SetLight should reject out-of-range light indices");
}

void RenderTargetTextureUsesSingleSampleableResource()
{
#if !defined(_WIN32)
    return;
#else
    HWND window = CreateTestWindow();
    TestCheck(window != NULL, "DX9 render target test needs a hidden window");

    CKDX9Rasterizer rasterizer;
    if (!rasterizer.Start(window))
    {
        DestroyWindow(window);
        return;
    }

    if (rasterizer.GetDriverCount() == 0)
    {
        DestroyWindow(window);
        return;
    }

    CKDX9RasterizerDriver *driver = static_cast<CKDX9RasterizerDriver *>(rasterizer.GetDriver(0));
    CKDX9RasterizerContext *context = static_cast<CKDX9RasterizerContext *>(driver->CreateContext());
    TestCheck(context != NULL, "DX9 driver should create a context");
    TestCheck(context->Create(window, 0, 0, 64, 64, 32, FALSE, 0, 24, 8) == TRUE,
              "DX9 test context should initialize");

    CKDWORD texture = rasterizer.CreateObjectIndex(CKRST_OBJ_TEXTURE);
    TestCheck(context->SetTargetTexture(texture, 64, 64) == TRUE,
              "SetTargetTexture should create a 2D render target texture");

    CKDX9TextureDesc *desc = static_cast<CKDX9TextureDesc *>(context->m_Textures[texture]);
    TestCheck(desc != NULL, "Render target texture should have a descriptor");
    TestCheck(desc->DxTexture != NULL, "Render target texture should keep the sampleable texture");
    TestCheck(desc->DxRenderTexture == NULL, "2D render targets should not keep a second render texture");

    IDirect3DSurface9 *surface = NULL;
    HRESULT hr = desc->DxTexture->GetSurfaceLevel(0, &surface);
    TestCheck(SUCCEEDED(hr) && surface != NULL, "Sampleable texture should expose a render surface");

    D3DSURFACE_DESC surfaceDesc = {};
    hr = surface->GetDesc(&surfaceDesc);
    SAFERELEASE(surface);
    TestCheck(SUCCEEDED(hr), "Render target surface should have a D3D description");
    TestCheck((surfaceDesc.Usage & D3DUSAGE_RENDERTARGET) != 0,
              "Sampleable texture surface should be the render target surface");

    TestCheck(context->SetTargetTexture(0) == TRUE,
              "SetTargetTexture(0) should restore the default render target");
    TestCheck((desc->Flags & CKRST_TEXTURE_RENDERTARGET) != 0,
              "Restoring the default target should not clear render-target identity");
    TestCheck(context->SetTexture(texture, 0) == TRUE,
              "Render target texture should remain bindable for sampling");

    driver->DestroyContext(context);
    DestroyWindow(window);
#endif
}

void ResizedTextureMipmapUploadStaysWithinTargetBuffer()
{
#if !defined(_WIN32)
    return;
#else
    HWND window = CreateTestWindow();
    TestCheck(window != NULL, "DX9 mipmap upload test needs a hidden window");

    GuardedDX9Rasterizer rasterizer;
    if (!rasterizer.Start(window))
    {
        DestroyWindow(window);
        return;
    }

    if (rasterizer.GetDriverCount() == 0)
    {
        DestroyWindow(window);
        return;
    }

    CKDX9RasterizerDriver *driver = static_cast<CKDX9RasterizerDriver *>(rasterizer.GetDriver(0));
    CKDX9RasterizerContext *context = static_cast<CKDX9RasterizerContext *>(driver->CreateContext());
    TestCheck(context != NULL, "DX9 driver should create a context");
    TestCheck(context->Create(window, 0, 0, 64, 64, 32, FALSE, 0, 24, 8) == TRUE,
              "DX9 mipmap test context should initialize");

    CKDWORD texture = rasterizer.CreateObjectIndex(CKRST_OBJ_TEXTURE);
    CKTextureDesc desired;
    desired.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA | CKRST_TEXTURE_MANAGED;
    VxPixelFormat2ImageDesc(_32_ARGB8888, desired.Format);
    desired.Format.Width = 64;
    desired.Format.Height = 64;
    desired.Format.BytesPerLine = 64 * 4;
    desired.MipMapCount = 2;
    TestCheck(context->CreateObject(texture, CKRST_OBJ_TEXTURE, &desired) == TRUE,
              "DX9 mipmap test texture should be created");

    CKBYTE image[128 * 128 * 4];
    memset(image, 0x7F, sizeof(image));

    VxImageDescEx source;
    VxPixelFormat2ImageDesc(_32_ARGB8888, source);
    source.Width = 128;
    source.Height = 128;
    source.BytesPerLine = 128 * 4;
    source.Image = image;

    TestCheck(context->LoadTexture(texture, source, -1) == TRUE,
              "DX9 resized mipmap texture upload should succeed");
    TestCheck(rasterizer.GuardIntact(),
              "DX9 resized mipmap upload must not write past the target-sized temporary buffer");

    driver->DestroyContext(context);
    DestroyWindow(window);
#endif
}

void ResizedCubeMipmapUploadStaysWithinTargetBuffer()
{
#if !defined(_WIN32)
    return;
#else
    HWND window = CreateTestWindow();
    TestCheck(window != NULL, "DX9 cube mipmap upload test needs a hidden window");

    GuardedDX9Rasterizer rasterizer;
    if (!rasterizer.Start(window))
    {
        DestroyWindow(window);
        return;
    }

    if (rasterizer.GetDriverCount() == 0)
    {
        DestroyWindow(window);
        return;
    }

    CKDX9RasterizerDriver *driver = static_cast<CKDX9RasterizerDriver *>(rasterizer.GetDriver(0));
    CKDX9RasterizerContext *context = static_cast<CKDX9RasterizerContext *>(driver->CreateContext());
    TestCheck(context != NULL, "DX9 driver should create a context");
    TestCheck(context->Create(window, 0, 0, 64, 64, 32, FALSE, 0, 24, 8) == TRUE,
              "DX9 cube mipmap test context should initialize");

    CKDWORD texture = rasterizer.CreateObjectIndex(CKRST_OBJ_TEXTURE);
    CKTextureDesc desired;
    desired.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA |
                    CKRST_TEXTURE_MANAGED | CKRST_TEXTURE_CUBEMAP;
    VxPixelFormat2ImageDesc(_32_ARGB8888, desired.Format);
    desired.Format.Width = 64;
    desired.Format.Height = 64;
    desired.Format.BytesPerLine = 64 * 4;
    desired.MipMapCount = 2;
    TestCheck(context->CreateObject(texture, CKRST_OBJ_TEXTURE, &desired) == TRUE,
              "DX9 cube mipmap test texture should be created");

    CKBYTE image[128 * 128 * 4];
    memset(image, 0x3F, sizeof(image));

    VxImageDescEx source;
    VxPixelFormat2ImageDesc(_32_ARGB8888, source);
    source.Width = 128;
    source.Height = 128;
    source.BytesPerLine = 128 * 4;
    source.Image = image;

    TestCheck(context->LoadCubeMapTexture(texture, source, CKRST_CUBEFACE_XPOS, -1) == TRUE,
              "DX9 resized cube mipmap upload should succeed");
    TestCheck(rasterizer.GuardIntact(),
              "DX9 resized cube mipmap upload must not write past the target-sized temporary buffer");

    driver->DestroyContext(context);
    DestroyWindow(window);
#endif
}

void CubeTextureFormatSearchRequiresCubeSupport()
{
#if !defined(_WIN32)
    return;
#else
    HWND window = CreateTestWindow();
    TestCheck(window != NULL, "DX9 cube format test needs a hidden window");

    CKDX9Rasterizer rasterizer;
    if (!rasterizer.Start(window))
    {
        DestroyWindow(window);
        return;
    }

    if (rasterizer.GetDriverCount() == 0)
    {
        DestroyWindow(window);
        return;
    }

    CKDX9RasterizerDriver *driver = static_cast<CKDX9RasterizerDriver *>(rasterizer.GetDriver(0));
    CKDX9RasterizerContext *context = static_cast<CKDX9RasterizerContext *>(driver->CreateContext());
    TestCheck(context != NULL, "DX9 driver should create a context");
    TestCheck(context->Create(window, 0, 0, 64, 64, 32, FALSE, 0, 24, 8) == TRUE,
              "DX9 cube format test context should initialize");

    CKDWORD texture = rasterizer.CreateObjectIndex(CKRST_OBJ_TEXTURE);
    CKTextureDesc desired;
    desired.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_CUBEMAP | CKRST_TEXTURE_MANAGED;
    VxPixelFormat2ImageDesc(_16_V8U8, desired.Format);
    desired.Format.Width = 64;
    desired.Format.Height = 64;
    desired.Format.BytesPerLine = 64 * 2;
    desired.MipMapCount = 0;

    TestCheck(context->CreateObject(texture, CKRST_OBJ_TEXTURE, &desired) == TRUE,
              "DX9 cubemap creation should fall back when the requested 2D format lacks cube support");

    CKDX9TextureDesc *desc = static_cast<CKDX9TextureDesc *>(context->m_Textures[texture]);
    TestCheck(desc != NULL && desc->DxCubeTexture != NULL,
              "DX9 cubemap fallback should create a cube texture");

    driver->DestroyContext(context);
    DestroyWindow(window);
#endif
}

void CubeRenderTargetFormatSearchRequiresCubeSupport()
{
#if !defined(_WIN32)
    return;
#else
    FakeFormatD3D9 fakeD3D9;
    CKDX9Rasterizer rasterizer;
    rasterizer.m_D3D9 = &fakeD3D9;

    CKDX9RasterizerDriver driver(&rasterizer);
    driver.m_AdapterIndex = 0;
    driver.m_DevType = D3DDEVTYPE_HAL;

    CKTextureDesc desired;
    desired.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_CUBEMAP | CKRST_TEXTURE_RENDERTARGET;
    VxPixelFormat2ImageDesc(_32_ARGB8888, desired.Format);

    D3DFORMAT selected = driver.FindNearestTextureFormat(&desired, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET);
    rasterizer.m_D3D9 = NULL;

    TestCheck(selected == D3DFMT_X8R8G8B8,
              "DX9 cubemap render target format search must require cube texture support");
#endif
}

void Pow2TextureCapsUseNextPowerOfTwo()
{
#if !defined(_WIN32)
    return;
#else
    HWND window = CreateTestWindow();
    TestCheck(window != NULL, "DX9 POW2 texture size test needs a hidden window");

    CKDX9Rasterizer rasterizer;
    if (!rasterizer.Start(window))
    {
        DestroyWindow(window);
        return;
    }

    if (rasterizer.GetDriverCount() == 0)
    {
        DestroyWindow(window);
        return;
    }

    CKDX9RasterizerDriver *driver = static_cast<CKDX9RasterizerDriver *>(rasterizer.GetDriver(0));
    CKDX9RasterizerContext *context = static_cast<CKDX9RasterizerContext *>(driver->CreateContext());
    TestCheck(context != NULL, "DX9 POW2 texture size test should create a context");
    TestCheck(context->Create(window, 0, 0, 64, 64, 32, FALSE, 0, 24, 8) == TRUE,
              "DX9 POW2 texture size context should initialize");

    driver->m_3DCaps.TextureCaps |= CKRST_TEXTURECAPS_POW2;

    CKDWORD texture = rasterizer.CreateObjectIndex(CKRST_OBJ_TEXTURE);
    CKTextureDesc desired;
    desired.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA | CKRST_TEXTURE_MANAGED;
    VxPixelFormat2ImageDesc(_32_ARGB8888, desired.Format);
    desired.Format.Width = 100;
    desired.Format.Height = 80;
    desired.Format.BytesPerLine = 100 * 4;
    desired.MipMapCount = 0;

    TestCheck(context->CreateObject(texture, CKRST_OBJ_TEXTURE, &desired) == TRUE,
              "DX9 POW2 texture size test should create the texture");

    CKDX9TextureDesc *desc = static_cast<CKDX9TextureDesc *>(context->m_Textures[texture]);
    TestCheck(desc != NULL && desc->DxTexture != NULL,
              "DX9 POW2 texture size test should keep a 2D texture");

    D3DSURFACE_DESC surfaceDesc = {};
    HRESULT hr = desc->DxTexture->GetLevelDesc(0, &surfaceDesc);
    TestCheck(SUCCEEDED(hr), "DX9 POW2 texture size test should read texture desc");
    TestCheck(surfaceDesc.Width == 128 && surfaceDesc.Height == 128,
              "DX9 POW2 texture sizing should use the next power-of-two");

    driver->DestroyContext(context);
    DestroyWindow(window);
#endif
}

void SpriteCreationUsesNextPowerOfTwoWhenItFits()
{
#if !defined(_WIN32)
    return;
#else
    HWND window = CreateTestWindow();
    TestCheck(window != NULL, "DX9 sprite POW2 sizing test needs a hidden window");

    CKDX9Rasterizer rasterizer;
    if (!rasterizer.Start(window))
    {
        DestroyWindow(window);
        return;
    }

    if (rasterizer.GetDriverCount() == 0)
    {
        DestroyWindow(window);
        return;
    }

    CKDX9RasterizerDriver *driver = static_cast<CKDX9RasterizerDriver *>(rasterizer.GetDriver(0));
    CKDX9RasterizerContext *context = static_cast<CKDX9RasterizerContext *>(driver->CreateContext());
    TestCheck(context != NULL, "DX9 sprite POW2 sizing test should create a context");
    TestCheck(context->Create(window, 0, 0, 64, 64, 32, FALSE, 0, 24, 8) == TRUE,
              "DX9 sprite POW2 sizing context should initialize");

    CKDWORD sprite = rasterizer.CreateObjectIndex(CKRST_OBJ_SPRITE);
    CKSpriteDesc desired;
    desired.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA | CKRST_TEXTURE_MANAGED;
    VxPixelFormat2ImageDesc(_32_ARGB8888, desired.Format);
    desired.Format.Width = 100;
    desired.Format.Height = 80;
    desired.Format.BytesPerLine = 100 * 4;
    desired.MipMapCount = 0;

    TestCheck(context->CreateObject(sprite, CKRST_OBJ_SPRITE, &desired) == TRUE,
              "DX9 sprite POW2 sizing test should create the sprite");

    CKSpriteDesc *desc = context->GetSpriteData(sprite);
    TestCheck(desc != NULL, "DX9 sprite POW2 sizing test should keep sprite data");
    TestCheck(desc->Textures.Size() == 1,
              "CKRasterizerLib sprite creation should use one next-POW2 texture when it fits");

    CKSPRTextInfo *info = desc->Textures.Begin();
    TestCheck(info != NULL && info->w == 100 && info->h == 80 && info->sw == 128 && info->sh == 128,
              "CKRasterizerLib sprite storage should cover the full sprite with next POW2 dimensions");

    driver->DestroyContext(context);
    DestroyWindow(window);
#endif
}

void RenderTargetTextureCanSwitchFromCubeTo2D()
{
#if !defined(_WIN32)
    return;
#else
    HWND window = CreateTestWindow();
    TestCheck(window != NULL, "DX9 render target switch test needs a hidden window");

    CKDX9Rasterizer rasterizer;
    if (!rasterizer.Start(window))
    {
        DestroyWindow(window);
        return;
    }

    if (rasterizer.GetDriverCount() == 0)
    {
        DestroyWindow(window);
        return;
    }

    CKDX9RasterizerDriver *driver = static_cast<CKDX9RasterizerDriver *>(rasterizer.GetDriver(0));
    CKDX9RasterizerContext *context = static_cast<CKDX9RasterizerContext *>(driver->CreateContext());
    TestCheck(context != NULL, "DX9 driver should create a context");
    TestCheck(context->Create(window, 0, 0, 64, 64, 32, FALSE, 0, 24, 8) == TRUE,
              "DX9 render target switch context should initialize");

    CKDWORD texture = rasterizer.CreateObjectIndex(CKRST_OBJ_TEXTURE);
    TestCheck(context->SetTargetTexture(texture, 64, -1, CKRST_CUBEFACE_XPOS) == TRUE,
              "SetTargetTexture should create a cubemap render target");
    TestCheck(context->SetTargetTexture(0) == TRUE,
              "SetTargetTexture(0) should restore the default render target after cubemap");

    TestCheck(context->SetTargetTexture(texture, 64, 64) == TRUE,
              "SetTargetTexture should recreate the same object as a 2D render target");
    CKDX9TextureDesc *desc = static_cast<CKDX9TextureDesc *>(context->m_Textures[texture]);
    TestCheck(desc != NULL, "Render target switch should keep a texture descriptor");
    TestCheck((desc->Flags & CKRST_TEXTURE_CUBEMAP) == 0,
              "2D render target recreation must clear cubemap identity");
    TestCheck(desc->DxTexture != NULL, "2D render target recreation should keep a 2D texture");

    TestCheck(context->SetTargetTexture(0) == TRUE,
              "SetTargetTexture(0) should restore the default render target after 2D recreation");
    TestCheck(context->SetTexture(texture, 0) == TRUE,
              "Recreated 2D render target should bind as a 2D texture");

    driver->DestroyContext(context);
    DestroyWindow(window);
#endif
}

void RenderTargetTextureUsesRequestedSizeForExistingObject()
{
#if !defined(_WIN32)
    return;
#else
    HWND window = CreateTestWindow();
    TestCheck(window != NULL, "DX9 render target size test needs a hidden window");

    CKDX9Rasterizer rasterizer;
    if (!rasterizer.Start(window))
    {
        DestroyWindow(window);
        return;
    }

    if (rasterizer.GetDriverCount() == 0)
    {
        DestroyWindow(window);
        return;
    }

    CKDX9RasterizerDriver *driver = static_cast<CKDX9RasterizerDriver *>(rasterizer.GetDriver(0));
    CKDX9RasterizerContext *context = static_cast<CKDX9RasterizerContext *>(driver->CreateContext());
    TestCheck(context != NULL, "DX9 driver should create a context");
    TestCheck(context->Create(window, 0, 0, 64, 64, 32, FALSE, 0, 24, 8) == TRUE,
              "DX9 render target size context should initialize");

    CKDWORD texture = rasterizer.CreateObjectIndex(CKRST_OBJ_TEXTURE);
    CKTextureDesc desired;
    desired.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA | CKRST_TEXTURE_MANAGED;
    VxPixelFormat2ImageDesc(_32_ARGB8888, desired.Format);
    desired.Format.Width = 32;
    desired.Format.Height = 32;
    desired.Format.BytesPerLine = 32 * 4;
    desired.MipMapCount = 0;
    TestCheck(context->CreateObject(texture, CKRST_OBJ_TEXTURE, &desired) == TRUE,
              "DX9 size test should create the original texture");

    TestCheck(context->SetTargetTexture(texture, 64, 48) == TRUE,
              "SetTargetTexture should recreate an existing object with requested dimensions");
    CKDX9TextureDesc *desc = static_cast<CKDX9TextureDesc *>(context->m_Textures[texture]);
    TestCheck(desc != NULL && desc->DxTexture != NULL,
              "DX9 size test should keep a 2D render target texture");

    IDirect3DSurface9 *surface = NULL;
    HRESULT hr = desc->DxTexture->GetSurfaceLevel(0, &surface);
    TestCheck(SUCCEEDED(hr) && surface != NULL, "DX9 size test should expose a render target surface");

    D3DSURFACE_DESC surfaceDesc = {};
    hr = surface->GetDesc(&surfaceDesc);
    SAFERELEASE(surface);
    TestCheck(SUCCEEDED(hr), "DX9 size test surface should have a description");
    TestCheck(surfaceDesc.Width == 64 && surfaceDesc.Height == 48,
              "SetTargetTexture should use the requested dimensions for an existing object");

    TestCheck(context->SetTargetTexture(0) == TRUE,
              "SetTargetTexture(0) should restore the default render target after size test");

    driver->DestroyContext(context);
    DestroyWindow(window);
#endif
}

void CopyToTextureSupportsCubeFaces()
{
#if !defined(_WIN32)
    return;
#else
    HWND window = CreateTestWindow();
    TestCheck(window != NULL, "DX9 cubemap copy test needs a hidden window");

    CKDX9Rasterizer rasterizer;
    if (!rasterizer.Start(window))
    {
        DestroyWindow(window);
        return;
    }

    if (rasterizer.GetDriverCount() == 0)
    {
        DestroyWindow(window);
        return;
    }

    CKDX9RasterizerDriver *driver = static_cast<CKDX9RasterizerDriver *>(rasterizer.GetDriver(0));
    CKDX9RasterizerContext *context = static_cast<CKDX9RasterizerContext *>(driver->CreateContext());
    TestCheck(context != NULL, "DX9 driver should create a context");
    TestCheck(context->Create(window, 0, 0, 64, 64, 32, FALSE, 0, 24, 8) == TRUE,
              "DX9 cubemap copy context should initialize");

    CKDWORD texture = rasterizer.CreateObjectIndex(CKRST_OBJ_TEXTURE);
    CKTextureDesc desired;
    desired.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_CUBEMAP | CKRST_TEXTURE_RENDERTARGET;
    VxPixelFormat2ImageDesc(_32_ARGB8888, desired.Format);
    desired.Format.Width = 64;
    desired.Format.Height = 64;
    desired.Format.BytesPerLine = 64 * 4;
    desired.MipMapCount = 0;
    TestCheck(context->CreateObject(texture, CKRST_OBJ_TEXTURE, &desired) == TRUE,
              "DX9 cubemap copy test should create a render-target cubemap");

    TestCheck(context->CopyToTexture(texture, NULL, NULL, CKRST_CUBEFACE_ZNEG) == TRUE,
              "CopyToTexture should copy the back buffer to the requested cubemap face");

    CKDX9TextureDesc *desc = static_cast<CKDX9TextureDesc *>(context->m_Textures[texture]);
    TestCheck(desc != NULL && desc->DxCubeTexture != NULL,
              "DX9 cubemap copy should keep the cubemap resource");
    TestCheck((desc->Flags & CKRST_TEXTURE_CUBEMAP) != 0,
              "DX9 cubemap copy should preserve the cubemap flag");

    driver->DestroyContext(context);
    DestroyWindow(window);
#endif
}

void ResizeWithActiveCubemapRenderTargetDoesNotCrash()
{
#if !defined(_WIN32)
    return;
#else
    HWND window = CreateTestWindow();
    TestCheck(window != NULL, "DX9 cubemap reset test needs a hidden window");

    CKDX9Rasterizer rasterizer;
    if (!rasterizer.Start(window))
    {
        DestroyWindow(window);
        return;
    }

    if (rasterizer.GetDriverCount() == 0)
    {
        DestroyWindow(window);
        return;
    }

    CKDX9RasterizerDriver *driver = static_cast<CKDX9RasterizerDriver *>(rasterizer.GetDriver(0));
    CKDX9RasterizerContext *context = static_cast<CKDX9RasterizerContext *>(driver->CreateContext());
    TestCheck(context != NULL, "DX9 driver should create a context");
    TestCheck(context->Create(window, 0, 0, 64, 64, 32, FALSE, 0, 24, 8) == TRUE,
              "DX9 cubemap reset context should initialize");

    CKDWORD texture = rasterizer.CreateObjectIndex(CKRST_OBJ_TEXTURE);
    TestCheck(context->SetTargetTexture(texture, 64, -1, CKRST_CUBEFACE_YPOS) == TRUE,
              "DX9 cubemap reset test should bind a cubemap render target");

    TestCheck(context->Resize(0, 0, 80, 80, 0) == TRUE,
              "Resize should reset safely with an active cubemap render target");

    driver->DestroyContext(context);
    DestroyWindow(window);
#endif
}

} // namespace

int main()
{
    TestFramework tests;
    tests.Run("D3DFMT_UNKNOWN maps to UNKNOWN_PF", &UnknownD3DFormatMapsToUnknownPixelFormat);
    tests.Run("_32_ARGB8888 maps to D3DFMT_A8R8G8B8", &Argb8888MapsToD3DFormat);
    tests.Run("D3DFormatToTextureDesc marks A8R8G8B8 correctly", &A8R8G8B8TextureDescCarriesRgbAlphaAndBpp);
    tests.Run("Unsupported D3D formats do not create valid texture descs", &UnsupportedD3DFormatDoesNotCreateValidTextureDesc);
    tests.Run("SetLight rejects null data", &SetLightRejectsNullData);
    tests.Run("SetLight rejects out-of-range index", &SetLightRejectsOutOfRangeIndex);
    tests.Run("2D render target textures use one sampleable resource", &RenderTargetTextureUsesSingleSampleableResource);
    tests.Run("Resized 2D mipmap upload stays within target buffer", &ResizedTextureMipmapUploadStaysWithinTargetBuffer);
    tests.Run("Resized cube mipmap upload stays within target buffer", &ResizedCubeMipmapUploadStaysWithinTargetBuffer);
    tests.Run("Cubemap format search requires cube texture support", &CubeTextureFormatSearchRequiresCubeSupport);
    tests.Run("Cubemap render target format search requires cube texture support", &CubeRenderTargetFormatSearchRequiresCubeSupport);
    tests.Run("POW2 texture caps use next power-of-two sizing", &Pow2TextureCapsUseNextPowerOfTwo);
    tests.Run("Sprite creation uses next power-of-two sizing when it fits", &SpriteCreationUsesNextPowerOfTwoWhenItFits);
    tests.Run("Render target texture can switch from cube to 2D", &RenderTargetTextureCanSwitchFromCubeTo2D);
    tests.Run("Render target texture uses requested size for existing object", &RenderTargetTextureUsesRequestedSizeForExistingObject);
    tests.Run("CopyToTexture supports cubemap faces", &CopyToTextureSupportsCubeFaces);
    tests.Run("Resize handles active cubemap render targets", &ResizeWithActiveCubemapRenderTargetDoesNotCrash);
    return tests.ExitCode();
}
