#include "CKDX9Rasterizer.h"
#include "TestTriangleMultiset.h"

#if defined(_WIN32)
#include <windows.h>
#endif
#include <string.h>
#include <type_traits>

static_assert(std::is_same<decltype(&CKRasterizerContext::SetTexture),
                           CKBOOL (CKRasterizerContext::*)(CKDWORD, int)>::value,
              "CKRasterizerContext SetTexture must match the Virtools ABI");
static_assert(std::is_same<decltype(&CKRasterizerContext::SetTextureStageState),
                           CKBOOL (CKRasterizerContext::*)(int, CKRST_TEXTURESTAGESTATETYPE, CKDWORD)>::value,
              "CKRasterizerContext SetTextureStageState must match the Virtools ABI");
static_assert(std::is_same<decltype(&CKDX9RasterizerContext::SetTexture),
                           CKBOOL (CKDX9RasterizerContext::*)(CKDWORD, int)>::value,
              "CKDX9 SetTexture must exactly override CKRasterizerContext");
static_assert(std::is_same<decltype(&CKDX9RasterizerContext::SetTextureStageState),
                           CKBOOL (CKDX9RasterizerContext::*)(int, CKRST_TEXTURESTAGESTATETYPE, CKDWORD)>::value,
              "CKDX9 SetTextureStageState must exactly override CKRasterizerContext");

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

class FakeAdapterCountD3D9 : public FakeFormatD3D9
{
public:
    FakeAdapterCountD3D9() : adapterIdentifierCalled(false) {}

    HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT, DWORD, D3DADAPTER_IDENTIFIER9 *)
    {
        adapterIdentifierCalled = true;
        return D3D_OK;
    }

    bool adapterIdentifierCalled;
};

class FakeDepthStencilD3D9 : public FakeFormatD3D9
{
public:
    HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT, D3DDEVTYPE, D3DFORMAT, DWORD Usage,
                                                D3DRESOURCETYPE RType, D3DFORMAT CheckFormat)
    {
        if ((Usage & D3DUSAGE_RENDERTARGET) != 0 && RType == D3DRTYPE_SURFACE)
            return D3D_OK;

        if ((Usage & D3DUSAGE_DEPTHSTENCIL) != 0 && RType == D3DRTYPE_SURFACE)
        {
            switch (CheckFormat)
            {
                case D3DFMT_D24S8:
                case D3DFMT_D24X8:
                case D3DFMT_D32:
                case D3DFMT_D16:
                    return D3D_OK;
                default:
                    return D3DERR_NOTAVAILABLE;
            }
        }

        return FakeFormatD3D9::CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_UNKNOWN, Usage, RType, CheckFormat);
    }

    HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT, D3DDEVTYPE, D3DFORMAT,
                                                     D3DFORMAT, D3DFORMAT CheckFormat)
    {
        switch (CheckFormat)
        {
            case D3DFMT_D24S8:
            case D3DFMT_D24X8:
            case D3DFMT_D32:
            case D3DFMT_D16:
                return D3D_OK;
            default:
                return D3DERR_NOTAVAILABLE;
        }
    }
};

bool ReadRenderTargetPixel(CKDX9RasterizerContext *context, IDirect3DSurface9 *surface,
                           UINT x, UINT y, CKDWORD *pixel)
{
    if (!context || !context->m_Device || !surface || !pixel)
        return false;

    D3DSURFACE_DESC desc = {};
    if (FAILED(surface->GetDesc(&desc)))
        return false;

    IDirect3DSurface9 *systemSurface = NULL;
    HRESULT hr = context->m_Device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format,
                                                                D3DPOOL_SYSTEMMEM, &systemSurface, NULL);
    if (FAILED(hr) || !systemSurface)
        return false;

    hr = context->m_Device->GetRenderTargetData(surface, systemSurface);
    if (FAILED(hr))
    {
        SAFERELEASE(systemSurface);
        return false;
    }

    D3DLOCKED_RECT locked = {};
    hr = systemSurface->LockRect(&locked, NULL, D3DLOCK_READONLY);
    if (FAILED(hr))
    {
        SAFERELEASE(systemSurface);
        return false;
    }

    const CKBYTE *row = static_cast<const CKBYTE *>(locked.pBits) + y * locked.Pitch;
    *pixel = *reinterpret_cast<const CKDWORD *>(row + x * 4);

    systemSurface->UnlockRect();
    SAFERELEASE(systemSurface);
    return true;
}
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

