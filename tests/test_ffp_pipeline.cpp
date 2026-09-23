#include "CKFixedFunctionPipeline.h"
#include "CKFFContextState.h"
#include "CKFFImage.h"
#include "CKFFPresentDraw.h"
#include "CKFFSpecializationInfo.h"
#include "CKFFUniformState.h"
#include "CKRenderSettings.h"
#include "FFPRecordingHarness.h"
#include "CKFFTestPipeline.h"
#include "TestTriangleMultiset.h"

#include <math.h>
#include <string.h>

#define CKFixedFunctionPipeline CKFFTestPipeline

namespace {

void PointImageScalingUsesDestinationPixelCenters()
{
    const CKDWORD source[] = {
        0xFF000001u, 0xFF000002u, 0xFF000003u,
        0xFF000004u, 0xFF000005u, 0xFF000006u,
    };
    XArray<CKBYTE> pixels((int)sizeof(source));
    pixels.Resize((int)sizeof(source));
    memcpy(pixels.Begin(), source, sizeof(source));

    VxImageDescEx image;
    VxPixelFormat2ImageDesc(_32_ARGB8888, image);
    image.Width = 3;
    image.Height = 2;
    image.BytesPerLine = 3 * 4;
    image.Image = pixels.Begin();

    TestCheck(CKFFScaleImagePoint(image, pixels, 2, 3), "point scaling succeeds");
    const CKDWORD *scaled = (const CKDWORD *)pixels.Begin();
    const CKDWORD expected[] = {
        0xFF000001u, 0xFF000003u,
        0xFF000004u, 0xFF000006u,
        0xFF000004u, 0xFF000006u,
    };
    TestCheck(image.Width == 2 && image.Height == 3 && image.BytesPerLine == 8 && image.Image == pixels.Begin(),
              "scaled descriptor follows the output storage");
    TestCheck(memcmp(scaled, expected, sizeof(expected)) == 0,
              "destination pixel centers select the expected source texels");

    pixels.Resize(4);
    image.Width = image.Height = 2;
    image.BytesPerLine = 8;
    image.Image = pixels.Begin();
    TestCheck(!CKFFScaleImagePoint(image, pixels, 4, 4),
              "point scaling rejects truncated source storage");
}

void FixedFunctionProgramDeclaresItsShaderInterface()
{
    TestCheck(strcmp(CKFFConstantBlockInfo(CKRST_BLOCK_MATRICES).Name, "u_ffMatrices") == 0 &&
                  CKFFConstantBlockInfo(CKRST_BLOCK_MATRICES).Mat4 &&
                  CKFFConstantBlockInfo(CKRST_BLOCK_MATRICES).Count == CKFF_MATRIX_VEC4_COUNT,
              "matrix shader ABI belongs to the fixed-function layer");
    TestCheck(CKFFConstantBlockInfo(CKRST_BLOCK_SPEC).Count == CKFF_SPEC_UNIFORM_VEC4_COUNT &&
                  !CKFFConstantBlockInfo(CKRST_BLOCK_SPEC).Mat4 &&
                  CKFFConstantBlockInfo(CKRST_BLOCK_COUNT).Name == NULL,
              "specialization block and invalid logical slot");
    TestCheck(strcmp(CKFFSamplerSlotName(0), "s_texture0") == 0 &&
                  strcmp(CKFFSamplerSlotName(CKFF_CUBE_SAMPLER_SLOT_BASE), "s_textureCube0") == 0 &&
                  strcmp(CKFFSamplerSlotName(CKFF_VOLUME_SAMPLER_SLOT_BASE + 3), "s_textureVolume3") == 0 &&
                  strcmp(CKFFSamplerSlotName(CKFF_SLOT_PRESENT), "s_sceneColor") == 0 &&
                  CKFFSamplerSlotName(CKFF_SLOT_COUNT) == NULL,
              "shader sampler names belong to the fixed-function interface");

    const CK_SHADER_FORMAT formats[] = {CKRST_SHADER_FORMAT_DXIL, CKRST_SHADER_FORMAT_SPIRV};
    for (CK_SHADER_FORMAT format : formats) {
        const CKFFProgramDesc program = CKFFBuildProgramInterface(1, 2, format);
        CKShaderDesc vertex, pixel;
        vertex.Format = pixel.Format = format;
        vertex.Profile = pixel.Profile = format == CKRST_SHADER_FORMAT_DXIL ?
            CKRST_SHADER_PROFILE_DX12 : CKRST_SHADER_PROFILE_SPIRV;
        pixel.Stage = CKRST_SHADER_PIXEL;
        vertex.UniformBufferCount = 2;
        pixel.UniformBufferCount = 1;
        pixel.SamplerCount = 16;
        TestCheck(CKFFValidateProgram(program, vertex, pixel) == CK_OK,
                  "FFP native declarations satisfy generic backend validation");
        TestCheck(program.UniformBuffers.Size() == 3 &&
                      program.UniformBuffers[0].Stage == CKRST_SHADER_VERTEX &&
                      program.UniformBuffers[0].Slot == 0 && program.UniformBuffers[0].Size == 512 &&
                      program.UniformBuffers[1].Stage == CKRST_SHADER_VERTEX &&
                      program.UniformBuffers[1].Slot == 1 && program.UniformBuffers[1].Size == 2368 &&
                      program.UniformBuffers[2].Stage == CKRST_SHADER_PIXEL &&
                      program.UniformBuffers[2].Slot == 0 && program.UniformBuffers[2].Size == 1424,
                  "native 3D declarations isolate per-draw matrices");
        TestCheck(program.Uniforms.Size() == 13 &&
                      program.Uniforms[9].Slot == CKRST_BLOCK_DRAW_PARAMS &&
                      program.Uniforms[9].BufferSlot == 0 && program.Uniforms[9].Offset == 0 &&
                      program.Samplers.Size() == 16,
                  "native packing follows the stage layout schema");
        TestCheck(program.Samplers[0].Dimension == CKFF_TEXTURE_2D &&
                      program.Samplers[8].Dimension == CKFF_TEXTURE_CUBE &&
                      program.Samplers[12].Dimension == CKFF_TEXTURE_3D &&
                      program.Samplers[15].BorderColorOffset == 912 + 15 * 16 &&
                      program.Samplers[15].SamplerStateOffset == 1168 + 15 * 16,
                  "sampler dimensions and border metadata are declared explicitly");
        TestCheck(program.VertexInputs.Size() == 16 && program.VertexInputs[6].Integer &&
                      program.VertexInputs[6].DefaultValue[3] == 0 &&
                      program.VertexInputs[4].DefaultValue[0] == 0x3f800000u &&
                      program.VertexInputs[4].DefaultValue[3] == 0x3f800000u &&
                      program.VertexInputs[8].Attribute == CKRST_ATTRIB_TEXCOORD0 &&
                      program.VertexInputs[8].DefaultValue[0] == 0 &&
                      program.VertexInputs[8].DefaultValue[3] == 0x3f800000u,
                  "missing FFP vertex streams keep shader-family defaults");

        const CKFFProgramDesc positionT = CKFFBuildProgramInterface(
            1, 2, format, FALSE, TRUE);
        vertex.UniformBufferCount = 1;
        TestCheck(CKFFValidateProgram(positionT, vertex, pixel) == CK_OK &&
                      positionT.UniformBuffers.Size() == 2 &&
                      positionT.UniformBuffers[0].Stage == CKRST_SHADER_VERTEX &&
                      positionT.UniformBuffers[0].Slot == 0 &&
                      positionT.UniformBuffers[0].Size == 2368,
                  "POSITIONT omits the unused matrix buffer and compacts native slots");

        const CKFFProgramDesc present = CKFFBuildProgramInterface(1, 2, format, TRUE);
        vertex.UniformBufferCount = 0;
        pixel.SamplerCount = 1;
        TestCheck(CKFFValidateProgram(present, vertex, pixel) == CK_OK &&
                      present.UniformBuffers.Size() == 1 &&
                      present.UniformBuffers[0].Stage == CKRST_SHADER_PIXEL &&
                      present.UniformBuffers[0].Size == 48 &&
                      present.Uniforms.Size() == 1 && present.Uniforms[0].Offset == 0 &&
                      present.Samplers[0].BorderColorOffset == 16 && present.Samplers[0].SamplerStateOffset == 32 &&
                      present.Samplers[0].Slot == CKFF_SLOT_PRESENT && present.Samplers[0].NativeSlot == 0 &&
                      present.VertexInputs.Size() == 2 && present.VertexInputs[1].Location == 8,
                  "presentation declares one native sampler independently of its logical slot");
    }

    const CKFFProgramDesc named = CKFFBuildProgramInterface(1, 2, CKRST_SHADER_FORMAT_BGFX);
    CKShaderDesc vertex, pixel;
    vertex.Format = pixel.Format = CKRST_SHADER_FORMAT_BGFX;
    vertex.Profile = pixel.Profile = CKRST_SHADER_PROFILE_DX11;
    pixel.Stage = CKRST_SHADER_PIXEL;
    TestCheck(CKFFValidateProgram(named, vertex, pixel) == CK_OK && named.UniformBuffers.Size() == 0 &&
                  named.Uniforms[CKRST_BLOCK_MATRICES].Name == "u_ffMatrices" &&
                  named.Samplers[15].MetadataBufferSlot == ~0u,
              "named-uniform artifacts need no invented buffer or border layout");
}

void ShaderCacheOwnsCatalogAndBuildsInterfacesOnlyOnProgramMiss()
{
    FFPRecordingDriver driver;
    FFPRecordingBackend backend(&driver);
    CKFFShaderSet shaders = backend.ShaderSet();
    CKFFTestShaderCache cache;
    TestCheck(cache.Init(backend.GetCaps(), shaders), "shader cache accepts explicit catalog");
    shaders = CKFFShaderSet();
    CKFFShaderKey key;
    const CKDWORD first = cache.GetProgram(&backend, key).Program;
    TestCheck(first != 0 && backend.CreatedProgramCount == 1 &&
                  backend.LastProgramInterface.Samplers.Size() == 16,
              "lazy creation uses its retained descriptor catalog and explicit interface");
    for (unsigned draw = 0; draw < 1000; ++draw)
        TestCheck(cache.GetProgram(&backend, key).Program == first, "cached program handle is stable");
    TestCheck(backend.CreatedProgramCount == 1 && backend.CreatedShaderCount == 2,
              "cached draws do not rebuild programs or resource declarations");

    const CKFFProgramBinding original = cache.GetProgram(&backend, key);
    for (int change = 0; change < 32; ++change) {
        key.FS.AlphaTestEnable = (change & 1) != 0;
        key.FS.AlphaFunc = (change & 7) + 1;
        key.FS.LastActiveTextureStage = (change & 7) + 1;
        auto &stage = key.FS.Stages[change & 7];
        stage.ColorOp = (change & 1) ? CKRST_TOP_ADD : CKRST_TOP_MODULATE;
        stage.ColorArg1 = CKRST_TA_TEXTURE | ((change & 2) ? CKRST_TA_COMPLEMENT : 0);
        stage.MirrorOnceMask = change & 7;
        for (int repeat = 0; repeat < 3; ++repeat) {
            const CKFFProgramBinding binding = cache.GetProgram(&backend, key);
            TestCheck(binding.Program == first &&
                          binding.Specialization == CKFFBuildSpecializationInfo(key.FS),
                      "repeated and changed materials must carry their own current specialization");
        }
    }
    TestCheck(original.Specialization == CKFFBuildSpecializationInfo(CKFFShaderKeyFS()),
              "later materials must not mutate an earlier draw binding");
    TestCheck(backend.CreatedProgramCount == 1,
              "fragment state changes must retain the fixed native program family");

    const float params[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    CKFFConstantSet constants;
    const CKERROR pushed = CKFFSetConstants(&constants, CKRST_BLOCK_VIEWPORT, params, 1);
    const auto &bytes = constants[CKRST_BLOCK_VIEWPORT].Bytes;
    TestCheck(pushed == CK_OK && bytes.Size() == sizeof(params) &&
                  memcmp(bytes.Begin(), params, sizeof(params)) == 0,
              "FFP converts one vec4 into sixteen backend bytes");
    cache.Shutdown(&backend);
}

void ShaderCacheRetainsAlternatingSpecializations()
{
    FFPRecordingDriver driver;
    FFPRecordingBackend backend(&driver);
    CKFFTestShaderCache cache;
    TestCheck(cache.Init(backend.GetCaps(), backend.ShaderSet()),
              "shader cache accepts the benchmark catalog");

    CKFFShaderKey keys[8];
    for (CKDWORD i = 0; i < 8; ++i) {
        keys[i].FS.AlphaTestEnable = true;
        keys[i].FS.AlphaFunc = i + 1;
        const CKFFProgramBinding binding = cache.GetProgram(&backend, keys[i]);
        TestCheck(binding.Program != 0 &&
                      binding.Specialization ==
                          CKFFBuildSpecializationInfo(keys[i].FS),
                  "each alternating fragment state must resolve correctly");
    }

    TestCheck(cache.GetCachedSpecializationCount(CKFF_PROGRAM_3D) == 8,
              "one vertex variant must retain the eight-material working set");
    for (const CKFFShaderKey &key : keys) {
        const CKFFProgramBinding binding = cache.GetProgram(&backend, key);
        TestCheck(binding.Specialization ==
                      CKFFBuildSpecializationInfo(key.FS),
                  "retained specialization values must survive alternation");
    }
    cache.Shutdown(&backend);
}

void ShaderCacheEvictsLeastRecentlyUsedSpecialization()
{
    FFPRecordingDriver driver;
    FFPRecordingBackend backend(&driver);
    CKFFTestShaderCache cache;
    TestCheck(cache.Init(backend.GetCaps(), backend.ShaderSet()),
              "shader cache accepts the benchmark catalog");

    const size_t capacity = CKFFShaderCache::GetSpecializationCapacity();
    std::vector<CKFFShaderKey> keys(capacity + 1);
    for (size_t i = 0; i <= capacity; ++i) {
        keys[i].FS.AlphaTestEnable = true;
        keys[i].FS.AlphaFunc = static_cast<CKDWORD>((i % 8) + 1);
        keys[i].FS.LastActiveTextureStage = static_cast<CKDWORD>(i / 8);
    }
    for (size_t i = 0; i < capacity; ++i)
        cache.GetProgram(&backend, keys[i]);

    cache.GetProgram(&backend, keys[0]);
    cache.GetProgram(&backend, keys[capacity]);

    TestCheck(cache.GetCachedSpecializationCount(CKFF_PROGRAM_3D) == capacity,
              "specialization cache must remain bounded");
    TestCheck(cache.HasCachedSpecialization(CKFF_PROGRAM_3D, keys[0].FS),
              "recently used specialization must survive eviction");
    TestCheck(!cache.HasCachedSpecialization(CKFF_PROGRAM_3D, keys[1].FS),
              "least recently used specialization must be evicted");
    TestCheck(cache.HasCachedSpecialization(CKFF_PROGRAM_3D, keys[capacity].FS),
              "new specialization must enter the bounded cache");
    cache.Shutdown(&backend);
}

void NullRasterizerSupportsHeadlessFFP()
{
    CKRecordingRasterizer rasterizer;
    TestCheck(rasterizer.Start(NULL) && rasterizer.GetDriverCount() == 1,
              "recording rasterizer must expose its headless driver");
    CKRecordingRasterizerDriver *driver =
        static_cast<CKRecordingRasterizerDriver *>(rasterizer.GetDriver(0));
    CKRecordingBackend *first = driver->CreateBackend();
    CKRecordingBackend *second = driver->CreateBackend();
    TestCheck(first != NULL && second != NULL, "Null driver must create headless backends");
    CKRasterizerInitParameters firstDesc;
    firstDesc.Width = 64;
    firstDesc.Height = 64;
    firstDesc.Bpp = 32;
    firstDesc.ZBpp = 24;
    firstDesc.StencilBpp = 8;
    CKRasterizerInitParameters secondDesc;
    secondDesc.Width = 32;
    secondDesc.Height = 32;
    secondDesc.Bpp = 32;
    secondDesc.ZBpp = 16;
    secondDesc.StencilBpp = 0;
    TestCheck(first->Init(&firstDesc) == CK_OK && second->Init(&secondDesc) == CK_OK,
              "Null backends must allow independent headless contexts");

    CKFixedFunctionPipeline ffp;
    CKFFShaderSet shaders;
    TestCheck(driver->GetShaderSet(first->GetCaps(), shaders), "Null rasterizer shader catalog");
    TestCheck(ffp.Init(first, shaders),
              "FFP must initialize its programs on the headless backend");
    TestCheck(ffp.Shutdown() == CK_OK,
              "Headless FFP shutdown must release its resources cleanly");
    TestCheck(driver->DestroyBackend(first) && driver->DestroyBackend(second),
              "Idle headless backends must be independently destroyable");
}

void MissingShaderPayloadFamilyFailsInitialization()
{
    FFPRecordingDriver driver(CKRST_SHADER_PROFILE_SPIRV, 0,
                              CKRST_SHADER_FORMAT_SPIRV);
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    TestCheck(!ffp.Init(context.StartedBackend(), context.ShaderSet()),
              "FFP must not pass bgfx containers to a raw SPIR-V backend");
    TestCheck(context.CreatedShaderCount == 0,
              "missing payload family fails before backend shader creation");
}

CKDWORD FloatStageState(float value) {
    union {
        float F;
        CKDWORD D;
    } u;
    u.F = value;
    return u.D;
}

VXPRIMITIVETYPE DrawStateTopology(const CKDrawState &state) {
    return (VXPRIMITIVETYPE)((state.Mid >> 6) & 0x7u);
}

struct ShaderProfileCase {
    CK_SHADER_PROFILE Profile;
    const char *Name;
};

static const ShaderProfileCase kSamplerLayoutProfiles[] = {
    {CKRST_SHADER_PROFILE_DX11, "dx11"},
    {CKRST_SHADER_PROFILE_DX12, "dx12"},
    {CKRST_SHADER_PROFILE_SPIRV, "spirv"},
    {CKRST_SHADER_PROFILE_GLSL, "glsl"},
    {CKRST_SHADER_PROFILE_ESSL, "essl"},
    {CKRST_SHADER_PROFILE_MSL, "metal"},
};

CKFFSpecializationInfo CurrentDrawSpecialization(CKFixedFunctionPipeline &ffp,
                                                 const FFPRecordingBackend &context) {
    const CKDWORD uniform = context.GetBlockUniformForTests(CKRST_BLOCK_SPEC);
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator it =
        context.Log.FloatUniforms.find(uniform);
    if (it == context.Log.FloatUniforms.end() ||
        it->second.size() < CKFF_SPEC_UNIFORM_VEC4_COUNT * 4)
        return CKFFSpecializationInfo();
    return CKFFSpecializationInfo::Unpack24(it->second.data(), CKFF_SPEC_UNIFORM_VEC4_COUNT * 4);
}

void DrawVertexBufferApproximatesStencilWriteMasks() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetRenderState(VXRENDERSTATE_STENCILENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_STENCILFUNC, VXCMP_EQUAL);
    ffp.SetRenderState(VXRENDERSTATE_STENCILPASS, VXSTENCILOP_REPLACE);
    ffp.SetRenderState(VXRENDERSTATE_STENCILREF, 0x12);
    ffp.SetRenderState(VXRENDERSTATE_STENCILMASK, 0xF0);
    ffp.SetRenderState(VXRENDERSTATE_STENCILWRITEMASK, 0x0F);

    CKBOOL drawn = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(drawn && context.Log.DrawCount == 1,
              "A partial stencil write mask must still submit the draw");
    TestCheck(context.Log.LastStencilWriteMask == 0xFF &&
                  context.Log.LastStencilReadMask == 0xF0,
              "A partial stencil write mask approximates to writing every bit");
    TestCheck((ffp.GetLastDrawApproximationMask() & (1ull << CKRST_DIAG_APPROX_STENCIL_WRITE_MASK)) != 0 &&
                  ffp.GetApproximatedDrawCount(CKRST_DIAG_APPROX_STENCIL_WRITE_MASK) == 1,
              "Partial stencil write mask approximation must be reported once per draw");
    TestCheck(ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_NONE,
              "An approximated draw is not a rejected draw");

    ffp.SetRenderState(VXRENDERSTATE_STENCILWRITEMASK, 0x00);
    drawn = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    const CKDWORD stencilOps = context.Log.LastState.Mid &
        (CKRST_STENCIL_FAIL(0xF) | CKRST_STENCIL_ZFAIL(0xF) | CKRST_STENCIL_PASS(0xF));
    TestCheck(drawn && context.Log.DrawCount == 2,
              "A zero stencil write mask must still submit the draw");
    TestCheck(stencilOps == (CKRST_STENCIL_FAIL(VXSTENCILOP_KEEP) |
                             CKRST_STENCIL_ZFAIL(VXSTENCILOP_KEEP) |
                             CKRST_STENCIL_PASS(VXSTENCILOP_KEEP)) &&
                  (context.Log.LastState.Mid & CKRST_STENCIL_ENABLE) != 0,
              "A zero stencil write mask approximates to KEEP operations with the test still enabled");
    TestCheck(ffp.GetApproximatedDrawCount(CKRST_DIAG_APPROX_STENCIL_WRITE_MASK) == 2,
              "Zero stencil write mask approximation must be counted");

    ffp.Shutdown();
}

void DrawVertexBufferSubmitsRepresentableStencilMasks() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetRenderState(VXRENDERSTATE_STENCILENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_STENCILFUNC, VXCMP_EQUAL);
    ffp.SetRenderState(VXRENDERSTATE_STENCILPASS, VXSTENCILOP_REPLACE);
    ffp.SetRenderState(VXRENDERSTATE_STENCILREF, 0x12);
    ffp.SetRenderState(VXRENDERSTATE_STENCILMASK, 0xF0);
    ffp.SetRenderState(VXRENDERSTATE_STENCILWRITEMASK, 0xFF);

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(drawn && context.Log.DrawCount == 1,
              "Representable stencil masks must submit once");
    TestCheck(context.Log.StencilRefSetCount == 1 &&
                  context.Log.StencilMaskSetCount == 1,
              "Representable stencil state must reach the backend");
    TestCheck(context.Log.LastStencilRef == 0x12 &&
                  context.Log.LastStencilReadMask == 0xF0 &&
                  context.Log.LastStencilWriteMask == 0xFF,
              "Representable stencil state must preserve ref and masks");

