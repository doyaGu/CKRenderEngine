#include "CKSdlGpuBackend.h"
#include "CKSdlGpuNativeShaders.h"
#include "CKSdlGpuShaders.h"
#include "CKPresentStage.h"
#include <SDL3/SDL.h>
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>

static bool CheckReadback(CKSdlGpuBackend &backend, const CKBackendReadbackTicket &ticket,
                          const std::vector<CKDWORD> &expected, unsigned tolerance, const char *name)
{
    if (backend.PollReadback(ticket, TRUE) != CKRST_READBACK_READY || ticket->Data.size() != expected.size() * 4) return false;
    for (size_t i = 0; i < expected.size(); ++i) {
        CKDWORD pixel = 0;
        std::memcpy(&pixel, ticket->Data.data() + i * 4, 4);
        for (unsigned channel = 0; channel < 32; channel += 8)
            if (unsigned(std::abs(int((pixel >> channel) & 255) - int((expected[i] >> channel) & 255))) > tolerance) {
                std::fprintf(stderr, "%s pixel %u: %08x expected %08x (tolerance %u)\n", name,
                    unsigned(i), unsigned(pixel), unsigned(expected[i]), tolerance);
                return false;
            }
    }
    return true;
}

static bool CheckGenericProgram(CKSdlGpuBackend &backend)
{
    CKShaderDesc vertex, fragment;
    const auto format = backend.GetCaps().ShaderFormat == CKRST_SHADER_FORMAT_DXIL ?
        SDL_GPU_SHADERFORMAT_DXIL : SDL_GPU_SHADERFORMAT_SPIRV;
    CKDWORD vs = 0, fs = 0, program = 0, texture = 0, output = 0, target = 0;
    if (!CKSdlGpuNativeVolumeShaders(format, vertex, fragment) ||
        backend.CreateShader(&vertex, &vs) != CK_OK || backend.CreateShader(&fragment, &fs) != CK_OK) return false;
    auto desc = CKSdlGpuNativeProgram(vs, fs, true);
    desc.Uniforms[0].Slot = 28; desc.Uniforms[1].Slot = 29;
    desc.Samplers[0].Slot = 7; // one sampler is an ordinary volume input, at an arbitrary logical slot
    if (backend.CreateProgram(&desc, &program) != CK_OK) return false;
    desc = {}; // the program must own all declarations and shader references
    backend.DestroyObject(vs, CKRST_OBJ_SHADER); backend.DestroyObject(fs, CKRST_OBJ_SHADER);

    CKTextureDesc image;
    VxPixelFormat2ImageDesc(_32_ARGB8888, image.Format);
    image.Format.Width = image.Format.Height = 4; image.Format.BytesPerLine = 16;
    image.Depth = 2; image.MipMapCount = 1;
    image.Flags = CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA | CKRST_TEXTURE_VOLUMEMAP;
    if (backend.CreateTexture(&image, nullptr, &texture) != CK_OK) return false;
    std::vector<CKDWORD> pixels(16, 0xffff0000);
    VxImageDescEx upload = image.Format; upload.Image = reinterpret_cast<CKBYTE *>(pixels.data());
    for (unsigned z = 0; z < 2; ++z)
        if (backend.UpdateTexture(texture, 0, z, nullptr, &upload) != CK_OK) return false;
    image.Depth = 1; image.Flags = CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA | CKRST_TEXTURE_RENDERTARGET;
    if (backend.CreateTexture(&image, nullptr, &output) != CK_OK) return false;
    CKBackendRenderTargetDesc attachment; attachment.ColorTexture = output;
    if (backend.CreateRenderTarget(&attachment, &target) != CK_OK) return false;
    CKBackendPassDesc pass; pass.RenderTarget = target; pass.Rect = {0, 0, 4, 4};
    pass.ClearFlags = CKRST_CTXCLEAR_COLOR;
    if (backend.BeginPass(&pass) != CK_OK) return false;
    CKBackendPipelineState state;
    state.State = CKDrawStateBuilder().Depth(FALSE, FALSE, VXCMP_ALWAYS).Cull(VXCULL_NONE).
        NoBlend().Topology(VX_TRIANGLELIST).Build();
    state.ScissorEnabled = TRUE; state.Scissor = {0, 0, 2, 4};
    backend.SetPipelineState(&state);
    CKSamplerDesc sampler = {CKRST_FILTER_NEAREST, CKRST_FILTER_NEAREST, CKRST_FILTER_NONE,
        CKRST_ADDRESS_CLAMP, CKRST_ADDRESS_CLAMP, CKRST_ADDRESS_CLAMP, 0, CKRST_COMPARE_NONE};
    backend.BindTexture(7, texture, &sampler);
    const float position[4] = {}, volume[8] = {4, 4, 2, 0, 4, 4, 1, 0};
    if (backend.PushConstants(28, position, sizeof(position)) != CK_OK ||
        backend.PushConstants(29, volume, sizeof(volume)) != CK_OK ||
        backend.PushConstants(29, volume, 16) != CK_OK) return false; // preserve the trailing vector
    CKBackendDraw draw; draw.Program = program; draw.VertexCount = 3; // no vertex buffer/layout
    if (backend.Draw(&draw) != CK_OK) return false;
    std::fill(pixels.begin(), pixels.end(), 0xff0000ff);
    if (backend.UpdateTexture(texture, 0, 0, nullptr, &upload) != CK_OK) return false;
    state.Scissor = {2, 0, 4, 4}; backend.SetPipelineState(&state);
    if (backend.Draw(&draw) != CK_OK) return false;
    CKReadbackDesc read; CKBackendReadbackTicket ticket;
    if (backend.ReadTexture(output, 0, &read, &ticket) != CK_OK) return false;
    backend.DestroyObject(program, CKRST_OBJ_PROGRAM); backend.DestroyObject(texture, CKRST_OBJ_TEXTURE);
    backend.DestroyObject(target, CKRST_OBJ_FRAMEBUFFER); backend.DestroyObject(output, CKRST_OBJ_TEXTURE);
    if (backend.Submit({CKRST_BACKEND_SYNC_UNCHANGED, FALSE}, nullptr) != CK_OK) return false;
    for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x)
        pixels[y * 4 + x] = x < 2 ? 0xffff0000 : 0xff800080;
    if (!CheckReadback(backend, ticket, pixels, 1, "generic program ordered update")) return false;
    std::puts("SDL_gpu generic program: procedural input, logical sampler 7, byte slots 28/29, ordered slice update and retained resources passed");

    // Use the existing textured shader to exercise real two-stream/indexed
    // geometry, including descriptors whose memory could be reused next frame.
    CKBackendShaderSet shaders;
    if (!CKSdlGpuShaderSet(format, shaders) ||
        backend.CreateShader(&shaders.Shaders[CKRST_SHADER_PRESENT_VERTEX], &vs) != CK_OK ||
        backend.CreateShader(&shaders.Shaders[CKRST_SHADER_PRESENT_FRAGMENT], &fs) != CK_OK) return false;
    desc = CKFFBuildProgramInterface(vs, fs, backend.GetCaps().ShaderFormat, TRUE);
    if (backend.CreateProgram(&desc, &program) != CK_OK) return false;
    backend.DestroyObject(vs, CKRST_OBJ_SHADER); backend.DestroyObject(fs, CKRST_OBJ_SHADER);
    CKVertexElementDesc element = {CKRST_ATTRIB_POSITION, CKRST_ATTRIBTYPE_FLOAT, 3, FALSE, FALSE, 0};
    CKVertexLayoutDesc layoutDesc = {&element, 1, 12};
    CKDWORD positionLayout = 0, texcoordLayout = 0, otherLayout = 0;
    if (backend.CreateVertexLayout(&layoutDesc, &positionLayout) != CK_OK ||
        backend.CreateVertexLayout(&layoutDesc, &otherLayout) != CK_OK) return false;
    element.Attrib = CKRST_ATTRIB_TEXCOORD0; element.Count = 2; layoutDesc.Stride = 8;
    if (backend.CreateVertexLayout(&layoutDesc, &texcoordLayout) != CK_OK ||
        backend.CreateTexture(&image, nullptr, &output) != CK_OK) return false;
    attachment.ColorTexture = output;
    if (backend.CreateRenderTarget(&attachment, &target) != CK_OK) return false;
    pass.RenderTarget = target;
    if (backend.BeginPass(&pass) != CK_OK) return false;
    state.ScissorEnabled = FALSE; backend.SetPipelineState(&state);
    backend.BindTexture(CKFF_SLOT_PRESENT, 0, &sampler);
    const float present[4] = {0.25f, 0.25f, 0, 0};
    if (CKFFPushConstants(&backend, CKRST_BLOCK_PRESENT_PARAMS, present, 1) != CK_OK) return false;
    const float positions[9] = {-1, -1, 0, 3, -1, 0, -1, 3, 0};
    const float texcoords[6] = {0, 1, 2, 1, 0, -1};
    const CKWORD indices[3] = {0, 1, 2};
    auto allocate = [&](CKBackendTransientVertices &vertices, CKBackendTransientVertices &uv,
                        CKBackendTransientIndices &index) {
        if (!backend.AllocTransientVertices(3, positionLayout, &vertices) ||
            !backend.AllocTransientVertices(3, texcoordLayout, &uv) ||
            !backend.AllocTransientIndices(3, FALSE, &index)) return false;
        std::memcpy(vertices.Data, positions, sizeof(positions));
        std::memcpy(uv.Data, texcoords, sizeof(texcoords));
        std::memcpy(index.Data, indices, sizeof(indices));
        return true;
    };
    CKBackendTransientVertices vertices, uv;
    CKBackendTransientIndices index;
    if (!allocate(vertices, uv, index)) return false;
    draw = CKBackendDraw(); draw.Program = program; draw.Layout = positionLayout;
    draw.Stream1Layout = texcoordLayout; draw.VertexCount = draw.IndexCount = 3;
    draw.TransientVertices = &vertices; draw.Stream1Transient = &uv; draw.TransientIndices = &index;
    if (backend.Draw(&draw) != CK_OK) return false;
    auto rejected = [&](const CKBackendDraw &candidate, const char *name) {
        if (backend.Draw(&candidate) == CKERR_INVALIDPARAMETER) return true;
        std::fprintf(stderr, "SDL_gpu accepted invalid transient descriptor: %s\n", name);
        return false;
    };
    CKBackendTransientVertices badVertices = vertices;
    CKBackendDraw badDraw = draw; badDraw.TransientVertices = &badVertices;
    badVertices.Token = 0;
    if (!rejected(badDraw, "zero vertex token")) return false;
    badVertices = vertices; badVertices.Token = index.Token;
    if (!rejected(badDraw, "wrong vertex token")) return false;
    badVertices = vertices; ++badVertices.Count;
    if (!rejected(badDraw, "vertex allocation size")) return false;
    badVertices = vertices; badVertices.Data = const_cast<float *>(positions);
    if (!rejected(badDraw, "vertex allocation pointer")) return false;
    badVertices = vertices; badVertices.Layout = otherLayout;
    if (!rejected(badDraw, "vertex allocation layout")) return false;
    badVertices = vertices; badDraw.Layout = otherLayout;
    if (!rejected(badDraw, "draw layout disagrees with allocation")) return false;
    CKBackendTransientVertices badUv = uv; ++badUv.Count;
    badDraw = draw; badDraw.Stream1Transient = &badUv;
    if (!rejected(badDraw, "stream 1 allocation size")) return false;
    CKBackendTransientIndices badIndex = index;
    badDraw = draw; badDraw.TransientIndices = &badIndex;
    badIndex.Token = vertices.Token;
    if (!rejected(badDraw, "wrong index token")) return false;
    badIndex = index; ++badIndex.Count;
    if (!rejected(badDraw, "index allocation size")) return false;
    badIndex = index; badIndex.Data = const_cast<CKWORD *>(indices);
    if (!rejected(badDraw, "index allocation pointer")) return false;
    badIndex = index; badIndex.Index32 = TRUE;
    if (!rejected(badDraw, "index allocation format")) return false;
    if (backend.ReadTexture(output, 0, &read, &ticket) != CK_OK ||
        backend.Submit({CKRST_BACKEND_SYNC_UNCHANGED, FALSE}, nullptr) != CK_OK ||
        !CheckReadback(backend, ticket, std::vector<CKDWORD>(16, 0xffffffff), 0, "two-stream transient draw")) return false;

    pass.ClearFlags = 0;
    if (backend.BeginPass(&pass) != CK_OK || !rejected(draw, "previous frame before reallocation")) return false;
    CKBackendTransientVertices currentVertices, currentUv;
    CKBackendTransientIndices currentIndex;
    if (!allocate(currentVertices, currentUv, currentIndex) ||
        !rejected(draw, "previous frame after reallocation")) return false;
    draw.TransientVertices = &currentVertices; draw.Stream1Transient = &currentUv;
    draw.TransientIndices = &currentIndex;
    badVertices = currentVertices; badVertices.Token = vertices.Token;
    badDraw = draw; badDraw.TransientVertices = &badVertices;
    if (!rejected(badDraw, "old vertex token with reused current storage")) return false;
    badUv = currentUv; badUv.Token = uv.Token;
    badDraw = draw; badDraw.Stream1Transient = &badUv;
    if (!rejected(badDraw, "old stream 1 token with reused current storage")) return false;
    badIndex = currentIndex; badIndex.Token = index.Token;
    badDraw = draw; badDraw.TransientIndices = &badIndex;
    if (!rejected(badDraw, "old index token with reused current storage") || backend.Draw(&draw) != CK_OK) return false;
    backend.DestroyObject(program, CKRST_OBJ_PROGRAM); backend.DestroyObject(target, CKRST_OBJ_FRAMEBUFFER);
    backend.DestroyObject(output, CKRST_OBJ_TEXTURE); backend.DestroyObject(positionLayout, CKRST_OBJ_VERTEXLAYOUT);
    backend.DestroyObject(texcoordLayout, CKRST_OBJ_VERTEXLAYOUT); backend.DestroyObject(otherLayout, CKRST_OBJ_VERTEXLAYOUT);
    if (backend.Submit({CKRST_BACKEND_SYNC_UNCHANGED, FALSE}, nullptr) != CK_OK) return false;
    std::puts("SDL_gpu transient geometry: two streams, indices, descriptor identity and submission expiry passed");
    return true;
}