void TextureBlendModulateAlphaKeepsVirtoolsModulateContract()
{
    CKDX9TextureBlendState state;
    TestCheck(GetDX9TextureBlendState(VXTEXTUREBLEND_MODULATEALPHA, state),
              "MODULATEALPHA should have a DX9 texture blend mapping");

    TestCheck(state.ColorOp == D3DTOP_MODULATE,
              "MODULATEALPHA color should remain texture times diffuse in CK2_3D");
    TestCheck(state.ColorArg1 == D3DTA_TEXTURE && state.ColorArg2 == D3DTA_CURRENT,
              "MODULATEALPHA color modulation should use texture and current color");
    TestCheck(state.AlphaOp == D3DTOP_MODULATE,
              "MODULATEALPHA alpha should remain texture alpha times diffuse alpha");
    TestCheck(state.AlphaArg1 == D3DTA_TEXTURE && state.AlphaArg2 == D3DTA_CURRENT,
              "MODULATEALPHA alpha modulation should use texture and current alpha");
}

void TextureBlendModulateKeepsPlainMultiply()
{
    CKDX9TextureBlendState state;
    TestCheck(GetDX9TextureBlendState(VXTEXTUREBLEND_MODULATE, state),
              "MODULATE should have a DX9 texture blend mapping");

    TestCheck(state.ColorOp == D3DTOP_MODULATE,
              "MODULATE color should remain plain texture times diffuse");
    TestCheck(state.AlphaOp == D3DTOP_MODULATE,
              "MODULATE alpha should remain plain texture alpha times diffuse alpha");
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

void DefaultDepthFormatKeepsStencilAvailable()
{
#if !defined(_WIN32)
    return;
#else
    FakeDepthStencilD3D9 fakeD3D9;
    CKDX9Rasterizer rasterizer;
    rasterizer.m_D3D9 = &fakeD3D9;

    CKDX9RasterizerDriver driver(&rasterizer);
    driver.m_AdapterIndex = 0;
    driver.m_DevType = D3DDEVTYPE_HAL;

    const D3DFORMAT selected = driver.FindNearestDepthFormat(D3DFMT_X8R8G8B8, -1, -1);
    rasterizer.m_D3D9 = NULL;

    TestCheck(selected == D3DFMT_D24S8,
              "Default DX9 depth format selection should keep stencil available for ShadowStencil");
#endif
}

void InitializeCapsRejectsNegativeAdapterIndexBeforeD3DQuery()
{
#if !defined(_WIN32)
    return;
#else
    FakeAdapterCountD3D9 fakeD3D9;
    CKDX9Rasterizer rasterizer;
    rasterizer.m_D3D9 = &fakeD3D9;

    CKDX9RasterizerDriver driver(&rasterizer);

    TestCheck(driver.InitializeCaps(-1, D3DDEVTYPE_HAL) == FALSE,
              "InitializeCaps should reject negative adapter indices");
    TestCheck(!fakeD3D9.adapterIdentifierCalled,
              "InitializeCaps must validate adapter indices before querying D3D");

    rasterizer.m_D3D9 = NULL;
#endif
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

void SpriteCreationRespectsSquareOnlyTextureCaps()
{
#if !defined(_WIN32)
    return;
#else
    HWND window = CreateTestWindow();
    TestCheck(window != NULL, "DX9 sprite square-only sizing test needs a hidden window");

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
    TestCheck(context != NULL, "DX9 sprite square-only sizing test should create a context");
    TestCheck(context->Create(window, 0, 0, 64, 64, 32, FALSE, 0, 24, 8) == TRUE,
              "DX9 sprite square-only sizing context should initialize");

    driver->m_3DCaps.TextureCaps |= CKRST_TEXTURECAPS_SQUAREONLY;

    CKDWORD sprite = rasterizer.CreateObjectIndex(CKRST_OBJ_SPRITE);
    CKSpriteDesc desired;
    desired.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA | CKRST_TEXTURE_MANAGED;
    VxPixelFormat2ImageDesc(_32_ARGB8888, desired.Format);
    desired.Format.Width = 100;
    desired.Format.Height = 40;
    desired.Format.BytesPerLine = 100 * 4;
    desired.MipMapCount = 0;

    TestCheck(context->CreateObject(sprite, CKRST_OBJ_SPRITE, &desired) == TRUE,
              "DX9 sprite square-only sizing test should create the sprite");

    CKSpriteDesc *desc = context->GetSpriteData(sprite);
    TestCheck(desc != NULL && desc->Textures.Size() == 1,
              "CKRasterizerLib square-only sprite creation should still use one texture when it fits");

    CKSPRTextInfo *info = desc->Textures.Begin();
    TestCheck(info != NULL && info->w == 100 && info->h == 40 && info->sw == 128 && info->sh == 128,
              "CKRasterizerLib sprite storage should match square-only texture expansion");

    driver->DestroyContext(context);
    DestroyWindow(window);
#endif
}

void CubeTextureUploadRejectsNullImage()
{
#if !defined(_WIN32)
    return;
#else
    HWND window = CreateTestWindow();
    TestCheck(window != NULL, "DX9 cubemap null upload test needs a hidden window");

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
    TestCheck(context != NULL, "DX9 cubemap null upload test should create a context");
    TestCheck(context->Create(window, 0, 0, 64, 64, 32, FALSE, 0, 24, 8) == TRUE,
              "DX9 cubemap null upload context should initialize");

    CKDWORD texture = rasterizer.CreateObjectIndex(CKRST_OBJ_TEXTURE);
    CKTextureDesc desired;
    desired.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA |
                    CKRST_TEXTURE_MANAGED | CKRST_TEXTURE_CUBEMAP;
    VxPixelFormat2ImageDesc(_32_ARGB8888, desired.Format);
    desired.Format.Width = 32;
    desired.Format.Height = 32;
    desired.Format.BytesPerLine = 32 * 4;
    desired.MipMapCount = 0;
    TestCheck(context->CreateObject(texture, CKRST_OBJ_TEXTURE, &desired) == TRUE,
              "DX9 cubemap null upload test should create a cubemap texture");

    VxImageDescEx source;
    VxPixelFormat2ImageDesc(_32_ARGB8888, source);
    source.Width = 32;
    source.Height = 32;
    source.BytesPerLine = 32 * 4;
    source.Image = NULL;

    TestCheck(context->LoadCubeMapTexture(texture, source, CKRST_CUBEFACE_XPOS, 0) == FALSE,
              "LoadCubeMapTexture should reject a null source image");

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

void CopyToTextureHonorsDestinationBounds()
{
#if !defined(_WIN32)
    return;
#else
    HWND window = CreateTestWindow();
    TestCheck(window != NULL, "DX9 CopyToTexture bounds test needs a hidden window");

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
    TestCheck(context != NULL, "DX9 CopyToTexture bounds test should create a context");
    TestCheck(context->Create(window, 0, 0, 64, 64, 32, FALSE, 0, 24, 8) == TRUE,
              "DX9 CopyToTexture bounds context should initialize");

    CKDWORD texture = rasterizer.CreateObjectIndex(CKRST_OBJ_TEXTURE);
    CKTextureDesc desired;
    desired.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_RENDERTARGET;
    VxPixelFormat2ImageDesc(_32_ARGB8888, desired.Format);
    desired.Format.Width = 64;
    desired.Format.Height = 64;
    desired.Format.BytesPerLine = 64 * 4;
    desired.MipMapCount = 0;
    TestCheck(context->CreateObject(texture, CKRST_OBJ_TEXTURE, &desired) == TRUE,
              "DX9 CopyToTexture bounds test should create a render-target texture");

    TestCheck(context->SetTargetTexture(texture, 64, 64) == TRUE,
              "DX9 CopyToTexture bounds test should bind the texture target");
    context->Clear(CKRST_CTXCLEAR_COLOR, 0x00000000);
    TestCheck(context->SetTargetTexture(0) == TRUE,
              "DX9 CopyToTexture bounds test should restore the back buffer");

    context->Clear(CKRST_CTXCLEAR_COLOR, 0x00FF0000);
    VxRect src(0.0f, 0.0f, 32.0f, 32.0f);
    VxRect dest(0.0f, 0.0f, 16.0f, 16.0f);
    TestCheck(context->CopyToTexture(texture, &src, &dest, CKRST_CUBEFACE_XPOS) == TRUE,
              "CopyToTexture should copy into the destination rectangle");

    CKDX9TextureDesc *desc = static_cast<CKDX9TextureDesc *>(context->m_Textures[texture]);
    IDirect3DSurface9 *surface = NULL;
    HRESULT hr = desc && desc->DxTexture ? desc->DxTexture->GetSurfaceLevel(0, &surface) : E_FAIL;
    TestCheck(SUCCEEDED(hr) && surface != NULL, "DX9 CopyToTexture bounds test should expose the texture surface");

    CKDWORD insidePixel = 0;
    CKDWORD outsidePixel = 0;
    TestCheck(ReadRenderTargetPixel(context, surface, 8, 8, &insidePixel),
              "DX9 CopyToTexture bounds test should read the copied pixel");
    TestCheck(ReadRenderTargetPixel(context, surface, 24, 24, &outsidePixel),
              "DX9 CopyToTexture bounds test should read the pixel outside Dest");
    SAFERELEASE(surface);

    TestCheck((insidePixel & 0x00FFFFFF) == 0x00FF0000,
              "CopyToTexture should write pixels inside Dest");
    TestCheck((outsidePixel & 0x00FFFFFF) == 0x00000000,
              "CopyToTexture must not write outside the destination rectangle");

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
    tests.Run("MODULATEALPHA keeps CK2_3D modulation contract", &TextureBlendModulateAlphaKeepsVirtoolsModulateContract);
    tests.Run("MODULATE keeps plain multiply", &TextureBlendModulateKeepsPlainMultiply);
    tests.Run("SetLight rejects null data", &SetLightRejectsNullData);
    tests.Run("SetLight rejects out-of-range index", &SetLightRejectsOutOfRangeIndex);
    tests.Run("Default DX9 depth format keeps stencil available", &DefaultDepthFormatKeepsStencilAvailable);
    tests.Run("InitializeCaps rejects negative adapter indices", &InitializeCapsRejectsNegativeAdapterIndexBeforeD3DQuery);
    tests.Run("2D render target textures use one sampleable resource", &RenderTargetTextureUsesSingleSampleableResource);
    tests.Run("Resized 2D mipmap upload stays within target buffer", &ResizedTextureMipmapUploadStaysWithinTargetBuffer);
    tests.Run("Resized cube mipmap upload stays within target buffer", &ResizedCubeMipmapUploadStaysWithinTargetBuffer);
    tests.Run("Cubemap format search requires cube texture support", &CubeTextureFormatSearchRequiresCubeSupport);
    tests.Run("Cubemap render target format search requires cube texture support", &CubeRenderTargetFormatSearchRequiresCubeSupport);
    tests.Run("POW2 texture caps use next power-of-two sizing", &Pow2TextureCapsUseNextPowerOfTwo);
    tests.Run("Sprite creation uses next power-of-two sizing when it fits", &SpriteCreationUsesNextPowerOfTwoWhenItFits);
    tests.Run("Sprite creation respects square-only texture caps", &SpriteCreationRespectsSquareOnlyTextureCaps);
    tests.Run("Cubemap upload rejects null source images", &CubeTextureUploadRejectsNullImage);
    tests.Run("Render target texture can switch from cube to 2D", &RenderTargetTextureCanSwitchFromCubeTo2D);
    tests.Run("Render target texture uses requested size for existing object", &RenderTargetTextureUsesRequestedSizeForExistingObject);
    tests.Run("CopyToTexture supports cubemap faces", &CopyToTextureSupportsCubeFaces);
    tests.Run("CopyToTexture honors destination bounds", &CopyToTextureHonorsDestinationBounds);
    tests.Run("Resize handles active cubemap render targets", &ResizeWithActiveCubemapRenderTargetDoesNotCrash);
    return tests.ExitCode();
}
