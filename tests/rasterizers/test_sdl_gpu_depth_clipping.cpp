#include "CKSdlGpuNativeShaders.h"
#include "CKSdlGpuShaderPack.h"

#include <SDL3/SDL.h>
#include <stdio.h>

bool CheckSdlGpuDepthClipping()
{
    const char *driver = SDL_getenv("CKRE_SDL_GPU_DRIVER");
    if (driver && SDL_strcmp(driver, "auto") == 0) driver = NULL;
    // Use the same validation mode as the rasterizer.
    const char *debugSetting = SDL_getenv("CKRE_SDL_GPU_DEBUG");
    const bool debug = debugSetting && SDL_strcmp(debugSetting, "0") != 0;
    SDL_GPUDevice *device = SDL_CreateGPUDevice(CKSdlGpuShaderPackFormats(), debug, driver);
    if (!device) {
        fprintf(stderr, "SDL depth clipping device creation failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_GPUShader *vertex = NULL, *fragment = NULL;
    SDL_GPUTexture *color = NULL, *depth = NULL;
    SDL_GPUTransferBuffer *download = NULL;
    SDL_GPUGraphicsPipeline *pipeline = NULL;
    SDL_GPUCommandBuffer *commands = NULL;
    SDL_GPUFence *fence = NULL;
    const bool ok = [&]() {
        bool pixelsMatch = true;
        const SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
        const SDL_GPUShaderFormat format = formats & SDL_GPU_SHADERFORMAT_DXIL ? SDL_GPU_SHADERFORMAT_DXIL :
            formats & SDL_GPU_SHADERFORMAT_MSL ? SDL_GPU_SHADERFORMAT_MSL : SDL_GPU_SHADERFORMAT_SPIRV;
        CKShaderDesc vs, fs;
        if (!CKSdlGpuLoadShaders(format) || !CKSdlGpuNativeClearShaders(format, vs, fs)) return false;
        SDL_GPUShaderCreateInfo shader = {};
        shader.code = vs.Code; shader.code_size = vs.CodeSize; shader.entrypoint = vs.EntryPoint;
        shader.format = format; shader.stage = SDL_GPU_SHADERSTAGE_VERTEX; shader.num_uniform_buffers = 1;
        vertex = SDL_CreateGPUShader(device, &shader);
        shader.code = fs.Code; shader.code_size = fs.CodeSize; shader.entrypoint = fs.EntryPoint;
        shader.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        fragment = SDL_CreateGPUShader(device, &shader);
        if (!vertex || !fragment) return false;
        SDL_GPUTextureCreateInfo texture = {};
        texture.type = SDL_GPU_TEXTURETYPE_2D; texture.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        texture.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET; texture.width = texture.height = 4;
        texture.layer_count_or_depth = texture.num_levels = 1;
        color = SDL_CreateGPUTexture(device, &texture);
        texture.format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        texture.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        depth = SDL_CreateGPUTexture(device, &texture);
        SDL_GPUTransferBufferCreateInfo transfer = {};
        transfer.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD; transfer.size = 256 * 4;
        download = SDL_CreateGPUTransferBuffer(device, &transfer);
        if (!color || !depth || !download) return false;
        for (int clip = 0; clip < 2; ++clip) for (int test = 0; test < 2; ++test) {
            SDL_GPUColorTargetDescription target = {};
            target.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
            SDL_GPUGraphicsPipelineCreateInfo info = {};
            info.vertex_shader = vertex; info.fragment_shader = fragment;
            info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            info.rasterizer_state.enable_depth_clip = clip != 0;
            info.depth_stencil_state.enable_depth_test = test != 0;
            info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_ALWAYS;
            info.target_info.color_target_descriptions = &target; info.target_info.num_color_targets = 1;
            info.target_info.has_depth_stencil_target = true;
            info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
            pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
            if (!pipeline) return false;
            for (int plane = 0; plane < 2; ++plane) {
                const float position[4] = {plane ? 1.5f : -0.5f, 0, 0, 0};
                const float red[4] = {1, 0, 0, 1};
                commands = SDL_AcquireGPUCommandBuffer(device);
                if (!commands) return false;
                SDL_PushGPUVertexUniformData(commands, 0, position, sizeof(position));
                SDL_PushGPUFragmentUniformData(commands, 0, red, sizeof(red));
                SDL_GPUColorTargetInfo colorTarget = {};
                colorTarget.texture = color; colorTarget.clear_color.a = 1;
                colorTarget.load_op = SDL_GPU_LOADOP_CLEAR; colorTarget.store_op = SDL_GPU_STOREOP_STORE;
                SDL_GPUDepthStencilTargetInfo depthTarget = {};
                depthTarget.texture = depth; depthTarget.clear_depth = 1;
                depthTarget.load_op = SDL_GPU_LOADOP_CLEAR; depthTarget.store_op = SDL_GPU_STOREOP_DONT_CARE;
                depthTarget.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
                depthTarget.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
                SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &colorTarget, 1, &depthTarget);
                if (!pass) return false;
                SDL_BindGPUGraphicsPipeline(pass, pipeline);
                SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
                SDL_EndGPURenderPass(pass);
                SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(commands);
                if (!copy) return false;
                SDL_GPUTextureRegion source = {};
                source.texture = color; source.w = source.h = 4; source.d = 1;
                SDL_GPUTextureTransferInfo destination = {};
                destination.transfer_buffer = download; destination.pixels_per_row = 64; destination.rows_per_layer = 4;
                SDL_DownloadFromGPUTexture(copy, &source, &destination);
                SDL_EndGPUCopyPass(copy);
                fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
                commands = NULL;
                if (!fence || !SDL_WaitForGPUFences(device, true, &fence, 1)) return false;
                const unsigned char *pixels = static_cast<const unsigned char *>(SDL_MapGPUTransferBuffer(device, download, false));
                if (!pixels) return false;
                const unsigned char *pixel = pixels + 2 * 256 + 2 * 4;
                const bool matches = pixel[0] == (clip ? 0 : 255) && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 255;
                printf("SDL depth clipping: clip=%d test=%d z=%g RGBA=%u,%u,%u,%u\n",
                       clip, test, position[0], unsigned(pixel[0]), unsigned(pixel[1]), unsigned(pixel[2]), unsigned(pixel[3]));
                SDL_UnmapGPUTransferBuffer(device, download);
                SDL_ReleaseGPUFence(device, fence); fence = NULL;
                if (!matches) pixelsMatch = false;
            }
            SDL_ReleaseGPUGraphicsPipeline(device, pipeline); pipeline = NULL;
        }
        return pixelsMatch;
    }();
    if (!ok) fprintf(stderr, "SDL depth clipping failed: %s\n", SDL_GetError());
    if (commands) SDL_CancelGPUCommandBuffer(commands);
    if (fence) SDL_ReleaseGPUFence(device, fence);
    if (pipeline) SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    if (download) SDL_ReleaseGPUTransferBuffer(device, download);
    if (color) SDL_ReleaseGPUTexture(device, color);
    if (depth) SDL_ReleaseGPUTexture(device, depth);
    if (vertex) SDL_ReleaseGPUShader(device, vertex);
    if (fragment) SDL_ReleaseGPUShader(device, fragment);
    SDL_DestroyGPUDevice(device);
    return ok;
}
