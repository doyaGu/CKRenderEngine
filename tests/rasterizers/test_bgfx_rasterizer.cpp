#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "CKBgfxRasterizer.h"
#include "CKBgfxRasterizerContext.h"
#include "CKBgfxResources.h"
#include "CKBgfxInternal.h"
#include "CKBgfxDrawMapTrace.h"
#include "CKRasterizerValidation.h"
#include "VxWindowFunctions.h"

// Pull in bgfx defines for PT mask constants
#include <bgfx/defines.h>

// ============================================================================
// Test infrastructure
// ============================================================================

static int g_TestCount = 0;
static int g_PassCount = 0;
static int g_FailCount = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_TestCount++; \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d): %s\n", msg, __LINE__, #cond); \
        g_FailCount++; \
    } else { \
        g_PassCount++; \
    } \
} while(0)

#define TEST_SECTION(name) printf("\n[%s]\n", name)

static bool HasDisplayMode(CKRasterizerDriver *driver, int width, int height, int bpp, int refreshRate)
{
    for (int i = 0; driver && i < driver->GetDisplayModeCount(); ++i) {
        VxDisplayMode mode;
        if (!driver->GetDisplayMode(i, &mode))
            return false;
        if (mode.Width == width &&
            mode.Height == height &&
            mode.Bpp == bpp &&
            mode.RefreshRate == refreshRate) {
            return true;
        }
    }

    return false;
}

static bool DisplayModesAreSorted(CKRasterizerDriver *driver)
{
    for (int i = 1; driver && i < driver->GetDisplayModeCount(); ++i) {
        VxDisplayMode prev;
        VxDisplayMode cur;
        if (!driver->GetDisplayMode(i - 1, &prev) ||
            !driver->GetDisplayMode(i, &cur))
            return false;
        if (prev.Width != cur.Width) {
            if (prev.Width > cur.Width)
                return false;
            continue;
        }
        if (prev.Height != cur.Height) {
            if (prev.Height > cur.Height)
                return false;
            continue;
        }
        if (prev.Bpp != cur.Bpp) {
            if (prev.Bpp > cur.Bpp)
                return false;
            continue;
        }
        if (prev.RefreshRate > cur.RefreshRate)
            return false;
    }
    return true;
}

static const char *GetBgfxShaderSource(const CKShaderDesc &shader,
                                       CKDWORD &sourceSize)
{
    sourceSize = 0;
    const CKBYTE *data = static_cast<const CKBYTE *>(shader.Code);
    const CKDWORD dataSize = shader.CodeSize;
    if (!data || dataSize < 14 ||
        !((data[0] == 'V' || data[0] == 'F') &&
          data[1] == 'S' && data[2] == 'H'))
        return NULL;

    const CKDWORD version = data[3];
    CKDWORD cursor = version < 6 ? 8 : 12;
    if (cursor + 2 > dataSize)
        return NULL;
    const CKDWORD uniformCount = CKDWORD(data[cursor]) |
        (CKDWORD(data[cursor + 1]) << 8);
    cursor += 2;
    for (CKDWORD uniform = 0; uniform < uniformCount; ++uniform) {
        if (cursor >= dataSize)
            return NULL;
        CKDWORD entrySize = 1 + data[cursor] + 6;
        if (version >= 8)
            entrySize += 2;
        if (version >= 10)
            entrySize += 2;
        if (entrySize > dataSize - cursor)
            return NULL;
        cursor += entrySize;
    }

    if (cursor + 4 > dataSize)
        return NULL;
    sourceSize = CKDWORD(data[cursor]) |
        (CKDWORD(data[cursor + 1]) << 8) |
        (CKDWORD(data[cursor + 2]) << 16) |
        (CKDWORD(data[cursor + 3]) << 24);
    cursor += 4;
    if (sourceSize >= dataSize - cursor || data[cursor + sourceSize] != 0) {
        sourceSize = 0;
        return NULL;
    }
    return reinterpret_cast<const char *>(data + cursor);
}

// ============================================================================
// Fill mode / topology interaction
// ============================================================================

static void TestFillModeTopology()
{
    TEST_SECTION("Fill Mode / Topology Interaction");

    // Case 1: Solid fill + triangle list = no PT bits (default = triangles)
    {
        CKDrawStateBuilder b;
        b.Fill(VXFILL_SOLID).Topology(VX_TRIANGLELIST);
        CKDrawState s = b.Build();
        uint64_t pt = CKBgfxState(s) & BGFX_STATE_PT_MASK;
        TEST_ASSERT(pt == 0, "solid + trianglelist => no PT bits");
    }

    // Case 2: Solid fill + line list = PT_LINES
    {
        CKDrawStateBuilder b;
        b.Fill(VXFILL_SOLID).Topology(VX_LINELIST);
        CKDrawState s = b.Build();
        uint64_t pt = CKBgfxState(s) & BGFX_STATE_PT_MASK;
        TEST_ASSERT(pt == BGFX_STATE_PT_LINES, "solid + linelist => PT_LINES");
    }

    // Case 3: Solid fill + point list = PT_POINTS
    {
        CKDrawStateBuilder b;
        b.Fill(VXFILL_SOLID).Topology(VX_POINTLIST);
        CKDrawState s = b.Build();
        uint64_t pt = CKBgfxState(s) & BGFX_STATE_PT_MASK;
        TEST_ASSERT(pt == BGFX_STATE_PT_POINTS, "solid + pointlist => PT_POINTS");
    }

    // Case 4: Solid fill + tri strip = PT_TRISTRIP
    {
        CKDrawStateBuilder b;
        b.Fill(VXFILL_SOLID).Topology(VX_TRIANGLESTRIP);
        CKDrawState s = b.Build();
        uint64_t pt = CKBgfxState(s) & BGFX_STATE_PT_MASK;
        TEST_ASSERT(pt == BGFX_STATE_PT_TRISTRIP, "solid + tristrip => PT_TRISTRIP");
    }

    // Case 5: Solid fill + line strip = PT_LINESTRIP
    {
        CKDrawStateBuilder b;
        b.Fill(VXFILL_SOLID).Topology(VX_LINESTRIP);
        CKDrawState s = b.Build();
        uint64_t pt = CKBgfxState(s) & BGFX_STATE_PT_MASK;
        TEST_ASSERT(pt == BGFX_STATE_PT_LINESTRIP, "solid + linestrip => PT_LINESTRIP");
    }

    // Case 6: Wireframe fill overrides topology = always PT_LINES
    {
        CKDrawStateBuilder b;
        b.Fill(VXFILL_WIREFRAME).Topology(VX_TRIANGLELIST);
        CKDrawState s = b.Build();
        uint64_t pt = CKBgfxState(s) & BGFX_STATE_PT_MASK;
        TEST_ASSERT(pt == BGFX_STATE_PT_LINES, "wireframe + trianglelist => PT_LINES (overridden)");
    }

    // Case 7: Wireframe fill overrides tri strip too
    {
        CKDrawStateBuilder b;
        b.Fill(VXFILL_WIREFRAME).Topology(VX_TRIANGLESTRIP);
        CKDrawState s = b.Build();
        uint64_t pt = CKBgfxState(s) & BGFX_STATE_PT_MASK;
        TEST_ASSERT(pt == BGFX_STATE_PT_LINES, "wireframe + tristrip => PT_LINES (overridden)");
    }

    // Case 8: Point fill overrides topology = always PT_POINTS
    {
        CKDrawStateBuilder b;
        b.Fill(VXFILL_POINT).Topology(VX_TRIANGLELIST);
        CKDrawState s = b.Build();
        uint64_t pt = CKBgfxState(s) & BGFX_STATE_PT_MASK;
        TEST_ASSERT(pt == BGFX_STATE_PT_POINTS, "point + trianglelist => PT_POINTS (overridden)");
    }

    // Case 9: Point fill overrides line strip
    {
        CKDrawStateBuilder b;
        b.Fill(VXFILL_POINT).Topology(VX_LINESTRIP);
        CKDrawState s = b.Build();
        uint64_t pt = CKBgfxState(s) & BGFX_STATE_PT_MASK;
        TEST_ASSERT(pt == BGFX_STATE_PT_POINTS, "point + linestrip => PT_POINTS (overridden)");
    }

    // Case 10: Verify no spurious bit collisions (old bug: OR of both would set invalid combo)
    {
        CKDrawStateBuilder b;
        b.Fill(VXFILL_WIREFRAME).Topology(VX_TRIANGLESTRIP);
        CKDrawState s = b.Build();
        uint64_t pt = CKBgfxState(s) & BGFX_STATE_PT_MASK;
        // Old code would produce PT_LINES | PT_TRISTRIP = 0x0003... (PT_LINESTRIP)
        // New code produces only PT_LINES
        TEST_ASSERT(pt == BGFX_STATE_PT_LINES, "no spurious bit collision from fill+topology");
        TEST_ASSERT((pt & BGFX_STATE_PT_MASK) == BGFX_STATE_PT_LINES,
                    "masked result is purely PT_LINES");
    }
}

