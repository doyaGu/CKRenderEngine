#include "CKBgfxRasterizer.h"
#include "CKFFSamplerLayout.h"
#include "CKFFShaderCache.h"
#include "CKFFSpecializedModuleTable.h"
#include "CKRenderSettings.h"
#include "TestTriangleMultiset.h"

#include "shaders/generated/CKFFSpecializedModuleTable.generated.h"

#include <SDL3/SDL.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

char g_OpenGLRuntimeFailure[512];

void TestCheckf(bool condition, const char *format, ...)
{
    if (condition)
        return;

    va_list args;
    va_start(args, format);
    vsnprintf(g_OpenGLRuntimeFailure, sizeof(g_OpenGLRuntimeFailure), format, args);
    va_end(args);
    TestFail(g_OpenGLRuntimeFailure);
}

bool EnvFlagEnabled(const char *name)
{
    const char *value = getenv(name);
    return value &&
           (strcmp(value, "1") == 0 ||
            strcmp(value, "true") == 0 ||
            strcmp(value, "TRUE") == 0 ||
            strcmp(value, "on") == 0 ||
            strcmp(value, "ON") == 0);
}

void SetEnvValue(const char *name, const char *value)
{
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

void SetSelectStage(CKFFShaderKeyFSStage &stage,
                    CKDWORD arg,
                    bool hasTexture,
                    CKDWORD samplerType)
{
    stage = CKFFShaderKeyFSStage();
    stage.ColorOp = CKRST_TOP_SELECTARG1;
    stage.ColorArg1 = arg;
    stage.AlphaOp = CKRST_TOP_SELECTARG1;
    stage.AlphaArg1 = arg;
    stage.HasTexture = hasTexture;
    stage.SamplerType = samplerType;
}

const CKFFSpecializedModuleEntry *FirstGeneratedGLSLEntry()
{
    for (size_t i = 0; i < g_CKFFSpecializedModuleCount; ++i) {
        if (g_CKFFSpecializedModules[i].Profile == CKRST_SHADER_PROFILE_GLSL)
            return &g_CKFFSpecializedModules[i];
    }
    return NULL;
}

CKFFShaderKey MakeRuntimeKeySkeleton()
{
    const CKFFSpecializedModuleEntry *entry = FirstGeneratedGLSLEntry();
    TestCheck(entry != NULL,
              "OpenGL runtime test requires at least one generated GLSL full-specialized key");

    CKFFShaderKey key;
    key.VS = entry->Key.VS;
    key.FS = CKFFShaderKeyFS();
    return key;
}

CKFFShaderKey MakeStageFourFallbackKey(CKDWORD samplerType)
{
    CKFFShaderKey key = MakeRuntimeKeySkeleton();
    for (CKDWORD stage = 0; stage < 4; ++stage)
        SetSelectStage(key.FS.Stages[stage], CKRST_TA_CURRENT, false, CKFF_SAMPLER_2D);
    SetSelectStage(key.FS.Stages[4], CKRST_TA_TEXTURE, true, samplerType);
    key.FS.LastActiveTextureStage = 4;
    return key;
}

CKFFShaderKey MakeVolumeCubeStaticLayoutKey()
{
    CKFFShaderKey key = MakeRuntimeKeySkeleton();
    SetSelectStage(key.FS.Stages[0], CKRST_TA_TEXTURE, true, CKFF_SAMPLER_VOLUME);
    SetSelectStage(key.FS.Stages[1], CKRST_TA_TEXTURE, true, CKFF_SAMPLER_CUBE);
    key.FS.LastActiveTextureStage = 1;
    return key;
}

void RunShaderProgramCase(CKBgfxRasterizerContext *context,
                          const CKFFShaderKey &key,
                          bool uberShader,
                          bool expectedFullSpecialized,
                          const char *caseName)
{
    CKRenderSettingsClearOverridesForTests();
    CKRenderSettingsSetOverrideForTests(CKRenderSettingsSection::FFP,
                                        "UberShader",
                                        uberShader ? "1" : "0");

    const CKDWORD fatalBefore = context->GetFatalCountForTests();

    CKFFShaderCache cache;
    cache.Init(context);
    const CKFFProgramBinding binding = cache.GetProgram(key);
    TestCheckf(binding.Program != 0,
               "%s must create a real OpenGL FFP shader program", caseName);
    TestCheckf(binding.FullSpecialized == expectedFullSpecialized,
               "%s selected route mismatch fullSpecialized=%u expected=%u",
               caseName,
               binding.FullSpecialized ? 1u : 0u,
               expectedFullSpecialized ? 1u : 0u);

    TestCheckf(context->Frame(CKRST_FRAME_SYNC_IMMEDIATE) == CK_OK,
               "%s must process bgfx frame after program creation", caseName);
    TestCheckf(context->GetFatalCountForTests() == fatalBefore,
               "%s triggered a bgfx fatal callback during OpenGL shader creation/link",
               caseName);

    cache.Shutdown();
    context->Frame(CKRST_FRAME_SYNC_IMMEDIATE);
    CKRenderSettingsClearOverridesForTests();
}

void OpenGLRuntimeCreatesRepresentativeFFPPrograms()
{
    SetEnvValue("CKBGFX_RENDERER_BACKEND", "opengl");

    TestCheckf(SDL_Init(SDL_INIT_VIDEO),
               "SDL video init failed: %s", SDL_GetError());

    SDL_Window *window = SDL_CreateWindow("ffp-opengl-runtime",
                                          64, 64,
                                          SDL_WINDOW_HIDDEN);
    TestCheckf(window != NULL,
               "SDL hidden window creation failed: %s", SDL_GetError());

    CKBgfxRasterizer rasterizer;
    TestCheck(rasterizer.Start((WIN_HANDLE)window) == TRUE,
              "CKBgfxRasterizer must start for OpenGL runtime test");
    TestCheck(rasterizer.GetDriverCount() > 0,
              "CKBgfxRasterizer must expose a driver");

    CKRasterizerDriver *driver = rasterizer.GetDriver(0);
    TestCheck(driver != NULL,
              "CKBgfxRasterizer driver must exist");

    CKRasterizerContext *baseContext = driver->CreateContext();
    TestCheck(baseContext != NULL,
              "CKBgfxRasterizer driver must create a context");

    CKBgfxRasterizerContext *context = static_cast<CKBgfxRasterizerContext *>(baseContext);
    TestCheck(context->Create((WIN_HANDLE)window, 0, 0, 64, 64, 32,
                              FALSE, 0, 24, 8) == TRUE,
              "bgfx OpenGL context creation must succeed");

    CKShaderTargetDesc target;
    TestCheck(driver->GetShaderTarget(&target) == CK_OK,
              "OpenGL runtime driver must expose a shader target");
    TestCheckf(target.Profile == CKRST_SHADER_PROFILE_GLSL,
               "OpenGL runtime driver selected shader profile 0x%08X instead of GLSL",
               target.Profile);

    const CKFFSpecializedModuleEntry *fullEntry = FirstGeneratedGLSLEntry();
    TestCheck(fullEntry != NULL,
              "OpenGL runtime test requires a generated GLSL specialized module");
    RunShaderProgramCase(context, fullEntry->Key, false, true,
                         "full-specialized GLSL route");
    RunShaderProgramCase(context, MakeStageFourFallbackKey(CKFF_SAMPLER_2D),
                         false, false,
                         "stage 4 uber fallback GLSL route");
    RunShaderProgramCase(context, MakeStageFourFallbackKey(CKFF_SAMPLER_VOLUME),
                         false, false,
                         "stage 4 volume fallback GLSL route");
    RunShaderProgramCase(context, MakeVolumeCubeStaticLayoutKey(),
                         false, false,
                         "volume+cube static sampler GLSL route");
    RunShaderProgramCase(context, fullEntry->Key, true, false,
                         "forced uber GLSL route");

    driver->DestroyContext(context);
    rasterizer.Close();
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);

    printf("  coverage: openglRuntimeProgramCases=5\n");
}

} // namespace

int main()
{
    if (!EnvFlagEnabled("CKRE_RUN_OPENGL_RUNTIME_TESTS")) {
        printf("SKIPPED: set CKRE_RUN_OPENGL_RUNTIME_TESTS=1 to run the Linux OpenGL runtime gate.\n");
        return 0;
    }

    TestFramework tests;
    tests.Run("OpenGL runtime creates representative FFP programs",
              &OpenGLRuntimeCreatesRepresentativeFFPPrograms);
    return tests.ExitCode();
}
