#include "FFPCoverageDomain.h"
#include "FFPDiagnosticHarness.h"
#include "TestTriangleMultiset.h"

#include "CKFFShaderCache.h"
#include "CKRenderSettings.h"

#include <stdarg.h>
#include <stdio.h>

namespace {

struct Pixel {
    int R;
    int G;
    int B;
    int A;
};

struct PixelStage {
    CKDWORD ColorOp;
    CKDWORD ColorArg1;
    CKDWORD ColorArg2;
    CKDWORD AlphaOp;
    CKDWORD AlphaArg1;
    CKDWORD AlphaArg2;
    bool ResultIsTemp;
    CKDWORD SamplerType;
};

struct PixelScene {
    const char *Name;
    CKDWORD StageCount;
    PixelStage Stages[CKFF_STATE_DESC_TEXTURE_STAGES];
    bool AlphaTest;
    bool Fog;
    bool FlatShade;
    Pixel Expected;
};

char g_PixelParityFailure[512];

void TestCheckf(bool condition, const char *format, ...)
{
    if (condition)
        return;

    va_list args;
    va_start(args, format);
    vsnprintf(g_PixelParityFailure, sizeof(g_PixelParityFailure), format, args);
    va_end(args);
    TestFail(g_PixelParityFailure);
}

int ClampByte(int value)
{
    if (value < 0)
        return 0;
    if (value > 255)
        return 255;
    return value;
}

Pixel MakePixel(int r, int g, int b, int a)
{
    Pixel p = {ClampByte(r), ClampByte(g), ClampByte(b), ClampByte(a)};
    return p;
}

Pixel SourcePixel(CKDWORD arg, const Pixel &diffuse, const Pixel &current,
                  const Pixel &texture, const Pixel &tfactor,
                  const Pixel &specular, const Pixel &temp,
                  const Pixel &constant)
{
    switch (FFPCoverageBaseTextureArg(arg)) {
    case CKRST_TA_DIFFUSE: return diffuse;
    case CKRST_TA_CURRENT: return current;
    case CKRST_TA_TEXTURE: return texture;
    case CKRST_TA_TFACTOR: return tfactor;
    case CKRST_TA_SPECULAR: return specular;
    case CKRST_TA_TEMP: return temp;
    case CKRST_TA_CONSTANT: return constant;
    default: break;
    }
    return MakePixel(0, 0, 0, 255);
}

Pixel ApplyArgModifiers(CKDWORD arg, Pixel p)
{
    if ((arg & CKRST_TA_ALPHAREPLICATE) != 0)
        p.R = p.G = p.B = p.A;
    if ((arg & CKRST_TA_COMPLEMENT) != 0) {
        p.R = 255 - p.R;
        p.G = 255 - p.G;
        p.B = 255 - p.B;
    }
    return p;
}

Pixel ReadArg(CKDWORD arg, const Pixel &diffuse, const Pixel &current,
              const Pixel &texture, const Pixel &tfactor,
              const Pixel &specular, const Pixel &temp,
              const Pixel &constant)
{
    return ApplyArgModifiers(arg, SourcePixel(arg, diffuse, current, texture,
                                              tfactor, specular, temp, constant));
}

int Mul255(int a, int b)
{
    return (a * b + 127) / 255;
}

Pixel EvalOp(CKDWORD op, const Pixel &a, const Pixel &b, const Pixel &current)
{
    switch (op) {
    case CKRST_TOP_SELECTARG1:
        return a;
    case CKRST_TOP_SELECTARG2:
        return b;
    case CKRST_TOP_MODULATE:
        return MakePixel(Mul255(a.R, b.R), Mul255(a.G, b.G), Mul255(a.B, b.B), Mul255(a.A, b.A));
    case CKRST_TOP_MODULATE2X:
        return MakePixel(Mul255(a.R, b.R) * 2, Mul255(a.G, b.G) * 2, Mul255(a.B, b.B) * 2, Mul255(a.A, b.A) * 2);
    case CKRST_TOP_ADD:
        return MakePixel(a.R + b.R, a.G + b.G, a.B + b.B, a.A + b.A);
    case CKRST_TOP_SUBTRACT:
        return MakePixel(a.R - b.R, a.G - b.G, a.B - b.B, a.A - b.A);
    case CKRST_TOP_BLENDDIFFUSEALPHA:
        return MakePixel((a.R * current.A + b.R * (255 - current.A) + 127) / 255,
                         (a.G * current.A + b.G * (255 - current.A) + 127) / 255,
                         (a.B * current.A + b.B * (255 - current.A) + 127) / 255,
                         (a.A * current.A + b.A * (255 - current.A) + 127) / 255);
    default:
        return current;
    }
}

Pixel EvalScene(const PixelScene &scene)
{
    const Pixel diffuse = MakePixel(180, 120, 80, 160);
    const Pixel texture = MakePixel(96, 192, 128, 200);
    const Pixel tfactor = MakePixel(64, 64, 255, 128);
    const Pixel specular = MakePixel(24, 32, 48, 255);
    const Pixel constant = MakePixel(220, 40, 90, 180);
    Pixel current = diffuse;
    Pixel temp = MakePixel(0, 0, 0, 0);

    for (CKDWORD stage = 0; stage < scene.StageCount; ++stage) {
        const PixelStage &s = scene.Stages[stage];
        const Pixel a = ReadArg(s.ColorArg1, diffuse, current, texture, tfactor, specular, temp, constant);
        const Pixel b = ReadArg(s.ColorArg2, diffuse, current, texture, tfactor, specular, temp, constant);
        Pixel result = EvalOp(s.ColorOp, a, b, current);
        if (s.ResultIsTemp)
            temp = result;
        else
            current = result;
    }

    if (scene.Fog) {
        current.R = (current.R + 40) / 2;
        current.G = (current.G + 80) / 2;
        current.B = (current.B + 120) / 2;
    }
    if (scene.AlphaTest && current.A <= 96)
        current = MakePixel(0, 0, 0, 0);
    if (scene.FlatShade)
        current.B = ClampByte(current.B + 7);
    return current;
}

bool SameSpecialization(const CKFFSpecializationInfo &lhs,
                        const CKFFSpecializationInfo &rhs)
{
    if (lhs.DwordCount() != rhs.DwordCount())
        return false;
    for (CKDWORD i = 0; i < lhs.DwordCount(); ++i) {
        if (lhs.Data()[i] != rhs.Data()[i])
            return false;
    }
    return true;
}

CKFFShaderKey MakeSceneKey(const PixelScene &scene)
{
    CKFFShaderKey key = MakeFFPCoverageVSKey(false);
    key.FS = CKFFShaderKeyFS();
    key.FS.LastActiveTextureStage = scene.StageCount == 0 ? 0 : scene.StageCount - 1;
    key.FS.AlphaTestEnable = scene.AlphaTest;
    key.FS.AlphaFunc = scene.AlphaTest ? VXCMP_GREATER : 0;
    key.FS.FogEnable = scene.Fog;
    key.FS.VertexFogMode = scene.Fog ? 1 : 0;
    key.FS.PixelFogMode = 0;
    key.FS.FlatShade = scene.FlatShade;

    for (CKDWORD stage = 0; stage < scene.StageCount; ++stage) {
        const PixelStage &src = scene.Stages[stage];
        CKFFShaderKeyFSStage &dst = key.FS.Stages[stage];
        dst.ColorOp = src.ColorOp;
        dst.ColorArg1 = src.ColorArg1;
        dst.ColorArg2 = src.ColorArg2;
        dst.AlphaOp = src.AlphaOp;
        dst.AlphaArg1 = src.AlphaArg1;
        dst.AlphaArg2 = src.AlphaArg2;
        dst.ResultIsTemp = src.ResultIsTemp;
        dst.HasTexture = FFPCoverageArgUsesTexture(src.ColorArg1) ||
                         FFPCoverageArgUsesTexture(src.ColorArg2) ||
                         FFPCoverageArgUsesTexture(src.AlphaArg1) ||
                         FFPCoverageArgUsesTexture(src.AlphaArg2);
        dst.SamplerType = src.SamplerType;
    }
    return key;
}

void RunSceneRoute(const PixelScene &scene, bool uberMode)
{
    CKRenderSettingsClearOverridesForTests();
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::FFP, "UberShader",
                                        uberMode ? "1" : "0");