static bool CheckCompressedMips(CKSdlGpuBackend &backend)
{
    const VX_PIXELFORMAT formats[] = {_DXT1, _DXT3, _DXT5};
    const CKDWORD beforePixels[] = {0x80800000, 0x88ff0000, 0x80ff0000};
    const CKDWORD afterPixels[] = {0x90801000, 0x90ef1000, 0x88ef1000};
    for (unsigned format = 0; format < 3; ++format) for (bool cube : {false, true}) {
        CKTextureDesc desc;
        VxPixelFormat2ImageDesc(formats[format], desc.Format);
        desc.Format.Width = desc.Format.Height = 4;
        desc.MipMapCount = CKRST_MIPMAP_GENERATE;
        desc.Flags = CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA | (cube ? CKRST_TEXTURE_CUBEMAP : 0);
        CKDWORD texture = 0, output = 0;
        if (backend.CreateTexture(&desc, nullptr, &texture) != CK_OK) return false;
        unsigned char block[16] = {};
        if (!format) {
            block[0] = 31; block[3] = 248;
            for (unsigned y = 0; y < 4; ++y) block[4 + y] = 0xf5; // red, red, transparent, transparent
        } else {
            block[9] = 248; // opaque red color endpoint, color indices zero
            if (format == 1) std::memset(block, 0x88, 8);
            else block[0] = 128;
        }
        VxImageDescEx upload = desc.Format;
        upload.Image = block; upload.TotalImageSize = format ? 16 : 8;
        for (unsigned layer = 0; layer < (cube ? 6u : 1u); ++layer)
            if (backend.UpdateTexture(texture, 0, layer, nullptr, &upload) != CK_OK) return false;
        CKTextureDesc readDesc;
        VxPixelFormat2ImageDesc(_32_ARGB8888, readDesc.Format);
        readDesc.Format.Width = readDesc.Format.Height = 1;
        readDesc.MipMapCount = 1; readDesc.Flags = CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
        if (backend.CreateTexture(&readDesc, nullptr, &output) != CK_OK) return false;
        auto capture = [&](unsigned layer, CKBackendReadbackTicket &ticket) {
            CKReadbackDesc read;
            return backend.Blit(output, 0, 0, 0, 0, texture, 2, layer, nullptr) == CK_OK &&
                   backend.ReadTexture(output, 0, &read, &ticket) == CK_OK;
        };
        CKBackendReadbackTicket before, after, other;
        if (!capture(0, before)) return false;
        CKDWORD green = 0xff00ff00;
        upload = readDesc.Format; upload.BytesPerLine = 4; upload.Image = reinterpret_cast<CKBYTE *>(&green);
        CKRECT patch = {3, 3, 4, 4};
        if (backend.UpdateTexture(texture, 0, 0, &patch, &upload) != CK_OK || !capture(0, after) ||
            (cube && !capture(5, other)) || backend.Submit({CKRST_BACKEND_SYNC_UNCHANGED, FALSE}, nullptr) != CK_OK) return false;
        if (!CheckReadback(backend, before, {beforePixels[format]}, 0, "compressed old mip") ||
            !CheckReadback(backend, after, {afterPixels[format]}, 0, "compressed patched mip") ||
            (cube && !CheckReadback(backend, other, {beforePixels[format]}, 0, "compressed untouched face"))) return false;
        backend.DestroyObject(texture, CKRST_OBJ_TEXTURE); backend.DestroyObject(output, CKRST_OBJ_TEXTURE);
    }
    std::puts("SDL_gpu BC1/2/3 auto mips preserve alpha, individual patches, old readbacks and cube faces");
    return true;
}

