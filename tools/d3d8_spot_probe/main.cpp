// Wine's declaration headers can also be used with MSVC. No Wine runtime or
// D3DX dependency is involved: Direct3DCreate8 comes from system32/d3d8.dll.
#ifndef __MSABI_LONG
#define __MSABI_LONG(x) x##L
#endif
#include <windows.h>
#include <d3d8.h>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <cstring>

namespace {
void Check(HRESULT result, const char *call) {
    if (FAILED(result)) {
        std::fprintf(stderr, "%s failed: %08lx\n", call, result);
        std::exit(2);
    }
}
#define CHECK_D3D(call) Check(call, #call)
}

int main(int argc, char **argv) {
    const bool coverage = argc == 2 && std::strcmp(argv[1], "--coverage") == 0;
    if (argc != 1 && !coverage) { std::fprintf(stderr, "Usage: d3d8_spot_probe [--coverage]\n"); return 2; }
    HMODULE module = LoadLibraryExW(L"d3d8.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) { std::fprintf(stderr, "Cannot load system d3d8.dll\n"); return 2; }
    wchar_t path[MAX_PATH] = {};
    if (!GetModuleFileNameW(module, path, MAX_PATH)) return 2;
    std::printf("Runtime: %ls\n", path);
    const auto create = reinterpret_cast<IDirect3D8 *(WINAPI *)(UINT)>(GetProcAddress(module, "Direct3DCreate8"));
    if (!create) return 2;
    IDirect3D8 *d3d = create(D3D_SDK_VERSION);
    if (!d3d) return 2;
    D3DADAPTER_IDENTIFIER8 adapter = {};
    CHECK_D3D(d3d->GetAdapterIdentifier(0, 0, &adapter));
    std::printf("Adapter: %s; driver=%s version=%08lx:%08lx\n", adapter.Description, adapter.Driver,
                adapter.DriverVersion.HighPart, adapter.DriverVersion.LowPart);
    HWND window = CreateWindowExW(0, L"STATIC", L"D3D8 spotlight probe", WS_OVERLAPPEDWINDOW,
        0, 0, 64, 64, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!window) return 2;
    D3DDISPLAYMODE mode = {};
    CHECK_D3D(d3d->GetAdapterDisplayMode(0, &mode));
    if (mode.Format != D3DFMT_X8R8G8B8 && mode.Format != D3DFMT_A8R8G8B8) {
        std::fprintf(stderr, "Expected a 32-bit RGB display format\n"); return 2;
    }
    unsigned failures = 0;
    for (bool software : {false, true}) {
        D3DPRESENT_PARAMETERS pp = {};
        pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.BackBufferWidth = coverage ? 640 : 64;
        pp.BackBufferHeight = coverage ? 480 : 64; pp.BackBufferFormat = mode.Format;
        pp.hDeviceWindow = window; pp.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
        IDirect3DDevice8 *device = NULL;
        CHECK_D3D(d3d->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_FPU_PRESERVE |
            (software ? D3DCREATE_SOFTWARE_VERTEXPROCESSING : D3DCREATE_HARDWARE_VERTEXPROCESSING), &pp, &device));
        D3DMATRIX identity = {};
        identity._11 = identity._22 = identity._33 = identity._44 = 1;
        D3DMATRIX projection = identity;
        projection._11 = projection._22 = 10; projection._33 = 0.2f;
        CHECK_D3D(device->SetTransform(D3DTS_WORLD, &identity));
        CHECK_D3D(device->SetTransform(D3DTS_VIEW, &identity));
        CHECK_D3D(device->SetTransform(D3DTS_PROJECTION, &projection));
        CHECK_D3D(device->SetRenderState(D3DRS_LIGHTING, TRUE));
        CHECK_D3D(device->SetRenderState(D3DRS_AMBIENT, 0));
        CHECK_D3D(device->SetRenderState(D3DRS_COLORVERTEX, FALSE));
        CHECK_D3D(device->SetRenderState(D3DRS_SPECULARENABLE, FALSE));
        CHECK_D3D(device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
        CHECK_D3D(device->SetRenderState(D3DRS_ZENABLE, FALSE));
        CHECK_D3D(device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1));
        CHECK_D3D(device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE));
        if (coverage) {
            // A silhouette triangle from the procedural spotlight scene.
            // The two paths receive identical matrices and unlit vertices.
            const D3DMATRIX world = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0.800000012f,0,1};
            const D3DMATRIX view = {1.00000012f,-1.66666689e-6f,-3.33333361e-7f,0,
                -4.45299861e-7f,0.832050323f,-0.554700196f,0,
                -1.49871721e-6f,0.554700196f,0.832050383f,0,
                -2.16333119e-5f,0,21.6333084f,1};
            const D3DMATRIX proj = {2.41421342f,0,0,0, 0,3.21895123f,0,0,
                0,0,1.00050032f,1, 0,0,-0.100050032f,0};
            CHECK_D3D(device->SetTransform(D3DTS_WORLD, &world));
            CHECK_D3D(device->SetTransform(D3DTS_VIEW, &view));
            CHECK_D3D(device->SetTransform(D3DTS_PROJECTION, &proj));
            CHECK_D3D(device->SetRenderState(D3DRS_LIGHTING, FALSE));
            struct CoverageVertex { float x, y, z; DWORD color; };
            const CoverageVertex triangle[] = {
                {-0.204024419f,0.647213638f,0.423660964f,0xffffffff},
                {-0.293182582f,0.647213638f,0.367639154f,0xffffffff},
                {-0.35269919f,0.565685451f,0.442270607f,0xffffffff}};
            CHECK_D3D(device->SetVertexShader(D3DFVF_XYZ | D3DFVF_DIFFUSE));
            CHECK_D3D(device->Clear(0, NULL, D3DCLEAR_TARGET, 0, 1, 0));
            CHECK_D3D(device->BeginScene());
            CHECK_D3D(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, triangle, sizeof(CoverageVertex)));
            CHECK_D3D(device->EndScene());
            IDirect3DSurface8 *surface = NULL;
            CHECK_D3D(device->GetRenderTarget(&surface));
            D3DLOCKED_RECT rect = {};
            CHECK_D3D(surface->LockRect(&rect, NULL, D3DLOCK_READONLY));
            const DWORD pixel = *reinterpret_cast<const DWORD *>(static_cast<const char *>(rect.pBits) + 188 * rect.Pitch + 311 * 4);
            CHECK_D3D(surface->UnlockRect()); surface->Release();
            std::printf("coverage vp=%s pixel=311,188 rgb=%06lx\n", software ? "software" : "hardware", pixel & 0xffffff);
            if (software) {
                IDirect3DVertexBuffer8 *source = NULL, *transformed = NULL;
                struct ScreenVertex { float x, y, z, rhw; DWORD color; };
                CHECK_D3D(device->CreateVertexBuffer(sizeof(triangle), 0, D3DFVF_XYZ | D3DFVF_DIFFUSE, D3DPOOL_SYSTEMMEM, &source));
                CHECK_D3D(device->CreateVertexBuffer(3 * sizeof(ScreenVertex), 0, D3DFVF_XYZRHW | D3DFVF_DIFFUSE, D3DPOOL_SYSTEMMEM, &transformed));
                BYTE *mapped = NULL;
                CHECK_D3D(source->Lock(0, 0, &mapped, 0));
                std::memcpy(mapped, triangle, sizeof(triangle));
                CHECK_D3D(source->Unlock());
                CHECK_D3D(device->SetStreamSource(0, source, sizeof(CoverageVertex)));
                CHECK_D3D(device->ProcessVertices(0, 0, 3, transformed, 0));
                CHECK_D3D(transformed->Lock(0, 0, &mapped, D3DLOCK_READONLY));
                for (unsigned i = 0; i < 3; ++i) {
                    const ScreenVertex &v = reinterpret_cast<const ScreenVertex *>(mapped)[i];
                    std::printf("screen vertex=%u xyzrhw=%.9g,%.9g,%.9g,%.9g\n", i, v.x, v.y, v.z, v.rhw);
                }
                CHECK_D3D(transformed->Unlock()); transformed->Release(); source->Release();
            }
            device->Release();
            continue;
        }
        D3DMATERIAL8 material = {};
        material.Diffuse = {0.2f, 0.2f, 0.2f, 1};
        CHECK_D3D(device->SetMaterial(&material));
        struct Vertex { float x, y, z, nx, ny, nz; };
        const Vertex vertices[] = {{-0.05f,-0.05f,2,0,0,-1}, {0.05f,-0.05f,2,0,0,-1},
                                   {0.05f,0.05f,2,0,0,-1}, {-0.05f,0.05f,2,0,0,-1}};
        CHECK_D3D(device->SetVertexShader(D3DFVF_XYZ | D3DFVF_NORMAL));
        for (float theta : {0.0f, 0.34906585f, 0.6f}) {
            D3DLIGHT8 light = {};
            light.Type = D3DLIGHT_SPOT; light.Diffuse = {1, 1, 1, 1}; light.Direction = {0, 0, 1};
            light.Range = 30; light.Attenuation0 = 1;
            light.Theta = theta; light.Phi = 0.78539819f; light.Falloff = 1;
            CHECK_D3D(device->SetLight(0, &light));
            CHECK_D3D(device->LightEnable(0, TRUE));
            CHECK_D3D(device->Clear(0, NULL, D3DCLEAR_TARGET, 0, 1, 0));
            CHECK_D3D(device->BeginScene());
            CHECK_D3D(device->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, vertices, sizeof(Vertex)));
            CHECK_D3D(device->EndScene());
            IDirect3DSurface8 *surface = NULL;
            CHECK_D3D(device->GetRenderTarget(&surface));
            D3DLOCKED_RECT rect = {};
            CHECK_D3D(surface->LockRect(&rect, NULL, D3DLOCK_READONLY));
            const DWORD pixel = *reinterpret_cast<const DWORD *>(static_cast<const char *>(rect.pBits) + 32 * rect.Pitch + 32 * 4);
            CHECK_D3D(surface->UnlockRect());
            surface->Release();
            bool bounded = true;
            for (unsigned shift : {0u, 8u, 16u}) {
                const unsigned channel = (pixel >> shift) & 255;
                bounded = bounded && channel >= 50 && channel <= 51;
            }
            std::printf("vp=%s theta=%.8f rgb=%06lx bounded=%s\n", software ? "software" : "hardware",
                        theta, pixel & 0xffffff, bounded ? "yes" : "no");
            if (software && !bounded) ++failures;
        }
        device->Release();
    }
    DestroyWindow(window);
    d3d->Release();
    FreeLibrary(module);
    // Hardware differences are observations. Only the software reference is
    // required to satisfy the independently specified diffuse intensity.
    return failures ? 1 : 0;
}
