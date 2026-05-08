// bgfx_cubes: CKRasterizer API demo
// Renders a grid of rotating colored cubes using the abstract CKRasterizer
// interface backed by CKBgfxRasterizer. Mirrors bgfx 01-cubes.

#include "CKRasterizer.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#define LOG(fmt, ...) do { fprintf(stderr, fmt "\n", ##__VA_ARGS__); fflush(stderr); } while (0)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// Entry point exported by CKBgfxRasterizer.
extern "C" void CKRasterizerGetInfo(CKRasterizerInfo *info);

// ============================================================================
// Embedded precompiled bgfx D3D11 shaders (from bgfx examples/01-cubes)
// ============================================================================

static const unsigned char s_vs_cubes[] = {
    0x56, 0x53, 0x48, 0x0b, 0x00, 0x00, 0x00, 0x00, 0xa4, 0x8b, 0xef, 0x49,
    0x01, 0x00, 0x0f, 0x75, 0x5f, 0x6d, 0x6f, 0x64, 0x65, 0x6c, 0x56, 0x69,
    0x65, 0x77, 0x50, 0x72, 0x6f, 0x6a, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xd8, 0x01, 0x00, 0x00, 0x44, 0x58, 0x42, 0x43,
    0x19, 0x40, 0x51, 0x48, 0x64, 0xf0, 0x4f, 0x1e, 0x9e, 0x94, 0x2e, 0xc7,
    0x38, 0x04, 0x0f, 0xf4, 0x01, 0x00, 0x00, 0x00, 0xd8, 0x01, 0x00, 0x00,
    0x03, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x7c, 0x00, 0x00, 0x00,
    0xd0, 0x00, 0x00, 0x00, 0x49, 0x53, 0x47, 0x4e, 0x48, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x0f, 0x0f, 0x00, 0x00, 0x3e, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x07, 0x07, 0x00, 0x00, 0x43, 0x4f, 0x4c, 0x4f,
    0x52, 0x00, 0x50, 0x4f, 0x53, 0x49, 0x54, 0x49, 0x4f, 0x4e, 0x00, 0xab,
    0x4f, 0x53, 0x47, 0x4e, 0x4c, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x08, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0f, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x0f, 0x00, 0x00, 0x00, 0x53, 0x56, 0x5f, 0x50, 0x4f, 0x53, 0x49, 0x54,
    0x49, 0x4f, 0x4e, 0x00, 0x43, 0x4f, 0x4c, 0x4f, 0x52, 0x00, 0xab, 0xab,
    0x53, 0x48, 0x45, 0x58, 0x00, 0x01, 0x00, 0x00, 0x50, 0x00, 0x01, 0x00,
    0x40, 0x00, 0x00, 0x00, 0x6a, 0x08, 0x00, 0x01, 0x59, 0x00, 0x00, 0x04,
    0x46, 0x8e, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
    0x5f, 0x00, 0x00, 0x03, 0xf2, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x5f, 0x00, 0x00, 0x03, 0x72, 0x10, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x67, 0x00, 0x00, 0x04, 0xf2, 0x20, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x65, 0x00, 0x00, 0x03, 0xf2, 0x20, 0x10, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x68, 0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00,
    0x38, 0x00, 0x00, 0x08, 0xf2, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x56, 0x15, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x46, 0x8e, 0x20, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x32, 0x00, 0x00, 0x0a,
    0xf2, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0x8e, 0x20, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x10, 0x10, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x46, 0x0e, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x32, 0x00, 0x00, 0x0a, 0xf2, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x46, 0x8e, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0xa6, 0x1a, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x46, 0x0e, 0x10, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0xf2, 0x20, 0x10, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x46, 0x0e, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x46, 0x8e, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
    0x36, 0x00, 0x00, 0x05, 0xf2, 0x20, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x46, 0x1e, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x00, 0x00, 0x01,
    0x00, 0x02, 0x05, 0x00, 0x01, 0x00, 0x40, 0x00
};