static bool CheckGpuVolumeMips(CKSdlGpuBackend &backend)
{
    const unsigned dimensions[][3] = {{4, 4, 4}, {7, 5, 3}, {1, 1, 5}};
    for (const auto &size : dimensions) {
        const unsigned width = size[0], height = size[1], depth = size[2];
        CKTextureDesc desc;
        VxPixelFormat2ImageDesc(_32_ARGB8888, desc.Format);
        desc.Format.Width = width; desc.Format.Height = height; desc.Format.BytesPerLine = width * 4;
        desc.Depth = depth; desc.MipMapCount = CKRST_MIPMAP_GENERATE;
        desc.Flags = CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA | CKRST_TEXTURE_VOLUMEMAP | CKRST_TEXTURE_RENDERTARGET;
        CKDWORD texture = 0, source = 0, framebuffer = 0;
        if (backend.CreateTexture(&desc, nullptr, &texture) != CK_OK) return false;
        std::vector<CKDWORD> base(size_t(width) * height * depth);
        VxImageDescEx upload = desc.Format;
        for (unsigned z = 0; z < depth; ++z) {
            for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x)
                base[(z * height + y) * width + x] = 0x80000000u | ((x * 17 + z * 9) << 16) | ((y * 21) << 8) | (z * 43);
            upload.Image = reinterpret_cast<CKBYTE *>(base.data() + z * width * height);
            if (backend.UpdateTexture(texture, 0, z, nullptr, &upload) != CK_OK) return false;
        }
        struct Capture { CKBackendReadbackTicket Ticket; std::vector<CKDWORD> Expected; unsigned Tolerance = 1; };
        std::vector<Capture> captures;
        // Independent CPU box-filter reference checks every generated texel and
        // every slice, including axes that have already shrunk to one texel.
        auto capture = [&]() {
            auto previous = base;
            unsigned pw = width, ph = height, pd = depth, mip = 0;
            while (pw > 1 || ph > 1 || pd > 1) {
                ++mip;
                const unsigned w = std::max(1u, pw / 2), h = std::max(1u, ph / 2), d = std::max(1u, pd / 2);
                std::vector<CKDWORD> next(size_t(w) * h * d);
                for (unsigned z = 0; z < d; ++z) for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
                    unsigned sums[4] = {}, count = 0;
                    for (unsigned iz = z * pd / d; iz < (z + 1) * pd / d; ++iz)
                        for (unsigned iy = y * ph / h; iy < (y + 1) * ph / h; ++iy)
                            for (unsigned ix = x * pw / w; ix < (x + 1) * pw / w; ++ix) {
                                CKDWORD pixel = previous[(iz * ph + iy) * pw + ix];
                                for (unsigned c = 0; c < 4; ++c) sums[c] += (pixel >> (c * 8)) & 255;
                                ++count;
                            }
                    for (unsigned c = 0; c < 4; ++c) next[(z * h + y) * w + x] |= ((sums[c] + count / 2) / count) << (c * 8);
                }
                CKTextureDesc readDesc = desc;
                readDesc.Flags = CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
                readDesc.Depth = 1; readDesc.MipMapCount = 1;
                readDesc.Format.Width = w; readDesc.Format.Height = h;
                CKDWORD output = 0;
                if (backend.CreateTexture(&readDesc, nullptr, &output) != CK_OK) return false;
                for (unsigned z = 0; z < d; ++z) {
                    Capture result;
                    CKReadbackDesc read;
                    if (backend.Blit(output, 0, 0, 0, 0, texture, mip, z, nullptr) != CK_OK ||
                        backend.ReadTexture(output, 0, &read, &result.Ticket) != CK_OK) return false;
                    result.Expected.assign(next.begin() + z * w * h, next.begin() + (z + 1) * w * h);
                    captures.push_back(std::move(result));
                }
                backend.DestroyObject(output, CKRST_OBJ_TEXTURE);
                previous = std::move(next); pw = w; ph = h; pd = d;
            }
            return true;
        };
        if (!capture()) return false;
        CKTextureDesc sourceDesc = desc;
        sourceDesc.Flags = CKRST_TEXTURE_RGB; sourceDesc.Depth = 1; sourceDesc.MipMapCount = 1;
        std::vector<CKDWORD> green(width * height, 0xff00ff00);
        upload.Image = reinterpret_cast<CKBYTE *>(green.data());
        if (backend.CreateTexture(&sourceDesc, &upload, &source) != CK_OK ||
            backend.Blit(texture, 0, depth - 1, 0, 0, source, 0, 0, nullptr) != CK_OK) return false;
        std::copy(green.begin(), green.end(), base.begin() + (depth - 1) * width * height);
        if (!capture()) return false;
        CKDWORD blue = 0xff0000ff;
        upload.Width = upload.Height = 1; upload.BytesPerLine = 4; upload.Image = reinterpret_cast<CKBYTE *>(&blue);
        CKRECT patch = {int(width - 1), int(height - 1), int(width), int(height)};
        if (backend.UpdateTexture(texture, 0, depth - 1, &patch, &upload) != CK_OK) return false;
        base.back() = blue;
        if (!capture()) return false;
        CKBackendRenderTargetDesc target;
        target.ColorTexture = texture; target.ColorLayer = 1;
        if (backend.CreateRenderTarget(&target, &framebuffer) != CK_OK) return false;
        CKBackendPassDesc pass;
        pass.RenderTarget = framebuffer; pass.Rect = {0, 0, int(width), int(height)};
        pass.ClearFlags = CKRST_CTXCLEAR_COLOR; pass.ClearColor = 0xffff0000;
        if (backend.BeginPass(&pass) != CK_OK) return false;
        std::fill(base.begin() + width * height, base.begin() + 2 * width * height, 0xffff0000);
        if (!capture()) return false;
        // Reopen the same volume attachment after its copy/resolve boundary.
        // A rectangular clear must load the previous slice and preserve its
        // remaining texels, all other slices, and previously queued readbacks.
        pass.Rect = {0, 0, 1, 1}; pass.ClearColor = 0xff0000ff;
        if (backend.BeginPass(&pass) != CK_OK) return false;
        base[width * height] = pass.ClearColor;
        if (!capture()) return false;
        CKDWORD output = 0;
        if (backend.CreateTexture(&sourceDesc, nullptr, &output) != CK_OK) return false;
        for (unsigned z = 0; z < depth; ++z) {
            Capture result;
            CKReadbackDesc read;
            if (backend.Blit(output, 0, 0, 0, 0, texture, 0, z, nullptr) != CK_OK ||
                backend.ReadTexture(output, 0, &read, &result.Ticket) != CK_OK) return false;
            result.Expected.assign(base.begin() + z * width * height, base.begin() + (z + 1) * width * height);
            result.Tolerance = 0;
            captures.push_back(std::move(result));
        }
        backend.DestroyObject(output, CKRST_OBJ_TEXTURE);
        if (backend.Submit({CKRST_BACKEND_SYNC_UNCHANGED, FALSE}, nullptr) != CK_OK) return false;
        for (const auto &result : captures)
            if (!CheckReadback(backend, result.Ticket, result.Expected, result.Tolerance, "GPU volume mip")) return false;
        backend.DestroyObject(framebuffer, CKRST_OBJ_RENDERTARGET);
        backend.DestroyObject(texture, CKRST_OBJ_TEXTURE); backend.DestroyObject(source, CKRST_OBJ_TEXTURE);
    }
    std::puts("SDL_gpu volume GPU copy / patch / RTT / continued rectangular clear pass for 4x4x4, 7x5x3 and 1x1x5; old tickets retained");
    return true;
}