// ============================================================================
// CKDrawStateBuilder bit layout correctness
// ============================================================================

static void TestDrawStateBuilderLayout()
{
    TEST_SECTION("CKDrawStateBuilder Bit Layout");

    // Verify fill mode bits are at Lo[13:12]
    {
        CKDrawStateBuilder b;
        b.Fill(VXFILL_WIREFRAME);
        CKDrawState s = b.Build();
        CKDWORD fillBits = (s.Lo >> 12) & 0x3;
        TEST_ASSERT(fillBits == 1, "VXFILL_WIREFRAME maps to 1 in bits [13:12]");
    }
    {
        CKDrawStateBuilder b;
        b.Fill(VXFILL_POINT);
        CKDrawState s = b.Build();
        CKDWORD fillBits = (s.Lo >> 12) & 0x3;
        TEST_ASSERT(fillBits == 2, "VXFILL_POINT maps to 2 in bits [13:12]");
    }
    {
        CKDrawStateBuilder b;
        b.Fill(VXFILL_SOLID);
        CKDrawState s = b.Build();
        CKDWORD fillBits = (s.Lo >> 12) & 0x3;
        TEST_ASSERT(fillBits == 0, "VXFILL_SOLID maps to 0 in bits [13:12]");
    }

    // Verify topology bits are at Mid[8:6]
    {
        CKDrawStateBuilder b;
        b.Topology(VX_POINTLIST);
        CKDrawState s = b.Build();
        CKDWORD ptBits = (s.Mid >> 6) & 0x7;
        TEST_ASSERT(ptBits == VX_POINTLIST, "VX_POINTLIST stored correctly");
    }
    {
        CKDrawStateBuilder b;
        b.Topology(VX_LINELIST);
        CKDrawState s = b.Build();
        CKDWORD ptBits = (s.Mid >> 6) & 0x7;
        TEST_ASSERT(ptBits == VX_LINELIST, "VX_LINELIST stored correctly");
    }
    {
        CKDrawStateBuilder b;
        b.Topology(VX_TRIANGLESTRIP);
        CKDrawState s = b.Build();
        CKDWORD ptBits = (s.Mid >> 6) & 0x7;
        TEST_ASSERT(ptBits == VX_TRIANGLESTRIP, "VX_TRIANGLESTRIP stored correctly");
    }

    // Verify fill mode doesn't clobber other Lo bits
    {
        CKDrawStateBuilder b;
        b.Depth(TRUE, TRUE, VXCMP_LESSEQUAL).Fill(VXFILL_WIREFRAME);
        CKDrawState s = b.Build();
        TEST_ASSERT(s.Lo & CKRST_STATE_DEPTH_TEST, "depth test preserved after fill set");
        TEST_ASSERT(s.Lo & CKRST_STATE_DEPTH_WRITE, "depth write preserved after fill set");
        CKDWORD fillBits = (s.Lo >> 12) & 0x3;
        TEST_ASSERT(fillBits == 1, "fill mode set correctly alongside other bits");
    }
}

static CKDWORD ExtractStencilRef(uint32_t stencil)
{
    return (stencil & BGFX_STENCIL_FUNC_REF_MASK) >> BGFX_STENCIL_FUNC_REF_SHIFT;
}

static CKDWORD ExtractStencilRMask(uint32_t stencil)
{
    return (stencil & BGFX_STENCIL_FUNC_RMASK_MASK) >> BGFX_STENCIL_FUNC_RMASK_SHIFT;
}