static const unsigned char s_fs_cubes[] = {
    0x46, 0x53, 0x48, 0x0b, 0xa4, 0x8b, 0xef, 0x49, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xf8, 0x00, 0x00, 0x00, 0x44, 0x58, 0x42, 0x43, 0x50, 0xef,
    0x6d, 0x1a, 0x00, 0x93, 0x06, 0x9c, 0xf0, 0x68, 0xce, 0x7c, 0xb9, 0x39,
    0x12, 0x62, 0x01, 0x00, 0x00, 0x00, 0xf8, 0x00, 0x00, 0x00, 0x03, 0x00,
    0x00, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0xb4, 0x00,
    0x00, 0x00, 0x49, 0x53, 0x47, 0x4e, 0x4c, 0x00, 0x00, 0x00, 0x02, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x0f, 0x0f, 0x00, 0x00, 0x53, 0x56, 0x5f, 0x50, 0x4f, 0x53,
    0x49, 0x54, 0x49, 0x4f, 0x4e, 0x00, 0x43, 0x4f, 0x4c, 0x4f, 0x52, 0x00,
    0xab, 0xab, 0x4f, 0x53, 0x47, 0x4e, 0x2c, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x53, 0x56, 0x5f, 0x54, 0x41, 0x52,
    0x47, 0x45, 0x54, 0x00, 0xab, 0xab, 0x53, 0x48, 0x45, 0x58, 0x3c, 0x00,
    0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x6a, 0x08,
    0x00, 0x01, 0x62, 0x10, 0x00, 0x03, 0xf2, 0x10, 0x10, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x65, 0x00, 0x00, 0x03, 0xf2, 0x20, 0x10, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x36, 0x00, 0x00, 0x05, 0xf2, 0x20, 0x10, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x46, 0x1e, 0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x3e, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00
};

// ============================================================================
// Cube geometry (identical to bgfx 01-cubes)
// ============================================================================

struct PosColorVertex {
    float x, y, z;
    uint32_t abgr;
};

static PosColorVertex s_cubeVertices[] = {
    {-1.0f,  1.0f,  1.0f, 0xff000000},
    { 1.0f,  1.0f,  1.0f, 0xff0000ff},
    {-1.0f, -1.0f,  1.0f, 0xff00ff00},
    { 1.0f, -1.0f,  1.0f, 0xff00ffff},
    {-1.0f,  1.0f, -1.0f, 0xffff0000},
    { 1.0f,  1.0f, -1.0f, 0xffff00ff},
    {-1.0f, -1.0f, -1.0f, 0xffffff00},
    { 1.0f, -1.0f, -1.0f, 0xffffffff},
};

static const uint16_t s_cubeIndices[] = {
    0, 1, 2, 1, 3, 2,
    4, 6, 5, 5, 6, 7,
    0, 2, 4, 4, 2, 6,
    1, 5, 3, 5, 7, 3,
    0, 4, 1, 4, 5, 1,
    2, 3, 6, 6, 3, 7,
};

// ============================================================================
// Matrix helpers -- produce column-major float[16] matching bx::mtx* output.
// We write directly into the flat float[16] view of VxMatrix so the byte
// layout bgfx receives is identical to what bx:: helpers produce.
// ============================================================================

static void MtxLookAtLH(VxMatrix &out,
                         float eyeX, float eyeY, float eyeZ,
                         float atX,  float atY,  float atZ,
                         float upX,  float upY,  float upZ)
{
    float fwdX = atX - eyeX, fwdY = atY - eyeY, fwdZ = atZ - eyeZ;
    float len = sqrtf(fwdX * fwdX + fwdY * fwdY + fwdZ * fwdZ);
    fwdX /= len; fwdY /= len; fwdZ /= len;

    float rightX = upY * fwdZ - upZ * fwdY;
    float rightY = upZ * fwdX - upX * fwdZ;
    float rightZ = upX * fwdY - upY * fwdX;
    len = sqrtf(rightX * rightX + rightY * rightY + rightZ * rightZ);
    rightX /= len; rightY /= len; rightZ /= len;

    float realUpX = fwdY * rightZ - fwdZ * rightY;
    float realUpY = fwdZ * rightX - fwdX * rightZ;
    float realUpZ = fwdX * rightY - fwdY * rightX;

    float tx = -(rightX * eyeX + rightY * eyeY + rightZ * eyeZ);
    float ty = -(realUpX * eyeX + realUpY * eyeY + realUpZ * eyeZ);
    float tz = -(fwdX * eyeX + fwdY * eyeY + fwdZ * eyeZ);

    float *m = (float *)&out;
    m[ 0] = rightX;   m[ 1] = realUpX;  m[ 2] = fwdX;   m[ 3] = 0.0f;
    m[ 4] = rightY;   m[ 5] = realUpY;  m[ 6] = fwdY;   m[ 7] = 0.0f;
    m[ 8] = rightZ;   m[ 9] = realUpZ;  m[10] = fwdZ;   m[11] = 0.0f;
    m[12] = tx;        m[13] = ty;       m[14] = tz;     m[15] = 1.0f;
}