    ffp.Shutdown();
}

void DrawVertexBufferPropagatesBackendFailure() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());
    context.Log.DrawError = CKERR_INVALIDPARAMETER;

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(!drawn,
              "A backend submit failure must fail the originating FFP draw");
    TestCheck(context.Log.DrawCount == 1,
              "The failing backend submit must be attempted exactly once");
    TestCheck(ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_BACKEND_ERROR,
              "A backend draw failure must report the backend-error reason");
    TestCheck(context.Log.DiscardCount == 1,
              "The backend must report the rejected draw exactly once");

    context.Log.DrawError = CK_OK;
    TestCheck(ffp.DrawVertexBuffer(
                  VX_TRIANGLELIST,
                  1, 0, 0, 3, 0, 0,
                  CKRST_DP_CL_V, CKRST_DP_CL_V, 1),
              "A draw after recovered submit failure must still submit");
    ffp.Shutdown();
}

void IgnoredRenderStatesReportDiagnostics() {
    struct IgnoredStateCase {
        VXRENDERSTATETYPE State;
        CKDWORD Value;
        CKDWORD ResetValue;
        CKRST_DIAGNOSTIC Diagnostic;
    };
    const IgnoredStateCase cases[] = {
        {VXRENDERSTATE_DITHERENABLE, TRUE, FALSE, CKRST_DIAG_IGNORE_DITHER},
        {VXRENDERSTATE_ZBIAS, 1, 0, CKRST_DIAG_APPROX_ZBIAS},
        {VXRENDERSTATE_LINEPATTERN, 0xFFFFu, 0, CKRST_DIAG_IGNORE_LINEPATTERN},
        {VXRENDERSTATE_EDGEANTIALIAS, TRUE, FALSE, CKRST_DIAG_IGNORE_ANTIALIAS},
        {VXRENDERSTATE_CLIPPING, FALSE, TRUE, CKRST_DIAG_IGNORE_CLIPPING_OFF},
        {VXRENDERSTATE_SOFTWAREVPROCESSING, TRUE, FALSE, CKRST_DIAG_IGNORE_SOFTWAREVPROCESSING},
        {VXRENDERSTATE_FILLMODE, VXFILL_POINT, VXFILL_SOLID, CKRST_DIAG_APPROX_FILLMODE_POINT},
    };

    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    CKBOOL allDrawn = TRUE;
    CKBOOL allReported = TRUE;
    for (CKDWORD i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        ffp.SetRenderState(cases[i].State, cases[i].Value);
        const CKBOOL drawn = ffp.DrawVertexBuffer(
            VX_TRIANGLELIST,
            1, 0, 0, 3, 0, 0,
            CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
        allDrawn = allDrawn && drawn;
        allReported = allReported &&
            ffp.GetLastDrawApproximationMask() == (1ull << cases[i].Diagnostic) &&
            ffp.GetApproximatedDrawCount(cases[i].Diagnostic) == 1;
        ffp.SetRenderState(cases[i].State, cases[i].ResetValue);
    }

    TestCheck(allDrawn && context.Log.DrawCount == sizeof(cases) / sizeof(cases[0]),
              "Ignored or approximated render states must keep submitting draws");
    TestCheck(allReported,
              "Each ignored or approximated render state must report exactly its own diagnostic");

    const CKBOOL clean = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    TestCheck(clean && ffp.GetLastDrawApproximationMask() == 0,
              "A draw with default states must report no approximation");

    ffp.Shutdown();
}

void InvalidStateValuesRejectBeforeBackendEncoding() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetRenderState(VXRENDERSTATE_FILLMODE, 99);
    TestCheck(ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                                   1, 0, 0, 3, 0, 0,
                                   CKRST_DP_CL_V, CKRST_DP_CL_V, 1) == FALSE &&
                  ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_STATE_VALUE,
              "invalid raster state values must reject before backend encoding");
    ffp.SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_SOLID);

    ffp.SetRenderState(VXRENDERSTATE_SHADEMODE, VXSHADE_PHONG);
    TestCheck(ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                                   1, 0, 0, 3, 0, 0,
                                   CKRST_DP_CL_V, CKRST_DP_CL_V, 1) == FALSE &&
                  ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_STATE_VALUE,
              "unsupported Phong shade mode must not silently become Gouraud");
    ffp.SetRenderState(VXRENDERSTATE_SHADEMODE, VXSHADE_GOURAUD);

    ffp.SetTexture(0, 1);
    ffp.SetTextureStageState(0, CKRST_TSS_MINFILTER, 99);
    TestCheck(ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                                   1, 0, 0, 3, 0, 0,
                                   CKRST_DP_CL_V, CKRST_DP_CL_V, 1) == FALSE &&
                  ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_STATE_VALUE,
              "invalid sampler state values must reject before backend encoding");
    TestCheck(context.Log.DrawCount == 0,
              "invalid state values must not reach backend submission");

    ffp.Shutdown();
}

void UnsupportedTextureStageStatesApproximateWithDiagnostics() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());
    ffp.SetTexture(0, 101, CKRST_TEXTURE_VALID);

    ffp.SetTextureStageState(
        0, CKRST_TSS_STAGEBLEND, STAGEBLEND(VXBLEND_SRCCOLOR, VXBLEND_DESTALPHA));
    CKBOOL drawn = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);
    TestCheck(drawn && context.Log.DrawCount == 1 &&
                  ffp.GetLastDrawApproximationMask() == 0 &&
                  spec.GetStage(0, CKFF_SPEC_STAGE_COLOR_OP) == CKFF_TOP_STAGEBLEND,
              "An arbitrary STAGEBLEND pair must use the exact shader operation");
    const CKDWORD stageUniform = context.GetBlockUniformForTests(CKRST_BLOCK_STAGE_PARAMS);
    const std::vector<float> &stageParams = context.Log.FloatUniforms[stageUniform];
    TestCheck(stageParams.size() >= 4 &&
                  stageParams[3] == (float)STAGEBLEND(VXBLEND_SRCCOLOR, VXBLEND_DESTALPHA),
              "STAGEBLEND factors must reach the shared fragment shader");

    ffp.ResetTextureStage(0);
    ffp.SetTexture(0, 101, CKRST_TEXTURE_VALID);
    ffp.SetTextureStageState(0, CKRST_TSS_STAGEBLEND, STAGEBLEND(VXBLEND_ONE, VXBLEND_ONE));
    drawn = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    spec = CurrentDrawSpecialization(ffp, context);
    TestCheck(drawn && ffp.GetLastDrawApproximationMask() == 0 &&
                  spec.GetStage(0, CKFF_SPEC_STAGE_COLOR_OP) == CKRST_TOP_ADD,
              "STAGEBLEND(ONE, ONE) is the exact ADD combiner");

    ffp.ResetTextureStage(0);
    ffp.SetTexture(0, 101, CKRST_TEXTURE_VALID);
    ffp.SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_MIPLINEAR);
    ffp.SetTextureStageState(0, CKRST_TSS_MIPMAPLODBIAS, FloatStageState(1.0f));
    drawn = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    TestCheck(drawn && ffp.GetLastDrawApproximationMask() == (1ull << CKRST_DIAG_IGNORE_SAMPLER_LOD),
              "Mip LOD controls are ignored with a diagnostic");

    ffp.SetTextureStageState(0, CKRST_TSS_MIPMAPLODBIAS, FloatStageState(0.0f));
    ffp.SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_ANISOTROPIC);
    ffp.SetTextureStageState(0, CKRST_TSS_MAXANISOTROPY, 4);
    drawn = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    TestCheck(drawn && ffp.GetLastDrawApproximationMask() == (1ull << CKRST_DIAG_APPROX_ANISOTROPY) &&
                  context.Log.LastTextureSampler.MinFilter == CKRST_FILTER_ANISOTROPIC,
              "Anisotropy levels above one approximate to the anisotropic switch with a diagnostic");

    ffp.SetTextureStageState(0, CKRST_TSS_MAXANISOTROPY, 1);
    drawn = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    TestCheck(drawn && ffp.GetLastDrawApproximationMask() == 0 &&
                  context.Log.LastTextureSampler.MinFilter == CKRST_FILTER_LINEAR &&
                  context.Log.LastTextureSampler.MipFilter == CKRST_FILTER_LINEAR,
              "MAXANISOTROPY one must reduce anisotropic filtering to linear filtering");

    ffp.SetTextureStageState(0, CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSMIRRORONCE);
    drawn = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    TestCheck(drawn && ffp.GetLastDrawApproximationMask() == 0,
              "2D MIRRORONCE addressing preserves the sampler footprint in the shader");

    ffp.SetTexture(0, 101, CKRST_TEXTURE_VALID | CKRST_TEXTURE_VOLUMEMAP);
    drawn = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    TestCheck(drawn && ffp.GetLastDrawApproximationMask() == 0,
              "Volume MIRRORONCE preserves the sampler footprint in the shader");

    ffp.Shutdown();
}

void SingleCubeVolumeLayoutUsesGenericMixedSamplerModule() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());
    ffp.SetTexture(0, 101, CKRST_TEXTURE_VALID | CKRST_TEXTURE_CUBEMAP);
    ffp.SetTexture(1, 102, CKRST_TEXTURE_VALID);
    ffp.SetTexture(2, 103, CKRST_TEXTURE_VALID | CKRST_TEXTURE_VOLUMEMAP);

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(drawn && context.Log.DrawCount == 1,
              "one cube and one volume texture must use the generic mixed sampler module");
    const FFPRecordingBackend &u = context;
    bool sawCube = false;
    bool sawVolume = false;
    for (const FFPTextureBinding &binding : context.Log.TextureBindings) {
        if (binding.Stage == 8 && binding.Uniform == u.GetSamplerUniformForTests(8 + 0) &&
            binding.Texture == 101) {
            sawCube = true;
        }
        if (binding.Stage == 12 && binding.Uniform == u.GetSamplerUniformForTests(12 + 0) &&
            binding.Texture == 103) {
            sawVolume = true;
        }
    }
    TestCheck(sawCube && sawVolume,
              "generic mixed sampler module must use stable cube and volume slots");
    ffp.Shutdown();
}

void DrawVertexBufferStopsBeforeSubmitAfterBindingFailure() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());
    context.Log.StateError = CKERR_INVALIDPARAMETER;

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(!drawn,
              "A backend state-binding failure must fail the originating FFP draw");
    TestCheck(context.Log.DrawCount == 0,
              "A state-binding failure must stop before backend submit");
    TestCheck(ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_BACKEND_ERROR,
              "A state-binding failure must report the backend-error reason");
    TestCheck(context.Log.DiscardCount == 1,
              "A failed state binding must discard pending backend state");

    context.Log.StateError = CK_OK;
    TestCheck(ffp.DrawVertexBuffer(
                  VX_TRIANGLELIST,
                  1, 0, 0, 3, 0, 0,
                  CKRST_DP_CL_V, CKRST_DP_CL_V, 1),
              "A draw after recovered state failure must still submit");
    ffp.Shutdown();
}

void DrawVertexBufferRejectsConstantPacketFailure() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());
    context.Log.UniformError = CKERR_INVALIDPARAMETER;

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(!drawn,
              "A backend uniform failure must fail the originating FFP draw");
    TestCheck(context.Log.UniformSetCount == 0,
              "A rejected constant packet is not partially consumed");
    TestCheck(context.Log.StateSetCount == 0 && context.Log.DrawCount == 0,
              "A uniform failure must stop before state binding and submit");
    TestCheck(ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_BACKEND_ERROR,
              "A uniform failure must report the backend-error reason");
    TestCheck(context.Log.DiscardCount == 1,
              "A failed uniform upload must discard pending backend state");

    context.Log.UniformError = CK_OK;
    TestCheck(ffp.DrawVertexBuffer(
                  VX_TRIANGLELIST,
                  1, 0, 0, 3, 0, 0,
                  CKRST_DP_CL_V, CKRST_DP_CL_V, 1),
              "A draw after recovered uniform failure must still submit");
    ffp.Shutdown();
}

void DrawVertexBufferUploadsAlphaPrecision() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_ALPHAFUNC, VXCMP_GREATER);
    ffp.SetRenderState(VXRENDERSTATE_ALPHAREF, 0x12345680);
    ffp.SetAlphaTestPrecision(0x2);

    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    const CKDWORD uniform = context.GetBlockUniformForTests(CKRST_BLOCK_DRAW_PARAMS);
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator it =
        context.Log.FloatUniforms.find(uniform);

    TestCheck(it != context.Log.FloatUniforms.end(),
              "FFP draw must upload draw params");
    TestCheck(it->second.size() >= 36,
              "FFP draw params must contain alpha-test slot");
    TestCheck(it->second[32] == 0x80,
              "FFP draw params must upload alpha ref low byte");
    const CKDWORD alphaFuncPrecision = (CKDWORD)it->second[33];
    TestCheck((alphaFuncPrecision & 0xFu) == VXCMP_GREATER,
              "FFP draw params must upload alpha-test compare function");
    TestCheck(((alphaFuncPrecision >> 4) & 0xFu) == 0x2,
              "FFP draw params must upload current alpha-test precision");

    ffp.Shutdown();
}

void DrawVertexBufferSetsFlatShadeSpecialization() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetRenderState(VXRENDERSTATE_SHADEMODE, VXSHADE_GOURAUD);
    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    const CKFFSpecializationInfo gouraudSpec = CurrentDrawSpecialization(ffp, context);
    TestCheck(gouraudSpec.Get(CKFF_SPEC_FLAT_SHADE) == 0,
              "Gouraud shade mode must not set flat shade specialization");

    ffp.SetRenderState(VXRENDERSTATE_SHADEMODE, VXSHADE_FLAT);
    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    const CKFFSpecializationInfo flatSpec = CurrentDrawSpecialization(ffp, context);
    TestCheck(flatSpec.Get(CKFF_SPEC_FLAT_SHADE) == 1,
              "Flat shade mode must set flat shade specialization");

    ffp.Shutdown();
}

void DrawVertexBufferUploadsFogParams() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    const float fogStart = 10.0f;
    const float fogEnd = 30.0f;
    const float fogDensity = 0.125f;
    ffp.SetRenderState(VXRENDERSTATE_FOGENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_FOGVERTEXMODE, VXFOG_EXP);
    ffp.SetRenderState(VXRENDERSTATE_FOGPIXELMODE, VXFOG_NONE);
    ffp.SetRenderState(VXRENDERSTATE_FOGSTART, FloatStageState(fogStart));
    ffp.SetRenderState(VXRENDERSTATE_FOGEND, FloatStageState(fogEnd));
    ffp.SetRenderState(VXRENDERSTATE_FOGDENSITY, FloatStageState(fogDensity));

    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    const CKDWORD uniform = context.GetBlockUniformForTests(CKRST_BLOCK_DRAW_PARAMS);
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator it =
        context.Log.FloatUniforms.find(uniform);

    TestCheck(it != context.Log.FloatUniforms.end(),
              "FFP fog draw must upload draw params");
    TestCheck(it->second.size() >= 44,
              "FFP fog draw params must contain fog slot");
    TestCheck(it->second[40] == fogStart,
              "FFP fog params must upload fog start");
    TestCheck(it->second[41] == fogEnd,
              "FFP fog params must upload fog end");
    TestCheck(it->second[42] == fogDensity,
              "FFP fog params must upload fog density");
    TestCheck(it->second[43] == (float)VXFOG_EXP,
              "FFP fog params must upload vertex fog mode");

    ffp.Shutdown();
}

void PositionTFogUsesPositionTShaderKey() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetRenderState(VXRENDERSTATE_FOGENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_FOGVERTEXMODE, VXFOG_LINEAR);

    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKFF_VF_POSITIONT | CKFF_VF_COLOR0 | CKFF_VF_COLOR1, 1);

    const CKDWORD uniform = context.GetBlockUniformForTests(CKRST_BLOCK_DRAW_PARAMS);
    const CKDWORD matrixUniform = context.GetBlockUniformForTests(CKRST_BLOCK_MATRICES);
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator it =
        context.Log.FloatUniforms.find(uniform);
    TestCheck(context.Log.FloatUniforms.find(matrixUniform) == context.Log.FloatUniforms.end(),
              "POSITIONT draws must not upload transformed 3D matrix uniforms");
    TestCheck(it != context.Log.FloatUniforms.end() && it->second.size() >= 44,
              "POSITIONT fog draw must upload fog params");
    TestCheck(it->second[43] == (float)VXFOG_LINEAR,
              "POSITIONT fog draw must upload vertex fog mode");

    ffp.Shutdown();
}

void RangeFogChangesSpecialization() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetRenderState(VXRENDERSTATE_FOGENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_FOGVERTEXMODE, VXFOG_LINEAR);
    ffp.SetRenderState(VXRENDERSTATE_RANGEFOGENABLE, FALSE);
    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    const CKFFSpecializationInfo specNoRange = CurrentDrawSpecialization(ffp, context);
    TestCheck(specNoRange.Get(CKFF_SPEC_RANGE_FOG) == 0,
              "Range fog disabled must clear range fog specialization");

    ffp.SetRenderState(VXRENDERSTATE_RANGEFOGENABLE, TRUE);
    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    const CKFFSpecializationInfo specRange = CurrentDrawSpecialization(ffp, context);
    TestCheck(specRange.Get(CKFF_SPEC_RANGE_FOG) == 1,
              "Range fog enabled must set range fog specialization");

    ffp.Shutdown();
}

void PixelFogOverridesVertexFogMode() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetRenderState(VXRENDERSTATE_FOGENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_FOGVERTEXMODE, VXFOG_EXP);
    ffp.SetRenderState(VXRENDERSTATE_FOGPIXELMODE, VXFOG_LINEAR);

    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKFF_VF_POSITION | CKFF_VF_NORMAL, 1);

    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);

    const CKDWORD uniform = context.GetBlockUniformForTests(CKRST_BLOCK_DRAW_PARAMS);
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator it =
        context.Log.FloatUniforms.find(uniform);

    TestCheck(spec.Get(CKFF_SPEC_VERTEX_FOG_MODE) == VXFOG_NONE,
              "Pixel fog must clear vertex fog specialization");
    TestCheck(spec.Get(CKFF_SPEC_PIXEL_FOG_MODE) == VXFOG_LINEAR,
              "Pixel fog must keep the pixel fog specialization");
    TestCheck(it != context.Log.FloatUniforms.end() &&
                  it->second.size() >= 44 &&
                  it->second[35] == (float)VXFOG_LINEAR &&
                  it->second[43] == (float)VXFOG_NONE,
              "Pixel fog draw params must clear vertex fog mode and preserve pixel fog mode");
    TestCheck(it != context.Log.FloatUniforms.end() &&
                  it->second.size() >= 32 &&
                  it->second[31] == 0.0f,
              "Pixel fog mode must not leak into the inline-light flag");

    ffp.Shutdown();
}