    const Pixel actual = EvalScene(scene);
    TestCheckf(actual.R == scene.Expected.R && actual.G == scene.Expected.G &&
                   actual.B == scene.Expected.B && actual.A == scene.Expected.A,
               "Synthetic pixel scene %s CPU reference changed: got %u,%u,%u,%u",
               scene.Name, actual.R, actual.G, actual.B, actual.A);

    FFPDiagnosticDriver driver(CKRST_SHADER_PROFILE_DX11);
    FFPDiagnosticContext context(&driver);
    CKFFShaderCache cache;
    cache.Init(&context);

    const CKFFShaderKey key = MakeSceneKey(scene);
    const CKFFSpecializationInfo expectedSpec = CKFFBuildSpecializationInfo(key.FS);
    const CKFFProgramBinding binding = cache.GetProgram(key);

    TestCheckf(binding.Program != 0,
               "Synthetic pixel scene %s must create a shader program in %s mode",
               scene.Name, uberMode ? "uber" : "normal");
    TestCheckf(SameSpecialization(binding.Specialization, expectedSpec),
               "Synthetic pixel scene %s specialization mismatch in %s mode",
               scene.Name, uberMode ? "uber" : "normal");

    cache.Shutdown();
    CKRenderSettingsClearOverridesForTests();
}

