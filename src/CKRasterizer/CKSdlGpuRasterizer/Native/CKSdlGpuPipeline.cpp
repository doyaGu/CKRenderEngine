#include "CKSdlGpuRasterizerContext.h"

SDL_GPUVertexElementFormat CKSdlGpuVertexFormat(const CKVertexElementDesc &e)
{
    if (e.Count < 1 || e.Count > 4) return SDL_GPU_VERTEXELEMENTFORMAT_INVALID;
    if (e.Type == CKRST_ATTRIBTYPE_FLOAT)
        return SDL_GPUVertexElementFormat(SDL_GPU_VERTEXELEMENTFORMAT_FLOAT + e.Count - 1);
    if (e.Count != 2 && e.Count != 4) return SDL_GPU_VERTEXELEMENTFORMAT_INVALID;
    int base = 0;
    switch (e.Type) {
    case CKRST_ATTRIBTYPE_INT8: base = e.Normalized ? SDL_GPU_VERTEXELEMENTFORMAT_BYTE2_NORM : SDL_GPU_VERTEXELEMENTFORMAT_BYTE2; break;
    case CKRST_ATTRIBTYPE_UINT8: base = e.Normalized ? SDL_GPU_VERTEXELEMENTFORMAT_UBYTE2_NORM : SDL_GPU_VERTEXELEMENTFORMAT_UBYTE2; break;
    case CKRST_ATTRIBTYPE_INT16: base = e.Normalized ? SDL_GPU_VERTEXELEMENTFORMAT_SHORT2_NORM : SDL_GPU_VERTEXELEMENTFORMAT_SHORT2; break;
    case CKRST_ATTRIBTYPE_UINT16: base = e.Normalized ? SDL_GPU_VERTEXELEMENTFORMAT_USHORT2_NORM : SDL_GPU_VERTEXELEMENTFORMAT_USHORT2; break;
    case CKRST_ATTRIBTYPE_HALF: base = SDL_GPU_VERTEXELEMENTFORMAT_HALF2; break;
    default: return SDL_GPU_VERTEXELEMENTFORMAT_INVALID;
    }
    return SDL_GPUVertexElementFormat(base + (e.Count == 4 ? 1 : 0));
}

static SDL_GPUBlendFactor BlendFactor(unsigned factor, bool alpha)
{
    switch (factor) {
    case VXBLEND_ZERO: return SDL_GPU_BLENDFACTOR_ZERO;
    case VXBLEND_ONE: return SDL_GPU_BLENDFACTOR_ONE;
    case VXBLEND_SRCCOLOR: return alpha ? SDL_GPU_BLENDFACTOR_SRC_ALPHA : SDL_GPU_BLENDFACTOR_SRC_COLOR;
    case VXBLEND_INVSRCCOLOR: return alpha ? SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA : SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
    case VXBLEND_SRCALPHA: return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    case VXBLEND_INVSRCALPHA: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    case VXBLEND_DESTALPHA: return SDL_GPU_BLENDFACTOR_DST_ALPHA;
    case VXBLEND_INVDESTALPHA: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
    case VXBLEND_DESTCOLOR: return alpha ? SDL_GPU_BLENDFACTOR_DST_ALPHA : SDL_GPU_BLENDFACTOR_DST_COLOR;
    case VXBLEND_INVDESTCOLOR: return alpha ? SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA : SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
    case VXBLEND_SRCALPHASAT: return alpha ? SDL_GPU_BLENDFACTOR_ONE : SDL_GPU_BLENDFACTOR_SRC_ALPHA_SATURATE;
    default: return SDL_GPU_BLENDFACTOR_ONE;
    }
}

static void AddVertexStream(
    const CKSdlGpuProgram &program,
    const CKSdlGpuLayout *layout,
    unsigned slot,
    SDL_GPUVertexAttribute (&locations)[16],
    XArray<SDL_GPUVertexBufferDescription> &streams)
{
    if (!layout)
        return;
    streams.PushBack({slot, layout->Stride, SDL_GPU_VERTEXINPUTRATE_VERTEX, 0});
    for (const CKVertexElementDesc &element : layout->Elements) {
        const int location = program.AttributeLocations[element.Attrib];
        if (location < 0)
            continue;
        locations[location] = {
            unsigned(location), slot, CKSdlGpuVertexFormat(element), element.Offset};
    }
}

static SDL_GPUStencilOpState StencilState(unsigned function, unsigned fail,
                                          unsigned depthFail, unsigned pass)
{
    SDL_GPUStencilOpState state = {};
    state.compare_op = SDL_GPUCompareOp(function ? function : VXCMP_ALWAYS);
    state.fail_op = SDL_GPUStencilOp(fail ? fail : VXSTENCILOP_KEEP);
    state.depth_fail_op = SDL_GPUStencilOp(depthFail ? depthFail : VXSTENCILOP_KEEP);
    state.pass_op = SDL_GPUStencilOp(pass ? pass : VXSTENCILOP_KEEP);
    return state;
}