void DrawVertexBufferCompactsClipPlaneUniforms() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    VxPlane plane1;
    plane1.m_Normal = VxVector(1.0f, 2.0f, 3.0f);
    plane1.m_D = 4.0f;
    VxPlane plane3;
    plane3.m_Normal = VxVector(5.0f, 6.0f, 7.0f);
    plane3.m_D = 8.0f;
    ffp.SetUserClipPlane(1, plane1);
    ffp.SetUserClipPlane(3, plane3);
    ffp.SetRenderState(VXRENDERSTATE_CLIPPLANEENABLE, (1u << 1) | (1u << 3));

    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    const CKDWORD planesUniform = context.GetBlockUniformForTests(CKRST_BLOCK_CLIP_PLANES);
    const CKDWORD paramsUniform = context.GetBlockUniformForTests(CKRST_BLOCK_CLIP_PARAMS);
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator planes =
        context.Log.FloatUniforms.find(planesUniform);
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator params =
        context.Log.FloatUniforms.find(paramsUniform);

    TestCheck(planes != context.Log.FloatUniforms.end(),
              "Enabled clip planes must upload compacted plane uniform");
    TestCheck(params != context.Log.FloatUniforms.end(),
              "Enabled clip planes must upload clip params uniform");
    TestCheck(params->second[0] == 2.0f,
              "Clip params must contain enabled clip plane count");
    TestCheck(planes->second[0] == 1.0f && planes->second[1] == 2.0f &&
                  planes->second[2] == 3.0f && planes->second[3] == 4.0f,
              "First uploaded clip plane must be the lowest enabled index");
    TestCheck(planes->second[4] == 5.0f && planes->second[5] == 6.0f &&
                  planes->second[6] == 7.0f && planes->second[7] == 8.0f,
              "Second uploaded clip plane must be the next enabled index");

    ffp.Shutdown();
}

void DrawVertexBufferSkipsClipUniformsWhenDisabled() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    VxPlane plane;
    plane.m_Normal = VxVector(1.0f, 0.0f, 0.0f);
    plane.m_D = 1.0f;
    ffp.SetUserClipPlane(0, plane);
    ffp.SetRenderState(VXRENDERSTATE_CLIPPLANEENABLE, 0);

    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    const CKDWORD planesUniform = context.GetBlockUniformForTests(CKRST_BLOCK_CLIP_PLANES);
    const CKDWORD paramsUniform = context.GetBlockUniformForTests(CKRST_BLOCK_CLIP_PARAMS);
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator params =
        context.Log.FloatUniforms.find(paramsUniform);
    TestCheck(context.Log.FloatUniforms.find(planesUniform) == context.Log.FloatUniforms.end(),
              "Disabled clip planes must not upload clip plane uniform");
    TestCheck(params == context.Log.FloatUniforms.end() || params->second[0] == 0.0f,
              "Disabled clip planes must either skip clip params or upload count zero");

    ffp.Shutdown();
}

void ClipPlanesUseDedicatedVertexShaderVariant() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    const void *defaultVertexShaderCode = context.LastVertexShaderCode;

    VxPlane plane;
    plane.m_Normal = VxVector(1.0f, 0.0f, 0.0f);
    plane.m_D = 1.0f;
    ffp.SetUserClipPlane(0, plane);
    ffp.SetRenderState(VXRENDERSTATE_CLIPPLANEENABLE, 1u);
    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(defaultVertexShaderCode != nullptr,
              "Default draw must create a vertex shader");
    TestCheck(context.LastVertexShaderCode != nullptr &&
                  context.LastVertexShaderCode != defaultVertexShaderCode,
              "Clip plane draw must use a dedicated vertex shader variant");

    ffp.Shutdown();
}

void ResultArgTempPreservesEveryActiveStage() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(0, CKRST_TSS_RESULTARG0, CKRST_TA_TEMP);
    ffp.SetTextureStageState(1, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(1, CKRST_TSS_ARG1, CKRST_TA_TEMP);
    ffp.SetTextureStageState(1, CKRST_TSS_RESULTARG0, CKRST_TA_TEMP);

    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);

    TestCheck(spec.GetStage(0, CKFF_SPEC_STAGE_RESULT_IS_TEMP) == 1,
              "Non-final active stage must preserve RESULTARG=TEMP");
    TestCheck(spec.GetStage(1, CKFF_SPEC_STAGE_RESULT_IS_TEMP) == 1,
              "Final TEMP writes must leave the final CURRENT color unchanged");

    ffp.Shutdown();
}

void Modulate4XStaysInTextureStageSpecialization() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_MODULATE4X);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG2, CKRST_TA_CURRENT);

    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);

    TestCheck(spec.GetStage(0, CKFF_SPEC_STAGE_COLOR_OP) == CKRST_TOP_MODULATE4X,
              "MODULATE4X must remain a normal texture-stage specialization op");

    ffp.Shutdown();
}

void PremodulateStaysInTextureStageSpecialization() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_PREMODULATE);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG2, CKRST_TA_CURRENT);

    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);

    TestCheck(spec.GetStage(0, CKFF_SPEC_STAGE_COLOR_OP) == CKRST_TOP_PREMODULATE,
              "PREMODULATE must remain encoded as the stage color op");
    TestCheck(spec.Get(CKFF_SPEC_LAST_ACTIVE_TEXTURE_STAGE) == 0,
              "Single-stage PREMODULATE draw must keep last active stage at zero");

    ffp.Shutdown();
}

void TextureArgModifiersStayInSpecialization() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE | CKRST_TA_COMPLEMENT);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE | CKRST_TA_ALPHAREPLICATE);
    ffp.SetTexture(0, 7);
    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V | CKRST_DP_STAGE(0), CKFF_VF_POSITION | CKFF_VF_TEXCOORD0, 1);

    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);

    TestCheck(spec.GetStage(0, CKFF_SPEC_STAGE_COLOR_ARG1) ==
                  CKFFSpecializationInfo::RepackArg(CKRST_TA_TEXTURE | CKRST_TA_COMPLEMENT),
              "COMPLEMENT must remain in stage specialization");
    TestCheck(spec.GetStage(0, CKFF_SPEC_STAGE_ALPHA_ARG1) ==
                  CKFFSpecializationInfo::RepackArg(CKRST_TA_TEXTURE | CKRST_TA_ALPHAREPLICATE),
              "ALPHAREPLICATE must remain in stage specialization");

    ffp.Shutdown();
}

void NullTextureStagePreservesSpecialization() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetTexture(0, 0);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(1, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(1, CKRST_TSS_ARG1, CKRST_TA_CURRENT);

    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);

    TestCheck(spec.GetStage(0, CKFF_SPEC_STAGE_COLOR_OP) == CKRST_TOP_SELECTARG1,
              "Unbound texture stage must keep its original color op");
    TestCheck(spec.GetStage(0, CKFF_SPEC_STAGE_COLOR_ARG1) == CKRST_TA_TEXTURE,
              "Unbound texture stage must keep TEXTURE as its color arg");
    TestCheck(spec.Get(CKFF_SPEC_LAST_ACTIVE_TEXTURE_STAGE) == 1,
              "Unbound texture stage must not truncate later active stages");

    ffp.Shutdown();
}

void StageConstantDoesNotCreateTextureDependency() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetTexture(0, 0);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_CONSTANT);
    ffp.SetTextureStageState(0, CKRST_TSS_CONSTANT, 0x80402010u);

    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);

    const CKDWORD stageParamsUniform = context.GetBlockUniformForTests(CKRST_BLOCK_STAGE_PARAMS);
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator stageParams =
        context.Log.FloatUniforms.find(stageParamsUniform);

    TestCheck(spec.GetStage(0, CKFF_SPEC_STAGE_COLOR_OP) == CKRST_TOP_SELECTARG1,
              "D3DTA_CONSTANT must not disable the stage when no texture is bound");
    TestCheck(spec.GetStage(0, CKFF_SPEC_STAGE_COLOR_ARG1) == CKRST_TA_CONSTANT,
              "D3DTA_CONSTANT must remain encoded in specialization");
    const size_t constant = CKFFStageParamIndex(0, CKFF_STAGE_PARAM_CONSTANT) * 4;
    TestCheck(stageParams != context.Log.FloatUniforms.end() &&
                  stageParams->second.size() >= constant + 4 &&
                  stageParams->second[constant + 0] == 0x40 / 255.0f &&
                  stageParams->second[constant + 1] == 0x20 / 255.0f &&
                  stageParams->second[constant + 2] == 0x10 / 255.0f &&
                  stageParams->second[constant + 3] == 0x80 / 255.0f,
              "Stage constant must upload in stage params");

    ffp.Shutdown();
}

void CubeTextureUsesCubeSamplerSpecializationAndBinding() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetTexture(0, 77, CKRST_TEXTURE_VALID | CKRST_TEXTURE_CUBEMAP);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);

    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);
    TestCheck(spec.GetStage(0, CKFF_SPEC_STAGE_SAMPLER_TYPE) == CKFF_SAMPLER_CUBE,
              "Cubemap texture must mark stage 0 as cube sampler");
    TestCheck(context.Log.TextureBindCount == 1,
              "Cubemap draw must bind one texture");
    TestCheck(context.Log.LastTextureUniform == context.GetSamplerUniformForTests(8 + 0),
              "Cubemap draw must bind the cube sampler uniform");
    TestCheck(context.Log.LastTextureHandle == 77,
              "Cubemap draw must bind the requested texture handle");

    ffp.Shutdown();
}

void VolumeTextureBindsFirstVolumeSampler() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetTexture(0, 91, CKRST_TEXTURE_VALID | CKRST_TEXTURE_VOLUMEMAP);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_MODULATE);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG2, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_MODULATE);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG2, CKRST_TA_DIFFUSE);

    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_TR_CL_V, CKRST_DP_TR_CL_V, 1);

    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);
    TestCheck(context.Log.DrawCount == 1,
              "Volume texture draw must submit through the uber shader");
    TestCheck((spec.GetStage(0, CKFF_SPEC_STAGE_COLOR_OP) == CKRST_TOP_MODULATE) &&
              (spec.GetStage(0, CKFF_SPEC_STAGE_SAMPLER_TYPE) == CKFF_SAMPLER_VOLUME),
              "Volume draw must keep the stage op and volume sampler type in the specialization data");
    TestCheck(context.Log.TextureBindCount == 1,
              "Volume draw must bind one texture");
    TestCheck(context.Log.LastTextureStage == 12 &&
              context.Log.LastTextureUniform == context.GetSamplerUniformForTests(12 + 0),
              "The first volume stage must bind slot 12 and s_textureVolume0");

    ffp.Shutdown();
}

void VolumeTextureStageSevenBindsVolumeSampler() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    for (CKDWORD stage = 0; stage < 7; ++stage) {
        ffp.SetTextureStageState(stage, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
        ffp.SetTextureStageState(stage, CKRST_TSS_ARG1, CKRST_TA_CURRENT);
        ffp.SetTextureStageState(stage, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
        ffp.SetTextureStageState(stage, CKRST_TSS_AARG1, CKRST_TA_CURRENT);
    }
    ffp.SetTexture(7, 107, CKRST_TEXTURE_VALID | CKRST_TEXTURE_VOLUMEMAP);
    ffp.SetTextureStageState(7, CKRST_TSS_OP, CKRST_TOP_MODULATE);
    ffp.SetTextureStageState(7, CKRST_TSS_ARG1, CKRST_TA_CURRENT);
    ffp.SetTextureStageState(7, CKRST_TSS_ARG2, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(7, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(7, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);

    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_TR_CL_V, CKRST_DP_TR_CL_V, 1);

    TestCheck(context.Log.DrawCount == 1,
              "Volume stage 7 must submit through the uber shader");
    TestCheck(context.Log.TextureBindCount == 1,
              "Volume stage 7 draw must bind one texture");
    TestCheck(context.Log.LastTextureStage == 12 &&
              context.Log.LastTextureUniform == context.GetSamplerUniformForTests(12 + 0),
              "A volume texture on stage 7 is the first volume stage and binds slot 12 / s_textureVolume0");

    ffp.Shutdown();
}

void RunVolumeAndCubeBindTheirTypeSlots(CK_SHADER_PROFILE profile) {
    FFPRecordingDriver driver(profile);
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetTexture(0, 201, CKRST_TEXTURE_VALID | CKRST_TEXTURE_VOLUMEMAP);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_MODULATE);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG2, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);

    ffp.SetTexture(1, 202, CKRST_TEXTURE_VALID | CKRST_TEXTURE_CUBEMAP);
    ffp.SetTextureStageState(1, CKRST_TSS_OP, CKRST_TOP_ADD);
    ffp.SetTextureStageState(1, CKRST_TSS_ARG1, CKRST_TA_CURRENT);
    ffp.SetTextureStageState(1, CKRST_TSS_ARG2, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(1, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(1, CKRST_TSS_AARG1, CKRST_TA_CURRENT);

    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_TR_CL_V, CKRST_DP_TR_CL_V, 1);

    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);

    TestCheck(context.Log.DrawCount == 1,
              "Volume + cube draw must submit through the uber shader");
    TestCheck(spec.GetStage(0, CKFF_SPEC_STAGE_SAMPLER_TYPE) == CKFF_SAMPLER_VOLUME &&
                  spec.GetStage(1, CKFF_SPEC_STAGE_SAMPLER_TYPE) == CKFF_SAMPLER_CUBE,
              "Volume + cube draw must carry both sampler types in the specialization data");
    TestCheck(spec.GetStage(0, CKFF_SPEC_STAGE_COLOR_OP) == CKRST_TOP_MODULATE &&
                  spec.GetStage(1, CKFF_SPEC_STAGE_COLOR_OP) == CKRST_TOP_ADD,
              "Volume + cube draw must keep the texture stage ops in the specialization data");
    const FFPRecordingBackend &u = context;
    bool sawVolume = false;
    bool sawCube = false;
    for (const FFPTextureBinding &binding : context.Log.TextureBindings) {
        if (binding.Stage == 12 && binding.Uniform == u.GetSamplerUniformForTests(12 + 0) && binding.Texture == 201)
            sawVolume = true;
        if (binding.Stage == 8 && binding.Uniform == u.GetSamplerUniformForTests(8 + 0) && binding.Texture == 202)
            sawCube = true;
    }
    TestCheck(sawVolume && sawCube,
              "Volume + cube draw must bind each texture to the slot block of its sampler type");

    ffp.Shutdown();
}

void VolumeAndCubeBindTheirTypeSlots() {
    for (const ShaderProfileCase &profile : kSamplerLayoutProfiles) {
        printf("  profile %s\n", profile.Name);
        RunVolumeAndCubeBindTheirTypeSlots(profile.Profile);
    }
}

void RunArbitrarySingleVolumeCubePlacementSharesTheProgram(CK_SHADER_PROFILE profile) {
    FFPRecordingDriver driver(profile);
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetTexture(0, 211, CKRST_TEXTURE_VALID | CKRST_TEXTURE_VOLUMEMAP);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_MODULATE);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG2, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);

    ffp.SetTextureStageState(1, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(1, CKRST_TSS_ARG1, CKRST_TA_CURRENT);
    ffp.SetTextureStageState(1, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(1, CKRST_TSS_AARG1, CKRST_TA_CURRENT);

    ffp.SetTexture(2, 212, CKRST_TEXTURE_VALID | CKRST_TEXTURE_CUBEMAP);
    ffp.SetTextureStageState(2, CKRST_TSS_OP, CKRST_TOP_ADD);
    ffp.SetTextureStageState(2, CKRST_TSS_ARG1, CKRST_TA_CURRENT);
    ffp.SetTextureStageState(2, CKRST_TSS_ARG2, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(2, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(2, CKRST_TSS_AARG1, CKRST_TA_CURRENT);

    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_TR_CL_V, CKRST_DP_TR_CL_V, 1);

    TestCheck(context.Log.DrawCount == 1,
              "arbitrary single volume+cube placement must draw through the uber shader");
    TestCheck(context.CreatedProgramCount == 1,
              "arbitrary sampler placement must not create a dedicated program");

    const FFPRecordingBackend &u = context;
    bool sawVolume = false;
    bool sawCube = false;
    for (const FFPTextureBinding &binding : context.Log.TextureBindings) {
        if (binding.Stage == 12 && binding.Uniform == u.GetSamplerUniformForTests(12 + 0) && binding.Texture == 211)
            sawVolume = true;
        if (binding.Stage == 8 && binding.Uniform == u.GetSamplerUniformForTests(8 + 0) && binding.Texture == 212)
            sawCube = true;
    }
    TestCheck(sawVolume && sawCube,
              "logical stages must map onto the type slot blocks by ordinal");

    ffp.Shutdown();
}

void ArbitrarySingleVolumeCubePlacementSharesTheProgram() {
    for (const ShaderProfileCase &profile : kSamplerLayoutProfiles) {
        printf("  profile %s\n", profile.Name);
        RunArbitrarySingleVolumeCubePlacementSharesTheProgram(profile.Profile);
    }
}

void RunMultipleMixedSamplersUseTypeRankedSlots(CK_SHADER_PROFILE profile) {
    FFPRecordingDriver driver(profile);
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetTexture(0, 401, CKRST_TEXTURE_VALID | CKRST_TEXTURE_CUBEMAP);
    ffp.SetTexture(1, 402, CKRST_TEXTURE_VALID | CKRST_TEXTURE_CUBEMAP);
    ffp.SetTexture(2, 403, CKRST_TEXTURE_VALID | CKRST_TEXTURE_VOLUMEMAP);
    ffp.SetTexture(3, 404, CKRST_TEXTURE_VALID | CKRST_TEXTURE_VOLUMEMAP);

    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_TR_CL_V, CKRST_DP_TR_CL_V, 1);

    TestCheck(context.Log.DrawCount == 1,
              "multiple mixed samplers must draw through the uber shader");
    const FFPRecordingBackend &u = context;
    bool found[4] = {};
    for (const FFPTextureBinding &binding : context.Log.TextureBindings) {
        if (binding.Stage == 8 && binding.Uniform == u.GetSamplerUniformForTests(8 + 0) && binding.Texture == 401)
            found[0] = true;
        if (binding.Stage == 9 && binding.Uniform == u.GetSamplerUniformForTests(8 + 1) && binding.Texture == 402)
            found[1] = true;
        if (binding.Stage == 12 && binding.Uniform == u.GetSamplerUniformForTests(12 + 0) && binding.Texture == 403)
            found[2] = true;
        if (binding.Stage == 13 && binding.Uniform == u.GetSamplerUniformForTests(12 + 1) && binding.Texture == 404)
            found[3] = true;
    }
    TestCheck(found[0] && found[1] && found[2] && found[3],
              "multiple mixed samplers must use stable type-ranked slots");
    ffp.Shutdown();
}

void MultipleMixedSamplersUseTypeRankedSlots() {
    for (const ShaderProfileCase &profile : kSamplerLayoutProfiles) {
        printf("  profile %s\n", profile.Name);
        RunMultipleMixedSamplersUseTypeRankedSlots(profile.Profile);
    }
}

void FifthCubeStageSamplesAsUnbound() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    for (CKDWORD stage = 0; stage < 5; ++stage) {
        ffp.SetTexture(stage, 501 + stage, CKRST_TEXTURE_VALID | CKRST_TEXTURE_CUBEMAP);
        ffp.SetTextureStageState(stage, CKRST_TSS_OP, CKRST_TOP_MODULATE);
        ffp.SetTextureStageState(stage, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
        ffp.SetTextureStageState(stage, CKRST_TSS_ARG2, CKRST_TA_CURRENT);
        ffp.SetTextureStageState(stage, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
        ffp.SetTextureStageState(stage, CKRST_TSS_AARG1, CKRST_TA_CURRENT);
    }

    const CKBOOL drawn = ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                                              1, 0, 0, 3, 0, 0,
                                              CKRST_DP_TR_CL_V, CKRST_DP_TR_CL_V, 1);

    TestCheck(drawn && context.Log.DrawCount == 1,
              "five cube stages must still submit (approximation, not rejection)");
    TestCheck(context.Log.TextureBindCount == 4,
              "only four cube textures fit the fixed sampler layout");
    const FFPRecordingBackend &u = context;
    bool found[4] = {};
    bool boundFifth = false;
    for (const FFPTextureBinding &binding : context.Log.TextureBindings) {
        for (CKDWORD ordinal = 0; ordinal < 4; ++ordinal) {
            if (binding.Stage == 8 + ordinal && binding.Uniform == u.GetSamplerUniformForTests(8 + ordinal) &&
                binding.Texture == 501 + ordinal)
                found[ordinal] = true;
        }
        if (binding.Texture == 505)
            boundFifth = true;
    }
    TestCheck(found[0] && found[1] && found[2] && found[3] && !boundFifth,
              "the first four cube stages bind slots 8..11 and the fifth stays unbound");

    const CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);
    TestCheck(spec.GetStage(4, CKFF_SPEC_STAGE_SAMPLER_TYPE) == CKFF_SAMPLER_2D &&
                  spec.Get(CKFF_SPEC_LAST_ACTIVE_TEXTURE_STAGE) == 4,
              "the overflowing stage must specialize as an untextured 2D stage that stays active");
    const CKDWORD stageParams = u.GetBlockUniformForTests(CKRST_BLOCK_STAGE_PARAMS);
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator it =
        context.Log.FloatUniforms.find(stageParams);
    TestCheck(it != context.Log.FloatUniforms.end() &&
                  it->second.size() >= CKFF_STAGE_PARAM_VEC4_COUNT * 4 &&
                  it->second[CKFFStageParamIndex(3, CKFF_STAGE_PARAM_COORD) * 4 + 2] == 1.0f &&
                  it->second[CKFFStageParamIndex(4, CKFF_STAGE_PARAM_COORD) * 4 + 2] == 0.0f,
              "stage params must mark the overflowing stage as having no texture");
    TestCheck(ffp.GetLastDrawApproximationMask() == (1ull << CKRST_DIAG_APPROX_SAMPLER_SLOTS) &&
                  ffp.GetApproximatedDrawCount(CKRST_DIAG_APPROX_SAMPLER_SLOTS) == 1,
              "sampler slot overflow must be reported as an approximation");

    ffp.Shutdown();
}

