#include "CKSdlGpuInternal.h"
#include "CKSdlGpuShaders.h"
#include "CKSdlGpuTextureData.h"
#include <cstdio>

int main()
{
    unsigned failures = 0;
    auto check = [&](bool value, const char *name) { if (!value) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); } };
    {
        CKBackendProgramLayout layout;
        layout.Buffers = {{CKRST_SHADER_VERTEX, 0, 0, 16}, {CKRST_SHADER_PIXEL, 0, 16, 32},
                          {CKRST_SHADER_VERTEX, 1, 16, 32}};
        layout.Data.resize(48, 1);
        CKSdlGpuUniformBatch batch;
        CKSdlGpuUniformCursor cursor, otherProgram;
        std::array<unsigned, 2 * CKBACKEND_MAX_UNIFORM_BUFFERS> first = {}, second = {}, third = {}, other = {};
        batch.Snapshot(layout, cursor, first);
        check(batch.Data.size() == 48 && first[1] == first[2], "shared stage data is snapshotted once");
        for (unsigned i = 0; i < 256; ++i) batch.Snapshot(layout, cursor, second);
        check(batch.Data.size() == 48 && first == second, "256 unchanged draws reuse immutable buffer versions");
        layout.Data[0] = 2;
        batch.Snapshot(layout, cursor, second);
        check(batch.Data.size() == 64 && second[0] != first[0] && second[1] == first[1] &&
              batch.Data[first[0]] == 1 && batch.Data[second[0]] == 2,
              "object change leaves the shared fragment data and earlier draw intact");
        // Metadata is written directly by the backend, independently of the
        // producer's revision. It must participate in snapshot identity.
        layout.Data[40] = 3;
        batch.Snapshot(layout, cursor, third);
        check(batch.Data.size() == 96 && third[0] == second[0] && third[1] != second[1] &&
              batch.Data[second[1] + 24] == 1 && batch.Data[third[1] + 24] == 3,
              "sampler metadata changes cannot overwrite or reuse old draw bytes");
        batch.Clear();
        layout.Data.assign(48, 4);
        batch.Snapshot(layout, cursor, first);
        check(batch.Data.size() == 48 && first[0] == 0 && batch.Data[0] == 4,
              "a new batch never follows a previous batch's offsets");
    }
    {
        CKSdlGpuProgram program;
        program.Identity = 0x10001;
        CKBackendSamplerBinding declaration;
        declaration.Stage = CKRST_SHADER_PIXEL; declaration.NativeSlot = 7;
        program.Interface.Samplers.push_back(declaration);
        CKSdlGpuTable<CKSdlGpuTexture> textures;
        auto texture = std::make_shared<CKSdlGpuTexture>();
        auto handle = textures.Add(texture);
        // Aliased inert tokens exercise ownership without constructing a GPU.
        auto token = std::make_shared<int>(1);
        std::shared_ptr<SDL_GPUSampler> sampler(token, reinterpret_cast<SDL_GPUSampler *>(token.get()));
        CKSdlGpuBindingBatch batch;
        CKSdlGpuBindingBatch::Inputs inputs;
        inputs.Textures[0] = &textures.Borrow(handle); inputs.Samplers[0] = &sampler;
        const unsigned original = batch.Intern(program, inputs);
        const long owners = texture.use_count();
        for (unsigned i = 0; i < 256; ++i)
            check(batch.Intern(program, inputs) == original, "repeated draw finds its binding group");
        check(batch.Size() == 1 && texture.use_count() == owners,
              "resource retention follows unique binding groups, not draws");
        check(batch[original].Fragment[7].sampler == sampler.get(), "logical declaration maps to the native slot");
        batch.MarkReferenced();
        check(texture->Referenced, "encoded binding groups mark sampled textures for version preservation");
        textures.Remove(handle);
        auto replacementTexture = std::make_shared<CKSdlGpuTexture>();
        auto replacementHandle = textures.Add(replacementTexture);
        inputs.Textures[0] = &textures.Borrow(replacementHandle);
        const unsigned replacement = batch.Intern(program, inputs);
        check(replacement != original && batch[original].Textures[0] == texture &&
              batch[replacement].Textures[0] == replacementTexture, "deleted texture and reused handle keep separate group lifetimes");
        inputs.Textures[0] = &texture;
        check(batch.Intern(program, inputs) == original, "nonconsecutive draws reuse an earlier group");
        auto secondToken = std::make_shared<int>(2);
        std::shared_ptr<SDL_GPUSampler> secondSampler(secondToken, reinterpret_cast<SDL_GPUSampler *>(secondToken.get()));
        inputs.Samplers[0] = &secondSampler;
        check(batch.Intern(program, inputs) != original, "sampler state distinguishes binding groups");
        program.Identity = 0x20001;
        check(batch.Intern(program, inputs) == 3, "program generation distinguishes native slot layouts");
        batch.Clear();
        check(batch.Size() == 0 && texture.use_count() == 1, "batch completion releases resource ownership");
    }
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
