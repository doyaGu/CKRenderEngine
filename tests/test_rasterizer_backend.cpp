// CPU draw-command behavior exercised through the test-owned recording
// implementation: passes are sequential, draws carry the sticky pipeline
// state and bound textures, and Present closes the frame and reports stats.

#include <stdio.h>
#include <string.h>

#include "FFPRecordingHarness.h"
#include "TestTriangleMultiset.h"

namespace {

struct Fixture {
    FFPRecordingDriver Driver;
    FFPRecordingBackend *Backend;  // the recording backend and its log

    Fixture() : Backend(NULL)
    {
        Backend = static_cast<FFPRecordingBackend *>(Driver.CreateBackend());
        TestCheck(Backend != NULL, "recording backend");
        CKRasterizerInitParameters init;
        init.Width = 64;
        init.Height = 48;
        init.Bpp = 32;
        init.ZBpp = 24;
        init.StencilBpp = 8;
        TestCheck(Backend->Init(&init) == CK_OK, "backend Init");
    }
    ~Fixture()
    {
        if (Backend)
            Driver.DestroyBackend(Backend);
    }
};

CKDWORD MakeLayout(CKRecordingBackend *b)
{
    CKVertexElementDesc elements[1];
    memset(elements, 0, sizeof(elements));
    elements[0].Attrib = CKRST_ATTRIB_POSITION;
    elements[0].Type = CKRST_ATTRIBTYPE_FLOAT;
    elements[0].Count = 3;
    CKVertexLayoutDesc desc;
    desc.Elements = elements;
    desc.ElementCount = 1;
    desc.Stride = 12;
    CKDWORD layout = 0;
    TestCheck(b->CreateVertexLayout(&desc, &layout) == CK_OK && layout != 0, "vertex layout");
    return layout;
}

CKDWORD MakeProgram(CKRecordingBackend *b)
{
    static const CKBYTE blob[4] = {1, 2, 3, 4};
    CKShaderDesc shader;
    shader.Format = b->GetCaps().ShaderFormat;
    shader.Profile = b->GetCaps().ShaderProfile;
    shader.Code = blob;
    shader.CodeSize = sizeof(blob);
    shader.Stage = CKRST_SHADER_VERTEX;
    CKDWORD vs = 0, fs = 0, program = 0;
    TestCheck(b->CreateShader(&shader, &vs) == CK_OK && vs != 0, "vertex shader");
    shader.Stage = CKRST_SHADER_PIXEL;
    shader.UniformBufferCount = shader.SamplerCount = 1;
    TestCheck(b->CreateShader(&shader, &fs) == CK_OK && fs != 0, "pixel shader");
    CKFFProgramDesc desc;
    desc.VertexShader = vs;
    desc.PixelShader = fs;
    CKFFUniformBufferBinding buffer;
    buffer.Stage = CKRST_SHADER_PIXEL;
    buffer.Size = 32;
    desc.UniformBuffers.PushBack(buffer);
    CKFFUniformBinding uniform;
    uniform.Slot = 27;
    uniform.Name = "u_customData";
    uniform.Count = 2;
    uniform.Stage = CKRST_SHADER_PIXEL;
    desc.Uniforms.PushBack(uniform);
    CKFFSamplerBinding sampler;
    sampler.Slot = 2;
    sampler.Name = "s_customImage";
    desc.Samplers.PushBack(sampler);
    TestCheck(b->CreateProgram(&desc, &program) == CK_OK && program != 0, "program");
    return program;
}

CKERROR UpdateBuffer(CKRecordingBackend *backend, CKBufferKind kind,
                     CKDWORD buffer, CKDWORD offset,
                     CKDWORD size, const void *data)
{
    CKBufferUpdateDesc desc;
    desc.Kind = kind;
    desc.Buffer = buffer;
    desc.Offset = offset;
    desc.Size = size;
    desc.Data = data;
    return backend->UpdateBuffer(&desc);
}

void TestCapsAndTables()
{
    Fixture f;
    const CKRasterizerDeviceCaps &caps = f.Backend->GetCaps();
    TestCheck(caps.ShaderFormat == CKRST_SHADER_FORMAT_DXIL, "caps carry the shader payload format");
    TestCheck(caps.ShaderProfile == CKRST_SHADER_PROFILE_DX12, "caps carry the shader profile");
    TestCheck(caps.MaxTextureSize > 0, "caps carry the texture size limit");
    TestCheck((caps.Features & CKRST_DEVCAPS_TEXTURE_READBACK) != 0, "caps carry the device features");
    TestCheck(f.Backend->GetDeviceStatus() == CK_OK && f.Backend->IsIdle(), "idle after Init");

    CKRasterizerTargetDesc targetDefaults;
    TestCheck(targetDefaults.ShaderFormat == CKRST_SHADER_FORMAT_UNKNOWN &&
                  targetDefaults.ShaderProfile == CKRST_SHADER_PROFILE_UNKNOWN,
              "shader target descriptor defaults are explicit");

    CKShaderDesc shaderDefaults;
    TestCheck(shaderDefaults.Format == CKRST_SHADER_FORMAT_UNKNOWN &&
                  shaderDefaults.Profile == CKRST_SHADER_PROFILE_UNKNOWN &&
                  shaderDefaults.EntryPoint && strcmp(shaderDefaults.EntryPoint, "main") == 0,
              "shader descriptor defaults are explicit");
    TestCheck(shaderDefaults.SamplerCount == 0 &&
                  shaderDefaults.StorageTextureCount == 0 &&
                  shaderDefaults.StorageBufferCount == 0 &&
                  shaderDefaults.UniformBufferCount == 0,
              "shader resource layout defaults to empty");

    TestCheck(caps.MaxTextureBindings == CKFF_TEXTURE_SLOT_COUNT,
              "generic logical texture capacity is independent of a shader family");
}

void TestResources()
{
    Fixture f;
    CKRecordingBackend *b = f.Backend;
    CKTextureDesc tex;
    VxPixelFormat2ImageDesc(_32_ARGB8888, tex.Format);
    tex.Format.Width = 8;
    tex.Format.Height = 8;
    tex.Format.BytesPerLine = 32;
    tex.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA | CKRST_TEXTURE_RENDERTARGET;
    tex.MipMapCount = 1;
    CKDWORD color = 0;
    TestCheck(b->CreateTexture(&tex, NULL, &color) == CK_OK && color != 0, "render target texture");
    TestCheck(b->IsObjectAlive(color, CKRST_OBJ_TEXTURE), "texture alive");
    CKDepthTextureDesc depthDesc;
    depthDesc.Width = 8;
    depthDesc.Height = 8;
    CKDWORD depth = 0;
    TestCheck(b->CreateDepthTexture(&depthDesc, &depth) == CK_OK && depth != 0, "depth texture");
    CKRenderTargetDesc rtDesc;
    rtDesc.ColorTexture = color;
    rtDesc.DepthTexture = depth;
    CKDWORD rt = 0;
    TestCheck(b->CreateRenderTarget(&rtDesc, &rt) == CK_OK && rt != 0, "render target");
    TestCheck(b->IsObjectAlive(rt, CKRST_OBJ_RENDERTARGET), "render target alive");

    const CKDWORD layout = MakeLayout(b);
    float vertices[3 * 3] = {0};
    CKBufferDesc vbDesc;
    vbDesc.Kind = CKRST_BUFFER_VERTEX;
    vbDesc.Size = sizeof(vertices);
    vbDesc.Stride = 12;
    vbDesc.Layout = layout;
    vbDesc.InitialData = vertices;
    CKDWORD vb = 0;
    TestCheck(b->CreateBuffer(&vbDesc, &vb) == CK_OK && vb != 0, "vertex buffer");
    vbDesc.Size = 13;
    CKDWORD bad = 0;
    TestCheck(b->CreateBuffer(&vbDesc, &bad) != CK_OK && bad == 0, "vertex buffer size must be a multiple of the stride");
    CKWORD indices[3] = {0, 1, 2};
    CKBufferDesc ibDesc;
    ibDesc.Kind = CKRST_BUFFER_INDEX;
    ibDesc.Size = sizeof(indices);
    ibDesc.Dynamic = TRUE;
    CKDWORD ib = 0;
    TestCheck(b->CreateBuffer(&ibDesc, &ib) == CK_OK && ib != 0, "index buffer");
    TestCheck(UpdateBuffer(b, CKRST_BUFFER_INDEX, ib,
                           0, sizeof(indices), indices) == CK_OK,
              "index buffer update");
    TestCheck(UpdateBuffer(b, CKRST_BUFFER_VERTEX, ib,
                           0, sizeof(indices), indices) != CK_OK,
              "buffer kind mismatch rejected");
    TestCheck(UpdateBuffer(b, CKRST_BUFFER_INDEX, 0xDEAD,
                           0, 4, indices) != CK_OK,
              "unknown buffer update rejected");
    ibDesc.Kind = (CKBufferKind)99;
    bad = 0;
    TestCheck(b->CreateBuffer(&ibDesc, &bad) == CKERR_INVALIDPARAMETER && bad == 0,
              "unknown buffer kind rejected");

    TestCheck(b->DestroyObject(rt, CKRST_OBJ_RENDERTARGET) == CK_OK && !b->IsObjectAlive(rt, CKRST_OBJ_RENDERTARGET),
              "render target destroyed");
    TestCheck(b->DestroyObject(depth, CKRST_OBJ_TEXTURE) == CK_OK, "depth destroyed");
    TestCheck(b->DestroyObject(color, CKRST_OBJ_TEXTURE) == CK_OK && !b->IsObjectAlive(color, CKRST_OBJ_TEXTURE),
              "texture destroyed");
    TestCheck(b->DestroyObject(vb, CKRST_OBJ_VERTEXBUFFER) == CK_OK, "vertex buffer destroyed");
    TestCheck(b->DestroyObject(ib, CKRST_OBJ_INDEXBUFFER) == CK_OK, "index buffer destroyed");
    TestCheck(b->DestroyObject(layout, CKRST_OBJ_VERTEXLAYOUT) == CK_OK, "layout destroyed");
}

void TestFrame()
{
    Fixture f;
    CKRecordingBackend *b = f.Backend;
    const CKDWORD layout = MakeLayout(b);
    const CKDWORD program = MakeProgram(b);

    // Draws outside a pass are refused.
    CKDrawCommand draw;
    draw.Program = program;
    draw.Layout = layout;
    CKTransientVertexData tv;
    TestCheck(b->AllocTransientVertices(3, layout, &tv) && tv.Data && tv.Count == 3 && tv.Stride == 12 && tv.Token != 0,
              "transient vertices");
    draw.TransientVertices = &tv;
    draw.VertexCount = 3;
    TestCheck(b->Draw(&draw) == CKERR_INVALIDOPERATION, "draw without a pass rejected");

    // Pass 1: clear + draw with a sticky state and a bound texture.
    CKRenderPassDesc pass;
    pass.Rect.right = 64;
    pass.Rect.bottom = 48;
    pass.ClearFlags = CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH;
    pass.ClearColor = 0xFF102030;
    pass.Name = "scene";
    TestCheck(b->BeginPass(&pass) == CK_OK, "BeginPass");
    TestCheck(!f.Backend->PassClears.empty() && f.Backend->PassClears.back().Color == 0xFF102030 &&
                  f.Backend->PassClears.back().Rect.right == 64,
              "the pass clear reaches the backend");

    CKFFPipelineState state;
    state.State.Lo = CKRST_STATE_WRITE_RGBA | CKRST_STATE_DEPTH_TEST | CKRST_STATE_DEPTH_FUNC(VXCMP_LESSEQUAL);
    state.StencilRef = 0x107; // clamped to 8 bits
    state.ScissorEnabled = TRUE;
    state.Scissor.left = 4;
    state.Scissor.top = 4;
    state.Scissor.right = 20;
    state.Scissor.bottom = 20;
    draw.Pipeline = state;
    CKTextureDesc tex;
    VxPixelFormat2ImageDesc(_32_ARGB8888, tex.Format);
    tex.Format.Width = 4;
    tex.Format.Height = 4;
    tex.Format.BytesPerLine = 16;
    tex.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
    tex.MipMapCount = 1;
    CKDWORD texture = 0;
    TestCheck(b->CreateTexture(&tex, NULL, &texture) == CK_OK, "texture");
    CKSamplerDesc sampler;
    memset(&sampler, 0, sizeof(sampler));
    sampler.MinFilter = CKRST_FILTER_LINEAR;
    CKFFTextureBindings bindings;
    bindings[2].Texture = texture; bindings[2].Sampler = sampler;
    draw.Textures = &bindings;
    CKFFConstantSet constants;
    draw.Constants = &constants;
    float block[8] = {0};
    TestCheck(constants.Set(27, block, sizeof(block)) == CK_OK, "push arbitrary logical byte slot");
    draw.Marker = "first";
    TestCheck(b->Draw(&draw) == CK_OK, "Draw");
    TestCheck(f.Backend->Log.Draws.size() == 1 && f.Backend->Log.Draws[0].Program == program &&
                  f.Backend->Log.Draws[0].Marker == "first",
              "the draw reaches the backend with its program and marker");
    TestCheck(f.Backend->Log.LastState.Lo == state.State.Lo, "the pipeline state reaches the backend");
    TestCheck(f.Backend->Log.LastStencilRef == 0x07, "the stencil ref is clamped to 8 bits");
    TestCheck(f.Backend->Log.ScissorEnabled && f.Backend->Log.LastScissor.right == 20, "the scissor reaches the backend");
    TestCheck(f.Backend->Log.TextureBindCount == 1 && f.Backend->Log.LastTextureStage == 2 &&
                  f.Backend->Log.LastTextureHandle == texture &&
                  f.Backend->Log.LastTextureUniform == f.Backend->GetSamplerUniformForTests(2),
              "bound textures are set on their slot through the slot's sampler uniform");
    const CKDWORD dataUniform = f.Backend->GetBlockUniformForTests(27);
    TestCheck(f.Backend->Log.FloatUniforms.count(dataUniform) == 1 &&
                  f.Backend->Log.FloatUniforms[dataUniform].size() == 8,
              "The draw packet carries bytes through the declared logical slot");

    // Pass 2 follows pass 1 (passes are sequential); the caller explicitly reuses the packet.
    pass.ClearFlags = 0;
    pass.Name = "overlay";
    TestCheck(b->BeginPass(&pass) == CK_OK, "second BeginPass");
    bindings[2] = CKFFTextureSlot();
    draw.Marker = nullptr;
    TestCheck(b->Draw(&draw) == CK_OK, "second Draw");
    TestCheck(f.Backend->Log.Draws.size() == 2 && f.Backend->Log.Draws[1].Pass > f.Backend->Log.Draws[0].Pass,
              "the second draw lands on a later pass");
    TestCheck(f.Backend->Log.TextureBindCount == 1, "an unbound slot is not set again");
    TestCheck(f.Backend->Log.LastState.Lo == state.State.Lo, "the packet carries pipeline state across passes");

    // Blit inside the pass; layers are cube faces / slices and are range-checked.
    CKTextureDesc dstDesc = tex;
    dstDesc.Flags |= CKRST_TEXTURE_BLIT_DST | CKRST_TEXTURE_READBACK;
    CKDWORD dst = 0;
    TestCheck(b->CreateTexture(&dstDesc, NULL, &dst) == CK_OK, "blit destination");
    TestCheck(b->Blit(dst, 0, 0, 0, 0, texture, 0, 0, NULL) == CK_OK, "Blit");
    TestCheck(b->Blit(texture, 0, 0, 0, 0, dst, 0, 0, NULL) == CK_OK,
              "ordinary textures accept copies without the internal BLIT_DST flag");
    TestCheck(b->Blit(dst, 0, 1, 0, 0, texture, 0, 0, NULL) == CKERR_INVALIDPARAMETER, "a 2D texture has one layer");

    // Present closes the frame and fills the stats.
    CKDWORD frame = 0;
    TestCheck(b->Submit(CKRST_PRESENT_IMMEDIATE, TRUE, &frame) == CK_OK &&
                  b->GetLastSubmitId() != 0 &&
                  b->GetCompletedSubmitId() == b->GetLastSubmitId(),
              "Present");
    TestCheck(f.Backend->Frames.size() == 1 && f.Backend->Frames[0] == CKRST_PRESENT_IMMEDIATE, "Present(IMMEDIATE) recorded");
    const CKRecordingStats &stats = b->GetStats();
    TestCheck(stats.Frames == 1 && stats.Passes == 2 && stats.Draws == 2 && stats.Blits == 2, "stats of the frame");
    TestCheck(b->IsIdle(), "idle after Present");
    TestCheck(b->Submit(CKRST_PRESENT_UNCHANGED, FALSE, &frame) == CK_OK &&
                  f.Backend->Frames.back() == CKRST_PRESENT_UNCHANGED,
              "resource submission retains the presentation sync mode");
    TestCheck(b->GetStats().Passes == 0 && b->GetStats().Draws == 0, "an empty frame has no passes");

    // Readback: the recording backend reports the layout of a zero-filled
    // image.
    CKReadbackDesc readback;
    TestCheck(b->ReadTexture(dst, 0, &readback, NULL) == CK_OK && readback.Width == 4 && readback.Height == 4 &&
                  readback.RequiredSize == 4 * 4 * 4,
              "ReadTexture layout");
    TestCheck(b->DestroyObject(dst, CKRST_OBJ_TEXTURE) == CK_OK && b->DestroyObject(texture, CKRST_OBJ_TEXTURE) == CK_OK,
              "cleanup");
}

} // namespace

int main()
{
    TestFramework framework;
    framework.Run("caps and tables", TestCapsAndTables);
    framework.Run("resources", TestResources);
    framework.Run("frame", TestFrame);
    return framework.ExitCode();
}