void MultipleVolumeTexturesBindEachVolumeSampler() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetTexture(0, 301, CKRST_TEXTURE_VALID | CKRST_TEXTURE_VOLUMEMAP);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);

    ffp.SetTextureStageState(1, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(1, CKRST_TSS_ARG1, CKRST_TA_CURRENT);
    ffp.SetTextureStageState(1, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(1, CKRST_TSS_AARG1, CKRST_TA_CURRENT);

    ffp.SetTexture(2, 302, CKRST_TEXTURE_VALID | CKRST_TEXTURE_VOLUMEMAP);
    ffp.SetTextureStageState(2, CKRST_TSS_OP, CKRST_TOP_ADD);
    ffp.SetTextureStageState(2, CKRST_TSS_ARG1, CKRST_TA_CURRENT);
    ffp.SetTextureStageState(2, CKRST_TSS_ARG2, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(2, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(2, CKRST_TSS_AARG1, CKRST_TA_CURRENT);

    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_TR_CL_V, CKRST_DP_TR_CL_V, 1);

    const FFPRecordingBackend &u = context;
    bool sawStage0 = false;
    bool sawStage2 = false;
    for (const FFPTextureBinding &binding : context.Log.TextureBindings) {
        if (binding.Stage == 12 && binding.Uniform == u.GetSamplerUniformForTests(12 + 0) && binding.Texture == 301)
            sawStage0 = true;
        if (binding.Stage == 13 && binding.Uniform == u.GetSamplerUniformForTests(12 + 1) && binding.Texture == 302)
            sawStage2 = true;
    }
    TestCheck(context.Log.DrawCount == 1,
              "Multi-volume runtime draw must submit");
    TestCheck(sawStage0 && sawStage2,
              "Multiple volume stages must bind consecutive volume samplers by ordinal");

    ffp.Shutdown();
}

void DepthTextureCompareFuncUploadsSamplerAndSpecialization() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetTexture(0, 101, CKRST_TEXTURE_VALID | CKRST_TEXTURE_DEPTHSTENCIL);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);

    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    const CKFFSpecializationInfo noCompareSpec = CurrentDrawSpecialization(ffp, context);
    TestCheck(noCompareSpec.GetStage(0, CKFF_SPEC_STAGE_SAMPLER_TYPE) == CKFF_SAMPLER_DEPTH,
              "Depth texture must mark stage 0 as depth sampler");
    TestCheck(noCompareSpec.GetStage(0, CKFF_SPEC_STAGE_SAMPLER_COMPARE_FUNC) == CKRST_COMPARE_NONE,
              "Depth texture without compare func must keep compare mask empty");
    TestCheck(context.Log.LastTextureSampler.CompareFunc == CKRST_COMPARE_NONE,
              "Depth texture without compare func must bind a non-compare sampler");

    ffp.SetTextureStageState(0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_NEAREST);
    ffp.SetTextureStageState(0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_NEAREST);
    ffp.SetTextureStageState(0, CKRST_TSS_COMPAREFUNC, CKRST_COMPARE_LEQUAL);
    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    const CKFFSpecializationInfo compareSpec = CurrentDrawSpecialization(ffp, context);
    TestCheck(compareSpec.GetStage(0, CKFF_SPEC_STAGE_SAMPLER_COMPARE_FUNC) == CKRST_COMPARE_LEQUAL,
              "Depth compare func must enter specialization mask");
    TestCheck(context.Log.LastTextureSampler.CompareFunc == CKRST_COMPARE_NONE,
              "Depth compare func must stay shader-evaluated and bind a non-compare sampler");

    ffp.Shutdown();
}

void FilteredDepthTextureCompareApproximates() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetTexture(0, 101, CKRST_TEXTURE_VALID | CKRST_TEXTURE_DEPTHSTENCIL);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_COMPAREFUNC, CKRST_COMPARE_LEQUAL);

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(drawn && context.Log.DrawCount == 1,
              "Filtered shader depth compare must draw");
    TestCheck(ffp.GetLastDrawApproximationMask() == (1ull << CKRST_DIAG_APPROX_COMPAREFUNC_FILTER),
              "Filtered shader depth compare must report its approximation");
    const CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);
    TestCheck(spec.GetStage(0, CKFF_SPEC_STAGE_SAMPLER_COMPARE_FUNC) == CKRST_COMPARE_LEQUAL,
              "The compare function still reaches the shader");

    ffp.Shutdown();
}

void AffineTextureCoordinatesReachSharedShader() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());
    ffp.SetRenderState(VXRENDERSTATE_TEXTUREPERSPECTIVE, FALSE);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTexture(0, 77, CKRST_TEXTURE_VALID);

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    const CKDWORD drawUniform = context.GetBlockUniformForTests(CKRST_BLOCK_DRAW_PARAMS);
    const std::vector<float> &drawParams = context.Log.FloatUniforms[drawUniform];
    TestCheck(drawn && context.Log.DrawCount == 1 &&
                  ffp.GetLastDrawApproximationMask() == 0 &&
                  drawParams.size() > CKFF_DRAW_PARAM_MATERIAL_POWER * 4 + 2 &&
                  drawParams[CKFF_DRAW_PARAM_MATERIAL_POWER * 4 + 2] == 1.0f,
              "Disabled texture perspective must select affine interpolation in both shaders");
    ffp.Shutdown();
}

void InactiveUnsupportedStateDoesNotRejectDraw() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());
    ffp.SetRenderState(VXRENDERSTATE_TEXTUREPERSPECTIVE, FALSE);
    ffp.SetRenderState(VXRENDERSTATE_STENCILENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_STENCILWRITEMASK, 0x0Fu);
    ffp.SetRenderState(VXRENDERSTATE_STENCILFAIL, VXSTENCILOP_KEEP);
    ffp.SetRenderState(VXRENDERSTATE_STENCILZFAIL, VXSTENCILOP_KEEP);
    ffp.SetRenderState(VXRENDERSTATE_STENCILPASS, VXSTENCILOP_KEEP);

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    TestCheck(drawn && context.Log.DrawCount == 1,
              "Unsupported state bits with no output effect must not reject a draw");
    TestCheck(ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_NONE,
              "A successful no-effect state draw must clear the reject reason");
    TestCheck(ffp.GetLastDrawApproximationMask() == 0,
              "State bits without output effect must not report approximations");
    ffp.Shutdown();
}

void DisabledTextureStageIgnoresLaterUnsupportedState() {
    FFPRecordingDriver driver(CKRST_SHADER_PROFILE_GLSL,
                               CKRST_SHADER_TARGET_NDC_MINUS_ONE_TO_ONE |
                               CKRST_SHADER_TARGET_ORIGIN_BOTTOM_LEFT);
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());
    ffp.SetRenderState(VXRENDERSTATE_TEXTUREPERSPECTIVE, FALSE);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_DISABLE);
    ffp.SetTextureStageState(1, CKRST_TSS_OP, 0x7fffffffu);
    ffp.SetTextureStageState(1, CKRST_TSS_COMPAREFUNC, CKRST_COMPARE_LEQUAL);
    ffp.SetTexture(1, 77, CKRST_TEXTURE_VALID | CKRST_TEXTURE_RENDERTARGET |
                          CKRST_TEXTURE_CUBEMAP | CKRST_TEXTURE_DEPTHSTENCIL);

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(drawn && context.Log.DrawCount == 1,
              "COLOROP=DISABLE must make all later texture-stage state inactive");
    TestCheck(ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_NONE,
              "Inactive later texture stages must not report a draw rejection");
    ffp.Shutdown();
}

void BoundUnusedTextureDoesNotRequireAffineInterpolation() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());
    ffp.SetRenderState(VXRENDERSTATE_TEXTUREPERSPECTIVE, FALSE);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_DIFFUSE);
    ffp.SetTexture(0, 77, CKRST_TEXTURE_VALID);

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(drawn && context.Log.DrawCount == 1,
              "A bound but unused texture must not require affine interpolation support");
    TestCheck(ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_NONE,
              "Unused texture binding must not report an affine draw rejection");
    ffp.Shutdown();
}

void TextureStageSnapshotPreservesExplicitZeroArgument() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_DIFFUSE);

    CKFFTextureStageSnapshot snapshot;
    ffp.SaveTextureStage(0, snapshot);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);
    ffp.RestoreTextureStage(0, snapshot);
    ffp.SetTexture(0, 77, CKRST_TEXTURE_VALID);
    ffp.SetRenderState(VXRENDERSTATE_TEXTUREPERSPECTIVE, FALSE);

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(drawn && context.Log.DrawCount == 1,
              "Texture-stage snapshot must preserve an explicitly selected zero-valued argument");
    ffp.Shutdown();
}

void UnknownTextureOpRejectsDraw() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());
    ffp.SetTextureStageState(0, CKRST_TSS_OP, 0x7fffffffu);

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    TestCheck(!drawn && context.Log.DrawCount == 0,
              "An unknown texture op is an invalid value and must not draw");
    TestCheck(ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_TEXTURE_OP,
              "Unknown texture-op rejection must expose its reason");
    ffp.Shutdown();
}

void AlphaBumpOpIsRejected() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());
    ffp.SetTexture(0, 77, CKRST_TEXTURE_VALID);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_BUMPENVMAP);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    TestCheck(!drawn && context.Log.DrawCount == 0,
              "a bump op on the alpha channel must not draw");
    TestCheck(ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_TEXTURE_OP &&
                  ffp.GetLastDrawApproximationMask() == 0,
              "invalid alpha bump operation is rejected without approximation");

    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_BUMPENVMAP);
    TestCheck(ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                                   1, 0, 0, 3, 0, 0,
                                   CKRST_DP_CL_V, CKRST_DP_CL_V, 1) &&
                  ffp.GetLastDrawApproximationMask() == (1ull << CKRST_DIAG_APPROX_BUMP_TEXTURE_FLAGS),
              "A bump op on a texture without DuDv data samples it as an ordinary texture with a diagnostic");
    ffp.Shutdown();
}

void BottomLeftRenderTargetsSampleWithoutFlip() {
    FFPRecordingDriver driver(CKRST_SHADER_PROFILE_GLSL,
                               CKRST_SHADER_TARGET_NDC_MINUS_ONE_TO_ONE |
                               CKRST_SHADER_TARGET_ORIGIN_BOTTOM_LEFT);
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTexture(0, 77, CKRST_TEXTURE_VALID | CKRST_TEXTURE_RENDERTARGET |
                          CKRST_TEXTURE_CUBEMAP);

    CKBOOL drawn = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    TestCheck(drawn && context.Log.DrawCount == 1 && ffp.GetLastDrawApproximationMask() == 0,
              "Cube render targets sample exactly on bottom-left backends (rendered top-down)");

    ffp.SetTexture(0, 78, CKRST_TEXTURE_VALID | CKRST_TEXTURE_RENDERTARGET);
    drawn = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    const CKDWORD stageParams = context.GetBlockUniformForTests(CKRST_BLOCK_STAGE_PARAMS);
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator params =
        context.Log.FloatUniforms.find(stageParams);
    TestCheck(drawn && params != context.Log.FloatUniforms.end() &&
                  ((CKDWORD)params->second[CKFFStageParamIndex(0, CKFF_STAGE_PARAM_COORD) * 4 + 1] & 0x1000u) == 0,
              "2D render targets carry no sampling flip flag: the flip happens when rendering into them");
    ffp.Shutdown();
}

static bool NearlyEqual(float a, float b) {
    return a > b - 1e-4f && a < b + 1e-4f;
}

// Spec 4.4: the engine's viewport is a sub rectangle of the logical target;
// draws remap viewport-relative clip space into the target and clip with a
// scissor on the physical (RenderScale) target.
void ViewportMappingRemapsClipSpaceAndScissors() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());
    const CKDWORD matrixUniform = context.GetBlockUniformForTests(CKRST_BLOCK_MATRICES);
    const CKDWORD viewportUniform = context.GetBlockUniformForTests(CKRST_BLOCK_VIEWPORT);

    // Window 640x480 rendered into a 320x240 scene target, viewport 100,50 200x100.
    ffp.SetTargetExtents(640, 480, 320, 240);
    CKViewportData viewport = {};
    viewport.ViewX = 100;
    viewport.ViewY = 50;
    viewport.ViewWidth = 200;
    viewport.ViewHeight = 100;
    viewport.ViewZMax = 1.0f;
    ffp.SetViewport(viewport);
    const float *remap = ffp.GetViewportRemap();
    TestCheck(NearlyEqual(remap[0], 200.0f / 640.0f) && NearlyEqual(remap[1], 100.0f / 480.0f) &&
                  NearlyEqual(remap[2], -0.375f) && NearlyEqual(remap[3], 1.0f - 200.0f / 480.0f),
              "the viewport remap scales and offsets clip space into the target");
    CKRECT scissor;
    TestCheck(ffp.GetViewportScissor(&scissor) && scissor.left == 50 && scissor.top == 25 &&
                  scissor.right == 150 && scissor.bottom == 75,
              "the scissor is the viewport scaled to the physical target");

    ffp.DrawVertexBuffer(VX_TRIANGLELIST, 1, 0, 0, 3, 0, 0,
                         CKRST_DP_TR_CL_V, CKRST_DP_TR_CL_V, 1);
    std::vector<float> matrices = context.Log.FloatUniforms[matrixUniform];
    TestCheck(matrices.size() >= 16 && NearlyEqual(matrices[0], 200.0f / 640.0f) &&
                  NearlyEqual(matrices[5], 100.0f / 480.0f) &&
                  NearlyEqual(matrices[12], 401.0f / 640.0f - 1.0f) &&
                  NearlyEqual(matrices[13], 1.0f - 201.0f / 480.0f) && matrices[10] == 1.0f && matrices[15] == 1.0f,
              "3D draws carry the viewport remap and legacy pixel center in the projection");
    TestCheck(context.Log.ScissorEnabled && context.Log.LastScissor.left == 50 &&
                  context.Log.LastScissor.right == 150 && context.Log.LastScissor.bottom == 75,
              "3D draws set the viewport scissor");

    // Pre-transformed vertices are absolute window pixels: the remapped
    // mapping equals the full-window mapping.
    ffp.DrawVertexBuffer(VX_TRIANGLELIST, 1, 0, 0, 3, 0, 0,
                         CKRST_DP_VCT, CKFF_VF_POSITIONT | CKFF_VF_TEXCOORD0 | CKFF_VF_COLOR0, 1);
    std::vector<float> vp = context.Log.FloatUniforms[viewportUniform];
    TestCheck(vp.size() >= 4 && NearlyEqual(vp[0], 2.0f / 640.0f) && NearlyEqual(vp[1], -2.0f / 480.0f) &&
                  NearlyEqual(vp[2], -1.0f) && NearlyEqual(vp[3], 1.0f),
              "POSITIONT pixels stay absolute window pixels under a sub viewport");

    // Full viewport: identity, no scissor.
    viewport.ViewX = 0;
    viewport.ViewY = 0;
    viewport.ViewWidth = 640;
    viewport.ViewHeight = 480;
    ffp.SetViewport(viewport);
    remap = ffp.GetViewportRemap();
    TestCheck(remap[0] == 1.0f && remap[1] == 1.0f && remap[2] == 0.0f && remap[3] == 0.0f &&
                  !ffp.GetViewportScissor(NULL),
              "a full viewport maps identically and needs no scissor");
    ffp.DrawVertexBuffer(VX_TRIANGLELIST, 1, 0, 0, 3, 0, 0,
                         CKRST_DP_TR_CL_V, CKRST_DP_TR_CL_V, 1);
    TestCheck(!context.Log.ScissorEnabled, "full-viewport draws clear the scissor");
    ffp.Shutdown();

    // Render target on a bottom-left backend: the scissor rows are mirrored
    // like the image (spec 5.9).
    FFPRecordingDriver bottomLeft(CKRST_SHADER_PROFILE_GLSL,
                                   CKRST_SHADER_TARGET_NDC_MINUS_ONE_TO_ONE |
                                   CKRST_SHADER_TARGET_ORIGIN_BOTTOM_LEFT);
    FFPRecordingBackend rttContext(&bottomLeft);
    CKFixedFunctionPipeline rtt;
    rtt.Init(rttContext.StartedBackend(), rttContext.ShaderSet());
    rtt.SetRenderTargetActive(TRUE);
    rtt.SetTargetExtents(128, 64, 128, 64);
    CKViewportData topHalf = {};
    topHalf.ViewWidth = 64;
    topHalf.ViewHeight = 32;
    topHalf.ViewZMax = 1.0f;
    rtt.SetViewport(topHalf);
    TestCheck(rtt.GetViewportScissor(&scissor) && scissor.left == 0 && scissor.right == 64 &&
                  scissor.top == 32 && scissor.bottom == 64,
              "a flipped render target mirrors the viewport scissor");
    rtt.Shutdown();
}

void TransformedVerticesUseLegacyPixelCenters() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    TestCheck(ffp.Init(context.StartedBackend(), context.ShaderSet()), "pixel-center pipeline initialization");
    CKViewportData viewport = {};
    viewport.ViewWidth = 64;
    viewport.ViewHeight = 32;
    viewport.ViewZMax = 1.0f;
    ffp.SetViewport(viewport);
    ffp.SetTargetExtents(64, 32, 64, 32);
    TestCheck(ffp.DrawVertexBuffer(VX_TRIANGLELIST, 1, 0, 0, 3, 0, 0,
                                   CKRST_DP_TR_CL_V, CKRST_DP_TR_CL_V, 1),
              "pixel-center transformed draw");
    const std::vector<float> &matrix = context.Log.FloatUniforms[
        context.GetBlockUniformForTests(CKRST_BLOCK_MATRICES)];
    // With identity transforms, clip origin names legacy pixel (32,16).
    // Its modern raster position must be (32.5,16.5), the same sample as
    // a POSITIONT vertex naming that integer pixel center.
    TestCheck(matrix.size() >= 16 &&
                  fabsf((matrix[12] + 1.0f) * 32.0f - 32.5f) < 0.00001f &&
                  fabsf((1.0f - matrix[13]) * 16.0f - 16.5f) < 0.00001f,
              "3D projection must preserve the D3D8 integer pixel-center convention");
    ffp.Shutdown();
}

