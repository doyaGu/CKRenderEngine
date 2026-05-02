#include "CKDX9Rasterizer.h"
#include "TestTriangleMultiset.h"

#if defined(_WIN32)
#include <windows.h>
#endif

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
    return tests.ExitCode();
}