static bool CheckGeneratedMips(CKSdlGpuBackend &backend)
{
    for (unsigned kind = 0; kind < 3; ++kind) {
        CKTextureDesc desc;
        VxPixelFormat2ImageDesc(_32_ARGB8888, desc.Format);
        desc.Format.Width = desc.Format.Height = 4;
        desc.Format.BytesPerLine = 16;
        desc.Depth = kind == 2 ? 4 : 1;
        desc.Flags = CKRST_TEXTURE_RGB | (kind == 1 ? CKRST_TEXTURE_CUBEMAP :
            (kind == 2 ? CKRST_TEXTURE_VOLUMEMAP : 0));
        desc.MipMapCount = CKRST_MIPMAP_GENERATE;
        CKDWORD texture = 0, readTexture = 0;
        if (backend.CreateTexture(&desc, nullptr, &texture) != CK_OK) return false;
        CKDWORD pixels[16];
        VxImageDescEx upload = desc.Format;
        upload.Image = reinterpret_cast<CKBYTE *>(pixels);
        const unsigned layers = kind == 1 ? 6 : (kind == 2 ? 4 : 1);
        for (unsigned layer = 0; layer < layers; ++layer) {
            for (auto &pixel : pixels) pixel = layer & 1 ? 0xff0000ff : 0xffff0000;
            if (backend.UpdateTexture(texture, 0, layer, nullptr, &upload) != CK_OK) return false;
        }
        CKTextureDesc readDesc = desc;
        readDesc.Flags = CKRST_TEXTURE_RGB;
        readDesc.Format.Width = readDesc.Format.Height = 1;
        readDesc.MipMapCount = 1; readDesc.Depth = 1;
        if (backend.CreateTexture(&readDesc, nullptr, &readTexture) != CK_OK) return false;
        auto capture = [&](unsigned layer, CKBackendReadbackTicket &ticket) {
            CKReadbackDesc read;
            return backend.Blit(readTexture, 0, 0, 0, 0, texture, 2, layer, nullptr) == CK_OK &&
                backend.ReadTexture(readTexture, 0, &read, &ticket) == CK_OK;
        };
        CKBackendReadbackTicket before, after, other;
        if (!capture(0, before)) return false;
        for (unsigned i = 0; i < 4; ++i) pixels[i] = 0xff00ff00;
        upload.Width = upload.Height = 2; upload.BytesPerLine = 8;
        CKRECT region = {0, 0, 2, 2};
        if (backend.UpdateTexture(texture, 0, 0, &region, &upload) != CK_OK || !capture(0, after)) return false;
        if (kind == 1 && !capture(5, other)) return false;
        if (backend.Submit({CKRST_BACKEND_SYNC_UNCHANGED, FALSE}, nullptr) != CK_OK) return false;
        auto check = [&](const CKBackendReadbackTicket &ticket, CKDWORD expected) {
            if (backend.PollReadback(ticket, TRUE) != CKRST_READBACK_READY) return false;
            CKDWORD pixel = 0;
            std::memcpy(&pixel, ticket->Data.data(), 4);
            if (pixel != expected)
                std::fprintf(stderr, "generated mip kind=%u: %08x expected %08x\n", kind, unsigned(pixel), unsigned(expected));
            return pixel == expected;
        };
        if (!check(before, kind == 2 ? 0xff800080 : 0xffff0000) ||
            !check(after, kind == 2 ? 0xff701080 : 0xffbf4000) ||
            (other && !check(other, 0xff0000ff))) return false;
        backend.DestroyObject(texture, CKRST_OBJ_TEXTURE);
        backend.DestroyObject(readTexture, CKRST_OBJ_TEXTURE);
    }
    std::puts("SDL_gpu generated 2D/cube/volume mips preserve ordered patches and other faces");
    return true;
}