void RenderTargetOriginFlipsProjectionViewportAndWinding() {
    FFPRecordingDriver bottomLeft(CKRST_SHADER_PROFILE_GLSL,
                                   CKRST_SHADER_TARGET_NDC_MINUS_ONE_TO_ONE |
                                   CKRST_SHADER_TARGET_ORIGIN_BOTTOM_LEFT);
    FFPRecordingBackend context(&bottomLeft);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());
    CKViewportData viewport = {};
    viewport.ViewWidth = 640;
    viewport.ViewHeight = 480;
    viewport.ViewZMax = 1.0f;
    ffp.SetViewport(viewport);
    const CKDWORD matrixUniform = context.GetBlockUniformForTests(CKRST_BLOCK_MATRICES);
    const CKDWORD viewportUniform = context.GetBlockUniformForTests(CKRST_BLOCK_VIEWPORT);

    // Backbuffer: identity transforms and the default counter-clockwise cull.
    ffp.DrawVertexBuffer(VX_TRIANGLELIST, 1, 0, 0, 3, 0, 0,
                         CKRST_DP_TR_CL_V, CKRST_DP_TR_CL_V, 1);
    std::vector<float> matrices = context.Log.FloatUniforms[matrixUniform];
    TestCheck(matrices.size() >= 16 && matrices[0] == 1.0f && matrices[5] == 1.0f && matrices[10] == 1.0f,
              "backbuffer draws keep the projection as set");
    const CKDWORD backbufferCull = context.Log.LastState.Lo & CKRST_STATE_CULL(3);
    TestCheck(backbufferCull == CKRST_STATE_CULL(2), "default cull mode is counter-clockwise");

    // Render target on a bottom-left backend: Y flipped, winding mirrored.
    ffp.SetRenderTargetActive(TRUE);
    TestCheck(ffp.IsRenderTargetActive() && ffp.RenderTargetOriginFlip(),
              "a bound render target flips the origin on bottom-left backends");
    ffp.DrawVertexBuffer(VX_TRIANGLELIST, 1, 0, 0, 3, 0, 0,
                         CKRST_DP_TR_CL_V, CKRST_DP_TR_CL_V, 1);
    matrices = context.Log.FloatUniforms[matrixUniform];
    TestCheck(matrices.size() >= 16 && matrices[0] == 1.0f && matrices[5] == -1.0f &&
                  matrices[10] == 1.0f && matrices[15] == 1.0f,
              "render-target draws negate the clip-space Y of the projection");
    TestCheck((context.Log.LastState.Lo & CKRST_STATE_CULL(3)) == CKRST_STATE_CULL(1),
              "render-target draws mirror the front-face winding");
    ffp.SetRenderState(VXRENDERSTATE_INVERSEWINDING, TRUE);
    ffp.DrawVertexBuffer(VX_TRIANGLELIST, 1, 0, 0, 3, 0, 0,
                         CKRST_DP_TR_CL_V, CKRST_DP_TR_CL_V, 1);
    TestCheck((context.Log.LastState.Lo & CKRST_STATE_CULL(3)) == CKRST_STATE_CULL(2),
              "INVERSEWINDING combines with the origin flip");
    ffp.SetRenderState(VXRENDERSTATE_INVERSEWINDING, FALSE);

    // Pre-transformed vertices: the screen-to-clip Y mapping mirrors too.
    ffp.DrawVertexBuffer(VX_TRIANGLELIST, 1, 0, 0, 3, 0, 0,
                         CKRST_DP_VCT, CKFF_VF_POSITIONT | CKFF_VF_TEXCOORD0 | CKFF_VF_COLOR0, 1);
    std::vector<float> vp = context.Log.FloatUniforms[viewportUniform];
    TestCheck(vp.size() >= 4 && vp[1] > 0.0f && vp[3] == -1.0f,
              "POSITIONT draws into a render target use the mirrored viewport mapping");

    ffp.SetRenderTargetActive(FALSE);
    ffp.DrawVertexBuffer(VX_TRIANGLELIST, 1, 0, 0, 3, 0, 0,
                         CKRST_DP_VCT, CKFF_VF_POSITIONT | CKFF_VF_TEXCOORD0 | CKFF_VF_COLOR0, 1);
    vp = context.Log.FloatUniforms[viewportUniform];
    TestCheck(!ffp.RenderTargetOriginFlip() && vp.size() >= 4 && vp[1] < 0.0f && vp[3] == 1.0f &&
                  (context.Log.LastState.Lo & CKRST_STATE_CULL(3)) == CKRST_STATE_CULL(2),
              "releasing the render target restores the backbuffer mapping and winding");
    ffp.Shutdown();

    // Top-left backends never flip.
    FFPRecordingDriver topLeft;
    FFPRecordingBackend topLeftContext(&topLeft);
    CKFixedFunctionPipeline topLeftFfp;
    topLeftFfp.Init(topLeftContext.StartedBackend(), topLeftContext.ShaderSet());
    topLeftFfp.SetRenderTargetActive(TRUE);
    topLeftFfp.DrawVertexBuffer(VX_TRIANGLELIST, 1, 0, 0, 3, 0, 0,
                                CKRST_DP_TR_CL_V, CKRST_DP_TR_CL_V, 1);
    matrices = topLeftContext.Log.FloatUniforms[topLeftContext.GetBlockUniformForTests(CKRST_BLOCK_MATRICES)];
    TestCheck(topLeftFfp.IsRenderTargetActive() && !topLeftFfp.RenderTargetOriginFlip() &&
                  matrices.size() >= 16 && matrices[5] == 1.0f &&
                  (topLeftContext.Log.LastState.Lo & CKRST_STATE_CULL(3)) == CKRST_STATE_CULL(2),
              "top-left backends render into targets without any flip");
    topLeftFfp.Shutdown();
}

void BorderColorReachesBackendUnmodified() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_ADDRESSU, VXTEXTURE_ADDRESSBORDER);
    ffp.SetTextureStageState(0, CKRST_TSS_BORDERCOLOR, 0x80402010u);
    ffp.SetTexture(0, 77, CKRST_TEXTURE_VALID);

    const CKBOOL first = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    const CKBOOL second = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(first && second,
              "A representable border color must submit normally");
    TestCheck(context.Log.LastTextureSampler.BorderColor == 0x80402010u,
              "The backend receives the original ARGB color, independent of its palette model");

    ffp.Shutdown();
}

void BorderColorsAreNotQuantizedByTranslation() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    TestCheck(ffp.Init(context.StartedBackend(), context.ShaderSet()), "FFP initialization");
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_ADDRESSU, VXTEXTURE_ADDRESSBORDER);
    ffp.SetTexture(0, 77, CKRST_TEXTURE_VALID);
    for (CKDWORD i = 0; i < 32; ++i) {
        const CKDWORD color = 0x80402000u | i;
        ffp.SetTextureStageState(0, CKRST_TSS_BORDERCOLOR, color);
        TestCheck(ffp.DrawVertexBuffer(VX_TRIANGLELIST, 1, 0, 0, 3, 0, 0,
                      CKRST_DP_CL_V, CKRST_DP_CL_V, 1), "Border color draw succeeds");
        TestCheck(context.Log.LastTextureSampler.BorderColor == color &&
                      ffp.GetApproximatedDrawCount(CKRST_DIAG_APPROX_BORDER_COLOR) == 0,
                  "The core preserves more than sixteen distinct border colors");
    }
    ffp.Shutdown();
}

void UnusedBorderTexturesDoNotConsumePaletteSlots() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(0, CKRST_TSS_ADDRESSU, VXTEXTURE_ADDRESSBORDER);
    ffp.SetTexture(0, 77, CKRST_TEXTURE_VALID);

    CKBOOL allDrawsSucceeded = TRUE;
    for (CKDWORD i = 0; i < 17; ++i) {
        ffp.SetTextureStageState(0, CKRST_TSS_BORDERCOLOR, 0xFF000000u | i);
        allDrawsSucceeded = allDrawsSucceeded && ffp.DrawVertexBuffer(
            VX_TRIANGLELIST,
            1, 0, 0, 3, 0, 0,
            CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    }

    TestCheck(allDrawsSucceeded && context.Log.DrawCount == 17,
              "Unused border textures must not reject otherwise valid draws");
    TestCheck(context.PaletteSetCount == 0,
              "Unused border textures must not consume border palette slots");
    TestCheck(context.Log.TextureBindCount == 0,
              "Unused textures must not reach the backend binding path");

    ffp.Shutdown();
}

void UntexturedStageKeepsRuntimeStageParams() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_CONSTANT);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_CONSTANT);
    ffp.SetTextureStageState(0, CKRST_TSS_CONSTANT, 0x80402010u);

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    const CKDWORD uniform = context.GetBlockUniformForTests(CKRST_BLOCK_STAGE_PARAMS);
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator params =
        context.Log.FloatUniforms.find(uniform);
    TestCheck(drawn && params != context.Log.FloatUniforms.end(),
              "An untextured constant stage must upload stage params");
    const CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);
    TestCheck(spec.GetStage(0, CKFF_SPEC_STAGE_COLOR_OP) == CKRST_TOP_SELECTARG1 &&
                  spec.Get(CKFF_SPEC_LAST_ACTIVE_TEXTURE_STAGE) == 0,
              "Texture binding span must not disable an active untextured stage");
    TestCheck(params != context.Log.FloatUniforms.end() &&
                  params->second.size() >= 8 &&
                  params->second[CKFFStageParamIndex(0, CKFF_STAGE_PARAM_COORD) * 4 + 2] == 0.0f &&
                  params->second[CKFFStageParamIndex(0, CKFF_STAGE_PARAM_CONSTANT) * 4 + 3] == 0x80 / 255.0f,
              "Untextured stage params must carry no texture and the stage constant");
    TestCheck(context.Log.TextureBindCount == 0,
              "An untextured constant stage must not create a texture binding");

    ffp.Shutdown();
}

void PremodulateImplicitTextureDependencyBindsNextStage() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_PREMODULATE);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(1, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(1, CKRST_TSS_ARG1, CKRST_TA_CURRENT);
    ffp.SetTextureStageState(1, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(1, CKRST_TSS_AARG1, CKRST_TA_CURRENT);
    ffp.SetTexture(1, 88, CKRST_TEXTURE_VALID);

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    TestCheck(drawn && context.Log.DrawCount == 1,
              "PREMODULATE implicit texture dependency must remain drawable");
    TestCheck(context.Log.TextureBindCount == 1 &&
                  context.Log.LastTextureHandle == 88,
              "PREMODULATE must bind the next-stage texture used by CURRENT");

    ffp.Shutdown();
}

void ProgramFamilyIsSharedAcrossStateBindings() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetRenderState(VXRENDERSTATE_SHADEMODE, VXSHADE_GOURAUD);
    const CKBOOL gouraud = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    ffp.SetRenderState(VXRENDERSTATE_SHADEMODE, VXSHADE_FLAT);
    const CKBOOL flat = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    const CKFFSpecializationInfo current = CurrentDrawSpecialization(ffp, context);
    TestCheck(gouraud && flat && context.CreatedProgramCount == 1,
              "State variants of one vertex layout must share one backend program");
    TestCheck(ffp.CachedProgramCount() == 1,
              "The shader cache must hold one program per created vertex variant");
    TestCheck(current.Get(CKFF_SPEC_FLAT_SHADE) == 1,
              "The shared program must receive the current-draw specialization data");

    ffp.Shutdown();
}

void DrawValidationCacheInvalidatesOnStateChanges() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    const CKBOOL firstDraw = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    const CKBOOL cachedDraw = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    TestCheck(firstDraw && cachedDraw && context.Log.DrawCount == 2,
              "unchanged valid draw state remains submit-ready");

    ffp.SetRenderState(VXRENDERSTATE_DITHERENABLE, TRUE);
    const CKBOOL firstApproximation = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    const CKBOOL cachedApproximation = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    TestCheck(firstApproximation && cachedApproximation &&
                  ffp.GetLastDrawApproximationMask() ==
                      (1ull << CKRST_DIAG_IGNORE_DITHER) &&
                  ffp.GetApproximatedDrawCount(CKRST_DIAG_IGNORE_DITHER) == 2 &&
                  context.Log.DrawCount == 4,
              "cached validation replays per-draw approximation diagnostics");
    ffp.SetRenderState(VXRENDERSTATE_DITHERENABLE, FALSE);

    ffp.SetRenderState(VXRENDERSTATE_FILLMODE, 99);
    TestCheck(!ffp.DrawVertexBuffer(
                  VX_TRIANGLELIST,
                  1, 0, 0, 3, 0, 0,
                  CKRST_DP_CL_V, CKRST_DP_CL_V, 1) &&
                  ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_STATE_VALUE &&
                  context.Log.DrawCount == 4,
              "render-state changes invalidate cached draw validation");

    ffp.SetRenderState(VXRENDERSTATE_FILLMODE, VXFILL_SOLID);
    ffp.SetTexture(0, 1, CKRST_TEXTURE_VALID);
    TestCheck(ffp.DrawVertexBuffer(
                  VX_TRIANGLELIST,
                  1, 0, 0, 3, 0, 0,
                  CKRST_DP_CL_V, CKRST_DP_CL_V, 1) &&
                  context.Log.DrawCount == 5,
              "restored draw state can be validated and cached again");

    ffp.SetTextureStageState(0, CKRST_TSS_MINFILTER, 99);
    TestCheck(!ffp.DrawVertexBuffer(
                  VX_TRIANGLELIST,
                  1, 0, 0, 3, 0, 0,
                  CKRST_DP_CL_V, CKRST_DP_CL_V, 1) &&
                  ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_STATE_VALUE &&
                  context.Log.DrawCount == 5,
              "texture-stage changes invalidate cached draw validation");

    ffp.Shutdown();
}

class ApproximationReportingBackend : public FFPRecordingBackend {
public:
    explicit ApproximationReportingBackend(CKRecordingRasterizerDriver *driver)
        : FFPRecordingBackend(driver) {}

    uint64_t GetDrawApproximationMask() const override {
        return 1ull << CKRST_DIAG_APPROX_BORDER_COLOR;
    }
};

void BackendDrawApproximationMaskIsPreserved() {
    FFPRecordingDriver driver;
    ApproximationReportingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    const CKBOOL drawn = ffp.DrawVertexBuffer(
        VX_TRIANGLELIST,
        1, 0, 0, 3, 0, 0,
        CKRST_DP_CL_V, CKRST_DP_CL_V, 1);
    TestCheck(drawn && context.Log.DrawCount == 1 &&
                  ffp.GetLastDrawApproximationMask() ==
                      (1ull << CKRST_DIAG_APPROX_BORDER_COLOR) &&
                  ffp.GetApproximatedDrawCount(
                      CKRST_DIAG_APPROX_BORDER_COLOR) == 1,
              "nonzero backend draw diagnostics survive the zero-mask fast path");

    ffp.Shutdown();
}

void PreparedCachesInvalidateEveryUniformAndProgramDependency() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    TestCheck(ffp.Init(context.StartedBackend(), context.ShaderSet()),
              "prepared-cache pipeline initialization");

    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_MODULATE);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG2, CKRST_TA_DIFFUSE);
    ffp.SetTexture(0, 77, CKRST_TEXTURE_VALID);
    TestCheck(ffp.DrawVertexBuffer(VX_TRIANGLELIST, 1, 0, 0, 3, 0, 0,
                                   CKRST_DP_CL_V, CKRST_DP_CL_V, 1),
              "initial prepared-cache draw");

    const uint64_t firstMatrices = ffp.GetConstantRevision(CKRST_BLOCK_MATRICES);
    const uint64_t firstStageParams = ffp.GetConstantRevision(CKRST_BLOCK_STAGE_PARAMS);
    const uint64_t firstDrawParams = ffp.GetConstantRevision(CKRST_BLOCK_DRAW_PARAMS);

    VxMatrix world;
    Vx3DMatrixIdentity(world);
    world[3][0] = 4.0f;
    ffp.SetTransform(VXMATRIX_WORLD, world);
    TestCheck(ffp.DrawVertexBuffer(VX_TRIANGLELIST, 1, 0, 0, 3, 0, 0,
                                   CKRST_DP_CL_V, CKRST_DP_CL_V, 1),
              "world-only prepared-cache draw");
    TestCheck(ffp.GetConstantRevision(CKRST_BLOCK_MATRICES) > firstMatrices &&
                  ffp.GetConstantRevision(CKRST_BLOCK_STAGE_PARAMS) == firstStageParams &&
                  ffp.GetConstantRevision(CKRST_BLOCK_DRAW_PARAMS) == firstDrawParams,
              "world-only changes update object matrices without rewriting static blocks");

    CKMaterialData material = {};
    material.Diffuse = VxColor(0.25f, 0.5f, 0.75f, 1.0f);
    ffp.SetMaterial(&material);
    TestCheck(ffp.DrawVertexBuffer(VX_TRIANGLELIST, 1, 0, 0, 3, 0, 0,
                                   CKRST_DP_CL_V, CKRST_DP_CL_V, 1),
              "material prepared-cache draw");
    TestCheck(ffp.GetConstantRevision(CKRST_BLOCK_DRAW_PARAMS) > firstDrawParams,
              "material changes invalidate the static draw-parameter block");

    const uint64_t stageBeforeFlags = ffp.GetConstantRevision(CKRST_BLOCK_STAGE_PARAMS);
    ffp.SetTexture(0, 77, CKRST_TEXTURE_VALID | CKRST_TEXTURE_BUMPLUMINANCE);
    TestCheck(ffp.DrawVertexBuffer(VX_TRIANGLELIST, 1, 0, 0, 3, 0, 0,
                                   CKRST_DP_CL_V, CKRST_DP_CL_V, 1),
              "texture-flag prepared-cache draw");
    const CKDWORD stageUniform = context.GetBlockUniformForTests(CKRST_BLOCK_STAGE_PARAMS);
    const std::vector<float> &stageParams = context.Log.FloatUniforms[stageUniform];
    TestCheck(ffp.GetConstantRevision(CKRST_BLOCK_STAGE_PARAMS) > stageBeforeFlags &&
                  stageParams.size() >= 4 &&
                  (((CKDWORD)stageParams[1]) & CKFF_TTF_BUMP_UNORM) != 0,
              "non-program texture flags invalidate and repack stage parameters");

    VxVector positions[3] = {
        VxVector(-1.0f, -1.0f, 0.0f),
        VxVector(1.0f, -1.0f, 0.0f),
        VxVector(0.0f, 1.0f, 0.0f)
    };
    VxDrawPrimitiveData data = {};
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_CL_V;
    data.PositionPtr = positions;
    data.PositionStride = sizeof(VxVector);
    ffp.SetTexture(0, 0);
    ffp.SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, FALSE);
    TestCheck(ffp.DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &data),
              "initial software prepared-cache draw");
    TestCheck(CurrentDrawSpecialization(ffp, context).Get(
                  CKFF_SPEC_ALPHA_TEST_ENABLED) == 0,
              "initial software specialization has alpha testing disabled");
    ffp.SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, TRUE);
    TestCheck(ffp.DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &data),
              "mutated software prepared-cache draw");
    TestCheck(CurrentDrawSpecialization(ffp, context).Get(
                  CKFF_SPEC_ALPHA_TEST_ENABLED) == 1,
              "program-affecting state invalidates the software prepared cache");

    ffp.Shutdown();
}

void ProgramFamilyHasFourVertexVariants() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    TestCheck(context.CreatedProgramCount == 0 && ffp.CachedProgramCount() == 0,
              "Programs are created on first use");

    ffp.DrawVertexBuffer(VX_TRIANGLELIST, 1, 0, 0, 3, 0, 0,
                         CKRST_DP_TR_CL_V, CKRST_DP_TR_CL_V, 1);
    ffp.DrawVertexBuffer(VX_TRIANGLELIST, 1, 0, 0, 3, 0, 0,
                         CKRST_DP_VCT, CKFF_VF_POSITIONT | CKFF_VF_TEXCOORD0 | CKFF_VF_COLOR0, 1);
    TestCheck(context.CreatedProgramCount == 2,
              "3D and POSITIONT draws use two vertex variants");

    VxPlane plane;
    plane.m_Normal = VxVector(0.0f, 1.0f, 0.0f);
    plane.m_D = 0.5f;
    ffp.SetUserClipPlane(0, plane);
    ffp.SetRenderState(VXRENDERSTATE_CLIPPLANEENABLE, 1u);
    ffp.DrawVertexBuffer(VX_TRIANGLELIST, 1, 0, 0, 3, 0, 0,
                         CKRST_DP_TR_CL_V, CKRST_DP_TR_CL_V, 1);
    ffp.DrawVertexBuffer(VX_TRIANGLELIST, 1, 0, 0, 3, 0, 0,
                         CKRST_DP_VCT, CKFF_VF_POSITIONT | CKFF_VF_TEXCOORD0 | CKFF_VF_COLOR0, 1);
    TestCheck(context.CreatedProgramCount == 4 &&
                  ffp.CachedProgramCount() == CKFF_PROGRAM_VARIANT_COUNT,
              "User clip planes add the two clip-distance vertex variants");

    ffp.SetTexture(0, 77, CKRST_TEXTURE_VALID | CKRST_TEXTURE_CUBEMAP);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_MODULATE);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG2, CKRST_TA_DIFFUSE);
    ffp.SetRenderState(VXRENDERSTATE_FOGENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, TRUE);
    ffp.DrawVertexBuffer(VX_TRIANGLELIST, 1, 0, 0, 3, 0, 0,
                         CKRST_DP_TR_CL_V, CKRST_DP_TR_CL_V, 1);
    TestCheck(context.CreatedProgramCount == 4 && context.Log.DrawCount == 5,
              "Texture, fog and alpha-test state never create additional programs");

    ffp.Shutdown();
}