static void TestBgfxStencilWriteMaskEncoding()
{
    TEST_SECTION("Bgfx Stencil Write Mask Encoding");

    CKDrawState enabled = CKDrawStateBuilder()
        .Stencil(TRUE, VXCMP_ALWAYS,
                 VXSTENCILOP_REPLACE, VXSTENCILOP_INCRSAT, VXSTENCILOP_DECRSAT)
        .Build();

    uint32_t full = CKBgfxBuildFrontStencil(enabled, 0xAB, 0xFF, 0xFF);
    TEST_ASSERT(full != BGFX_STENCIL_NONE, "enabled stencil produces bgfx stencil state");
    TEST_ASSERT(ExtractStencilRef(full) == 0xAB, "full write mask keeps low 8-bit ref");
    TEST_ASSERT(ExtractStencilRMask(full) == 0xFF, "read mask is encoded separately");
    TEST_ASSERT(CKBgfxBuildBackStencil(enabled, 0xAB, 0xFF, 0xFF) == BGFX_STENCIL_NONE,
                "full write mask uses the default back stencil state");

    uint32_t disabled = CKBgfxBuildFrontStencil(CKDrawState(), 0xAB, 0xFF, 0xFF);
    TEST_ASSERT(disabled == BGFX_STENCIL_NONE, "disabled stencil clears bgfx stencil state");

    CKDrawState disabledWithBackOps = CKDrawStateBuilder()
        .Stencil(FALSE, VXCMP_ALWAYS,
                 VXSTENCILOP_REPLACE, VXSTENCILOP_INCRSAT, VXSTENCILOP_DECRSAT)
        .StencilBack(VXCMP_ALWAYS,
                     VXSTENCILOP_REPLACE, VXSTENCILOP_INCRSAT, VXSTENCILOP_DECRSAT)
        .Build();
    TEST_ASSERT(CKBgfxBuildFrontStencil(disabledWithBackOps, 0xAB, 0xFF, 0xFF) == BGFX_STENCIL_NONE,
                "disabled stencil clears front state even if ops are present");
    TEST_ASSERT(CKBgfxBuildBackStencil(disabledWithBackOps, 0xAB, 0xFF, 0xFF) == BGFX_STENCIL_NONE,
                "disabled stencil clears back state even if back ops are present");

    uint32_t noWrite = CKBgfxBuildFrontStencil(enabled, 0xAB, 0xFF, 0x00);
    TEST_ASSERT((noWrite & BGFX_STENCIL_OP_FAIL_S_MASK) == BGFX_STENCIL_OP_FAIL_S_KEEP,
                "writeMask zero forces stencil fail op to KEEP");
    TEST_ASSERT((noWrite & BGFX_STENCIL_OP_FAIL_Z_MASK) == BGFX_STENCIL_OP_FAIL_Z_KEEP,
                "writeMask zero forces depth fail op to KEEP");
    TEST_ASSERT((noWrite & BGFX_STENCIL_OP_PASS_Z_MASK) == BGFX_STENCIL_OP_PASS_Z_KEEP,
                "writeMask zero forces pass op to KEEP");

    uint32_t masked = CKBgfxBuildFrontStencil(enabled, 0xAB, 0x0F, 0x0F);
    TEST_ASSERT(ExtractStencilRef(masked) == 0x0B,
                "matching read/write mask constrains ref to writable bits");
    TEST_ASSERT(ExtractStencilRMask(masked) == 0x0F,
                "matching read/write mask keeps compare mask");
    TEST_ASSERT(ExtractStencilRMask(CKBgfxBuildBackStencil(enabled, 0xAB, 0x0F, 0x0F)) == 0x0F,
                "back mask encodes the stencil write mask");

    uint32_t partial = CKBgfxBuildFrontStencil(enabled, 0xAB, 0xF0, 0x0F);
    TEST_ASSERT(ExtractStencilRef(partial) == 0xAB,
                "partial write mask with distinct read mask keeps compare ref");
    TEST_ASSERT(ExtractStencilRMask(partial) == 0xF0,
                "partial write mask with distinct read mask keeps read mask");
    TEST_ASSERT(ExtractStencilRMask(CKBgfxBuildBackStencil(enabled, 0xAB, 0xF0, 0x0F)) == 0x0F,
                "write mask remains independent of the read mask");
    TEST_ASSERT(ExtractStencilRMask(CKBgfxBuildBackStencil(enabled, 0xAB, 0xFF, 0x00)) == 0x00,
                "zero write mask suppresses all stencil writes");
}

static void TestTextureVolumeDescriptorDefaults()
{
    TEST_SECTION("Texture Volume Descriptor Defaults");

    CKTextureDesc desc;
    TEST_ASSERT(desc.Depth == 1, "texture desc defaults to a single 2D slice");

    desc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_VOLUMEMAP;
    desc.Depth = 4;
    TEST_ASSERT((desc.Flags & CKRST_TEXTURE_VOLUMEMAP) != 0 && desc.Depth == 4,
                "volume texture desc carries explicit depth");
}

static void TestSamplerCompareFlags()
{
    TEST_SECTION("Sampler Compare Flags");

    CKSamplerDesc sampler = {};
    sampler.MinFilter = CKRST_FILTER_LINEAR;
    sampler.MagFilter = CKRST_FILTER_LINEAR;
    sampler.MipFilter = CKRST_FILTER_LINEAR;
    sampler.AddressU = CKRST_ADDRESS_WRAP;
    sampler.AddressV = CKRST_ADDRESS_WRAP;
    sampler.AddressW = CKRST_ADDRESS_WRAP;
    sampler.CompareFunc = CKRST_COMPARE_NONE;
    TEST_ASSERT((CKBgfxSamplerFlags(&sampler) & BGFX_SAMPLER_COMPARE_LEQUAL) == 0,
                "compare NONE emits no compare flags");

    sampler.CompareFunc = CKRST_COMPARE_LEQUAL;
    TEST_ASSERT((CKBgfxSamplerFlags(&sampler) & BGFX_SAMPLER_COMPARE_LEQUAL) != 0,
                "compare LEQUAL emits bgfx compare flag");

    sampler = {};
    sampler.MipFilter = CKRST_FILTER_NONE;
    TEST_ASSERT(CKBgfxSamplerWantsMipMaps(&sampler) == FALSE,
                "explicit no-mip sampler requests base-level sampling");
    sampler.MipFilter = CKRST_FILTER_LINEARMIPLINEAR;
    TEST_ASSERT(CKBgfxSamplerWantsMipMaps(&sampler) == TRUE,
                "trilinear sampler requests mip sampling");
}

static bool BgfxEmbedsShaderProfile(CK_SHADER_PROFILE profile)
{
    for (CKDWORD index = 0; index < CKBgfxRasterizerShaderProfileCount(); ++index) {
        if (CKBgfxRasterizerShaderProfile(index) == profile)
            return true;
    }
    return false;
}

static void TestBackendProfileMapping()
{
    TEST_SECTION("Backend Profile Mapping");

    bgfx::RendererType::Enum parsedRenderer = bgfx::RendererType::Noop;
    TEST_ASSERT(CKBgfxTryRendererType("auto", parsedRenderer) &&
                    parsedRenderer == bgfx::RendererType::Count,
                "Explicit auto renderer parses as automatic selection");
    TEST_ASSERT(CKBgfxTryRendererType("vulkan", parsedRenderer) &&
                    parsedRenderer == bgfx::RendererType::Vulkan,
                "Explicit Vulkan renderer parses exactly");
    TEST_ASSERT(CKBgfxTryRendererType("DIRECT3D12", parsedRenderer) &&
                    parsedRenderer == bgfx::RendererType::Direct3D12,
                "Renderer names remain case-insensitive");
    TEST_ASSERT(!CKBgfxTryRendererType("webgpu", parsedRenderer),
                "Backends without a shader profile must be rejected");
    TEST_ASSERT(!CKBgfxTryRendererType("vulakn", parsedRenderer),
                "Unknown explicit renderer must not fall back to auto");

    TEST_ASSERT(CKBgfxShaderProfile(bgfx::RendererType::Direct3D11) == CKRST_SHADER_PROFILE_DX11,
                "D3D11 renderer maps to dx11 shader profile");
    TEST_ASSERT(CKBgfxShaderProfile(bgfx::RendererType::Direct3D12) == CKRST_SHADER_PROFILE_DX12,
                "D3D12 renderer maps to dx12 shader profile");
    TEST_ASSERT(CKBgfxShaderProfile(bgfx::RendererType::Vulkan) == CKRST_SHADER_PROFILE_SPIRV,
                "Vulkan renderer maps to SPIR-V shader profile");
    TEST_ASSERT(CKBgfxShaderProfile(bgfx::RendererType::OpenGL) == CKRST_SHADER_PROFILE_GLSL,
                "OpenGL renderer maps to GLSL shader profile");
    TEST_ASSERT(CKBgfxShaderProfile(bgfx::RendererType::OpenGLES) == CKRST_SHADER_PROFILE_ESSL,
                "OpenGLES renderer maps to ESSL shader profile");
    TEST_ASSERT(CKBgfxShaderProfile(bgfx::RendererType::Metal) == CKRST_SHADER_PROFILE_MSL,
                "Metal renderer maps to MSL shader profile");
    TEST_ASSERT(CKBgfxShaderProfile(bgfx::RendererType::WebGPU) == CKRST_SHADER_PROFILE_UNKNOWN,
                "WebGPU remains unsupported while clip-distance shaders cannot be compiled");
    TEST_ASSERT(strcmp(CKBgfxRendererTypeName(bgfx::RendererType::WebGPU), "WebGPU") == 0,
                "Unsupported WebGPU requests must still be identified explicitly");
    TEST_ASSERT(strcmp(CKBgfxShaderProfileName(CKRST_SHADER_PROFILE_SPIRV), "spirv") == 0,
                "Shader profile name must be stable for diagnostics");
    TEST_ASSERT(strcmp(CKBgfxShaderProfileName(CKRST_SHADER_PROFILE_ESSL), "essl") == 0,
                "ESSL shader profile name must be stable for diagnostics");

    bgfx::RendererType::Enum renderers[bgfx::RendererType::Count];
    const uint8_t rendererCount = bgfx::getSupportedRenderers(bgfx::RendererType::Count, renderers);
    CKDWORD rendererProfiles = 0;
    bool embedded = true;
    for (uint8_t index = 0; index < rendererCount; ++index) {
        const CK_SHADER_PROFILE profile = CKBgfxShaderProfile(renderers[index]);
        if (profile == CKRST_SHADER_PROFILE_UNKNOWN)
            continue;
        ++rendererProfiles;
        embedded = embedded && BgfxEmbedsShaderProfile(profile);
    }
    TEST_ASSERT(embedded && rendererProfiles == CKBgfxRasterizerShaderProfileCount(),
                "the rasterizer embeds the shader profiles of exactly the bgfx renderers");
    TEST_ASSERT(CKBgfxRasterizerShaderProfile(CKBgfxRasterizerShaderProfileCount()) ==
                    CKRST_SHADER_PROFILE_UNKNOWN,
                "embedded shader profiles out of range are unknown");
}

