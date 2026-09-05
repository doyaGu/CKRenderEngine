#include "CKSdlGpuInternal.h"
#include "CKSdlGpuShaders.h"
#include "CKSdlGpuTextureData.h"
#include <cstdio>

int main()
{
    unsigned failures = 0;
    auto check = [&](bool value, const char *name) { if (!value) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); } };
    {
        unsigned char blocks[32] = {};
        VxImageDescEx image;
        std::vector<unsigned char> pixels;
        auto decode = [&](VX_PIXELFORMAT format, unsigned width = 4, unsigned height = 4) {
            VxPixelFormat2ImageDesc(format, image);
            image.Width = width; image.Height = height; image.TotalImageSize = sizeof(blocks); image.Image = blocks;
            return CKSdlGpuDecodeDXT(image, pixels);
        };
        auto pixel = [&](unsigned index) { CKDWORD value = 0; std::memcpy(&value, pixels.data() + index * 4, 4); return value; };
        // BC1 endpoints blue < red: indices 0,1,2,3 include transparent black.
        blocks[0] = 31; blocks[3] = 248;
        for (unsigned y = 0; y < 4; ++y) blocks[4 + y] = 0xe4;
        check(decode(_DXT1), "decode BC1");
        check(pixel(0) == 0xff0000ff && pixel(1) == 0xffff0000 &&
              pixel(2) == 0xff7f007f && pixel(3) == 0, "BC1 transparent palette");
        blocks[0] = 0; blocks[1] = 248; blocks[2] = 31; blocks[3] = 0;
        check(decode(_DXT1) && pixel(2) == 0xffaa0055 && pixel(3) == 0xff5500aa, "BC1 opaque palette");
        // A second green block covers the clipped last column of a 5x3 image.
        blocks[8] = 224; blocks[9] = 7;
        check(decode(_DXT1, 5, 3) && pixels.size() == 60 && pixel(4) == 0xff00ff00 &&
              pixel(14) == 0xff00ff00, "BC1 odd dimensions retain edge blocks");
        image.TotalImageSize = 15;
        check(!CKSdlGpuDecodeDXT(image, pixels), "reject incomplete BC payload");
        std::memset(blocks, 0, sizeof(blocks));
        blocks[8] = 31; blocks[11] = 248;
        for (unsigned y = 0; y < 4; ++y) blocks[12 + y] = 0xff;
        for (unsigned i = 0; i < 8; ++i) blocks[i] = (2 * i) | ((2 * i + 1) << 4);
        check(decode(_DXT3), "decode BC2");
        for (unsigned i = 0; i < 16; ++i)
            check(pixel(i) == ((i * 17u << 24) | 0x00aa0055), "BC2 explicit alpha and four-color palette");
        uint64_t indices = 0;
        for (unsigned i = 0; i < 16; ++i) indices |= uint64_t(i % 8) << (i * 3);
        for (unsigned i = 0; i < 6; ++i) blocks[2 + i] = (unsigned char)(indices >> (i * 8));
        blocks[0] = 210; blocks[1] = 0;
        check(decode(_DXT5), "decode BC3 interpolated alpha");
        const unsigned alpha7[] = {210, 0, 180, 150, 120, 90, 60, 30};
        for (unsigned i = 0; i < 16; ++i) check(pixel(i) == ((alpha7[i % 8] << 24) | 0x00aa0055), "BC3 seven-step alpha");
        blocks[0] = 0; blocks[1] = 200;
        check(decode(_DXT5), "decode BC3 explicit extremes");
        const unsigned alpha5[] = {0, 200, 40, 80, 120, 160, 0, 255};
        for (unsigned i = 0; i < 16; ++i) check(pixel(i) == ((alpha5[i % 8] << 24) | 0x00aa0055), "BC3 five-step alpha");
    }
    for (auto format : {SDL_GPU_SHADERFORMAT_DXIL, SDL_GPU_SHADERFORMAT_SPIRV}) {
        CKBackendShaderSet set;
        check(CKSdlGpuShaderSet(format, set) != FALSE, "complete native shader family");
        const auto payload = set.Shaders[0].Format, profile = set.Shaders[0].Profile;
        check(payload != CKRST_SHADER_FORMAT_BGFX, "native artifact ownership");
        for (unsigned role = 0; role < CKRST_BUILTIN_SHADER_COUNT; ++role) {
            auto incomplete = set;
            incomplete.Shaders[role].Code = nullptr;
            check(!incomplete.Matches(payload, profile), "missing variant excludes family");
        }
        auto mismatched = set;
        mismatched.InterfaceHash ^= 1;
        check(!mismatched.Matches(payload, profile), "ABI mismatch excludes family");
        check(set.Shaders[CKRST_SHADER_FF_FRAGMENT].SamplerCount == 16, "FFP logical slots retained");
        check(set.Shaders[CKRST_SHADER_PRESENT_FRAGMENT].SamplerCount == 1, "presentation uses native slot zero");
        CKShaderDesc vertex, fragment;
        check(CKSdlGpuNativeClearShaders(format, vertex, fragment) && vertex.UniformBufferCount == 1 &&
              fragment.UniformBufferCount == 1 && !vertex.SamplerCount && !fragment.SamplerCount,
              "clear shaders have private uniforms and no samplers");
        auto clearProgram = CKSdlGpuNativeProgram(1, 2, false);
        check(CKValidateBackendProgram(clearProgram, vertex, fragment) == CK_OK &&
              clearProgram.UniformBuffers.size() == 2 && clearProgram.UniformBuffers[0].Size == 16 &&
              clearProgram.UniformBuffers[1].Size == 16 && clearProgram.Uniforms.size() == 2 &&
              clearProgram.Uniforms[0].Name == "ckClear" && clearProgram.Uniforms[1].Name == "ckClear" &&
              clearProgram.VertexInputs.empty() && clearProgram.Samplers.empty(),
              "clear program declares independent 16-byte vertex and fragment data");
        check(CKSdlGpuNativeVolumeShaders(format, vertex, fragment) && vertex.UniformBufferCount == 1 &&
              fragment.UniformBufferCount == 1 && fragment.SamplerCount == 1,
              "volume mip helper exposes its own native shader family");
        auto volumeProgram = CKSdlGpuNativeProgram(1, 2, true);
        check(CKValidateBackendProgram(volumeProgram, vertex, fragment) == CK_OK &&
              volumeProgram.UniformBuffers[0].Size == 16 && volumeProgram.UniformBuffers[1].Size == 32 &&
              volumeProgram.Uniforms[1].Name == "ckVolumeParams" && volumeProgram.Uniforms[1].Count == 2 &&
              volumeProgram.Samplers.size() == 1 && volumeProgram.Samplers[0].Slot == 0 &&
              volumeProgram.Samplers[0].Dimension == CKBACKEND_TEXTURE_3D &&
              volumeProgram.Samplers[0].MetadataBufferSlot == ~0u && volumeProgram.VertexInputs.empty(),
              "volume helper uses 32-byte fragment parameters and one independent volume sampler");
        CKBackendProgramLayout layout;
        layout.Init(volumeProgram);
        check(layout.Data.size() == 48 && layout.BufferOffset(CKRST_SHADER_VERTEX, 0) == 0 &&
              layout.BufferOffset(CKRST_SHADER_PIXEL, 0) == 16,
              "private image operations do not allocate or share the FFP constant layout");
    }
    CKBackendShaderSet rejected;
    check(!CKSdlGpuShaderSet(SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV, rejected), "ambiguous payload rejected");
    CKShaderDesc invalidVertex, invalidFragment;
    check(!CKSdlGpuNativeClearShaders(SDL_GPU_SHADERFORMAT_INVALID, invalidVertex, invalidFragment) &&
          !CKSdlGpuNativeVolumeShaders(SDL_GPU_SHADERFORMAT_INVALID, invalidVertex, invalidFragment),
          "private helper families reject unsupported payload formats");
    CKSdlGpuTable<int> handles;
    auto old = handles.Add(std::make_shared<int>(10));
    auto reference = handles.Get(old);
    check(handles.Remove(old), "remove live resource");
    auto replacement = handles.Add(std::make_shared<int>(20));
    check(old != replacement && !handles.Get(old), "reuse does not revive stale handle");
    check(*reference == 10 && *handles.Get(replacement) == 20, "recorded references retain original resource");
    std::printf("SDL_gpu rasterizer: %u failure(s)\n", failures);
    return failures ? 1 : 0;
}