static bool CheckGpuWrittenMips(CKSdlGpuBackend &backend)
{
    CKTextureDesc desc;
    VxPixelFormat2ImageDesc(_32_ARGB8888, desc.Format);
    desc.Format.Width = desc.Format.Height = 4; desc.Format.BytesPerLine = 16;
    desc.Flags = CKRST_TEXTURE_RGB | CKRST_TEXTURE_RENDERTARGET;
    desc.MipMapCount = CKRST_MIPMAP_GENERATE;
    CKDWORD target = 0, source = 0, framebuffer = 0, pixels[16];
    if (backend.CreateTexture(&desc, nullptr, &target) != CK_OK) return false;
    VxImageDescEx upload = desc.Format;
    upload.Image = reinterpret_cast<CKBYTE *>(pixels);
    for (auto &pixel : pixels) pixel = 0xffff0000;
    if (backend.UpdateTexture(target, 0, 0, nullptr, &upload) != CK_OK) return false;
    desc.MipMapCount = 1; desc.Flags = CKRST_TEXTURE_RGB;
    for (auto &pixel : pixels) pixel = 0xff00ff00;
    if (backend.CreateTexture(&desc, &upload, &source) != CK_OK ||
        backend.Blit(target, 0, 0, 0, 0, source, 0, 0, nullptr) != CK_OK) return false;
    CKBackendReadbackTicket copied, patched, rendered;
    CKReadbackDesc read;
    if (backend.ReadTexture(target, 2, &read, &copied) != CK_OK) return false;
    for (auto &pixel : pixels) pixel = 0xff0000ff;
    upload.Width = upload.Height = 2; upload.BytesPerLine = 8;
    CKRECT rect = {0, 0, 2, 2};
    if (backend.UpdateTexture(target, 0, 0, &rect, &upload) != CK_OK ||
        backend.ReadTexture(target, 2, &read, &patched) != CK_OK) return false;
    CKBackendRenderTargetDesc targetDesc;
    targetDesc.ColorTexture = target;
    if (backend.CreateRenderTarget(&targetDesc, &framebuffer) != CK_OK) return false;
    CKBackendPassDesc pass;
    pass.RenderTarget = framebuffer; pass.Rect = {0, 0, 4, 4};
    pass.ClearFlags = CKRST_CTXCLEAR_COLOR; pass.ClearColor = 0xffff0000;
    if (backend.BeginPass(&pass) != CK_OK || backend.ReadTexture(target, 2, &read, &rendered) != CK_OK ||
        backend.Submit({CKRST_BACKEND_SYNC_UNCHANGED, FALSE}, nullptr) != CK_OK) return false;
    auto check = [&](const CKBackendReadbackTicket &ticket, CKDWORD expected) {
        if (backend.PollReadback(ticket, TRUE) != CKRST_READBACK_READY) return false;
        CKDWORD pixel = 0;
        std::memcpy(&pixel, ticket->Data.data(), 4);
        if (pixel != expected) std::fprintf(stderr, "GPU-written mip: %08x expected %08x\n", unsigned(pixel), unsigned(expected));
        return pixel == expected;
    };
    if (!check(copied, 0xff00ff00) || !check(patched, 0xff00bf40) || !check(rendered, 0xffff0000)) return false;
    backend.DestroyObject(framebuffer, CKRST_OBJ_RENDERTARGET);
    backend.DestroyObject(target, CKRST_OBJ_TEXTURE);
    backend.DestroyObject(source, CKRST_OBJ_TEXTURE);
    std::puts("SDL_gpu copy / patch / RTT regenerate mips without stale CPU data");
    return true;
}