static void TestBgfxStateBackendConventions()
{
    TEST_SECTION("Bgfx State Backend Conventions");

    CKDrawState writeDepth = CKDrawStateBuilder()
        .WriteRGBA(TRUE, FALSE, TRUE, FALSE)
        .Depth(TRUE, FALSE, VXCMP_GREATER)
        .Cull(VXCULL_CW)
        .FrontFaceCCW(TRUE)
        .Build();
    uint64_t state = CKBgfxState(writeDepth);

    TEST_ASSERT((state & BGFX_STATE_WRITE_R) != 0 &&
                    (state & BGFX_STATE_WRITE_G) == 0 &&
                    (state & BGFX_STATE_WRITE_B) != 0 &&
                    (state & BGFX_STATE_WRITE_A) == 0,
                "color write mask maps channel-by-channel");
    TEST_ASSERT((state & BGFX_STATE_DEPTH_TEST_GREATER) != 0,
                "VXCMP_GREATER maps to bgfx greater depth test");
    TEST_ASSERT((state & BGFX_STATE_WRITE_Z) == 0,
                "disabled depth write clears bgfx WRITE_Z");
    TEST_ASSERT((state & BGFX_STATE_CULL_CW) != 0,
                "Virtools CW cull maps to bgfx CW cull");
    TEST_ASSERT((state & BGFX_STATE_FRONT_CCW) != 0,
                "explicit front-face CCW bit reaches bgfx state");

    CKDrawState blend = CKDrawStateBuilder()
        .BlendSeparate(VXBLEND_SRCALPHA, VXBLEND_INVSRCALPHA,
                       VXBLEND_ONE, VXBLEND_ZERO)
        .BlendEquationSeparate(VXBLENDOP_SUBTRACT, VXBLENDOP_MAX)
        .Build();
    state = CKBgfxState(blend);
    TEST_ASSERT((state & BGFX_STATE_BLEND_MASK) != 0,
                "separate blend factors produce bgfx blend state");
    TEST_ASSERT((state & BGFX_STATE_BLEND_EQUATION_MASK) != 0,
                "separate blend equations produce bgfx equation state");
    for (const VXBLEND_MODE combined : {VXBLEND_BOTHSRCALPHA, VXBLEND_BOTHINVSRCALPHA}) {
        const CKDrawState unresolved = CKDrawStateBuilder().Blend(combined, VXBLEND_ZERO).Build();
        TEST_ASSERT(CKBgfxTryState(unresolved, state) == CKERR_INVALIDPARAMETER,
                    "native state requires the rasterizer to resolve combined legacy blend modes");
    }
}

static void TestSamplerFilterAndAddressConventions()
{
    TEST_SECTION("Sampler Filter and Address Conventions");

    CKSamplerDesc sampler = {};
    sampler.MinFilter = CKRST_FILTER_NEAREST;
    sampler.MagFilter = CKRST_FILTER_NEAREST;
    sampler.MipFilter = CKRST_FILTER_MIPNEAREST;
    sampler.AddressU = CKRST_ADDRESS_MIRROR;
    sampler.AddressV = CKRST_ADDRESS_CLAMP;
    sampler.AddressW = CKRST_ADDRESS_BORDER;
    sampler.BorderColor = 3;

    uint32_t flags = CKBgfxSamplerFlags(&sampler);
    TEST_ASSERT((flags & BGFX_SAMPLER_MIN_POINT) != 0 &&
                    (flags & BGFX_SAMPLER_MAG_POINT) != 0 &&
                    (flags & BGFX_SAMPLER_MIP_POINT) != 0,
                "nearest min/mag/mip filters map to point sampler flags");
    TEST_ASSERT((flags & BGFX_SAMPLER_U_MIRROR) != 0 &&
                    (flags & BGFX_SAMPLER_V_CLAMP) != 0 &&
                    (flags & BGFX_SAMPLER_W_BORDER) != 0,
                "U/V/W address modes map independently");
    TEST_ASSERT((flags & BGFX_SAMPLER_BORDER_COLOR_MASK) == BGFX_SAMPLER_BORDER_COLOR(3),
                "border color index is preserved in sampler flags");
    sampler.BorderColor = 0x80402010u;
    TEST_ASSERT(CKRasterizerValidateSampler(&sampler) == CK_OK,
                "the generic sampler accepts an actual ARGB border color");
    TEST_ASSERT(!CKBgfxTrySamplerFlags(&sampler, flags),
                "native sampler encoding requires an allocated palette index");
    sampler.BorderColor = 15;
    TEST_ASSERT(CKBgfxTrySamplerFlags(&sampler, flags),
                "all sixteen native palette entries are valid");
    sampler.BorderColor = 16;
    TEST_ASSERT(!CKBgfxTrySamplerFlags(&sampler, flags),
                "native palette indices beyond fifteen are rejected");
}