static void MtxPerspectiveFovLH(VxMatrix &out, float fovY, float aspect,
                                 float nearZ, float farZ)
{
    float h = 1.0f / tanf(fovY * 0.5f);
    float w = h / aspect;
    float range = farZ / (farZ - nearZ);

    float *m = (float *)&out;
    memset(m, 0, 64);
    m[ 0] = w;
    m[ 5] = h;
    m[10] = range;
    m[11] = 1.0f;
    m[14] = -nearZ * range;
}

static void MtxRotateXY(VxMatrix &out, float ax, float ay)
{
    float sx = sinf(ax), cx = cosf(ax);
    float sy = sinf(ay), cy = cosf(ay);

    float *m = (float *)&out;
    memset(m, 0, 64);
    m[ 0] =  cy;
    m[ 2] =  sy;
    m[ 4] =  sx * sy;  m[ 5] = cx;     m[ 6] = -sx * cy;
    m[ 8] = -cx * sy;  m[ 9] = sx;     m[10] =  cx * cy;
    m[15] =  1.0f;
}

// ============================================================================
// Window
// ============================================================================

static bool s_running = true;

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CLOSE:
    case WM_DESTROY:
        s_running = false;
        PostQuitMessage(0);
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            s_running = false;
            PostQuitMessage(0);
        }
        return 0;
    }
    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

static HWND CreateAppWindow(int width, int height, const char *title)
{
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = "BgfxCubesDemo";
    RegisterClassExA(&wc);

    RECT rc = { 0, 0, width, height };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, title,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        NULL, NULL, wc.hInstance, NULL);
    return hwnd;
}

// ============================================================================
// Main
// ============================================================================