PixelStage Stage(CKDWORD op, CKDWORD arg1, CKDWORD arg2, bool temp, CKDWORD sampler)
{
    PixelStage s = {op, arg1, arg2, CKRST_TOP_SELECTARG1, arg1, arg2, temp, sampler};
    return s;
}

static const PixelScene kSmokeScenes[] = {
    {"unlit_color", 1, {Stage(CKRST_TOP_SELECTARG1, CKRST_TA_DIFFUSE, CKRST_TA_CURRENT, false, CKFF_SAMPLER_2D)}, false, false, false, {180, 120, 80, 160}},
    {"stage0_modulate", 1, {Stage(CKRST_TOP_MODULATE, CKRST_TA_TEXTURE, CKRST_TA_DIFFUSE, false, CKFF_SAMPLER_2D)}, false, false, false, {68, 90, 40, 125}},
    {"stage_temp_current", 2, {Stage(CKRST_TOP_ADD, CKRST_TA_TEXTURE, CKRST_TA_TFACTOR, true, CKFF_SAMPLER_2D), Stage(CKRST_TOP_MODULATE, CKRST_TA_TEMP, CKRST_TA_DIFFUSE, false, CKFF_SAMPLER_2D)}, false, false, false, {113, 120, 80, 160}},
    {"full_four_stage", 4, {Stage(CKRST_TOP_MODULATE, CKRST_TA_TEXTURE, CKRST_TA_DIFFUSE, false, CKFF_SAMPLER_2D), Stage(CKRST_TOP_ADD, CKRST_TA_CURRENT, CKRST_TA_SPECULAR, false, CKFF_SAMPLER_2D), Stage(CKRST_TOP_SUBTRACT, CKRST_TA_CURRENT, CKRST_TA_TFACTOR, false, CKFF_SAMPLER_2D), Stage(CKRST_TOP_MODULATE2X, CKRST_TA_CURRENT, CKRST_TA_CONSTANT, false, CKFF_SAMPLER_2D)}, false, false, false, {48, 18, 0, 180}},
    {"stage_five_fallback", 5, {Stage(CKRST_TOP_SELECTARG1, CKRST_TA_DIFFUSE, CKRST_TA_CURRENT, false, CKFF_SAMPLER_2D), Stage(CKRST_TOP_MODULATE, CKRST_TA_CURRENT, CKRST_TA_TEXTURE, false, CKFF_SAMPLER_2D), Stage(CKRST_TOP_ADD, CKRST_TA_CURRENT, CKRST_TA_SPECULAR, false, CKFF_SAMPLER_2D), Stage(CKRST_TOP_SUBTRACT, CKRST_TA_CURRENT, CKRST_TA_TFACTOR, false, CKFF_SAMPLER_2D), Stage(CKRST_TOP_MODULATE, CKRST_TA_CURRENT, CKRST_TA_CONSTANT, false, CKFF_SAMPLER_2D)}, false, false, false, {24, 9, 0, 90}},
    {"volume_sampler", 1, {Stage(CKRST_TOP_SELECTARG1, CKRST_TA_TEXTURE, CKRST_TA_CURRENT, false, CKFF_SAMPLER_VOLUME)}, false, false, false, {96, 192, 128, 200}},
    {"cube_sampler_fog", 1, {Stage(CKRST_TOP_SELECTARG1, CKRST_TA_TEXTURE, CKRST_TA_CURRENT, false, CKFF_SAMPLER_CUBE)}, false, true, false, {68, 136, 124, 200}},
    {"depth_alpha_flat", 1, {Stage(CKRST_TOP_SELECTARG1, CKRST_TA_TEXTURE, CKRST_TA_CURRENT, false, CKFF_SAMPLER_DEPTH)}, true, false, true, {96, 192, 135, 200}},
};

void SmokeScenesMatchSyntheticReferenceInNormalAndUberModes()
{
    CKDWORD sceneCount = 0;
    for (CKDWORD i = 0; i < (CKDWORD)FFPCoverageArrayCount(kSmokeScenes); ++i) {
        RunSceneRoute(kSmokeScenes[i], false);
        RunSceneRoute(kSmokeScenes[i], true);
        ++sceneCount;
    }
    printf("  coverage: syntheticPixelSmokeScenes=%u routeExecutions=%u\n",
           sceneCount, sceneCount * 2u);
}

} // namespace

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    TestFramework tests;
    tests.Run("Smoke scenes match synthetic reference in normal and uber modes",
              &SmokeScenesMatchSyntheticReferenceInNormalAndUberModes);
    return tests.ExitCode();
}