static void TestOpenGLAutoMipPolicy()
{
    TEST_SECTION("OpenGL Auto Mip Policy");

    CKDWORD full = CKBgfxTextureMipCount(256, 128, 1);
    TEST_ASSERT(full == 9, "full mip count follows largest texture dimension");
    TEST_ASSERT(CKBgfxIsAutoMipRequest((CKDWORD)-1, full) == TRUE,
                "legacy -1 mip count requests automatic mipmaps");
    TEST_ASSERT(CKBgfxIsAutoMipRequest(full + 1, full) == TRUE,
                "oversized mip count requests automatic mipmaps");
    TEST_ASSERT(CKBgfxIsAutoMipRequest(4, full) == FALSE,
                "explicit in-range mip count is not automatic");
    TEST_ASSERT(CKBgfxCanRepresentMipCount(0, full) == TRUE &&
                CKBgfxCanRepresentMipCount(1, full) == TRUE &&
                CKBgfxCanRepresentMipCount(full, full) == TRUE &&
                CKBgfxCanRepresentMipCount((CKDWORD)-1, full) == TRUE &&
                CKBgfxCanRepresentMipCount(4, full) == FALSE,
                "bgfx rejects partial explicit chains that would leave physical mips uninitialized");

    TEST_ASSERT(CKBgfxShouldCreateTextureMipChain((CKDWORD)-1, full, TRUE, FALSE) == FALSE,
                "OpenGL defers automatic mips until data can populate the full chain");
    TEST_ASSERT(CKBgfxShouldCreateTextureMipChain((CKDWORD)-1, full, TRUE, TRUE) == TRUE,
                "OpenGL creates automatic mip chain when complete data is available");
    TEST_ASSERT(CKBgfxShouldCreateTextureMipChain((CKDWORD)-1, full, FALSE, FALSE) == FALSE,
                "all backends defer automatic mips until data can populate the full chain");
    TEST_ASSERT(CKBgfxShouldCreateTextureMipChain(0, full, TRUE, TRUE) == FALSE,
                "zero mip request creates a base-level texture");
    TEST_ASSERT(CKBgfxShouldCreateTextureMipChain(1, full, TRUE, TRUE) == FALSE,
                "one mip request creates a base-level texture");
    TEST_ASSERT(CKBgfxShouldCreateTextureMipChain(4, full, TRUE, FALSE) == TRUE,
                "explicit mip requests still allocate a mip chain");

    TEST_ASSERT(CKBgfxImageRowBytes(4, 32) == 16,
                "row byte helper derives uncompressed pitch from width and bpp");
    TEST_ASSERT(CKBgfxResolveImagePitch(4, 4, 32, 0) == 16,
                "zero pitch falls back to tight rows");
    TEST_ASSERT(CKBgfxResolveImagePitch(4, 4, 32, 20) == 20,
                "explicit padded pitch is preserved");
    TEST_ASSERT(CKBgfxResolveImagePitch(4, 4, 32, 64) == 64,
                "pitch equal to packed image size is still a row pitch");
    TEST_ASSERT(CKBgfxResolveImagePitch(4, 2, 32, 128) == 128,
                "large padded row pitch must not be divided by height");

    TEST_ASSERT(CKBgfxResolveAutoMipUpdateAction(TRUE, 1, TRUE, TRUE) == CKBGFX_AUTOMIP_UPDATE_PROMOTE,
                "full auto-mip update promotes base texture to a complete mip chain");
    TEST_ASSERT(CKBgfxResolveAutoMipUpdateAction(TRUE, full, TRUE, FALSE) == CKBGFX_AUTOMIP_UPDATE_DEMOTE,
                "unsupported full auto-mip update demotes incomplete mip chain");
    TEST_ASSERT(CKBgfxResolveAutoMipUpdateAction(TRUE, full, FALSE, FALSE) == CKBGFX_AUTOMIP_UPDATE_KEEP,
                "partial auto-mip update keeps existing mip chain instead of clearing texture content");
    TEST_ASSERT(CKBgfxResolveAutoMipUpdateAction(FALSE, full, FALSE, FALSE) == CKBGFX_AUTOMIP_UPDATE_NONE,
                "non-auto textures do not use auto-mip update actions");
}

// ============================================================================
// Rasterizer start/close lifecycle
// ============================================================================

static void TestBgfxRasterizerLifecycle()
{
    TEST_SECTION("CKBgfxRasterizer Start/Close Lifecycle");

    CKBgfxRasterizer rasterizer;
    TEST_ASSERT(rasterizer.GetDriverCount() == 0, "new rasterizer has no drivers");
    TEST_ASSERT(rasterizer.Start(NULL) == TRUE, "start succeeds without creating a backend");
    TEST_ASSERT(rasterizer.GetDriverCount() == 1, "start creates one bgfx driver");
    TEST_ASSERT(rasterizer.Start(NULL) == TRUE, "repeated start is idempotent");
    TEST_ASSERT(rasterizer.GetDriverCount() == 1, "repeated start does not add another driver");

    CKBgfxRasterizerDriver *driver =
        static_cast<CKBgfxRasterizerDriver *>(rasterizer.GetDriver(0));
    TEST_ASSERT(driver != NULL, "driver exists after start");
    CKRasterizerDriverDesc driverDesc = {};
    TEST_ASSERT(driver->GetDesc(&driverDesc) && driverDesc.Hardware,
                "bgfx driver is marked hardware");
    TEST_ASSERT(driver->GetDisplayModeCount() > 0,
                "bgfx driver exposes real modes or a minimal fallback list");
    TEST_ASSERT(DisplayModesAreSorted(driver), "bgfx display modes are sorted for screen-mode grouping");
    TEST_ASSERT(HasDisplayMode(driver, 640, 480, 32, 60),
                "bgfx driver exposes the legacy 640x480 windowed mode");
    TEST_ASSERT(!HasDisplayMode(driver, 800, 600, 16, 60),
                "bgfx driver does not fabricate legacy 16-bit display modes");
    TEST_ASSERT(driver->GetDesc(&driverDesc) && !driverDesc.CapsFinal,
                "bgfx legacy caps remain provisional until a backend initializes bgfx");
    TEST_ASSERT(rasterizer.GetDriver(1) == NULL, "out-of-range driver index yields NULL");

    XClassArray<CKFFShaderTarget> shaderTargets;
    driver->GetShaderTargets(shaderTargets);
    TEST_ASSERT(shaderTargets.Size() == (int)CKBgfxRasterizerShaderProfileCount(),
                "rasterizer advertises each embedded bgfx artifact profile");
    for (int targetIndex = 0; targetIndex < shaderTargets.Size(); ++targetIndex) {
        const CKFFShaderTarget &target = shaderTargets[targetIndex];
        CKRasterizerDeviceCaps caps;
        caps.ShaderFormat = target.Format;
        caps.ShaderProfile = target.Profile;
        CKFFShaderSet shaders;
        TEST_ASSERT(target.Format == CKRST_SHADER_FORMAT_BGFX && driver->GetShaderSet(caps, shaders) &&
                        shaders.Matches(target.Format, target.Profile),
                    "every advertised profile resolves a complete rasterizer shader catalog");
    }
    CKRasterizerDeviceCaps foreignTarget;
    foreignTarget.ShaderFormat = CKRST_SHADER_FORMAT_DXIL;
    foreignTarget.ShaderProfile = CKRST_SHADER_PROFILE_DX12;
    CKFFShaderSet foreignShaders;
    TEST_ASSERT(!driver->GetShaderSet(foreignTarget, foreignShaders) && !foreignShaders.Shaders[0].Code,
                "bgfx rasterizer never offers containers as native DXIL artifacts");

    rasterizer.Close();
    TEST_ASSERT(rasterizer.GetDriverCount() == 0, "close removes driver");
    rasterizer.Close();
    TEST_ASSERT(rasterizer.GetDriverCount() == 0, "repeated close is safe");
}