int main()
{
    const int WIDTH = 1280;
    const int HEIGHT = 720;

    LOG("Starting bgfx_cubes demo...");

    HWND hwnd = CreateAppWindow(WIDTH, HEIGHT, "CKRasterizer bgfx Cubes Demo");
    if (!hwnd) {
        LOG("Failed to create window");
        return 1;
    }
    LOG("Window created: %p", (void *)hwnd);

    // ---- Get rasterizer info via CK_LIB entry point ----
    CKRasterizerInfo rstInfo;
    CKRasterizerGetInfo(&rstInfo);
    LOG("Rasterizer: %s", rstInfo.Desc.CStr());

    CKRasterizer *rasterizer = rstInfo.StartFct(hwnd);
    if (!rasterizer) {
        LOG("Failed to start rasterizer");
        return 1;
    }
    LOG("Rasterizer started, driver count: %d", rasterizer->GetDriverCount());

    CKRasterizerDriver *driver = rasterizer->GetDriver(0);
    CKRasterizerContext *ctx = driver->CreateContext();
    if (!ctx->Create(hwnd, 0, 0, WIDTH, HEIGHT)) {
        LOG("Failed to create context");
        return 1;
    }
    LOG("Context created: %dx%d", WIDTH, HEIGHT);

    // ---- Create vertex layout ----
    CKVertexElementDesc elements[2] = {};
    elements[0].Attrib = CKRST_ATTRIB_POSITION;
    elements[0].Type = CKRST_ATTRIBTYPE_FLOAT;
    elements[0].Count = 3;
    elements[0].Normalized = FALSE;

    elements[1].Attrib = CKRST_ATTRIB_COLOR0;
    elements[1].Type = CKRST_ATTRIBTYPE_UINT8;
    elements[1].Count = 4;
    elements[1].Normalized = TRUE;

    CKVertexLayoutDesc layoutDesc = {};
    layoutDesc.Elements = elements;
    layoutDesc.ElementCount = 2;
    layoutDesc.Stride[0] = sizeof(PosColorVertex);

    CKDWORD hLayout = 1;
    if (ctx->CreateVertexLayout(hLayout, &layoutDesc) != CK_OK) {
        LOG("Failed to create vertex layout");
        return 1;
    }

    // ---- Create vertex buffer ----
    CKVertexBufferDesc vbDesc;
    vbDesc.m_MaxVertexCount = 8;
    vbDesc.m_VertexSize = sizeof(PosColorVertex);

    CKDWORD hVB = 1;
    if (ctx->CreateVertexBuffer(hVB, &vbDesc, s_cubeVertices) != CK_OK) {
        LOG("Failed to create vertex buffer");
        return 1;
    }

    // ---- Create index buffer ----
    CKIndexBufferDesc ibDesc;
    ibDesc.m_MaxIndexCount = 36;

    CKDWORD hIB = 1;
    if (ctx->CreateIndexBuffer(hIB, &ibDesc, FALSE, s_cubeIndices) != CK_OK) {
        LOG("Failed to create index buffer");
        return 1;
    }

    // ---- Create shaders and program ----
    CKShaderDesc vsDesc = {};
    vsDesc.Stage = CKRST_SHADER_VERTEX;
    vsDesc.Format = CKRST_SHADER_DXBC;
    vsDesc.Code = (CKBYTE *)s_vs_cubes;
    vsDesc.CodeSize = sizeof(s_vs_cubes);

    CKShaderDesc fsDesc = {};
    fsDesc.Stage = CKRST_SHADER_PIXEL;
    fsDesc.Format = CKRST_SHADER_DXBC;
    fsDesc.Code = (CKBYTE *)s_fs_cubes;
    fsDesc.CodeSize = sizeof(s_fs_cubes);

    CKDWORD hVS = 1, hFS = 2;
    if (ctx->CreateShader(hVS, &vsDesc) != CK_OK) {
        LOG("Failed to create vertex shader");
        return 1;
    }
    if (ctx->CreateShader(hFS, &fsDesc) != CK_OK) {
        LOG("Failed to create fragment shader");
        return 1;
    }

    CKProgramDesc progDesc = {};
    progDesc.VertexShader = hVS;
    progDesc.PixelShader = hFS;
    progDesc.ConsumeShaders = TRUE;

    CKDWORD hProgram = 1;
    if (ctx->CreateProgram(hProgram, &progDesc) != CK_OK) {
        LOG("Failed to create program");
        return 1;
    }

    LOG("Resources created: layout=%u, vb=%u, ib=%u, program=%u",
        hLayout, hVB, hIB, hProgram);

    // ---- Build draw state (no culling to aid debugging) ----
    CKDrawState drawState = CKDrawStateBuilder()
        .WriteRGBA(TRUE, TRUE, TRUE, TRUE)
        .Depth(TRUE, TRUE, VXCMP_LESS)
        .Cull(VXCULL_CW)
        .MSAA(TRUE)
        .Topology(VX_TRIANGLELIST)
        .Build();

    // ---- Setup view 0 ----
    ctx->SetViewClear(0,
        CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH,
        0xFF303030, 1.0f, 0);

    CKRECT viewRect = { 0, 0, WIDTH, HEIGHT };
    ctx->SetViewRect(0, viewRect);

    // ---- Main loop ----
    LARGE_INTEGER freq, startTime;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&startTime);

    int frameCount = 0;

    while (s_running) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!s_running) break;

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float time = (float)(now.QuadPart - startTime.QuadPart) / (float)freq.QuadPart;

        // View + projection matrices
        VxMatrix view, proj;
        MtxLookAtLH(view, 0.0f, 0.0f, -35.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
        MtxPerspectiveFovLH(proj, 60.0f * 3.14159265f / 180.0f,
                             (float)WIDTH / (float)HEIGHT, 0.1f, 100.0f);
        ctx->SetViewTransform(0, &view, &proj);

        ctx->TouchView(0);

        // Draw 11x11 grid of cubes
        CKRasterizerEncoder *enc = ctx->BeginEncoder();
        if (enc) {
            enc->SetVertexLayout(hLayout);

            for (int yy = 0; yy < 11; ++yy) {
                for (int xx = 0; xx < 11; ++xx) {
                    VxMatrix model;
                    MtxRotateXY(model, time + xx * 0.21f, time + yy * 0.37f);

                    float *mtx = (float *)&model;
                    mtx[12] = -15.0f + (float)xx * 3.0f;
                    mtx[13] = -15.0f + (float)yy * 3.0f;
                    mtx[14] = 0.0f;

                    CKDWORD transformIdx = ctx->AllocTransform(&model, 1);
                    enc->SetTransform(transformIdx, 1);
                    enc->SetVertexBuffer(0, hVB, 0, 8);
                    enc->SetIndexBuffer(hIB, 0, 36);
                    enc->SetState(drawState);
                    enc->Submit(0, hProgram, 0, CKRST_DISCARD_ALL);
                }
            }
            ctx->EndEncoder(enc);
        }

        ctx->Frame(CKRST_FRAME_SYNC_VSYNC);
        frameCount++;

        if (frameCount % 300 == 0)
            LOG("Frame %d (%.1f sec)", frameCount, time);
    }

    // ---- Cleanup ----
    LOG("Shutting down after %d frames", frameCount);
    driver->DestroyContext(ctx);
    rstInfo.CloseFct(rasterizer);
    DestroyWindow(hwnd);

    return 0;
}
