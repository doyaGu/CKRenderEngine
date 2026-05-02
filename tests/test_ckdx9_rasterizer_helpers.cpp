#include "CKDX9Rasterizer.h"
#include "TestTriangleMultiset.h"

namespace {

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
    return tests.ExitCode();
}