SDL_GPUGraphicsPipeline *CKSdlGpuRasterizerContext::Pipeline(const CKSdlGpuDraw &draw,
    SDL_GPUTextureFormat color, SDL_GPUTextureFormat depth, SDL_GPUSampleCount samples)
{
    const auto &state = draw.State.State;
    const CKDWORD keyValues[11] = {draw.LayoutHandle, draw.Layout1Handle,
        unsigned(color), unsigned(depth), unsigned(samples), state.Lo, state.Mid, state.Hi,
        draw.State.StencilReadMask, draw.State.StencilWriteMask,
        (CKDWORD)draw.State.DepthClipEnabled};
    CKSdlGpuPipelineKey key;
    std::memcpy(key.Values, keyValues, sizeof(keyValues));
    auto &pipelines = draw.Program->Pipelines;
    std::shared_ptr<SDL_GPUGraphicsPipeline> *found = pipelines.FindPtr(key);
    if (found) return found->get();
    SDL_GPUGraphicsPipelineCreateInfo info = {};
    info.vertex_shader = draw.Program->Vertex->Shader.get();
    info.fragment_shader = draw.Program->Fragment->Shader.get();
    XArray<SDL_GPUVertexAttribute> attributes;
    XArray<SDL_GPUVertexBufferDescription> streams;
    const auto &inputs = draw.Program->Interface.VertexInputs;
    if (inputs.Size() != 0) {
        const unsigned defaultSlot = draw.Layout1 ? 2 : 1;
        SDL_GPUVertexAttribute locations[16] = {};
        for (int i = 0; i < inputs.Size(); ++i) {
            const CKFFVertexInput &input = inputs[i];
            locations[input.Location] = {input.Location, defaultSlot,
                input.Integer ? SDL_GPU_VERTEXELEMENTFORMAT_UINT4 : SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
                input.Location * 16};
        }
        AddVertexStream(*draw.Program, draw.Layout, 0, locations, streams);
        AddVertexStream(*draw.Program, draw.Layout1, 1, locations, streams);
        streams.PushBack({defaultSlot, 256, SDL_GPU_VERTEXINPUTRATE_INSTANCE, 0});
        attributes.Reserve(inputs.Size());
        for (int i = 0; i < inputs.Size(); ++i)
            attributes.PushBack(locations[inputs[i].Location]);
        info.vertex_input_state.vertex_attributes = attributes.Begin();
        info.vertex_input_state.num_vertex_attributes = unsigned(attributes.Size());
        info.vertex_input_state.vertex_buffer_descriptions = streams.Begin();
        info.vertex_input_state.num_vertex_buffers = unsigned(streams.Size());
    }
    switch ((state.Mid >> 6) & 7) {
    case VX_POINTLIST: info.primitive_type = SDL_GPU_PRIMITIVETYPE_POINTLIST; break;
    case VX_LINELIST: info.primitive_type = SDL_GPU_PRIMITIVETYPE_LINELIST; break;
    case VX_LINESTRIP: info.primitive_type = SDL_GPU_PRIMITIVETYPE_LINESTRIP; break;
    case VX_TRIANGLESTRIP: info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP; break;
    default: info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST; break;
    }
    const unsigned fill = (state.Lo >> 12) & 3, cull = (state.Lo >> 10) & 3;
    if (fill == 2) info.primitive_type = SDL_GPU_PRIMITIVETYPE_POINTLIST;
    info.rasterizer_state.fill_mode = fill == 1 ? SDL_GPU_FILLMODE_LINE : SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = cull == 1 ? SDL_GPU_CULLMODE_FRONT : (cull == 2 ? SDL_GPU_CULLMODE_BACK : SDL_GPU_CULLMODE_NONE);
    info.rasterizer_state.front_face = state.Hi & CKRST_STATE_FRONT_CCW ? SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE : SDL_GPU_FRONTFACE_CLOCKWISE;
    info.rasterizer_state.enable_depth_clip = draw.State.DepthClipEnabled != FALSE;
    info.multisample_state.sample_count = samples;
    info.multisample_state.enable_alpha_to_coverage = (state.Lo & CKRST_STATE_ALPHA_COVERAGE) != 0;
    auto &ds = info.depth_stencil_state;
    ds.enable_depth_test = depth != SDL_GPU_TEXTUREFORMAT_INVALID && (state.Lo & CKRST_STATE_DEPTH_TEST) != 0;
    ds.enable_depth_write = depth != SDL_GPU_TEXTUREFORMAT_INVALID && (state.Lo & CKRST_STATE_DEPTH_WRITE) != 0;
    ds.compare_op = SDL_GPUCompareOp(std::max(1u, (state.Lo >> 6) & 15));
    ds.enable_stencil_test = depth != SDL_GPU_TEXTUREFORMAT_INVALID && (state.Mid & CKRST_STENCIL_ENABLE) != 0;
    ds.compare_mask = Uint8(draw.State.StencilReadMask); ds.write_mask = Uint8(draw.State.StencilWriteMask);
    ds.front_stencil_state = StencilState((state.Mid >> 10) & 15, (state.Mid >> 14) & 15, (state.Mid >> 18) & 15, (state.Mid >> 22) & 15);
    ds.back_stencil_state = state.Hi & 0xffff ? StencilState(state.Hi & 15, (state.Hi >> 4) & 15, (state.Hi >> 8) & 15, (state.Hi >> 12) & 15)
                                           : ds.front_stencil_state;
    SDL_GPUColorTargetDescription target = {};
    target.format = color;
    auto &blend = target.blend_state;
    blend.enable_color_write_mask = true; blend.color_write_mask = state.Lo & 15;
    const unsigned src = (state.Lo >> 16) & 15, dst = (state.Lo >> 20) & 15;
    const unsigned srcA = (state.Lo >> 24) & 15, dstA = (state.Lo >> 28) & 15;
    blend.enable_blend = src != 0;
    blend.src_color_blendfactor = BlendFactor(src, false); blend.dst_color_blendfactor = BlendFactor(dst, false);
    blend.src_alpha_blendfactor = BlendFactor(srcA ? srcA : src, true); blend.dst_alpha_blendfactor = BlendFactor(srcA ? dstA : dst, true);
    const unsigned eq = state.Mid & 7, eqA = (state.Mid >> 3) & 7;
    blend.color_blend_op = SDL_GPUBlendOp(eq ? eq : 1);
    blend.alpha_blend_op = SDL_GPUBlendOp(eqA ? eqA : (eq ? eq : 1));
    info.target_info.color_target_descriptions = &target; info.target_info.num_color_targets = 1;
    info.target_info.has_depth_stencil_target = depth != SDL_GPU_TEXTUREFORMAT_INVALID;
    info.target_info.depth_stencil_format = depth;
    auto pipeline = CKSdlGpuOwn(Device, SDL_CreateGPUGraphicsPipeline(Device, &info), SDL_ReleaseGPUGraphicsPipeline);
    if (!pipeline) {
        unsigned dimensions[3] = {};
        for (const CKFFSamplerBinding &sampler : draw.Program->Interface.Samplers)
            ++dimensions[sampler.Dimension];
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "FF pipeline details: fragment_bytes=%u compare_samplers=%u dimensions=%u/%u/%u",
                     draw.Program->Fragment->Desc.CodeSize,
                     draw.Program->CompareSamplerCount,
                     dimensions[CKFF_TEXTURE_2D], dimensions[CKFF_TEXTURE_CUBE],
                     dimensions[CKFF_TEXTURE_3D]);
        Fail("CreateGPUGraphicsPipeline");
        return nullptr;
    }
    pipelines.Insert(key, pipeline, FALSE);
    return pipeline.get();
}