// ============================================================================
// DrawMap trace binding-order helpers
// ============================================================================

static void TestDrawMapTraceBindingOrder()
{
    TEST_SECTION("DrawMap Trace Binding Order");

    TEST_ASSERT(CKBGFX_DRAWMAP_SCHEMA == 2,
                "DrawMap submit schema stays at version 2");
    TEST_ASSERT(strcmp(CKBGFX_DRAWMAP_TAG_SUBMIT_MAP, "SubmitMap") == 0,
                "SubmitMap tag is centralized");
    TEST_ASSERT(strcmp(CKBGFX_DRAWMAP_TAG_PROGRAM_MAP, "ProgramMap") == 0,
                "ProgramMap tag is centralized");
    TEST_ASSERT(strcmp(CKBGFX_DRAWMAP_TAG_TEXTURE_MAP, "TextureMap") == 0,
                "TextureMap tag is centralized");
    TEST_ASSERT(strcmp(CKBGFX_DRAWMAP_TAG_BUFFER_MAP, "BufferMap") == 0,
                "BufferMap tag is centralized");
    TEST_ASSERT(strcmp(CKBGFX_DRAWMAP_TAG_STATE_MAP, "StateMap") == 0,
                "StateMap tag is centralized");

    char buffer[128];
    CKDWORD offset = 0;
    buffer[0] = '\0';

    TEST_ASSERT(CKBgfxDrawMapAppendTextureBinding(buffer, sizeof(buffer),
                                                  &offset, 3, 77, 8, 21,
                                                  0x1234) == TRUE,
                "texture binding formatter succeeds");
    TEST_ASSERT(strcmp(buffer, " tex3=77:8:21:0x00001234") == 0,
                "texture binding formatter keeps draw-packet order");

    offset = 0;
    buffer[0] = '\0';
    TEST_ASSERT(CKBgfxDrawMapAppendVertexBinding(buffer, sizeof(buffer),
                                                 &offset, 1, 5, 2, 12, 9,
                                                 3) == TRUE,
                "vertex binding formatter succeeds");
    TEST_ASSERT(strcmp(buffer, " vb1=5:2:12:9:3") == 0,
                "vertex binding formatter keeps draw-packet order");
}

static void TestPersistentCacheCallback()
{
    TEST_SECTION("Persistent Cache Callback");

    const uint64_t cacheId = 0x434B525354544553ull;
    const unsigned char source[] = {0x43, 0x4B, 0x52, 0x53, 0x54, 0x01};
    unsigned char destination[sizeof(source)] = {};
    CKBgfxCallback callback;

    callback.cacheWrite(cacheId, source, (uint32_t)sizeof(source));
    TEST_ASSERT(callback.cacheReadSize(cacheId) == sizeof(source),
                "Cache callback reports the persisted byte count");
    TEST_ASSERT(callback.cacheRead(cacheId, destination,
                                   (uint32_t)sizeof(destination)),
                "Cache callback reads a complete persisted entry");
    TEST_ASSERT(memcmp(source, destination, sizeof(source)) == 0,
                "Cache callback preserves entry contents");

    XString cacheFile = CKBgfxModuleSiblingFile(
        (const void *)&TestPersistentCacheCallback,
        "CKBgfxCache/434B525354544553.bin");
    if (cacheFile.Length() > 0)
        VxDeleteFile(cacheFile.CStr());
}

// ============================================================================
// Debug overlay view allocation
// ============================================================================

static void TestDebugOverlayViewAllocation()
{
    TEST_SECTION("Debug Overlay View Allocation");

    const char *line0 = CKBgfxDebugViewLine0();
    const char *line1 = CKBgfxDebugViewLine1();
    TEST_ASSERT(strcmp(line0, "passes: sequential per BeginPass (clear, scene, composite,") == 0,
                "overlay first line describes the pass model");
    TEST_ASSERT(strcmp(line1, "        overlay, present, readback); draws carry sticky state") == 0,
                "overlay second line describes the pass model");
}

// ============================================================================
// Exact pixel-format mapping
// ============================================================================