static int Run(SDL_Window *window)
{
    CKSdlGpuBackend backend;
    CKBackendInitDesc init;
    init.Window = window; init.Width = 640; init.Height = 480;
    // The owner controls fullscreen; the backend only claims its window.
    const SDL_WindowFlags ownerFlags = SDL_GetWindowFlags(window);
    init.Fullscreen = TRUE;
    if (backend.Init(&init) != CK_OK) return 1;
    SDL_SyncWindow(window);
    if ((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != (ownerFlags & SDL_WINDOW_FULLSCREEN)) {
        std::fprintf(stderr, "backend changed the owner's fullscreen state\n");
        return 12;
    }
    CKTextureDesc texture;
    VxPixelFormat2ImageDesc(_32_ARGB8888, texture.Format);
    texture.Format.Width = texture.Format.Height = 4;
    texture.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB;
    CKDWORD previous = 0, replacement = 0;
    if (backend.CreateTexture(&texture, nullptr, &previous) != CK_OK) return 13;
    backend.BindTexture(0, previous, nullptr);
    if (backend.Submit({CKRST_BACKEND_SYNC_IMMEDIATE, FALSE}, nullptr) != CK_OK) return 14;
    backend.Shutdown();
    init.Fullscreen = FALSE;
    if (backend.Init(&init) != CK_OK || backend.IsObjectAlive(previous, CKRST_OBJ_TEXTURE) ||
        backend.CreateTexture(&texture, nullptr, &replacement) != CK_OK || replacement == previous) return 15;
    if (backend.DestroyObject(replacement, CKRST_OBJ_TEXTURE) != CK_OK) return 16;
    std::puts("SDL_gpu owner window, device reinitialization and stale handle rejection passed");
    if (!CheckGeneratedMips(backend)) return 17;
    if (!CheckGpuWrittenMips(backend)) return 19;
    if (!CheckCompressedMips(backend)) return 20;
    if (!CheckGpuVolumeMips(backend)) return 21;
    if (!CheckGenericProgram(backend)) return 22;
    CKPresentStage present;
    CKBackendShaderSet shaders;
    if (!CKSdlGpuShaderSet(backend.GetCaps().ShaderFormat == CKRST_SHADER_FORMAT_DXIL ?
        SDL_GPU_SHADERFORMAT_DXIL : SDL_GPU_SHADERFORMAT_SPIRV, shaders)) return 23;
    present.Init(&backend, shaders);
    if (!present.EnsureSceneTarget(640, 480, 0) || !present.EnsureNativeTarget(640, 480) || !present.EnsureResources()) return 2;
    CKBackendPassDesc pass;
    pass.Rect = {0, 0, 640, 480}; pass.RenderTarget = present.SceneTarget().FrameBuffer;
    pass.ClearFlags = CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH; pass.ClearColor = 0xff20b060;
    if (backend.BeginPass(&pass) != CK_OK) return 3;
    pass.RenderTarget = present.NativeTarget().FrameBuffer; pass.ClearFlags = 0;
    if (backend.BeginPass(&pass) != CK_OK || present.SubmitResolve(FALSE, 0) != CK_OK) return 4;
    CKBackendReadbackTicket ticket;
    CKReadbackDesc read;
    if (backend.ReadTexture(present.NativeTarget().ColorTexture, 0, &read, &ticket) != CK_OK) return 5;
    pass.RenderTarget = 0;
    if (backend.BeginPass(&pass) != CK_OK || present.SubmitBlit() != CK_OK ||
        backend.Submit({CKRST_BACKEND_SYNC_VSYNC, TRUE}, nullptr) != CK_OK) return 6;
    if (backend.PollReadback(ticket, TRUE) != CKRST_READBACK_READY) return 7;
    CKDWORD center = 0;
    std::memcpy(&center, ticket->Data.data() + 240 * read.RowPitch + 320 * 4, 4);
    if (center != 0xff20b060) { std::fprintf(stderr, "resolve pixel: %08x expected ff20b060\n", unsigned(center)); return 8; }
    std::printf("SDL_gpu native resolve, presentation and fence readback passed; window=%u size=640x480\n", SDL_GetWindowID(window));
    std::fflush(stdout);
    const Uint64 end = SDL_GetTicks() + (SDL_getenv("CKRE_GPU_TEST_HOLD") ? 120000 : 1500);
    while (SDL_GetTicks() < end) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) if (event.type == SDL_EVENT_QUIT) return 0;
        if (backend.BeginPass(&pass) != CK_OK || present.SubmitBlit() != CK_OK ||
            backend.Submit({CKRST_BACKEND_SYNC_VSYNC, TRUE}, nullptr) != CK_OK) return 9;
        SDL_Delay(10);
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2 || std::strcmp(argv[1], "--visible") != 0) {
        std::puts("Skipped: run --visible on the interactive desktop for GPU acceptance.");
        return 77;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) return 10;
    SDL_Window *window = SDL_CreateWindow("SDL_gpu rasterizer pixel tests", 640, 480, SDL_WINDOW_RESIZABLE);
    if (!window) { SDL_Quit(); return 11; }
    SDL_ShowWindow(window); SDL_RaiseWindow(window);
    if (SDL_getenv("CKRE_GPU_TEST_INTERACTIVE_START")) {
        SDL_SetWindowTitle(window, "SDL_gpu native tests - press Enter to start");
        const Uint64 deadline = SDL_GetTicks() + 120000;
        bool start = false;
        while (!start && SDL_GetTicks() < deadline) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) { SDL_DestroyWindow(window); SDL_Quit(); return 18; }
                if (event.type == SDL_EVENT_KEY_DOWN && event.key.windowID == SDL_GetWindowID(window) &&
                    event.key.key == SDLK_RETURN && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS)) start = true;
            }
            SDL_Delay(10);
        }
        if (!start) { SDL_DestroyWindow(window); SDL_Quit(); return 18; }
        SDL_SetWindowTitle(window, "SDL_gpu rasterizer pixel tests");
    }
    const int result = Run(window);
    SDL_DestroyWindow(window); SDL_Quit();
    return result;
}
