#include "CKSdlGpuInternal.h"

CKERROR CKSdlGpuDevice::CopyVolumeSlice(CKSdlGpuTexture &texture, unsigned mip, unsigned layer,
                                      SDL_GPUTexture *slice, bool toVolume)
{
    auto *copy = SDL_BeginGPUCopyPass(Commands);
    if (!copy) return Fail("BeginGPUCopyPass.volumeSlice");
    SDL_GPUTextureLocation volume = {}, plane = {};
    volume.texture = texture.Image.get(); volume.mip_level = mip; volume.z = layer;
    plane.texture = slice;
    SDL_CopyGPUTextureToTexture(copy, toVolume ? &plane : &volume, toVolume ? &volume : &plane,
        std::max(1u, texture.Info.width >> mip), std::max(1u, texture.Info.height >> mip), 1, false);
    SDL_EndGPUCopyPass(copy);
    return CK_OK;
}

CKERROR CKSdlGpuDevice::GenerateVolumeMips(CKSdlGpuTexture &texture)
{
    if (texture.Info.num_levels <= 1) return CK_OK;
    if (!EnsureCommands()) return Error;
    CKSdlGpuDraw draw;
    draw.Program = VolumeMipProgram;
    draw.State.State.Lo = CKRST_STATE_WRITE_RGBA;
    draw.State.State.Mid = CKRST_STATE_PT(VX_TRIANGLELIST);
    draw.State.State.Hi = 0;
    auto pipeline = Pipeline(draw, texture.Info.format, SDL_GPU_TEXTUREFORMAT_INVALID, SDL_GPU_SAMPLECOUNT_1);
    CKSamplerDesc samplerDesc = {CKRST_FILTER_NEAREST, CKRST_FILTER_NEAREST, CKRST_FILTER_NONE,
        CKRST_ADDRESS_CLAMP, CKRST_ADDRESS_CLAMP, CKRST_ADDRESS_CLAMP, 0, CKRST_COMPARE_NONE};
    auto sampler = Sampler(samplerDesc);
    if (!pipeline || !sampler) return Error;
    const float vertexParams[4] = {};
    float params[8] = {};
    for (unsigned mip = 1; mip < texture.Info.num_levels; ++mip) {
        auto inputInfo = texture.Info;
        inputInfo.width = std::max(1u, texture.Info.width >> (mip - 1));
        inputInfo.height = std::max(1u, texture.Info.height >> (mip - 1));
        inputInfo.layer_count_or_depth = CKSdlGpuTextureLayers(texture, mip - 1);
        inputInfo.num_levels = 1;
        inputInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        auto input = CKSdlGpuOwn(Device, SDL_CreateGPUTexture(Device, &inputInfo), SDL_ReleaseGPUTexture);
        if (!input) return Fail("CreateGPUTexture.volumeMip");
        // Native sampling views cover a complete mip chain. Snapshot the input
        // so no shader view can overlap the output attachment's resource state.
        auto *copy = SDL_BeginGPUCopyPass(Commands);
        if (!copy) return Fail("BeginGPUCopyPass.volumeMip");
        SDL_GPUTextureLocation source = {}, destination = {};
        source.texture = texture.Image.get(); source.mip_level = mip - 1;
        destination.texture = input.get();
        SDL_CopyGPUTextureToTexture(copy, &source, &destination, inputInfo.width, inputInfo.height,
            inputInfo.layer_count_or_depth, false);
        SDL_EndGPUCopyPass(copy);
        const unsigned width = std::max(1u, inputInfo.width >> 1), height = std::max(1u, inputInfo.height >> 1);
        const unsigned depth = std::max(1u, inputInfo.layer_count_or_depth >> 1);
        auto outputInfo = inputInfo;
        outputInfo.type = SDL_GPU_TEXTURETYPE_2D; outputInfo.layer_count_or_depth = 1;
        outputInfo.width = width; outputInfo.height = height;
        outputInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        auto output = CKSdlGpuOwn(Device, SDL_CreateGPUTexture(Device, &outputInfo), SDL_ReleaseGPUTexture);
        if (!output) return Fail("CreateGPUTexture.volumeMipOutput");
        params[0] = float(inputInfo.width); params[1] = float(inputInfo.height); params[2] = float(inputInfo.layer_count_or_depth);
        params[4] = float(width); params[5] = float(height); params[6] = float(depth);
        SDL_GPUTextureSamplerBinding binding = {input.get(), sampler.get()};
        for (unsigned z = 0; z < depth; ++z) {
            SDL_GPUColorTargetInfo target = {};
            target.texture = output.get();
            target.load_op = SDL_GPU_LOADOP_DONT_CARE; target.store_op = SDL_GPU_STOREOP_STORE;
            auto *pass = SDL_BeginGPURenderPass(Commands, &target, 1, nullptr);
            if (!pass) return Fail("BeginGPURenderPass.volumeMip");
            params[7] = float(z);
            SDL_BindGPUGraphicsPipeline(pass, pipeline.get());
            SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
            SDL_PushGPUVertexUniformData(Commands, 0, vertexParams, unsigned(sizeof(vertexParams)));
            SDL_PushGPUFragmentUniformData(Commands, 0, params, unsigned(sizeof(params)));
            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
            SDL_EndGPURenderPass(pass);
            if (CopyVolumeSlice(texture, mip, z, output.get(), true) != CK_OK) return Error;
            texture.Defined[mip * texture.Info.layer_count_or_depth + z] = true;
        }
        // SDL defers destruction while the encoded commands reference input.
    }
    return CK_OK;
}