void LegacyStageBlendZeroTerminatesStaleMultitextureState() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    VxVector positions[4] = {
        VxVector(-1.0f, -1.0f, 0.0f),
        VxVector( 1.0f, -1.0f, 0.0f),
        VxVector( 1.0f,  1.0f, 0.0f),
        VxVector(-1.0f,  1.0f, 0.0f)
    };
    Vx2DVector uvs[4] = {
        Vx2DVector(0.0f, 0.0f),
        Vx2DVector(1.0f, 0.0f),
        Vx2DVector(1.0f, 1.0f),
        Vx2DVector(0.0f, 1.0f)
    };
    CKDWORD colors[4] = {
        0x80FFFFFFu,
        0x80FFFFFFu,
        0x80FFFFFFu,
        0x80FFFFFFu
    };
    CKWORD indices[6] = {0, 1, 2, 0, 2, 3};
    VxDrawPrimitiveData data = {};
    data.VertexCount = 4;
    data.Flags = CKRST_DP_TR_CL_VCT;
    data.PositionPtr = positions;
    data.PositionStride = sizeof(VxVector);
    data.TexCoordPtr = uvs;
    data.TexCoordStride = sizeof(Vx2DVector);
    data.ColorPtr = colors;
    data.ColorStride = sizeof(CKDWORD);

    ffp.SetTexture(0, 101);
    ffp.SetTexture(1, 202);
    ffp.SetTextureStageState(1, CKRST_TSS_OP, CKRST_TOP_ADD);
    ffp.SetTextureStageState(1, CKRST_TSS_ARG1, CKRST_TA_CURRENT);
    ffp.SetTextureStageState(1, CKRST_TSS_ARG2, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(1, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(1, CKRST_TSS_AARG1, CKRST_TA_CURRENT);

    ffp.SetTexture(0, 303);
    ffp.SetTextureStageState(0, CKRST_TSS_TEXTUREMAPBLEND, VXTEXTUREBLEND_MODULATEALPHA);
    ffp.SetTextureStageState(1, CKRST_TSS_STAGEBLEND, 0);

    ffp.DrawPrimitive(VX_TRIANGLELIST, indices, 6, &data);

    TestCheck(context.Log.DrawCount == 1,
              "Particle-like draw must submit once");
    TestCheck(context.Log.TextureBindCount == 1,
              "STAGEBLEND zero on stage 1 must suppress stale stage 1 texture binding");
    TestCheck(context.Log.LastTextureStage == 0 &&
                  context.Log.LastTextureHandle == 303,
              "Particle-like draw must bind only its current stage 0 texture");

    ffp.Shutdown();
}

void LegacyTextureMapBlendClearsExplicitStageOps() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    VxVector positions[3] = {
        VxVector(-1.0f, -1.0f, 0.0f),
        VxVector( 1.0f, -1.0f, 0.0f),
        VxVector( 0.0f,  1.0f, 0.0f)
    };
    Vx2DVector uvs[3] = {
        Vx2DVector(0.0f, 0.0f),
        Vx2DVector(1.0f, 0.0f),
        Vx2DVector(0.5f, 1.0f)
    };
    CKDWORD colors[3] = {
        0x80FFFFFFu,
        0x80FFFFFFu,
        0x80FFFFFFu
    };
    VxDrawPrimitiveData data = {};
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TR_CL_VCT;
    data.PositionPtr = positions;
    data.PositionStride = sizeof(VxVector);
    data.TexCoordPtr = uvs;
    data.TexCoordStride = sizeof(Vx2DVector);
    data.ColorPtr = colors;
    data.ColorStride = sizeof(CKDWORD);

    ffp.SetTexture(0, 101);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_ADD);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_CURRENT);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG2, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_CURRENT);

    ffp.SetTextureStageState(0, CKRST_TSS_TEXTUREMAPBLEND, VXTEXTUREBLEND_MODULATEALPHA);
    ffp.DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &data);

    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);

    TestCheck(spec.GetStage(0, CKFF_SPEC_STAGE_COLOR_OP) == CKRST_TOP_MODULATE,
              "TEXTUREMAPBLEND must restore legacy modulate color op over stale explicit op");
    TestCheck(spec.GetStage(0, CKFF_SPEC_STAGE_ALPHA_OP) == CKRST_TOP_MODULATE,
              "TEXTUREMAPBLEND must restore legacy modulate alpha op over stale explicit op");

    ffp.Shutdown();
}

void PointSpriteDrawPrimitiveExpandsToTriangleList() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    VxVector position(0.0f, 0.0f, 0.0f);
    Vx2DVector uv(0.25f, 0.75f);
    VxDrawPrimitiveData data = {};
    data.VertexCount = 1;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_CL_V;
    data.PositionPtr = &position;
    data.PositionStride = sizeof(VxVector);
    data.TexCoordPtr = &uv;
    data.TexCoordStride = sizeof(Vx2DVector);

    const float pointSize = 2.0f;
    ffp.SetRenderState(VXRENDERSTATE_POINTSPRITEENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_POINTSIZE, FloatStageState(pointSize));
    ffp.SetTexture(0, 100);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(
        0, CKRST_TSS_TEXCOORDINDEX,
        CKFFPackTexcoordIndex(3, CKFF_TEXGEN_CAMERASPACEPOSITION));
    ffp.SetTextureStageState(
        0, CKRST_TSS_TEXTURETRANSFORMFLAGS,
        CKRST_TTF_COUNT3 | CKRST_TTF_PROJECTED);

    ffp.DrawPrimitive(VX_POINTLIST, nullptr, 1, &data);

    const CKDWORD stride = 36;
    TestCheck(context.Log.DrawCount == 1,
              "Point sprite draw must submit once");
    TestCheck(DrawStateTopology(context.Log.LastState) == VX_TRIANGLELIST,
              "Point sprite draw state must submit triangle-list topology");
    TestCheck(context.Log.LastVertexBytes.size() == stride * 4,
              "One point sprite must expand to four transient vertices");
    TestCheck(context.Log.LastIndexBytes.size() == sizeof(CKWORD) * 6,
              "One point sprite must expand to six transient indices");

    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);
    TestCheck(spec.GetStage(0, CKFF_SPEC_STAGE_PROJECTED) == 0,
              "point sprite sampling must bypass projected texture coordinates");
    const CKDWORD stageUniform = context.GetBlockUniformForTests(CKRST_BLOCK_STAGE_PARAMS);
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator packedStage =
        context.Log.FloatUniforms.find(stageUniform);
    const int coord = CKFFStageParamIndex(0, CKFF_STAGE_PARAM_COORD) * 4;
    TestCheck(packedStage != context.Log.FloatUniforms.end() &&
                  packedStage->second.size() > (size_t)(coord + 1) &&
                  packedStage->second[coord + 0] == 0.0f &&
                  packedStage->second[coord + 1] == 0.0f,
              "point sprite runtime params must bypass texgen and texture matrices");

    float uv0[2], uv1[2], uv2[2], uv3[2];
    memcpy(uv0, &context.Log.LastVertexBytes[12], sizeof(uv0));
    memcpy(uv1, &context.Log.LastVertexBytes[stride + 12], sizeof(uv1));
    memcpy(uv2, &context.Log.LastVertexBytes[stride * 2 + 12], sizeof(uv2));
    memcpy(uv3, &context.Log.LastVertexBytes[stride * 3 + 12], sizeof(uv3));
    TestCheck(uv0[0] == 0.0f && uv0[1] == 0.0f,
              "Point sprite vertex 0 must use UV (0,0)");
    TestCheck(uv1[0] == 1.0f && uv1[1] == 0.0f,
              "Point sprite vertex 1 must use UV (1,0)");
    TestCheck(uv2[0] == 1.0f && uv2[1] == 1.0f,
              "Point sprite vertex 2 must use UV (1,1)");
    TestCheck(uv3[0] == 0.0f && uv3[1] == 1.0f,
              "Point sprite vertex 3 must use UV (0,1)");

    ffp.Shutdown();
}

void PointSizeExpandsWithoutSpriteTexcoordReplacement() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    VxVector position(0.0f, 0.0f, 0.0f);
    Vx2DVector uv(0.25f, 0.75f);
    VxDrawPrimitiveData data = {};
    data.VertexCount = 1;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_CL_V;
    data.PositionPtr = &position;
    data.PositionStride = sizeof(VxVector);
    data.TexCoordPtr = &uv;
    data.TexCoordStride = sizeof(Vx2DVector);

    ffp.SetRenderState(VXRENDERSTATE_POINTSPRITEENABLE, FALSE);
    ffp.SetRenderState(VXRENDERSTATE_POINTSIZE, FloatStageState(2.0f));
    TestCheck(ffp.DrawPrimitive(VX_POINTLIST,
                                NULL, 0, &data) == TRUE,
              "point size must expand point primitives without sprite mode");
    TestCheck(DrawStateTopology(context.Log.LastState) == VX_TRIANGLELIST,
              "expanded point primitives must submit triangle-list topology");
    const CKDWORD stride = 36;
    TestCheck(context.Log.LastVertexBytes.size() == stride * 4,
              "point size must expand one point to four vertices");
    if (context.Log.LastVertexBytes.size() == stride * 4) {
        for (CKDWORD vertex = 0; vertex < 4; ++vertex) {
            float expandedUV[2] = {};
            memcpy(expandedUV,
                   &context.Log.LastVertexBytes[vertex * stride + 12],
                   sizeof(expandedUV));
            TestCheck(fabsf(expandedUV[0] - 0.25f) < 0.0001f &&
                          fabsf(expandedUV[1] - 0.75f) < 0.0001f,
                      "sprite-disabled point expansion must duplicate source texcoords");
        }
    }
    ffp.Shutdown();
}

void PersistentPointBuffersRequireTransientExpansionForComplexSizes() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetRenderState(VXRENDERSTATE_POINTSIZE, FloatStageState(4.0f));
    TestCheck(ffp.DrawVertexBuffer(VX_POINTLIST,
                                   1, 0, 0, 1, 0, 0,
                                   CKRST_DP_CL_V, CKRST_DP_CL_V, 1) == TRUE,
              "integer constant point size must submit through native point state");
    TestCheck(context.Log.PointSizeSetCount == 1 &&
                  context.Log.LastPointSize == 4.0f &&
                  ffp.GetLastDrawApproximationMask() == 0,
              "persistent point buffers must submit their exact constant point size");

    ffp.SetRenderState(VXRENDERSTATE_POINTSPRITEENABLE, TRUE);
    TestCheck(ffp.DrawVertexBuffer(VX_POINTLIST,
                                   1, 0, 0, 1, 0, 0,
                                   CKRST_DP_CL_V, CKRST_DP_CL_V, 1) == FALSE,
              "direct backend-buffer point sprite draw requires transient expansion");
    TestCheck(ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_PREPARE_FAILED,
              "direct point sprite path must reject instead of approximating");

    ffp.SetRenderState(VXRENDERSTATE_POINTSPRITEENABLE, FALSE);
    ffp.SetRenderState(VXRENDERSTATE_POINTSIZE, FloatStageState(1.5f));
    TestCheck(ffp.DrawVertexBuffer(VX_POINTLIST,
                                   1, 0, 0, 1, 0, 0,
                                   CKRST_DP_CL_V, CKRST_DP_CL_V, 1) == FALSE &&
                  ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_PREPARE_FAILED,
              "fractional direct point size requires transient expansion");

    ffp.SetRenderState(VXRENDERSTATE_POINTSIZE, FloatStageState(40.0f));
    TestCheck(ffp.DrawVertexBuffer(VX_POINTLIST,
                                   1, 0, 0, 1, 0, 0,
                                   CKRST_DP_CL_V, CKRST_DP_CL_V, 1) == FALSE &&
                  ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_PREPARE_FAILED,
              "oversized direct point size requires transient expansion");

    ffp.Shutdown();
}

void WrappedLineStripSubmitsAsLineList() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    VxVector positions[3] = {
        VxVector(0.0f, 0.0f, 0.0f),
        VxVector(1.0f, 0.0f, 0.0f),
        VxVector(2.0f, 0.0f, 0.0f)
    };
    Vx2DVector texcoords[3] = {
        Vx2DVector(0.9f, 0.0f),
        Vx2DVector(0.1f, 0.0f),
        Vx2DVector(0.8f, 0.0f)
    };
    VxDrawPrimitiveData data = {};
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_CL_V | CKRST_DP_STAGES1;
    data.PositionPtr = positions;
    data.PositionStride = sizeof(VxVector);
    data.TexCoordPtrs[0] = texcoords;
    data.TexCoordStrides[0] = sizeof(Vx2DVector);

    ffp.SetRenderState(VXRENDERSTATE_WRAP1, VXWRAP_U);
    TestCheck(ffp.DrawPrimitive(VX_LINESTRIP,
                                NULL, 0, &data) == TRUE,
              "Wrapped line strip draw must submit");
    TestCheck(DrawStateTopology(context.Log.LastState) == VX_LINELIST,
              "Wrapped line strip must submit its expanded line-list topology");

    data.TexCoordPtrs[0] = NULL;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_CL_V;
    TestCheck(ffp.DrawPrimitive(VX_LINESTRIP,
                                NULL, 0, &data) == TRUE,
              "Line strip without texcoords must still submit");
    TestCheck(DrawStateTopology(context.Log.LastState) == VX_LINESTRIP,
              "Inactive WRAP state must not change line-strip topology");

    ffp.Shutdown();
}

void PointSpriteUsesPerVertexPointSize() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    struct PointVertex {
        float x, y, z;
        float size;
    };
    PointVertex vertex = {0.0f, 0.0f, 0.0f, 6.0f};
    Vx2DVector uv(0.25f, 0.75f);
    VxDrawPrimitiveData data = {};
    data.VertexCount = 1;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_CL_V | CKRST_DP_PSIZE;
    data.PositionPtr = &vertex;
    data.PositionStride = sizeof(PointVertex);
    data.TexCoordPtr = &uv;
    data.TexCoordStride = sizeof(Vx2DVector);

    ffp.SetRenderState(VXRENDERSTATE_POINTSPRITEENABLE, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_POINTSIZE, FloatStageState(2.0f));
    ffp.SetRenderState(VXRENDERSTATE_POINTSIZE_MIN, FloatStageState(1.0f));
    ffp.SetRenderState(VXRENDERSTATE_POINTSIZE_MAX, FloatStageState(4.0f));
    CKViewportData viewport;
    viewport.ViewWidth = 100;
    viewport.ViewHeight = 50;
    ffp.SetViewport(viewport);

    ffp.DrawPrimitive(VX_POINTLIST, nullptr, 1, &data);

    const CKDWORD stride = 36;
    TestCheck(context.Log.LastVertexBytes.size() == stride * 4,
              "Per-vertex point sprite must expand to four transient vertices");
    if (context.Log.LastVertexBytes.size() == stride * 4) {
        float p0[3] = {};
        float p1[3] = {};
        memcpy(p0, &context.Log.LastVertexBytes[0], sizeof(p0));
        memcpy(p1, &context.Log.LastVertexBytes[stride], sizeof(p1));
        TestCheck(fabsf(p0[0] + 0.04f) < 0.001f && fabsf(p1[0] - 0.04f) < 0.001f,
                  "Per-vertex point size must produce four screen pixels after max clamp");
        TestCheck(fabsf(p0[1] - p1[1]) < 0.001f,
                  "Adjacent point sprite corners must stay on the same edge");
    }

    ffp.Shutdown();
}

void ProjectedSamplerStagesZeroToThreeEnterSpecializationMask() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_DIFFUSE);
    ffp.SetTextureStageState(1, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(1, CKRST_TSS_ARG1, CKRST_TA_CURRENT);
    ffp.SetTextureStageState(2, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(2, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(2, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_PROJECTED);
    ffp.SetTexture(2, 102, CKRST_TEXTURE_VALID);

    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);

    TestCheck(spec.GetStage(2, CKFF_SPEC_STAGE_PROJECTED) == 1,
              "Stage 2 projected sampler must be encoded in the specialization mask");

    ffp.Shutdown();
}

void ProjectedSamplerStageFourEntersSpecialization() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_DIFFUSE);
    for (int stage = 1; stage < 4; ++stage) {
        ffp.SetTextureStageState(stage, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
        ffp.SetTextureStageState(stage, CKRST_TSS_ARG1, CKRST_TA_CURRENT);
    }
    ffp.SetTextureStageState(4, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
    ffp.SetTextureStageState(4, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(4, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_PROJECTED);
    ffp.SetTexture(4, 104, CKRST_TEXTURE_VALID);

    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    CKFFSpecializationInfo spec = CurrentDrawSpecialization(ffp, context);

    const CKDWORD stageParamsUniform = context.GetBlockUniformForTests(CKRST_BLOCK_STAGE_PARAMS);
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator stageParams =
        context.Log.FloatUniforms.find(stageParamsUniform);

    TestCheck(spec.GetStage(4, CKFF_SPEC_STAGE_PROJECTED) == 1 &&
                  spec.GetStage(3, CKFF_SPEC_STAGE_PROJECTED) == 0,
              "Stage 4 projected sampler must be encoded in its own specialization field");
    TestCheck(stageParams != context.Log.FloatUniforms.end() &&
                  stageParams->second.size() >= CKFF_STAGE_PARAM_VEC4_COUNT * 4 &&
                  stageParams->second[CKFFStageParamIndex(4, CKFF_STAGE_PARAM_COORD) * 4 + 1] == (float)CKRST_TTF_PROJECTED,
              "Stage 4 stage params must preserve the projected transform flag for the vertex shader");

    ffp.Shutdown();
}

void DrawUploadsPerStageBumpEnvUniforms() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetTexture(0, 100, CKRST_TEXTURE_VALID | CKRST_TEXTURE_BUMPDUDV);
    ffp.SetTexture(1, 101);
    ffp.SetTexture(2, 102, CKRST_TEXTURE_VALID | CKRST_TEXTURE_BUMPDUDV);
    ffp.SetTexture(3, 103);
    ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_BUMPENVMAP);
    ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(0, CKRST_TSS_BUMPENVMAT00, FloatStageState(1.0f));
    ffp.SetTextureStageState(0, CKRST_TSS_BUMPENVMAT01, FloatStageState(2.0f));
    ffp.SetTextureStageState(0, CKRST_TSS_BUMPENVMAT10, FloatStageState(3.0f));
    ffp.SetTextureStageState(0, CKRST_TSS_BUMPENVMAT11, FloatStageState(4.0f));
    ffp.SetTextureStageState(0, CKRST_TSS_BUMPENVLSCALE, FloatStageState(5.0f));
    ffp.SetTextureStageState(0, CKRST_TSS_BUMPENVLOFFSET, FloatStageState(6.0f));

    ffp.SetTextureStageState(2, CKRST_TSS_OP, CKRST_TOP_BUMPENVMAP);
    ffp.SetTextureStageState(2, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
    ffp.SetTextureStageState(2, CKRST_TSS_BUMPENVMAT00, FloatStageState(7.0f));
    ffp.SetTextureStageState(2, CKRST_TSS_BUMPENVMAT01, FloatStageState(8.0f));
    ffp.SetTextureStageState(2, CKRST_TSS_BUMPENVMAT10, FloatStageState(9.0f));
    ffp.SetTextureStageState(2, CKRST_TSS_BUMPENVMAT11, FloatStageState(10.0f));
    ffp.SetTextureStageState(2, CKRST_TSS_BUMPENVLSCALE, FloatStageState(11.0f));
    ffp.SetTextureStageState(2, CKRST_TSS_BUMPENVLOFFSET, FloatStageState(12.0f));

    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKRST_DP_CL_V, 1);

    const CKDWORD bumpUniform = context.GetBlockUniformForTests(CKRST_BLOCK_BUMP_ENV);
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator bump =
        context.Log.FloatUniforms.find(bumpUniform);

    TestCheck(bump != context.Log.FloatUniforms.end(),
              "Bump env draws must upload bump env uniforms");
    TestCheck(context.Log.UniformCounts[bumpUniform] == CKFF_MAX_TEXTURE_STAGES * 2,
              "Bump env uniform upload must include every stage");
    TestCheck(bump->second.size() >= CKFF_MAX_TEXTURE_STAGES * 2 * 4,
              "Bump env uniform data must contain every stage slot");
    TestCheck(bump->second[0] == 1.0f && bump->second[1] == 2.0f &&
                  bump->second[2] == 3.0f && bump->second[3] == 4.0f,
              "Stage 0 bump env data must occupy slots 0 and 1");
    TestCheck(bump->second[16] == 7.0f && bump->second[17] == 8.0f &&
                  bump->second[18] == 9.0f && bump->second[19] == 10.0f,
              "Stage 2 bump env data must occupy slots 4 and 5");

    ffp.Shutdown();
}