static SDL_GPUSamplerAddressMode AddressMode(CK_ADDRESS_MODE mode)
{
    if (mode == CKRST_ADDRESS_WRAP) return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    if (mode == CKRST_ADDRESS_MIRROR) return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
    // BORDER is evaluated by the shader wrapper, including filter footprints.
    return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
}

std::shared_ptr<SDL_GPUSampler> CKSdlGpuRasterizerContext::Sampler(const CKSamplerDesc &desc)
{
    CKDWORD lodBiasBits = 0;
    std::memcpy(&lodBiasBits, &desc.MipLodBias, sizeof(lodBiasBits));
    const CKDWORD keyValues[10] = {unsigned(desc.MinFilter), unsigned(desc.MagFilter), unsigned(desc.MipFilter),
        unsigned(desc.AddressU), unsigned(desc.AddressV), unsigned(desc.AddressW), unsigned(desc.CompareFunc),
        desc.MinMipLevel, desc.MaxAnisotropy, lodBiasBits};
    CKSdlGpuSamplerKey key;
    std::memcpy(key.Values, keyValues, sizeof(keyValues));
    std::shared_ptr<SDL_GPUSampler> *found = Samplers.FindPtr(key);
    if (found) return *found;
    SDL_GPUSamplerCreateInfo info = {};
    info.min_filter = desc.MinFilter == CKRST_FILTER_NEAREST ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
    info.mag_filter = desc.MagFilter == CKRST_FILTER_NEAREST ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
    info.mipmap_mode = desc.MipFilter == CKRST_FILTER_LINEAR ||
                       desc.MipFilter == CKRST_FILTER_ANISOTROPIC
        ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    info.address_mode_u = AddressMode(desc.AddressU); info.address_mode_v = AddressMode(desc.AddressV); info.address_mode_w = AddressMode(desc.AddressW);
    // Ordinary shader paths apply the legacy bias explicitly through
    // SampleBias/SampleGrad. SampleCmp has no bias operand, so only comparison
    // samplers carry it in native sampler state.
    info.mip_lod_bias = desc.CompareFunc != CKRST_COMPARE_NONE ?
        desc.MipLodBias : 0.0f;
    info.enable_anisotropy = desc.MinFilter == CKRST_FILTER_ANISOTROPIC || desc.MagFilter == CKRST_FILTER_ANISOTROPIC;
    info.max_anisotropy = info.enable_anisotropy
        ? float(desc.MaxAnisotropy ? desc.MaxAnisotropy : 16u) : 1.0f;
    info.min_lod = desc.MipFilter == CKRST_FILTER_NONE ? 0.0f : float(desc.MinMipLevel);
    info.max_lod = desc.MipFilter == CKRST_FILTER_NONE ? 0.0f : 1000.0f;
    static const SDL_GPUCompareOp compare[] = {SDL_GPU_COMPAREOP_ALWAYS, SDL_GPU_COMPAREOP_LESS, SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
        SDL_GPU_COMPAREOP_EQUAL, SDL_GPU_COMPAREOP_GREATER_OR_EQUAL, SDL_GPU_COMPAREOP_GREATER, SDL_GPU_COMPAREOP_NOT_EQUAL,
        SDL_GPU_COMPAREOP_NEVER, SDL_GPU_COMPAREOP_ALWAYS};
    info.enable_compare = desc.CompareFunc != CKRST_COMPARE_NONE;
    info.compare_op = compare[unsigned(desc.CompareFunc) <= 8 ? unsigned(desc.CompareFunc) : 0];
    auto sampler = CKSdlGpuOwn(Device, SDL_CreateGPUSampler(Device, &info), SDL_ReleaseGPUSampler);
    if (!sampler) { Fail("CreateGPUSampler"); return {}; }
    Samplers.Insert(key, sampler, FALSE);
    return sampler;
}