static void TestExactPixelFormatMapping()
{
    TEST_SECTION("Exact Pixel Format Mapping");

    struct ExactFormatMapping {
        VX_PIXELFORMAT PixelFormat;
        bgfx::TextureFormat::Enum NativeFormat;
    };
    const ExactFormatMapping exactMappings[] = {
        {_32_ARGB8888, bgfx::TextureFormat::BGRA8},
        {_32_ABGR8888, bgfx::TextureFormat::RGBA8},
        {_24_BGR888, bgfx::TextureFormat::RGB8},
        {_16_RGB565, bgfx::TextureFormat::B5G6R5},
        {_16_BGR565, bgfx::TextureFormat::R5G6B5},
        {_16_ARGB1555, bgfx::TextureFormat::BGR5A1},
        {_16_ABGR1555, bgfx::TextureFormat::RGB5A1},
        {_16_ARGB4444, bgfx::TextureFormat::BGRA4},
        {_16_ABGR4444, bgfx::TextureFormat::RGBA4},
        {_DXT1, bgfx::TextureFormat::BC1},
        {_DXT3, bgfx::TextureFormat::BC2},
        {_DXT5, bgfx::TextureFormat::BC3},
        {_16_V8U8, bgfx::TextureFormat::RG8S},
        {_32_V16U16, bgfx::TextureFormat::RG16S},
    };

    for (int i = 0; i < (int)(sizeof(exactMappings) / sizeof(exactMappings[0])); ++i) {
        bgfx::TextureFormat::Enum nativeFormat = bgfx::TextureFormat::Count;
        VX_PIXELFORMAT pixelFormat = UNKNOWN_PF;
        TEST_ASSERT(CKBgfxTryTextureFormat(exactMappings[i].PixelFormat, nativeFormat),
                    "exact CK format maps to bgfx");
        TEST_ASSERT(nativeFormat == exactMappings[i].NativeFormat,
                    "exact CK format keeps its channel layout");
        TEST_ASSERT(CKBgfxTryPixelFormat(exactMappings[i].NativeFormat, pixelFormat),
                    "exact bgfx format maps back to CK");
        TEST_ASSERT(pixelFormat == exactMappings[i].PixelFormat,
                    "exact pixel-format mapping round-trips");
    }

    const VX_PIXELFORMAT incompatibleFormats[] = {
        _32_RGB888,
        _32_BGRA8888,
        _32_RGBA8888,
        _24_RGB888,
        _16_RGB555,
        _16_BGR555,
    };
    for (int i = 0; i < (int)(sizeof(incompatibleFormats) / sizeof(incompatibleFormats[0])); ++i) {
        bgfx::TextureFormat::Enum nativeFormat = bgfx::TextureFormat::Count;
        TEST_ASSERT(!CKBgfxTryTextureFormat(incompatibleFormats[i], nativeFormat),
                    "incompatible channel layout is rejected instead of aliased");
    }

    bgfx::TextureFormat::Enum storageFormat = bgfx::TextureFormat::Count;
    TEST_ASSERT(CKBgfxTryTextureStorageFormat(_16_L6V5U5, storageFormat) &&
                    storageFormat == bgfx::TextureFormat::RGBA8,
                "L6V5U5 uses portable RGBA8 storage");
    TEST_ASSERT(CKBgfxTryTextureStorageFormat(_32_X8L8V8U8, storageFormat) &&
                    storageFormat == bgfx::TextureFormat::RGBA8,
                "X8L8V8U8 uses portable RGBA8 storage");

    const CKWORD l6v5u5[] = {
        (CKWORD)(15u | (16u << 5) | (63u << 10)),
        (CKWORD)(16u | (15u << 5))
    };
    CKBYTE converted16[8] = {};
    TEST_ASSERT(CKBgfxConvertBumpLuminancePixels(
                    _16_L6V5U5, l6v5u5, sizeof(l6v5u5), 2, 1,
                    converted16, sizeof(converted16)),
                "L6V5U5 conversion succeeds");
    TEST_ASSERT(converted16[0] == 255 && converted16[1] == 0 &&
                    converted16[2] == 255 && converted16[3] == 255,
                "L6V5U5 positive U negative V and luminance decode exactly");
    TEST_ASSERT(converted16[4] == 0 && converted16[5] == 255 &&
                    converted16[6] == 0 && converted16[7] == 255,
                "L6V5U5 negative U positive V and zero luminance decode exactly");

    const CKDWORD x8l8v8u8 = 0x007F0180u;
    CKBYTE converted32[4] = {};
    TEST_ASSERT(CKBgfxConvertBumpLuminancePixels(
                    _32_X8L8V8U8, &x8l8v8u8, sizeof(x8l8v8u8), 1, 1,
                    converted32, sizeof(converted32)),
                "X8L8V8U8 conversion succeeds");
    TEST_ASSERT(converted32[0] == 0 && converted32[1] == 129 &&
                    converted32[2] == 127 && converted32[3] == 255,
                "X8L8V8U8 signed DuDv and luminance decode exactly");

    CKDWORD mipChain[21];
    for (int i = 0; i < 21; ++i)
        mipChain[i] = 0x00800000u;
    mipChain[16] = 0x0040007Fu;
    mipChain[20] = 0x00FF7F00u;
    CKBYTE convertedMipChain[21 * 4] = {};
    TEST_ASSERT(CKBgfxConvertBumpLuminanceMipChain(
                    _32_X8L8V8U8, mipChain, sizeof(mipChain),
                    4, 4, 3, convertedMipChain, sizeof(convertedMipChain)),
                "packed luminance bump mip-chain conversion succeeds");
    TEST_ASSERT(convertedMipChain[16 * 4 + 0] == 255 &&
                    convertedMipChain[16 * 4 + 1] == 128 &&
                    convertedMipChain[16 * 4 + 2] == 64,
                "first non-base bump mip starts at the expected offset");
    TEST_ASSERT(convertedMipChain[20 * 4 + 0] == 128 &&
                    convertedMipChain[20 * 4 + 1] == 255 &&
                    convertedMipChain[20 * 4 + 2] == 255,
                "last bump mip starts at the expected offset");
    TEST_ASSERT(!CKBgfxConvertBumpLuminanceMipChain(
                    _32_X8L8V8U8, mipChain, sizeof(mipChain) - 1,
                    4, 4, 3, convertedMipChain, sizeof(convertedMipChain)),
                "short packed luminance bump mip chains are rejected");

    bgfx::TextureInfo mipInfo;
    bgfx::calcTextureSize(mipInfo, 4, 4, 1, false, true, 1,
                          bgfx::TextureFormat::RGBA8);
    TEST_ASSERT(mipInfo.storageSize == sizeof(convertedMipChain),
                "RGBA8 mip-chain storage matches tightly packed converted data");
}

// ============================================================================
// Main
// ============================================================================

static void TestGenerationCheckedResourceTable()
{
    TEST_SECTION("Generation-checked Resource Table");

    VxMutex mutex;
    CKBgfxResourceTable<CKBgfxShaderRecord> table(mutex);
    CKBgfxShaderRecord *firstRecord = new CKBgfxShaderRecord();
    firstRecord->Handle = BGFX_INVALID_HANDLE;
    firstRecord->Stage = CKRST_SHADER_VERTEX;
    const CKDWORD first = table.Insert(firstRecord, 1);
    TEST_ASSERT(first != 0, "insertion atomically owns a slot and its record");
    TEST_ASSERT(table.IsAlive(first) && table.Get(first) == firstRecord,
                "the current generation resolves to its record");

    CKBgfxShaderRecord *removed = table.Remove(first);
    TEST_ASSERT(removed == firstRecord && !table.IsAlive(first) && table.Get(first) == NULL,
                "removal invalidates the old handle immediately");
    CKBgfxDestroyRecord(removed);

    CKBgfxShaderRecord *secondRecord = new CKBgfxShaderRecord();
    secondRecord->Handle = BGFX_INVALID_HANDLE;
    secondRecord->Stage = CKRST_SHADER_PIXEL;
    const CKDWORD second = table.Insert(secondRecord, 1);
    TEST_ASSERT(second != 0 && second != first &&
                    (second & 0xffffu) == (first & 0xffffu),
                "slot reuse advances the generation");
    TEST_ASSERT(table.Get(first) == NULL && table.Get(second) == secondRecord,
                "only the current generation resolves after slot reuse");
    CKBgfxShaderRecord *overflowRecord = new CKBgfxShaderRecord();
    overflowRecord->Handle = BGFX_INVALID_HANDLE;
    overflowRecord->Stage = CKRST_SHADER_VERTEX;
    TEST_ASSERT(table.Insert(overflowRecord, 1) == 0,
                "the live-slot limit rejects a concurrent second insertion");
    CKBgfxDestroyRecord(overflowRecord);
    table.DestroyAll();
    TEST_ASSERT(!table.IsAlive(second),
                "bulk destruction clears every live slot");
}

void TestBorderPaletteLifetime() {
    CKBgfxBorderPalette palette;
    for (CKDWORD i = 0; i < 16; ++i) {
        const auto entry = palette.Resolve(0xff000000u | (i * 16));
        TEST_ASSERT(entry.Index == i && entry.Added && entry.Available,
                    "Sixteen exact colors remain stable within a submission");
    }
    const auto repeated = palette.Resolve(0xff000050u);
    TEST_ASSERT(repeated.Index == 5 && !repeated.Added && repeated.Available,
                "An existing color reuses its entry");
    const auto overflow = palette.Resolve(0xff000052u);
    TEST_ASSERT(!overflow.Added && !overflow.Available,
                "Overflow requires shader-assisted border sampling");
    palette.Reset();
    const auto next = palette.Resolve(0x80402010u);
    TEST_ASSERT(next.Index == 0 && next.Added && next.Available,
                "A submitted frame releases the palette entries");
}