void UnsupportedBumpInputsApproximateWithDiagnostics() {
    {
        FFPRecordingDriver driver;
        FFPRecordingBackend context(&driver);
        CKFixedFunctionPipeline ffp;
        ffp.Init(context.StartedBackend(), context.ShaderSet());
        ffp.SetTexture(0, 100, CKRST_TEXTURE_VALID);
        ffp.SetTexture(1, 101);
        ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_BUMPENVMAP);
        ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
        TestCheck(ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                                       1, 0, 0, 3, 0, 0,
                                       CKRST_DP_CL_V, CKRST_DP_CL_V, 1),
                  "unsigned textures in bump mapping draw as ordinary textures");
        TestCheck(ffp.GetLastDrawApproximationMask() == (1ull << CKRST_DIAG_APPROX_BUMP_TEXTURE_FLAGS),
                  "unsigned bump mapping must report the bump texture approximation");
        ffp.Shutdown();
    }
    {
        FFPRecordingDriver driver;
        FFPRecordingBackend context(&driver);
        CKFixedFunctionPipeline ffp;
        ffp.Init(context.StartedBackend(), context.ShaderSet());
        ffp.SetTexture(0, 100, CKRST_TEXTURE_VALID | CKRST_TEXTURE_BUMPDUDV);
        ffp.SetTexture(1, 101);
        ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_BUMPENVMAPLUMINANCE);
        ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
        TestCheck(ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                                       1, 0, 0, 3, 0, 0,
                                       CKRST_DP_CL_V, CKRST_DP_CL_V, 1),
                  "luminance bump mapping without a luminance channel still draws");
        TestCheck(ffp.GetLastDrawApproximationMask() == (1ull << CKRST_DIAG_APPROX_BUMP_TEXTURE_FLAGS),
                  "missing luminance bump channel must report the bump texture approximation");
        ffp.Shutdown();
    }
    {
        FFPRecordingDriver driver;
        FFPRecordingBackend context(&driver);
        CKFixedFunctionPipeline ffp;
        ffp.Init(context.StartedBackend(), context.ShaderSet());
        ffp.SetTexture(0, 100, CKRST_TEXTURE_VALID |
                              CKRST_TEXTURE_BUMPDUDV |
                              CKRST_TEXTURE_BUMPLUMINANCE);
        ffp.SetTexture(1, 101);
        ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_BUMPENVMAPLUMINANCE);
        ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
        TestCheck(ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                                       1, 0, 0, 3, 0, 0,
                                       CKRST_DP_CL_V, CKRST_DP_CL_V, 1),
                  "packed luminance bump mapping must submit");
        TestCheck(ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_NONE &&
                      ffp.GetLastDrawApproximationMask() == 0,
                  "supported luminance bump mapping must report neither rejection nor approximation");
        ffp.Shutdown();
    }
    {
        FFPRecordingDriver driver;
        FFPRecordingBackend context(&driver);
        CKFixedFunctionPipeline ffp;
        ffp.Init(context.StartedBackend(), context.ShaderSet());
        ffp.SetTexture(0, 100, CKRST_TEXTURE_VALID | CKRST_TEXTURE_BUMPDUDV);
        ffp.SetTextureStageState(0, CKRST_TSS_OP, CKRST_TOP_SELECTARG1);
        ffp.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_DIFFUSE);
        ffp.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_BUMPENVMAP);
        TestCheck(!ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                                       1, 0, 0, 3, 0, 0,
                                       CKRST_DP_CL_V, CKRST_DP_CL_V, 1),
                  "BUMPENVMAP on the alpha channel is rejected");
        TestCheck(ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_TEXTURE_OP &&
                      ffp.GetLastDrawApproximationMask() == 0,
                  "alpha bump mapping has no silent substitute");
        ffp.Shutdown();
    }
}

bool LayoutHasAttrib(const std::vector<CKVertexElementDesc> &elements, CK_VERTEX_ATTRIB attrib) {
    for (size_t i = 0; i < elements.size(); ++i) {
        if (elements[i].Attrib == attrib)
            return true;
    }
    return false;
}

CKDWORD LayoutAttribCount(const std::vector<CKVertexElementDesc> &elements, CK_VERTEX_ATTRIB attrib) {
    for (size_t i = 0; i < elements.size(); ++i) {
        if (elements[i].Attrib == attrib)
            return elements[i].Count;
    }
    return 0;
}

void PositionTTextureTransformDoesNotUploadTextureMatrix() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    const CKDWORD texMatrixUniform = context.GetBlockUniformForTests(CKRST_BLOCK_TEX_MATRICES);

    VxMatrix texMatrix;
    texMatrix.SetIdentity();
    texMatrix[0][0] = 2.0f;
    ffp.SetTransform(VXMATRIX_TEXTURE0, texMatrix);
    ffp.SetTextureStageState(0, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_COUNT2);

    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_VCT, CKFF_VF_POSITIONT | CKFF_VF_TEXCOORD0 | CKFF_VF_COLOR0, 1);

    TestCheck(context.Log.FloatUniforms.find(texMatrixUniform) == context.Log.FloatUniforms.end(),
              "POSITIONT texture transform flags must not upload or use texture matrices");

    ffp.Shutdown();
}

void TextureTransformCountOneUploadsTextureMatrix() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    const CKDWORD texMatrixUniform = context.GetBlockUniformForTests(CKRST_BLOCK_TEX_MATRICES);

    VxMatrix texMatrix;
    texMatrix.SetIdentity();
    texMatrix[3][0] = 0.25f;
    ffp.SetTransform(VXMATRIX_TEXTURE0, texMatrix);
    ffp.SetTexcoordComponentCount(0, 1);
    ffp.SetTexture(0, 1);
    ffp.SetTextureStageState(0, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_COUNT1);

    VxVector positions[3] = {};
    float texcoords[3] = {0.0f, 0.5f, 1.0f};
    VxDrawPrimitiveData data = {};
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_STAGES0;
    data.PositionPtr = positions;
    data.PositionStride = sizeof(VxVector);
    data.TexCoordPtr = texcoords;
    data.TexCoordStride = sizeof(float);

    TestCheck(ffp.DrawPrimitive(VX_TRIANGLELIST,
                                nullptr, 0, &data) == TRUE,
              "COUNT1 texture transform draw must submit");
    TestCheck(context.Log.UniformCounts[texMatrixUniform] == 1,
              "COUNT1 texture transform must upload its matrix");

    ffp.Shutdown();
}

void LegacyTexcoordComponentCountReadsOnlyXY() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    VxVector positions[3] = {};
    float texcoords[3][4] = {
        {0.25f, 0.50f, 0.75f, 1.25f},
        {0.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 0.0f}
    };
    VxDrawPrimitiveData data = {};
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_CL_V | CKRST_DP_STAGES0;
    data.PositionPtr = positions;
    data.PositionStride = sizeof(VxVector);
    data.TexCoordPtr = texcoords;
    data.TexCoordStride = sizeof(texcoords[0]);

    ffp.DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &data);

    TestCheck(LayoutAttribCount(context.LastVertexLayoutElements, CKRST_ATTRIB_TEXCOORD0) == 4,
              "Legacy texcoords must still use the canonical float4 transient layout");
    TestCheck(context.Log.LastVertexBytes.size() >= 28,
              "Transient vertex bytes must contain position and float4 texcoord");
    if (context.Log.LastVertexBytes.size() >= 28) {
        float packed[4] = {};
        memcpy(packed, &context.Log.LastVertexBytes[12], sizeof(packed));
        TestCheck(packed[0] == 0.25f && packed[1] == 0.50f &&
                      packed[2] == 0.0f && packed[3] == 0.0f,
                  "Default DrawPrimitive texcoords must read only legacy xy components");
    }

    ffp.Shutdown();
}

void PipelineTexcoordComponentCountPreservesSourceZW() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    VxVector positions[3] = {};
    float texcoords[3][4] = {
        {0.25f, 0.50f, 0.75f, 1.25f},
        {0.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 0.0f}
    };
    VxDrawPrimitiveData data = {};
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_CL_V | CKRST_DP_STAGES0;
    data.PositionPtr = positions;
    data.PositionStride = sizeof(VxVector);
    data.TexCoordPtr = texcoords;
    data.TexCoordStride = sizeof(texcoords[0]);

    ffp.SetTexcoordComponentCount(0, 4);
    ffp.DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &data);

    TestCheck(LayoutAttribCount(context.LastVertexLayoutElements, CKRST_ATTRIB_TEXCOORD0) == 4,
              "Explicit 4-component texcoords must keep the float4 transient layout");
    TestCheck(context.Log.LastVertexBytes.size() >= 28,
              "Transient vertex bytes must contain position and float4 texcoord");
    if (context.Log.LastVertexBytes.size() >= 28) {
        float packed[4] = {};
        memcpy(packed, &context.Log.LastVertexBytes[12], sizeof(packed));
        TestCheck(packed[0] == 0.25f && packed[1] == 0.50f &&
                      packed[2] == 0.75f && packed[3] == 1.25f,
                  "Pipeline texcoord component state must preserve source z/w components");
    }

    ffp.Shutdown();
}

void InvalidTexcoordComponentCountFallsBackToLegacyXY() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    VxVector positions[3] = {};
    float texcoords[3][4] = {
        {0.25f, 0.50f, 0.75f, 1.25f},
        {0.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 0.0f}
    };
    VxDrawPrimitiveData data = {};
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_CL_V | CKRST_DP_STAGES0;
    data.PositionPtr = positions;
    data.PositionStride = sizeof(VxVector);
    data.TexCoordPtr = texcoords;
    data.TexCoordStride = sizeof(texcoords[0]);

    ffp.SetTexcoordComponentCount(0, 5);
    ffp.DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &data);

    TestCheck(context.Log.LastVertexBytes.size() >= 28,
              "Invalid texcoord count draw must produce transient bytes");
    if (context.Log.LastVertexBytes.size() >= 28) {
        float packed[4] = {};
        memcpy(packed, &context.Log.LastVertexBytes[12], sizeof(packed));
        TestCheck(packed[0] == 0.25f && packed[1] == 0.50f &&
                      packed[2] == 0.0f && packed[3] == 0.0f,
                  "Invalid texcoord component count must fall back to legacy xy");
    }

    ffp.SetTexcoordComponentCount(0, 0);
    ffp.DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &data);

    TestCheck(context.Log.LastVertexBytes.size() >= 28,
              "Zero texcoord count draw must produce transient bytes");
    if (context.Log.LastVertexBytes.size() >= 28) {
        float packed[4] = {};
        memcpy(packed, &context.Log.LastVertexBytes[12], sizeof(packed));
        TestCheck(packed[0] == 0.25f && packed[1] == 0.50f &&
                      packed[2] == 0.0f && packed[3] == 0.0f,
                  "Zero texcoord component count must fall back to legacy xy");
    }

    ffp.Shutdown();
}

void SimpleDrawPrimitiveDataUsesLegacyTexcoordPath() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    VxVector positions[3] = {};
    float texcoords[3][4] = {
        {0.25f, 0.50f, 0.75f, 1.25f},
        {0.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 0.0f}
    };
    VxDrawPrimitiveDataSimple data = {};
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_CL_V | CKRST_DP_STAGES0;
    data.PositionPtr = positions;
    data.PositionStride = sizeof(VxVector);
    data.TexCoordPtr = texcoords;
    data.TexCoordStride = sizeof(texcoords[0]);

    ffp.DrawPrimitive(VX_TRIANGLELIST, nullptr, 0,
                      (VxDrawPrimitiveData *)&data);

    TestCheck(context.Log.DrawCount == 1,
              "VxDrawPrimitiveDataSimple cast path must submit normally");
    TestCheck(context.Log.LastVertexBytes.size() >= 28,
              "Simple draw data path must produce transient bytes");
    if (context.Log.LastVertexBytes.size() >= 28) {
        float packed[4] = {};
        memcpy(packed, &context.Log.LastVertexBytes[12], sizeof(packed));
        TestCheck(packed[0] == 0.25f && packed[1] == 0.50f &&
                      packed[2] == 0.0f && packed[3] == 0.0f,
                  "Simple draw data path must not read extended texcoord metadata");
    }

    ffp.Shutdown();
}

void VertexBlendZeroWeightsUploadsMatrixPalette() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetRenderState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_0WEIGHTS);
    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKFF_VF_POSITION | CKFF_VF_BLENDWEIGHT, 1);

    const CKDWORD matrixUniform = context.GetBlockUniformForTests(CKRST_BLOCK_MATRICES);
    const CKDWORD paletteUniform = context.GetBlockUniformForTests(CKRST_BLOCK_VERTEX_BLEND_MATRICES);
    TestCheck(context.Log.UniformCounts[matrixUniform] == 4,
              "Normal vertex blend must keep base matrices separate from matrix palette");
    TestCheck(context.Log.UniformCounts[paletteUniform] == CKFF_VERTEX_BLEND_MATRIX_COUNT,
              "Normal vertex blend must upload the dedicated matrix palette uniform");
    const std::vector<float> &palette = context.Log.FloatUniforms[paletteUniform];
    for (int matrix = 1; matrix < CKFF_VERTEX_BLEND_MATRIX_COUNT; ++matrix)
        for (int element = 0; element < 16; ++element)
            TestCheck(palette[matrix * 16 + element] == (element % 5 == 0 ? 1.0f : 0.0f),
                      "unset vertex blend matrices must upload identity, not uninitialized storage");

    ffp.Shutdown();
}

void VertexBlendUploadsWorldMatrixPaletteForClipPlanes() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    VxMatrix world;
    world.SetIdentity();
    world[0][0] = 2.0f;
    VxMatrix view;
    view.SetIdentity();
    view[0][0] = 3.0f;
    ffp.SetTransform(VXMATRIX_WORLD, world);
    ffp.SetTransform(VXMATRIX_VIEW, view);

    const CKDWORD paletteUniform = context.GetBlockUniformForTests(CKRST_BLOCK_VERTEX_BLEND_MATRICES);

    VxPlane plane;
    plane.m_Normal = VxVector(1.0f, 0.0f, 0.0f);
    plane.m_D = 0.0f;
    ffp.SetUserClipPlane(0, plane);
    ffp.SetRenderState(VXRENDERSTATE_CLIPPLANEENABLE, 1u);
    ffp.SetRenderState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_0WEIGHTS);

    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKFF_VF_POSITION | CKFF_VF_BLENDWEIGHT, 1);

    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator matrices =
        context.Log.FloatUniforms.find(paletteUniform);
    TestCheck(matrices != context.Log.FloatUniforms.end() && matrices->second.size() >= 64,
              "Vertex blend with clip planes must upload matrix palette");
    if (matrices != context.Log.FloatUniforms.end() && matrices->second.size() >= 64) {
        TestCheck(matrices->second[0] == 2.0f,
                  "Default blend palette slot 0 must use the current world matrix");
    }

    ffp.Shutdown();
}

void VertexBlendUploadsExplicitMatrixPaletteSlot() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    VxMatrix palette;
    palette.SetIdentity();
    palette[1][1] = 4.0f;
    ffp.SetVertexBlendMatrix(1, palette);

    const CKDWORD paletteUniform = context.GetBlockUniformForTests(CKRST_BLOCK_VERTEX_BLEND_MATRICES);

    ffp.SetRenderState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_1WEIGHTS);
    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKFF_VF_POSITION | CKFF_VF_BLENDWEIGHT, 1);

    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator matrices =
        context.Log.FloatUniforms.find(paletteUniform);
    TestCheck(matrices != context.Log.FloatUniforms.end() && matrices->second.size() >= 32,
              "Explicit vertex blend palette must upload dedicated palette uniform");
    if (matrices != context.Log.FloatUniforms.end() && matrices->second.size() >= 32) {
        TestCheck(matrices->second[16 + 5] == 4.0f,
                  "Explicit vertex blend palette slot 1 must be preserved");
    }

    ffp.Shutdown();
}

void VertexBlendWeightFlagsCreateWeightLayout() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    struct Vertex {
        float Position[3];
        float Weights[2];
    } vertices[3] = {};

    VxDrawPrimitiveData data = {};
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_WEIGHTS2;
    data.PositionPtr = vertices;
    data.PositionStride = sizeof(Vertex);

    ffp.SetRenderState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_2WEIGHTS);
    ffp.DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &data);

    TestCheck(LayoutHasAttrib(context.LastVertexLayoutElements, CKRST_ATTRIB_WEIGHT),
              "DP weight flags must create a blend weight vertex attribute");
    TestCheck(!LayoutHasAttrib(context.LastVertexLayoutElements, CKRST_ATTRIB_INDICES),
              "Non-indexed vertex blend must not create blend indices attribute");

    ffp.Shutdown();
}

void VertexTweenWithoutStreamsRendersUntweened() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    VxVector positions[3] = {};
    VxDrawPrimitiveData data = {};
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TRANSFORM;
    data.PositionPtr = positions;
    data.PositionStride = sizeof(VxVector);

    ffp.SetRenderState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_TWEENING);
    TestCheck(ffp.DrawPrimitive(VX_TRIANGLELIST,
                                NULL, 0, &data) && context.Log.DrawCount == 1,
              "Vertex tween without its second stream must still draw");
    TestCheck(ffp.GetLastDrawApproximationMask() == (1ull << CKRST_DIAG_APPROX_VERTEX_BLEND_TWEEN),
              "Vertex tween input failures must report the tween approximation");
    const CKDWORD drawParams = context.GetBlockUniformForTests(CKRST_BLOCK_DRAW_PARAMS);
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator params =
        context.Log.FloatUniforms.find(drawParams);
    TestCheck(params != context.Log.FloatUniforms.end() &&
                  params->second.size() >= (CKFF_DRAW_PARAM_TWEEN + 1) * 4 &&
                  params->second[CKFF_DRAW_PARAM_TWEEN * 4 + 1] == (float)CKFF_VERTEX_BLEND_DISABLED,
              "The shader must receive the resolved (disabled) blend mode, not the raw render state");

    ffp.Shutdown();
}

void IndexedVertexBlendRequiresIndexLayout() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    struct Vertex {
        float Position[3];
        float Weights[2];
        CKDWORD Indices;
    } vertices[3] = {};

    VxDrawPrimitiveData data = {};
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_WEIGHTS2 | CKRST_DP_MATRIXPAL;
    data.PositionPtr = vertices;
    data.PositionStride = sizeof(Vertex);

    ffp.SetRenderState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_2WEIGHTS);
    ffp.SetRenderState(VXRENDERSTATE_INDEXVBLENDENABLE, TRUE);
    ffp.DrawPrimitive(VX_TRIANGLELIST, nullptr, 0, &data);

    TestCheck(LayoutHasAttrib(context.LastVertexLayoutElements, CKRST_ATTRIB_WEIGHT),
              "Indexed vertex blend must keep blend weight attribute");
    TestCheck(LayoutHasAttrib(context.LastVertexLayoutElements, CKRST_ATTRIB_INDICES),
              "Indexed vertex blend must create blend indices attribute");

    ffp.Shutdown();
}

void IndexedVertexBlendClampsPaletteOverflow() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    struct Vertex {
        float Position[3];
        float Weights[2];
        CKDWORD Indices;
    } vertices[3] = {};
    vertices[0].Indices = 4;

    VxDrawPrimitiveData data = {};
    data.VertexCount = 3;
    data.Flags = CKRST_DP_TRANSFORM | CKRST_DP_WEIGHTS2 | CKRST_DP_MATRIXPAL;
    data.PositionPtr = vertices;
    data.PositionStride = sizeof(Vertex);

    ffp.SetRenderState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_2WEIGHTS);
    ffp.SetRenderState(VXRENDERSTATE_INDEXVBLENDENABLE, TRUE);
    TestCheck(ffp.DrawPrimitive(VX_TRIANGLELIST,
                                nullptr, 0, &data) && context.Log.DrawCount == 1,
              "indexed blend must draw with an out-of-range matrix index");
    TestCheck(ffp.GetLastDrawApproximationMask() == (1ull << CKRST_DIAG_APPROX_VERTEX_BLEND_PALETTE),
              "indexed blend palette overflow must report the clamp approximation");
    TestCheck(ffp.GetLastDrawRejectReason() == CKFF_DRAW_REJECT_NONE,
              "indexed blend palette overflow is not a rejection");

    vertices[0].Indices = 0;
    TestCheck(ffp.DrawPrimitive(VX_TRIANGLELIST,
                                nullptr, 0, &data) && ffp.GetLastDrawApproximationMask() == 0,
              "in-range indices draw without approximation");

    VxMatrix matrix;
    matrix.SetIdentity();
    TestCheck(ffp.SetVertexBlendMatrix(CKFF_VERTEX_BLEND_MATRIX_COUNT, matrix) == FALSE,
              "setting a matrix outside the supported palette must fail");
    TestCheck(ffp.DrawPrimitive(VX_TRIANGLELIST,
                                nullptr, 0, &data) &&
                  ffp.GetLastDrawApproximationMask() == (1ull << CKRST_DIAG_APPROX_VERTEX_BLEND_PALETTE),
              "a palette overflow recorded by SetVertexBlendMatrix reports on the next indexed draw");

    ffp.Shutdown();
}