CKERROR CKSdlGpuRasterizerContext::ClearRect(SDL_GPURenderPass *pass, const CKRenderPassDesc &desc,
    SDL_GPUTextureFormat color, SDL_GPUTextureFormat depth, SDL_GPUSampleCount samples)
{
    CKSdlGpuDraw draw;
    draw.Program = ClearProgram.get();
    draw.State.State.Lo = desc.ClearFlags & CKRST_CTXCLEAR_COLOR ? CKRST_STATE_WRITE_RGBA : 0;
    draw.State.State.Mid = CKRST_STATE_PT(VX_TRIANGLELIST);
    draw.State.State.Hi = 0;
    if (desc.ClearFlags & CKRST_CTXCLEAR_DEPTH) draw.State.State.Lo |= CKRST_STATE_DEPTH_TEST | CKRST_STATE_DEPTH_WRITE | CKRST_STATE_DEPTH_FUNC(VXCMP_ALWAYS);
    if (desc.ClearFlags & CKRST_CTXCLEAR_STENCIL) draw.State.State.Mid |= CKRST_STENCIL_OPS(VXCMP_ALWAYS, VXSTENCILOP_REPLACE, VXSTENCILOP_REPLACE, VXSTENCILOP_REPLACE);
    auto pipeline = Pipeline(draw, color, depth, samples);
    if (!pipeline) return Error;
    const float vertexParams[4] = {desc.ClearZ, 0, 0, 0};
    const float rgba[4] = {float((desc.ClearColor >> 16) & 255) / 255, float((desc.ClearColor >> 8) & 255) / 255,
                          float(desc.ClearColor & 255) / 255, float(desc.ClearColor >> 24) / 255};
    SDL_BindGPUGraphicsPipeline(pass, pipeline);
    SDL_SetGPUStencilReference(pass, Uint8(desc.ClearStencil));
    SDL_PushGPUVertexUniformData(Commands, 0, vertexParams, unsigned(sizeof(vertexParams)));
    SDL_PushGPUFragmentUniformData(Commands, 0, rgba, unsigned(sizeof(rgba)));
    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
    return CK_OK;
}
