#include "CKSdlGpuShaderTarget.h"

#include <stdio.h>
#include <string.h>

namespace {

int g_Failures = 0;

void Check(bool condition, const char *message)
{
    if (condition)
        return;
    ++g_Failures;
    fprintf(stderr, "FAIL: %s\n", message);
}

void TestExactMappings()
{
    struct Case {
        SDL_GPUShaderFormat Native;
        CK_SHADER_FORMAT Format;
        CK_SHADER_PROFILE Profile;
    };
    const Case cases[] = {
        {SDL_GPU_SHADERFORMAT_SPIRV, CKRST_SHADER_FORMAT_SPIRV, CKRST_SHADER_PROFILE_SPIRV},
        {SDL_GPU_SHADERFORMAT_DXBC, CKRST_SHADER_FORMAT_DXBC, CKRST_SHADER_PROFILE_DX11},
        {SDL_GPU_SHADERFORMAT_DXIL, CKRST_SHADER_FORMAT_DXIL, CKRST_SHADER_PROFILE_DX12},
        {SDL_GPU_SHADERFORMAT_MSL, CKRST_SHADER_FORMAT_MSL, CKRST_SHADER_PROFILE_MSL},
        {SDL_GPU_SHADERFORMAT_METALLIB, CKRST_SHADER_FORMAT_METALLIB, CKRST_SHADER_PROFILE_MSL},
    };

    for (const Case &test : cases) {
        CKSdlGpuShaderTarget target;
        Check(CKSdlGpuMapShaderFormat(test.Native, &target), "known SDL_gpu shader format maps");
        Check(target.NativeFormat == test.Native, "native shader format is preserved");
        Check(target.Format == test.Format, "backend shader payload format maps exactly");
        Check(target.Profile == test.Profile, "shader profile maps exactly");
        Check(target.EntryPoint && strcmp(target.EntryPoint, "main") == 0,
              "shader entry point is explicit");
    }
}

void TestMappingRejectsInvalidInputs()
{
    Check(!CKSdlGpuMapShaderFormat(SDL_GPU_SHADERFORMAT_SPIRV, NULL),
          "mapping rejects a missing output target");

    CKSdlGpuShaderTarget target;
    target.NativeFormat = SDL_GPU_SHADERFORMAT_DXIL;
    target.Format = CKRST_SHADER_FORMAT_DXIL;
    target.Profile = CKRST_SHADER_PROFILE_DX12;
    target.EntryPoint = "stale";
    Check(!CKSdlGpuMapShaderFormat(SDL_GPU_SHADERFORMAT_INVALID, &target),
          "mapping rejects an unknown shader format");
    Check(target.NativeFormat == SDL_GPU_SHADERFORMAT_INVALID &&
              target.Format == CKRST_SHADER_FORMAT_UNKNOWN &&
              target.Profile == CKRST_SHADER_PROFILE_UNKNOWN &&
              target.EntryPoint == NULL,
          "failed mapping resets the output target");

    const SDL_GPUShaderFormat composite =
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL;
    Check(!CKSdlGpuMapShaderFormat(composite, &target),
          "mapping rejects a composite format mask");
    Check(target.NativeFormat == SDL_GPU_SHADERFORMAT_INVALID &&
              target.Format == CKRST_SHADER_FORMAT_UNKNOWN &&
              target.Profile == CKRST_SHADER_PROFILE_UNKNOWN,
          "composite mapping leaves no ambiguous target");
}

void TestSelectionUsesIntersection()
{
    CKSdlGpuShaderTarget target;
    const SDL_GPUShaderFormat device =
        SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV;

    Check(CKSdlGpuSelectShaderTarget(device, SDL_GPU_SHADERFORMAT_SPIRV, &target),
          "selector accepts a device/artifact intersection");
    Check(target.NativeFormat == SDL_GPU_SHADERFORMAT_SPIRV,
          "selector never chooses an unavailable artifact");

    Check(!CKSdlGpuSelectShaderTarget(device, SDL_GPU_SHADERFORMAT_MSL, &target),
          "selector rejects disjoint device and artifact formats");
    Check(target.Format == CKRST_SHADER_FORMAT_UNKNOWN &&
              target.Profile == CKRST_SHADER_PROFILE_UNKNOWN,
          "failed selection resets the target");

    Check(!CKSdlGpuSelectShaderTarget(device, SDL_GPU_SHADERFORMAT_SPIRV, NULL),
          "selector rejects a missing output target");
}

void TestPlatformPreferenceIsStable()
{
    CKSdlGpuShaderTarget target;
    const SDL_GPUShaderFormat all =
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXBC |
        SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL |
        SDL_GPU_SHADERFORMAT_METALLIB;
    Check(CKSdlGpuSelectShaderTarget(all, all, &target),
          "selector chooses a preferred platform format");
#if defined(_WIN32)
    Check(target.NativeFormat == SDL_GPU_SHADERFORMAT_DXIL,
          "Windows prefers DXIL");
#elif defined(__APPLE__)
    Check(target.NativeFormat == SDL_GPU_SHADERFORMAT_METALLIB,
          "Apple prefers metallib");
#else
    Check(target.NativeFormat == SDL_GPU_SHADERFORMAT_SPIRV,
          "other platforms prefer SPIR-V");
#endif
}

}

int main()
{
    TestExactMappings();
    TestMappingRejectsInvalidInputs();
    TestSelectionUsesIntersection();
    TestPlatformPreferenceIsStable();
    if (g_Failures != 0) {
        fprintf(stderr, "%d SDL_gpu support test(s) failed\n", g_Failures);
        return 1;
    }
    printf("SDL_gpu support tests passed\n");
    return 0;
}