void PositionTVertexBlendDoesNotUploadMatrixPalette() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetRenderState(VXRENDERSTATE_VERTEXBLEND, VXVBLEND_2WEIGHTS);
    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKFF_VF_POSITIONT | CKFF_VF_BLENDWEIGHT, 1);

    const CKDWORD matrixUniform = context.GetBlockUniformForTests(CKRST_BLOCK_MATRICES);
    TestCheck(context.Log.FloatUniforms.find(matrixUniform) == context.Log.FloatUniforms.end(),
              "POSITIONT vertex blend must not upload 3D matrix palette");

    ffp.Shutdown();
}

void LocalViewerDoesNotSplitShaderWhenLightingDisabled() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetRenderState(VXRENDERSTATE_LIGHTING, FALSE);
    ffp.SetRenderState(VXRENDERSTATE_LOCALVIEWER, FALSE);
    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKFF_VF_POSITION | CKFF_VF_NORMAL, 1);
    const void *nonLocalViewerShader = context.LastVertexShaderCode;
    const CKDWORD createdPrograms = context.CreatedProgramCount;

    ffp.SetRenderState(VXRENDERSTATE_LOCALVIEWER, TRUE);
    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKFF_VF_POSITION | CKFF_VF_NORMAL, 1);

    TestCheck(nonLocalViewerShader != nullptr,
              "Lighting-disabled draw must create a vertex shader");
    TestCheck(context.LastVertexShaderCode == nonLocalViewerShader,
              "LOCALVIEWER must not split the vertex shader key when lighting is disabled");
    TestCheck(context.CreatedProgramCount == createdPrograms,
              "LOCALVIEWER must not create a new FFP program when lighting is disabled");

    ffp.Shutdown();
}

void MaterialSourceUsesDeclaredDPColorStreams() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    ffp.Init(context.StartedBackend(), context.ShaderSet());

    ffp.SetRenderState(VXRENDERSTATE_LIGHTING, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_COLORVERTEX, TRUE);
    ffp.SetRenderState(VXRENDERSTATE_DIFFUSEFROMVERTEX, TRUE);
    ffp.DrawVertexBuffer(VX_TRIANGLELIST,
                         1, 0, 0, 3, 0, 0,
                         CKRST_DP_CL_V, CKFF_VF_POSITION | CKFF_VF_NORMAL | CKFF_VF_COLOR0, 1);

    const CKDWORD uniform = context.GetBlockUniformForTests(CKRST_BLOCK_DRAW_PARAMS);
    std::unordered_map<CKDWORD, std::vector<float> >::const_iterator it =
        context.Log.FloatUniforms.find(uniform);

    TestCheck(it != context.Log.FloatUniforms.end(),
              "Material-source draw must upload draw params");
    TestCheck(it != context.Log.FloatUniforms.end() &&
                  it->second.size() >= 24 &&
                  it->second[20] == (float)CKFF_MS_MATERIAL,
              "Format COLOR0 alone must not force material diffuse source without DP diffuse data");

    ffp.Shutdown();
}

void RepeatedStateSettersKeepPreparedCaches() {
    FFPRecordingDriver driver;
    FFPRecordingBackend context(&driver);
    CKFixedFunctionPipeline ffp;
    TestCheck(ffp.Init(context.StartedBackend(), context.ShaderSet()),
              "pipeline initialization");

    VxMatrix identity;
    identity.SetIdentity();
    VxPlane plane(1.0f, 0.0f, 0.0f, 0.0f);
    CKViewportData viewport = {};
    viewport.ViewWidth = 640;
    viewport.ViewHeight = 480;
    viewport.ViewZMax = 1.0f;

    ffp.SetTransform(VXMATRIX_WORLD, identity);
    ffp.SetTransform(VXMATRIX_VIEW, identity);
    ffp.SetTransform(VXMATRIX_PROJECTION, identity);
    ffp.SetViewport(viewport);
    ffp.SetUserClipPlane(0, plane);
    ffp.SetRenderOptions(TRUE, FALSE, FALSE);
    ffp.SetTextureStageState(0, CKRST_TSS_ADDRESS,
                             VXTEXTURE_ADDRESSCLAMP);
    TestCheck(ffp.DrawVertexBuffer(
                  VX_TRIANGLELIST, 1, 0, 0, 3, 0, 0,
                  CKRST_DP_CL_V, CKFF_VF_POSITION, 1),
              "draw primes prepared caches");

    const uint64_t revision = ffp.GetStaticUniformRevision();
    TestCheck(ffp.IsVertexBufferProgramCacheValid() && ffp.IsDrawValidationCacheValid(),
              "draw leaves prepared caches valid");

    ffp.SetTransform(VXMATRIX_WORLD, identity);
    ffp.SetTransform(VXMATRIX_VIEW, identity);
    ffp.SetTransform(VXMATRIX_PROJECTION, identity);
    ffp.SetViewport(viewport);
    ffp.SetUserClipPlane(0, plane);
    ffp.SetRenderOptions(TRUE, FALSE, FALSE);
    ffp.SetTextureStageState(0, CKRST_TSS_ADDRESS,
                             VXTEXTURE_ADDRESSCLAMP);

    TestCheck(ffp.GetStaticUniformRevision() == revision,
              "equal state does not advance the uniform revision");
    TestCheck(ffp.IsVertexBufferProgramCacheValid() && ffp.IsDrawValidationCacheValid(),
              "equal state does not invalidate prepared caches");

    ffp.ResetTextureStages(0, CKFF_MAX_TEXTURE_STAGES);
    const uint64_t resetRevision = ffp.GetStaticUniformRevision();
    ffp.ResetTextureStages(0, CKFF_MAX_TEXTURE_STAGES);
    TestCheck(ffp.GetStaticUniformRevision() == resetRevision,
              "repeating the same stage reset is free");

    ffp.Shutdown();
}

void SharedPresentPreparationBuildsTheCompleteDraw() {
    TestCheck(CKFFScaledDimension(0, 1.0f, 4096) == 1 &&
                  CKFFScaledDimension(100, 0.5f, 4096) == 50 &&
                  CKFFScaledDimension(100, 2.0f, 150) == 150 &&
                  CKFFScaledDimension(100, 1.0f, 0) == 0,
              "shared presentation sizing clamps zero, scale and device limits");
    float vertices[15] = {};
    CKTransientVertexData transient;
    transient.Data = vertices;
    transient.Count = 3;
    transient.Stride = 5 * sizeof(float);
    transient.Layout = 31;

    CKFFPresentDraw present;
    CKDrawCommand draw;
    TestCheck(present.Prepare(11, 100, 50, TRUE, TRUE, 0.25f,
                              FALSE, 21, 31, transient, draw) == CK_OK,
              "shared presentation draw prepares valid inputs");
    TestCheck(vertices[3] == 0.0f && vertices[4] == 0.0f &&
                  vertices[8] == 2.0f && vertices[9] == 0.0f &&
                  vertices[13] == 0.0f && vertices[14] == 2.0f,
              "fullscreen triangle has the unflipped UV orientation");
    TestCheck(draw.Program == 21 && draw.Layout == 31 &&
                  draw.TransientVertices == &transient &&
                  draw.VertexCount == 3 && draw.Textures && draw.Constants,
              "prepared draw carries program, layout, geometry and CPU state");
    const CKFFTextureSlot &slot = (*draw.Textures)[CKFF_SLOT_PRESENT];
    TestCheck(slot.Texture == 11 &&
                  slot.Sampler.MinFilter == CKRST_FILTER_LINEAR &&
                  slot.Sampler.MagFilter == CKRST_FILTER_LINEAR &&
                  slot.Sampler.AddressU == CKRST_ADDRESS_CLAMP &&
                  slot.Sampler.AddressV == CKRST_ADDRESS_CLAMP,
              "prepared draw carries the presentation texture and sampler");
    float params[4] = {};
    const XArray<CKBYTE> &bytes =
        (*draw.Constants)[CKRST_BLOCK_PRESENT_PARAMS].Bytes;
    if (bytes.Size() == sizeof(params))
        memcpy(params, bytes.Begin(), sizeof(params));
    TestCheck(bytes.Size() == sizeof(params) && params[0] == 0.01f &&
                  params[1] == 0.02f && params[2] == 1.0f &&
                  params[3] == 0.25f,
              "prepared draw carries texel size, FXAA and sharpness constants");

    TestCheck(present.Prepare(11, 100, 50, FALSE, FALSE, 0.0f,
                              TRUE, 21, 31, transient, draw) == CK_OK &&
                  vertices[4] == 1.0f && vertices[14] == -1.0f,
              "shared presentation draw applies vertical flipping");
    transient.Stride = 4 * sizeof(float);
    TestCheck(present.Prepare(11, 100, 50, FALSE, FALSE, 0.0f,
                              FALSE, 21, 31, transient, draw) ==
                  CKERR_OUTOFMEMORY,
              "undersized transient presentation vertices are rejected");
}

} // namespace

int main() {
    TestFramework tests;
    tests.Run("Point image scaling uses destination pixel centers",
              &PointImageScalingUsesDestinationPixelCenters);
    tests.Run("Shared presentation preparation builds the complete draw",
              &SharedPresentPreparationBuildsTheCompleteDraw);
    tests.Run("FFP declares generic program resources", &FixedFunctionProgramDeclaresItsShaderInterface);
    tests.Run("Shader catalog and program metadata stay outside cached draws",
              &ShaderCacheOwnsCatalogAndBuildsInterfacesOnlyOnProgramMiss);
    tests.Run("Shader cache retains alternating specializations",
              &ShaderCacheRetainsAlternatingSpecializations);
    tests.Run("Shader cache evicts least recently used specialization",
              &ShaderCacheEvictsLeastRecentlyUsedSpecialization);
    tests.Run("Null rasterizer supports headless FFP",
              &NullRasterizerSupportsHeadlessFFP);
    tests.Run("Missing shader payload family fails initialization",
              &MissingShaderPayloadFamilyFailsInitialization);
    tests.Run("DrawVertexBuffer approximates stencil write masks",
              &DrawVertexBufferApproximatesStencilWriteMasks);
    tests.Run("DrawVertexBuffer submits representable stencil masks",
              &DrawVertexBufferSubmitsRepresentableStencilMasks);
    tests.Run("Ignored render states report diagnostics",
              &IgnoredRenderStatesReportDiagnostics);
    tests.Run("Invalid state values reject before backend encoding",
              &InvalidStateValuesRejectBeforeBackendEncoding);
    tests.Run("Draw validation cache invalidates on state changes",
              &DrawValidationCacheInvalidatesOnStateChanges);
    tests.Run("Backend draw approximation mask is preserved",
              &BackendDrawApproximationMaskIsPreserved);
    tests.Run("Unsupported texture-stage states approximate with diagnostics",
              &UnsupportedTextureStageStatesApproximateWithDiagnostics);
    tests.Run("Single cube-volume layout uses generic mixed sampler module",
              &SingleCubeVolumeLayoutUsesGenericMixedSamplerModule);
    tests.Run("DrawVertexBuffer propagates backend failure",
              &DrawVertexBufferPropagatesBackendFailure);
    tests.Run("DrawVertexBuffer stops before submit after binding failure",
              &DrawVertexBufferStopsBeforeSubmitAfterBindingFailure);
    tests.Run("DrawVertexBuffer rejects constant packet failure",
              &DrawVertexBufferRejectsConstantPacketFailure);
    tests.Run("Affine texture coordinates reach the shared shader",
              &AffineTextureCoordinatesReachSharedShader);
    tests.Run("Inactive unsupported state does not reject draw",
              &InactiveUnsupportedStateDoesNotRejectDraw);
    tests.Run("Disabled texture stage ignores later unsupported state",
              &DisabledTextureStageIgnoresLaterUnsupportedState);
    tests.Run("Bound unused texture does not require affine interpolation",
              &BoundUnusedTextureDoesNotRequireAffineInterpolation);
    tests.Run("Texture-stage snapshot preserves explicit zero argument",
              &TextureStageSnapshotPreservesExplicitZeroArgument);
    tests.Run("Unknown texture op rejects draw",
              &UnknownTextureOpRejectsDraw);
    tests.Run("Alpha bump op is rejected",
              &AlphaBumpOpIsRejected);
    tests.Run("Bottom-left render targets sample without flip",
              &BottomLeftRenderTargetsSampleWithoutFlip);
    tests.Run("Render target origin flips projection, viewport and winding",
              &RenderTargetOriginFlipsProjectionViewportAndWinding);
    tests.Run("Transformed vertices use legacy pixel centers",
              &TransformedVerticesUseLegacyPixelCenters);
    tests.Run("Viewport mapping remaps clip space and scissors",
              &ViewportMappingRemapsClipSpaceAndScissors);
    tests.Run("DrawVertexBuffer uploads alpha precision",
              &DrawVertexBufferUploadsAlphaPrecision);
    tests.Run("DrawVertexBuffer sets flat shade specialization",
              &DrawVertexBufferSetsFlatShadeSpecialization);
    tests.Run("DrawVertexBuffer uploads fog params",
              &DrawVertexBufferUploadsFogParams);
    tests.Run("POSITIONT fog uses POSITIONT shader key",
              &PositionTFogUsesPositionTShaderKey);
    tests.Run("Range fog changes specialization",
              &RangeFogChangesSpecialization);
    tests.Run("Pixel fog overrides vertex fog mode",
              &PixelFogOverridesVertexFogMode);
    tests.Run("DrawVertexBuffer compacts clip plane uniforms",
              &DrawVertexBufferCompactsClipPlaneUniforms);
    tests.Run("DrawVertexBuffer skips clip uniforms when disabled",
              &DrawVertexBufferSkipsClipUniformsWhenDisabled);
    tests.Run("Clip planes use dedicated vertex shader variant",
              &ClipPlanesUseDedicatedVertexShaderVariant);
    tests.Run("RESULTARG TEMP preserves every active stage",
              &ResultArgTempPreservesEveryActiveStage);
    tests.Run("MODULATE4X stays in texture stage specialization",
              &Modulate4XStaysInTextureStageSpecialization);
    tests.Run("PREMODULATE stays in texture stage specialization",
              &PremodulateStaysInTextureStageSpecialization);
    tests.Run("Texture arg modifiers stay in specialization",
              &TextureArgModifiersStayInSpecialization);
    tests.Run("Null texture stage preserves specialization",
              &NullTextureStagePreservesSpecialization);
    tests.Run("Stage constant does not create texture dependency",
              &StageConstantDoesNotCreateTextureDependency);
    tests.Run("Cube texture uses cube sampler specialization and binding",
              &CubeTextureUsesCubeSamplerSpecializationAndBinding);
    tests.Run("Volume texture binds the first volume sampler",
              &VolumeTextureBindsFirstVolumeSampler);
    tests.Run("Volume texture stage seven binds volume sampler",
              &VolumeTextureStageSevenBindsVolumeSampler);
    tests.Run("Volume and cube bind their type slots",
              &VolumeAndCubeBindTheirTypeSlots);
    tests.Run("Arbitrary single volume-cube placement shares the program",
              &ArbitrarySingleVolumeCubePlacementSharesTheProgram);
    tests.Run("Multiple mixed samplers use type-ranked slots",
              &MultipleMixedSamplersUseTypeRankedSlots);
    tests.Run("Fifth cube stage samples as unbound",
              &FifthCubeStageSamplesAsUnbound);
    tests.Run("Multiple volume textures bind each volume sampler",
              &MultipleVolumeTexturesBindEachVolumeSampler);
    tests.Run("Depth texture compare func uploads sampler and specialization",
              &DepthTextureCompareFuncUploadsSamplerAndSpecialization);
    tests.Run("Filtered depth texture compare approximates",
              &FilteredDepthTextureCompareApproximates);
    tests.Run("Border color reaches backend unmodified",
              &BorderColorReachesBackendUnmodified);
    tests.Run("Translation preserves distinct border colors",
              &BorderColorsAreNotQuantizedByTranslation);
    tests.Run("Unused border textures do not consume palette slots",
              &UnusedBorderTexturesDoNotConsumePaletteSlots);
    tests.Run("Untextured stage keeps runtime stage params",
              &UntexturedStageKeepsRuntimeStageParams);
    tests.Run("PREMODULATE implicit texture dependency binds next stage",
              &PremodulateImplicitTextureDependencyBindsNextStage);
    tests.Run("Program family is shared across state bindings",
              &ProgramFamilyIsSharedAcrossStateBindings);
    tests.Run("Prepared caches invalidate every uniform and program dependency",
              &PreparedCachesInvalidateEveryUniformAndProgramDependency);
    tests.Run("Program family has four vertex variants",
              &ProgramFamilyHasFourVertexVariants);
    tests.Run("Legacy STAGEBLEND zero terminates stale multitexture state",
              &LegacyStageBlendZeroTerminatesStaleMultitextureState);
    tests.Run("Legacy TEXTUREMAPBLEND clears explicit stage ops",
              &LegacyTextureMapBlendClearsExplicitStageOps);
    tests.Run("Point sprite DrawPrimitive expands to triangle list",
              &PointSpriteDrawPrimitiveExpandsToTriangleList);
    tests.Run("Point size expands without sprite texcoord replacement",
              &PointSizeExpandsWithoutSpriteTexcoordReplacement);
    tests.Run("Persistent point buffers require transient expansion for complex sizes",
              &PersistentPointBuffersRequireTransientExpansionForComplexSizes);
    tests.Run("Wrapped line strip submits as line list",
              &WrappedLineStripSubmitsAsLineList);
    tests.Run("Point sprite uses per-vertex point size",
              &PointSpriteUsesPerVertexPointSize);
    tests.Run("POSITIONT texture transform does not upload texture matrix",
              &PositionTTextureTransformDoesNotUploadTextureMatrix);
    tests.Run("COUNT1 texture transform uploads texture matrix",
              &TextureTransformCountOneUploadsTextureMatrix);
    tests.Run("Legacy texcoord component count reads only xy",
              &LegacyTexcoordComponentCountReadsOnlyXY);
    tests.Run("Pipeline texcoord component count preserves source zw",
              &PipelineTexcoordComponentCountPreservesSourceZW);
    tests.Run("Invalid texcoord component count falls back to legacy xy",
              &InvalidTexcoordComponentCountFallsBackToLegacyXY);
    tests.Run("Simple DrawPrimitive data uses legacy texcoord path",
              &SimpleDrawPrimitiveDataUsesLegacyTexcoordPath);
    tests.Run("Projected sampler stages zero to three enter specialization mask",
              &ProjectedSamplerStagesZeroToThreeEnterSpecializationMask);
    tests.Run("Projected sampler stage four enters specialization",
              &ProjectedSamplerStageFourEntersSpecialization);
    tests.Run("Draw uploads per-stage bump env uniforms",
              &DrawUploadsPerStageBumpEnvUniforms);
    tests.Run("Unsupported bump inputs approximate with diagnostics",
              &UnsupportedBumpInputsApproximateWithDiagnostics);
    tests.Run("Vertex blend zero weights uploads matrix palette",
              &VertexBlendZeroWeightsUploadsMatrixPalette);
    tests.Run("Vertex blend uploads world matrix palette for clip planes",
              &VertexBlendUploadsWorldMatrixPaletteForClipPlanes);
    tests.Run("Vertex blend uploads explicit matrix palette slot",
              &VertexBlendUploadsExplicitMatrixPaletteSlot);
    tests.Run("Vertex blend weight flags create weight layout",
              &VertexBlendWeightFlagsCreateWeightLayout);
    tests.Run("Vertex tween without streams renders untweened",
              &VertexTweenWithoutStreamsRendersUntweened);
    tests.Run("Indexed vertex blend requires index layout",
              &IndexedVertexBlendRequiresIndexLayout);
    tests.Run("Indexed vertex blend clamps palette overflow",
              &IndexedVertexBlendClampsPaletteOverflow);
    tests.Run("POSITIONT vertex blend does not upload matrix palette",
              &PositionTVertexBlendDoesNotUploadMatrixPalette);
    tests.Run("LOCALVIEWER does not split shader when lighting disabled",
              &LocalViewerDoesNotSplitShaderWhenLightingDisabled);
    tests.Run("Material source uses declared DP color streams",
              &MaterialSourceUsesDeclaredDPColorStreams);
    tests.Run("Repeated state setters keep prepared caches",
              &RepeatedStateSettersKeepPreparedCaches);
    return tests.ExitCode();
}
