// Device and command coverage for the complete SDL_gpu rasterizer.
#include "CKSdlGpuRasterizerContext.h"
#include "CKSdlGpuShaders.h"
#include "CKSdlGpuTextureData.h"
#include "CKFFShaderInterface.h"
#include <cstdio>

int main()
{
    unsigned failures = 0;
    auto check = [&](bool value, const char *name) { if (!value) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); } };
    check(CKSdlGpuTextureFormat(_16_RGB565) == SDL_GPU_TEXTUREFORMAT_B5G6R5_UNORM &&
          CKSdlGpuTextureFormat(_16_ARGB1555) == SDL_GPU_TEXTUREFORMAT_B5G5R5A1_UNORM &&
          CKSdlGpuTextureFormat(_DXT1) == SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM &&
          CKSdlGpuTextureFormat(_DXT3) == SDL_GPU_TEXTUREFORMAT_BC2_RGBA_UNORM &&
          CKSdlGpuTextureFormat(_DXT5) == SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM &&
          CKSdlGpuTextureFormat(_DXT2) == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM &&
          CKSdlGpuTextureFormat(_DXT4) == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
          "premultiplied DXT formats use decoded BGRA storage");
    check(CKSdlGpuValidPresentSync(CKRST_PRESENT_UNCHANGED) &&
          CKSdlGpuValidPresentSync(CKRST_PRESENT_VSYNC) &&
          CKSdlGpuValidPresentSync(CKRST_PRESENT_IMMEDIATE) &&
          !CKSdlGpuValidPresentSync(static_cast<CKPresentSync>(99)),
          "only declared presentation synchronization modes are accepted");
    check(CKSdlGpuCanCopyPresent(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
                                 SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
                                 640, 480, 640, 480) &&
          !CKSdlGpuCanCopyPresent(SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                                  SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
                                  640, 480, 640, 480) &&
          !CKSdlGpuCanCopyPresent(SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
                                  SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
                                  640, 480, 1280, 960),
          "present copy fast path requires identical format and extent");
    check(CKSdlGpuSupportsSwapchainCopy("direct3d12") &&
          !CKSdlGpuSupportsSwapchainCopy("vulkan") &&
          !CKSdlGpuSupportsSwapchainCopy(nullptr),
          "only the verified D3D12 swapchain path uses transfer copy");
    {
        const auto program = CKFFBuildProgramInterface(1, 2, CKRST_SHADER_FORMAT_DXIL);
        CKFFProgramLayout layout;
        layout.Init(program);
        CKFFConstantSet constants;
        layout.Update(constants);
        CKSdlGpuUniformBatch batch;
        CKSdlGpuUniformCursor cursor;
        CKSdlGpuUniformBindings native;
        unsigned first[2 * CKFF_UNIFORM_BUFFER_COUNT] = {};
        unsigned moved[2 * CKFF_UNIFORM_BUFFER_COUNT] = {};
        unsigned fragment[2 * CKFF_UNIFORM_BUFFER_COUNT] = {};
        batch.Snapshot(layout, cursor, first);
        check(layout.Buffers.Size() == 3 &&
              layout.Buffers[0].Stage == CKRST_SHADER_VERTEX && layout.Buffers[0].Slot == 0 &&
              layout.Buffers[0].Size == 512 &&
              layout.Buffers[1].Stage == CKRST_SHADER_VERTEX && layout.Buffers[1].Slot == 1 &&
              layout.Buffers[1].Size == 2368 &&
              layout.Buffers[2].Stage == CKRST_SHADER_PIXEL && layout.Buffers[2].Slot == 0 &&
              layout.Buffers[2].Size == 1424 && batch.Data.Size() == 4304 &&
              native.NeedsPush(layout.Buffers[0], first[0], batch.Data) &&
              native.NeedsPush(layout.Buffers[1], first[1], batch.Data) &&
              native.NeedsPush(layout.Buffers[2], first[2], batch.Data),
              "first FFP draw binds every isolated native buffer");
        float matrix[16] = {}; matrix[12] = 2.0f;
        constants.Set(CKRST_BLOCK_MATRICES, matrix, sizeof(matrix));
        layout.Update(constants);
        batch.Snapshot(layout, cursor, moved);
        check(moved[0] != first[0] && moved[1] == first[1] && moved[2] == first[2] &&
              batch.Data.Size() == 4304 + 512 &&
              native.NeedsPush(layout.Buffers[0], moved[0], batch.Data) &&
              !native.NeedsPush(layout.Buffers[1], moved[1], batch.Data),
              "matrix-only changes snapshot/push 512 vertex bytes");
        const float bump[4] = {0.5f, 1, 0, 0};
        constants.Set(CKRST_BLOCK_BUMP_ENV, bump, sizeof(bump));
        layout.Update(constants);
        batch.Snapshot(layout, cursor, fragment);
        check(fragment[0] == moved[0] && fragment[1] == moved[1] &&
              fragment[2] != moved[2] && batch.Data.Size() == 4304 + 512 + 1424 &&
              !native.NeedsPush(layout.Buffers[0], fragment[0], batch.Data) &&
              native.NeedsPush(layout.Buffers[2], fragment[2], batch.Data),
              "fragment-only changes preserve the vertex buffer version and binding");
        auto shared = std::find_if(program.Uniforms.Begin(), program.Uniforms.End(), [](const auto &uniform) {
            return uniform.Slot == CKRST_BLOCK_DRAW_PARAMS && uniform.Stage == CKRST_SHADER_PIXEL;
        });
        const float factor[4] = {0.1f, 0.2f, 0.3f, 1};
        constants.Set(CKRST_BLOCK_DRAW_PARAMS, factor, sizeof(factor));
        layout.Update(constants);
        check(shared != program.Uniforms.End() &&
              std::memcmp(layout.Data.Begin() + layout.BufferOffset(CKRST_SHADER_PIXEL, 0) + shared->Offset,
                          factor, sizeof(factor)) == 0,
              "logical data used by both stages is copied into each stage's declared range");
    }
    {
        CKFFProgramLayout layout;
        CKFFProgramLayout::Buffer buffer = {CKRST_SHADER_VERTEX, 0, 0, 16};
        layout.Buffers.PushBack(buffer);
        buffer = {CKRST_SHADER_PIXEL, 0, 16, 32};
        layout.Buffers.PushBack(buffer);
        buffer = {CKRST_SHADER_VERTEX, 1, 16, 32};
        layout.Buffers.PushBack(buffer);
        layout.Data.Resize(48);
        memset(layout.Data.Begin(), 1, 48);
        CKSdlGpuUniformBatch batch;
        CKSdlGpuUniformCursor cursor, otherProgram;
        unsigned first[2 * CKFF_UNIFORM_BUFFER_COUNT] = {};
        unsigned second[2 * CKFF_UNIFORM_BUFFER_COUNT] = {};
        unsigned third[2 * CKFF_UNIFORM_BUFFER_COUNT] = {};
        unsigned other[2 * CKFF_UNIFORM_BUFFER_COUNT] = {};
        batch.Snapshot(layout, cursor, first);
        check(batch.Data.Size() == 48 && first[1] == first[2], "shared stage data is snapshotted once");
        for (unsigned i = 0; i < 256; ++i) batch.Snapshot(layout, cursor, second);
        check(batch.Data.Size() == 48 && memcmp(first, second, sizeof(first)) == 0,
              "256 unchanged draws reuse immutable buffer versions");
        layout.Data[0] = 2;
        check(layout.MarkDataChanged(0, 1), "object data mutation advances its buffer version");
        batch.Snapshot(layout, cursor, second);
        check(batch.Data.Size() == 64 && second[0] != first[0] && second[1] == first[1] &&
              batch.Data[first[0]] == 1 && batch.Data[second[0]] == 2,
              "object change leaves the shared fragment data and earlier draw intact");
        // Metadata is written directly by the backend, independently of the
        // producer's revision. It must participate in snapshot identity.
        layout.Data[40] = 3;
        check(layout.MarkDataChanged(40, 1), "sampler metadata mutation advances the shared buffer version");
        batch.Snapshot(layout, cursor, third);
        check(batch.Data.Size() == 96 && third[0] == second[0] && third[1] != second[1] &&
              batch.Data[second[1] + 24] == 1 && batch.Data[third[1] + 24] == 3,
              "sampler metadata changes cannot overwrite or reuse old draw bytes");
        CKSdlGpuUniformBindings bindings;
        check(bindings.NeedsPush(layout.Buffers[0], first[0], batch.Data) &&
              !bindings.NeedsPush(layout.Buffers[0], first[0], batch.Data) &&
              bindings.NeedsPush(layout.Buffers[0], second[0], batch.Data),
              "native push follows byte changes within the pass");
        batch.Snapshot(layout, otherProgram, other);
        check(!bindings.NeedsPush(layout.Buffers[0], other[0], batch.Data),
              "equal bytes at another arena offset do not require a second push");
        check(bindings.NeedsPush(layout.Buffers[1], third[1], batch.Data) &&
              bindings.NeedsPush(layout.Buffers[2], third[2], batch.Data),
              "shared bytes still bind independently to both shader stages");
        bindings.Invalidate();
        check(bindings.NeedsPush(layout.Buffers[0], other[0], batch.Data),
              "pipeline or pass invalidation requires a fresh push");
        batch.Clear();
        layout.Data.Resize(48);
        memset(layout.Data.Begin(), 4, 48);
        check(layout.MarkDataChanged(0, 48) && !layout.MarkDataChanged(47, 2),
              "uniform data revisions validate the modified byte range");
        batch.Snapshot(layout, cursor, first);
        check(batch.Data.Size() == 48 && first[0] == 0 && batch.Data[0] == 4,
              "a new batch never follows a previous batch's offsets");
    }
    {
        CKSdlGpuProgram program;
        program.Identity = 0x10001;
        CKFFSamplerBinding declaration;
        declaration.Stage = CKRST_SHADER_PIXEL; declaration.NativeSlot = 7;
        program.Interface.Samplers.PushBack(declaration);
        CKSdlGpuTable<CKSdlGpuTexture> textures;
        auto texture = std::make_shared<CKSdlGpuTexture>();
        auto handle = textures.Add(texture);
        // Aliased inert tokens exercise ownership without constructing a GPU.
        auto token = std::make_shared<int>(1);
        std::shared_ptr<SDL_GPUSampler> sampler(token, reinterpret_cast<SDL_GPUSampler *>(token.get()));
        CKSdlGpuBindingBatch batch;
        CKSdlGpuBindingBatch::Inputs inputs;
        inputs.Hash = program.Identity;
        inputs.Textures[0] = texture.get(); inputs.Samplers[0] = sampler.get();
        inputs.TextureOwners[0] = &textures.Borrow(handle); inputs.SamplerOwners[0] = &sampler;
        inputs.Hash = (inputs.Hash * 16777619u) ^ (reinterpret_cast<uintptr_t>(texture.get()) >> 4);
        inputs.Hash = (inputs.Hash * 16777619u) ^ (reinterpret_cast<uintptr_t>(sampler.get()) >> 4);
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
        inputs.Textures[0] = replacementTexture.get();
        inputs.TextureOwners[0] = &textures.Borrow(replacementHandle);
        inputs.Hash = program.Identity;
        inputs.Hash = (inputs.Hash * 16777619u) ^ (reinterpret_cast<uintptr_t>(replacementTexture.get()) >> 4);
        inputs.Hash = (inputs.Hash * 16777619u) ^ (reinterpret_cast<uintptr_t>(sampler.get()) >> 4);
        const unsigned replacement = batch.Intern(program, inputs);
        check(replacement != original && batch[original].Textures[0] == texture.get() &&
              batch[replacement].Textures[0] == replacementTexture.get(), "deleted texture and reused handle keep separate group lifetimes");
        inputs.Textures[0] = texture.get(); inputs.TextureOwners[0] = &texture;
        inputs.Hash = program.Identity;
        inputs.Hash = (inputs.Hash * 16777619u) ^ (reinterpret_cast<uintptr_t>(texture.get()) >> 4);
        inputs.Hash = (inputs.Hash * 16777619u) ^ (reinterpret_cast<uintptr_t>(sampler.get()) >> 4);
        check(batch.Intern(program, inputs) == original, "nonconsecutive draws reuse an earlier group");
        auto secondToken = std::make_shared<int>(2);
        std::shared_ptr<SDL_GPUSampler> secondSampler(secondToken, reinterpret_cast<SDL_GPUSampler *>(secondToken.get()));
        inputs.Samplers[0] = secondSampler.get(); inputs.SamplerOwners[0] = &secondSampler;
        inputs.Hash = program.Identity;
        inputs.Hash = (inputs.Hash * 16777619u) ^ (reinterpret_cast<uintptr_t>(texture.get()) >> 4);
        inputs.Hash = (inputs.Hash * 16777619u) ^ (reinterpret_cast<uintptr_t>(secondSampler.get()) >> 4);
        check(batch.Intern(program, inputs) != original, "sampler state distinguishes binding groups");
        program.Identity = 0x20001;
        inputs.Hash = program.Identity;
        inputs.Hash = (inputs.Hash * 16777619u) ^ (reinterpret_cast<uintptr_t>(texture.get()) >> 4);
        inputs.Hash = (inputs.Hash * 16777619u) ^ (reinterpret_cast<uintptr_t>(secondSampler.get()) >> 4);
        check(batch.Intern(program, inputs) == 3, "program generation distinguishes native slot layouts");
        batch.Clear();
        check(batch.Size() == 0 && texture.use_count() == 1, "batch completion releases resource ownership");
    }
    {
        CKSdlGpuDrawResourceBatch batch;
        auto program = std::make_shared<CKSdlGpuProgram>();
        auto layout = std::make_shared<CKSdlGpuLayout>();
        auto buffer = std::make_shared<CKSdlGpuBuffer>();
        check(batch.Retain(program) == program.get() && batch.Retain(program) == program.get() &&
              batch.Retain(layout) == layout.get() && batch.Retain(buffer) == buffer.get() &&
              program.use_count() == 2 && layout.use_count() == 2 && buffer.use_count() == 2,
              "draw resources are retained once per distinct batch resource");
        batch.Clear();
        check(program.use_count() == 1 && layout.use_count() == 1 && buffer.use_count() == 1,
              "draw resource ownership ends with the encoded batch");
    }
    {
        CKSdlGpuRasterizerContext context;
        check(context.CompleteEmptySubmissionsForTests(),
              "empty submissions complete in order");
    }
    {
        CKSdlGpuBuffer buffer;
        const CKWORD initial[] = {4, 1, 7, 2, 6, 3};
        buffer.IndexData.Resize(sizeof(initial));
        std::memcpy(buffer.IndexData.Begin(), initial, sizeof(initial));
        unsigned maximum = 0;
        check(buffer.FindMaxIndex(1, 4, false, maximum) && maximum == 7,
              "16-bit persistent index ranges find their maximum");
        const CKWORD replacement = 5;
        std::memcpy(buffer.IndexData.Begin() + 2 * sizeof(CKWORD),
                    &replacement, sizeof(replacement));
        check(buffer.FindMaxIndex(1, 4, false, maximum) && maximum == 7,
              "persistent index range lookup reuses the validated cache");
        buffer.InvalidateIndexRanges();
        check(buffer.FindMaxIndex(1, 4, false, maximum) && maximum == 6,
              "buffer updates invalidate persistent index range results");
        const CKDWORD wide[] = {0x10002u, 9u, 0x10001u};
        buffer.IndexData.Resize(sizeof(wide));
        std::memcpy(buffer.IndexData.Begin(), wide, sizeof(wide));
        buffer.InvalidateIndexRanges();
        check(buffer.FindMaxIndex(0, 3, true, maximum) && maximum == 0x10002u &&
              !buffer.FindMaxIndex(3, 1, true, maximum),
              "32-bit index ranges preserve width and reject out-of-bounds requests");
        check(buffer.FindMaxIndex(0, 3, false, maximum) && maximum == 9,
              "persistent index range cache keys include the index element width");
    }
    {
        unsigned char blocks[32] = {};
        VxImageDescEx image;
        XArray<unsigned char> pixels;
        auto decode = [&](VX_PIXELFORMAT format, unsigned width = 4, unsigned height = 4) {
            VxPixelFormat2ImageDesc(format, image);
            image.Width = width; image.Height = height; image.TotalImageSize = sizeof(blocks); image.Image = blocks;
            return CKSdlGpuDecodeDXT(image, pixels);
        };
        auto pixel = [&](unsigned index) { CKDWORD value = 0; std::memcpy(&value, pixels.Begin() + index * 4, 4); return value; };
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
        check(decode(_DXT1, 5, 3) && pixels.Size() == 60 && pixel(4) == 0xff00ff00 &&
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
        std::memset(blocks, 0, sizeof(blocks));
        blocks[0] = 0x08; // alpha 136, then zero
        blocks[1] = 0x01; // alpha 17, then zero
        blocks[9] = 0x78; // premultiplied red endpoint decodes to 123
        check(decode(_DXT2) && pixel(0) == 0x88e70000 && pixel(1) == 0 &&
              pixel(2) == 0x11ff0000,
              "DXT2 unpremultiplies explicit alpha, zeroes transparent pixels and clamps color");
        std::memset(blocks, 0, sizeof(blocks));
        blocks[8] = 31; blocks[11] = 248;
        for (unsigned y = 0; y < 4; ++y) blocks[12 + y] = 0xff;
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
        std::memset(blocks, 0, sizeof(blocks));
        blocks[0] = 128; blocks[2] = 0x08; // alpha 128, then zero
        blocks[9] = 0x78;
        check(decode(_DXT4) && pixel(0) == 0x80f50000 && pixel(1) == 0,
              "DXT4 unpremultiplies interpolated alpha and zeroes transparent pixels");
    }
    for (auto format : {SDL_GPU_SHADERFORMAT_DXIL, SDL_GPU_SHADERFORMAT_SPIRV}) {
        CKFFShaderSet set;
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
        mismatched = set;
        mismatched.InterfaceHash = CKFF_SHADER_INTERFACE_HASH;
        check(!mismatched.Matches(payload, profile), "old shared native layout hash is rejected");
        check(set.Shaders[CKRST_SHADER_FF_3D].UniformBufferCount == 2 &&
              set.Shaders[CKRST_SHADER_FF_3D_CLIP].UniformBufferCount == 2 &&
              set.Shaders[CKRST_SHADER_FF_POSITIONT].UniformBufferCount == 1 &&
              set.Shaders[CKRST_SHADER_FF_POSITIONT_CLIP].UniformBufferCount == 1 &&
              set.Shaders[CKRST_SHADER_FF_FRAGMENT].UniformBufferCount == 1,
              "native FFP shaders match variant-specific buffer layouts");
        check(set.Shaders[CKRST_SHADER_FF_FRAGMENT].SamplerCount == 16, "FFP logical slots retained");
        check(set.Shaders[CKRST_SHADER_PRESENT_FRAGMENT].SamplerCount == 1, "presentation uses native slot zero");
        CKShaderDesc vertex, fragment;
        check(CKSdlGpuNativeClearShaders(format, vertex, fragment) && vertex.UniformBufferCount == 1 &&
              fragment.UniformBufferCount == 1 && !vertex.SamplerCount && !fragment.SamplerCount,
              "clear shaders have private uniforms and no samplers");
        auto clearProgram = CKSdlGpuNativeProgram(1, 2, CKSDL_NATIVE_CLEAR);
        check(CKFFValidateProgram(clearProgram, vertex, fragment) == CK_OK &&
              clearProgram.UniformBuffers.Size() == 2 && clearProgram.UniformBuffers[0].Size == 16 &&
              clearProgram.UniformBuffers[1].Size == 16 && clearProgram.Uniforms.Size() == 2 &&
              clearProgram.Uniforms[0].Name == "ckClear" && clearProgram.Uniforms[1].Name == "ckClear" &&
              clearProgram.VertexInputs.Size() == 0 && clearProgram.Samplers.Size() == 0,
              "clear program declares independent 16-byte vertex and fragment data");
        check(CKSdlGpuNativeVolumeShaders(format, vertex, fragment) && vertex.UniformBufferCount == 1 &&
              fragment.UniformBufferCount == 1 && fragment.SamplerCount == 1,
              "volume mip helper exposes its own native shader family");
        auto volumeProgram = CKSdlGpuNativeProgram(1, 2, CKSDL_NATIVE_VOLUME);
        check(CKFFValidateProgram(volumeProgram, vertex, fragment) == CK_OK &&
              volumeProgram.UniformBuffers[0].Size == 16 && volumeProgram.UniformBuffers[1].Size == 32 &&
              volumeProgram.Uniforms[1].Name == "ckVolumeParams" && volumeProgram.Uniforms[1].Count == 2 &&
              volumeProgram.Samplers.Size() == 1 && volumeProgram.Samplers[0].Slot == 0 &&
              volumeProgram.Samplers[0].Dimension == CKFF_TEXTURE_3D &&
              volumeProgram.Samplers[0].MetadataBufferSlot == UINT32_MAX && volumeProgram.VertexInputs.Size() == 0,
              "volume helper uses 32-byte fragment parameters and one independent volume sampler");
        CKFFProgramLayout layout;
        layout.Init(volumeProgram);
        check(layout.Data.Size() == 48 && layout.BufferOffset(CKRST_SHADER_VERTEX, 0) == 0 &&
              layout.BufferOffset(CKRST_SHADER_PIXEL, 0) == 16,
              "private image operations do not allocate or share the FFP constant layout");
    }
    CKFFShaderSet rejected;
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