void TestShaderBorderMetadata() {
    CKSamplerDesc sampler;
    sampler.AddressU = CKRST_ADDRESS_BORDER;
    sampler.AddressV = CKRST_ADDRESS_MIRROR;
    sampler.AddressW = CKRST_ADDRESS_CLAMP;
    sampler.MinFilter = CKRST_FILTER_ANISOTROPIC;
    sampler.MagFilter = CKRST_FILTER_LINEAR;
    sampler.MipFilter = CKRST_FILTER_NEAREST;
    sampler.BorderColor = 0x80402010u;
    sampler.MaxAnisotropy = 8;
    sampler.ShaderAnisotropy = 1;
    float color[4], state[4];
    CKBgfxPackSamplerMetadata(sampler, color, state);
    TEST_ASSERT(color[0] == 64.0f / 255.0f && color[1] == 32.0f / 255.0f &&
                    color[2] == 16.0f / 255.0f && color[3] == 128.0f / 255.0f,
                "shader metadata preserves the exact ARGB border color as RGBA");
    TEST_ASSERT(state[0] == float(unsigned(CKRST_ADDRESS_BORDER) |
                                  (unsigned(CKRST_ADDRESS_MIRROR) << 4) |
                                  (unsigned(CKRST_ADDRESS_CLAMP) << 8)) &&
                    state[1] == float(CKRST_FILTER_ANISOTROPIC) &&
                    state[2] == float(CKRST_FILTER_LINEAR) &&
                    state[3] == float(unsigned(CKRST_FILTER_NEAREST) | (8u << 4)),
                "shader metadata preserves address, filter and anisotropy state");
}

void TestFixedFunctionFragmentSamplingVariants()
{
    TEST_SECTION("Fixed-Function Fragment Sampling Variants");
    const CK_SHADER_PROFILE profiles[] = {
        CKRST_SHADER_PROFILE_DX11,
        CKRST_SHADER_PROFILE_DX12,
        CKRST_SHADER_PROFILE_SPIRV,
        CKRST_SHADER_PROFILE_GLSL,
        CKRST_SHADER_PROFILE_ESSL,
        CKRST_SHADER_PROFILE_MSL,
    };
    CKRasterizerDeviceCaps caps;
    caps.ShaderFormat = CKRST_SHADER_FORMAT_BGFX;
    for (CK_SHADER_PROFILE profile : profiles) {
        caps.ShaderProfile = profile;
        if (!BgfxEmbedsShaderProfile(profile)) {
            CKShaderDesc none;
            TEST_ASSERT(!CKBgfxRasterizerFFFragmentShader(caps, CKFF_SAMPLER_LAYOUT_WIDE_2D, TRUE, none),
                        "a profile the build does not embed has no fixed-function shaders");
            continue;
        }
        for (CKDWORD layout = 0; layout < CKFF_SAMPLER_LAYOUT_COUNT; ++layout) {
            CKShaderDesc shaderControlled, hardware;
            TEST_ASSERT(CKBgfxRasterizerFFFragmentShader(
                            caps, (CKFFSamplerLayout)layout,
                            TRUE, shaderControlled) &&
                            shaderControlled.Code && shaderControlled.CodeSize,
                        "every bgfx profile exposes each shader-controlled sampler layout");
            TEST_ASSERT(CKBgfxRasterizerFFFragmentShader(
                            caps, (CKFFSamplerLayout)layout,
                            FALSE, hardware) &&
                            hardware.Code && hardware.CodeSize &&
                            hardware.Code != shaderControlled.Code,
                        "every bgfx profile exposes a distinct hardware-sampling layout");
            if (profile == CKRST_SHADER_PROFILE_GLSL ||
                profile == CKRST_SHADER_PROFILE_ESSL) {
                const char *expected = profile == CKRST_SHADER_PROFILE_GLSL
                    ? "#version 150\n"
                    : "#version 300 es\n";
                const CKDWORD expectedSize = (CKDWORD)strlen(expected);
                CKDWORD fullSourceSize = 0;
                CKDWORD nativeSourceSize = 0;
                const char *fullSource = GetBgfxShaderSource(
                    shaderControlled, fullSourceSize);
                const char *nativeSource = GetBgfxShaderSource(
                    hardware, nativeSourceSize);
                TEST_ASSERT(fullSource && fullSourceSize >= expectedSize &&
                                memcmp(fullSource, expected, expectedSize) == 0 &&
                                nativeSource && nativeSourceSize >= expectedSize &&
                                memcmp(nativeSource, expected, expectedSize) == 0,
                            "GL fixed-function shaders carry a static version prologue");
            }
        }
    }
    CKShaderDesc invalid;
    caps.ShaderProfile = CKBgfxRasterizerShaderProfile(0);
    TEST_ASSERT(!CKBgfxRasterizerFFFragmentShader(
                    caps, CKFF_SAMPLER_LAYOUT_WIDE_2D,
                    (CKBOOL)2, invalid),
                "bgfx rejects a non-boolean shader sampling requirement");
    TEST_ASSERT(!CKBgfxRasterizerFFFragmentShader(
                    caps, (CKFFSamplerLayout)-1, FALSE, invalid),
                "bgfx rejects a negative sampler layout");
}

static void TestRejectedShaderTargets()
{
    TEST_SECTION("Shader Target Selection Before Device Creation");
    const CKFFShaderTarget targets[] = {
        {CKRST_SHADER_FORMAT_DXIL, CKRST_SHADER_PROFILE_DX12},
        {CKRST_SHADER_FORMAT_DXBC, CKRST_SHADER_PROFILE_DX11},
        {CKRST_SHADER_FORMAT_SPIRV, CKRST_SHADER_PROFILE_SPIRV},
        {CKRST_SHADER_FORMAT_BGFX, CKRST_SHADER_PROFILE_UNKNOWN},
    };
    CKBgfxRasterizerContext backend;
    CKRasterizerInitParameters init;
    init.Width = init.Height = 16;
    for (const auto &target : targets) {
        init.ShaderTargets.Clear();
        init.ShaderTargets.PushBack(target);
        TEST_ASSERT(backend.Init(&init) == CKERR_NOTIMPLEMENTED,
                    "incompatible shader containers and profiles are rejected before native initialization");
        TEST_ASSERT(backend.GetDeviceStatus() != CK_OK,
                    "a rejected shader target does not create a device");
    }
    init.ShaderTargets.Clear();
    for (const auto &target : targets)
        init.ShaderTargets.PushBack(target);
    TEST_ASSERT(backend.Init(&init) == CKERR_NOTIMPLEMENTED,
                "a nonempty target list with no compatible pair is rejected as a whole");
}

int main()
{
    printf("=== CKBgfxRasterizer Unit Tests ===\n");

    TestBorderPaletteLifetime();
    TestShaderBorderMetadata();
    TestFixedFunctionFragmentSamplingVariants();
    TestRejectedShaderTargets();
    TestFillModeTopology();
    TestDrawStateBuilderLayout();
    TestBgfxStencilWriteMaskEncoding();
    TestTextureVolumeDescriptorDefaults();
    TestSamplerCompareFlags();
    TestBackendProfileMapping();
    TestBgfxStateBackendConventions();
    TestSamplerFilterAndAddressConventions();
    TestOpenGLAutoMipPolicy();
    TestBgfxRasterizerLifecycle();
    TestDrawMapTraceBindingOrder();
    TestPersistentCacheCallback();
    TestDebugOverlayViewAllocation();
    TestExactPixelFormatMapping();
    TestGenerationCheckedResourceTable();

    printf("\n=== Results: %d passed, %d failed, %d total ===\n",
           g_PassCount, g_FailCount, g_TestCount);

    return g_FailCount == 0 ? 0 : 1;
}
