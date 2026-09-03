// CKRasterizerBackend (spec 5.10) exercised on the recording backend (the
// NULL backend plus a log): the interface a backend has to implement, and
// the frame semantics the translation core relies on (passes are sequential,
// draws carry the sticky pipeline state and bound textures, Present closes
// the frame and reports stats).

#include <stdio.h>
#include <string.h>

#include "CKFFShaderABI.h"
#include "FFPDiagnosticHarness.h"
#include "TestTriangleMultiset.h"

namespace {

struct Fixture {
    FFPRecordingDriver Driver;
    FFPRecordingContext *Device;   // the recording backend and its log
    FFPRecordingContext *Backend;  // the same object through the interface under test

    Fixture() : Device(NULL), Backend(NULL)
    {
        Device = static_cast<FFPRecordingContext *>(Driver.CreateBackend());
        TestCheck(Device != NULL, "recording backend");
        Backend = Device;
        CKBackendInitDesc init;
        init.Width = 64;
        init.Height = 48;
        init.Bpp = 32;
        init.ZBpp = 24;
        init.StencilBpp = 8;
        TestCheck(Backend->Init(&init) == CK_OK, "backend Init");
    }
    ~Fixture()
    {
        if (Device)
            Driver.DestroyBackend(Device);
    }
};

CKDWORD MakeLayout(CKRasterizerBackend *b)
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

CKDWORD MakeProgram(CKRasterizerBackend *b)
{
    static const CKBYTE blob[4] = {1, 2, 3, 4};
    CKShaderDesc shader;
    shader.Format = CKRST_SHADER_FORMAT_NATIVE;
    shader.Profile = b->GetCaps().ShaderProfile;
    shader.Code = blob;
    shader.CodeSize = sizeof(blob);
    shader.Stage = CKRST_SHADER_VERTEX;
    CKDWORD vs = 0, fs = 0, program = 0;
    TestCheck(b->CreateShader(&shader, &vs) == CK_OK && vs != 0, "vertex shader");
    shader.Stage = CKRST_SHADER_PIXEL;
    TestCheck(b->CreateShader(&shader, &fs) == CK_OK && fs != 0, "pixel shader");
    TestCheck(b->CreateProgram(vs, fs, &program) == CK_OK && program != 0, "program");
    return program;
}

void TestCapsAndTables()
{
    Fixture f;
    const CKBackendCaps &caps = f.Backend->GetCaps();
    TestCheck(caps.ShaderProfile == CKRST_SHADER_PROFILE_DX11, "caps carry the shader profile");
    TestCheck(caps.MaxTextureSize > 0, "caps carry the texture size limit");
    TestCheck((caps.Features & CKRST_DEVCAPS_TEXTURE_READBACK) != 0, "caps carry the device features");
    TestCheck(f.Backend->GetDeviceStatus() == CK_OK && f.Backend->IsIdle(), "idle after Init");

    // The constant block table is the shader ABI.
    TestCheck(strcmp(CKBackendConstantBlockInfo(CKRST_BLOCK_MATRICES).Name, "u_ffMatrices") == 0 &&
                  CKBackendConstantBlockInfo(CKRST_BLOCK_MATRICES).Mat4 &&
                  CKBackendConstantBlockInfo(CKRST_BLOCK_MATRICES).Count == CKFF_MATRIX_VEC4_COUNT,
              "matrices block");
    TestCheck(CKBackendConstantBlockInfo(CKRST_BLOCK_SPEC).Count == CKFF_SPEC_UNIFORM_VEC4_COUNT &&
                  !CKBackendConstantBlockInfo(CKRST_BLOCK_SPEC).Mat4,
              "spec block");
    TestCheck(CKBackendConstantBlockInfo(CKRST_BLOCK_COUNT).Name == NULL, "invalid block");
    for (int block = 0; block < CKRST_BLOCK_COUNT; ++block) {
        TestCheck(CKBackendConstantBlockInfo((CKBackendConstantBlock)block).Name != NULL &&
                      f.Backend->GetBlockUniformForTests((CKBackendConstantBlock)block) != 0,
                  "every block has a name and a uniform");
    }
    TestCheck(strcmp(CKBackendSamplerSlotName(0), "s_texture0") == 0 &&
                  strcmp(CKBackendSamplerSlotName(CKFF_CUBE_SAMPLER_SLOT_BASE), "s_textureCube0") == 0 &&
                  strcmp(CKBackendSamplerSlotName(CKFF_VOLUME_SAMPLER_SLOT_BASE + 3), "s_textureVolume3") == 0 &&
                  strcmp(CKBackendSamplerSlotName(CKRST_BACKEND_SLOT_PRESENT), "s_sceneColor") == 0 &&
                  CKBackendSamplerSlotName(CKRST_BACKEND_SLOT_COUNT) == NULL,
              "sampler slot names follow the fixed layout");
}

void TestResources()
{
    Fixture f;
    CKRasterizerBackend *b = f.Backend;
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
    CKBackendDepthDesc depthDesc;
    depthDesc.Width = 8;
    depthDesc.Height = 8;
    CKDWORD depth = 0;
    TestCheck(b->CreateDepthTexture(&depthDesc, &depth) == CK_OK && depth != 0, "depth texture");
    CKBackendRenderTargetDesc rtDesc;
    rtDesc.ColorTexture = color;
    rtDesc.DepthTexture = depth;
    CKDWORD rt = 0;
    TestCheck(b->CreateRenderTarget(&rtDesc, &rt) == CK_OK && rt != 0, "render target");
    TestCheck(b->IsObjectAlive(rt, CKRST_OBJ_RENDERTARGET), "render target alive");

    const CKDWORD layout = MakeLayout(b);
    float vertices[3 * 3] = {0};
    CKBackendBufferDesc vbDesc;
    vbDesc.Kind = CKRST_BACKEND_BUFFER_VERTEX;
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
    CKBackendBufferDesc ibDesc;
    ibDesc.Kind = CKRST_BACKEND_BUFFER_INDEX;
    ibDesc.Size = sizeof(indices);
    ibDesc.Dynamic = TRUE;
    CKDWORD ib = 0;
    TestCheck(b->CreateBuffer(&ibDesc, &ib) == CK_OK && ib != 0, "index buffer");
    TestCheck(b->UpdateBuffer(ib, 0, sizeof(indices), indices) == CK_OK, "index buffer update");
    TestCheck(b->UpdateBuffer(0xDEAD, 0, 4, indices) != CK_OK, "unknown buffer update rejected");

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
    CKRasterizerBackend *b = f.Backend;
    const CKDWORD layout = MakeLayout(b);
    const CKDWORD program = MakeProgram(b);

    // Draws outside a pass are refused.
    CKBackendDraw draw;
    draw.Program = program;
    draw.Layout = layout;
    CKBackendTransientVertices tv;
    TestCheck(b->AllocTransientVertices(3, layout, &tv) && tv.Data && tv.Count == 3 && tv.Stride == 12 && tv.Token != 0,
              "transient vertices");
    draw.TransientVertices = &tv;
    draw.VertexCount = 3;
    TestCheck(b->Draw(&draw) == CKERR_INVALIDOPERATION, "draw without a pass rejected");

    // Pass 1: clear + draw with a sticky state and a bound texture.
    CKBackendPassDesc pass;
    pass.Rect.right = 64;
    pass.Rect.bottom = 48;
    pass.ClearFlags = CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH;
    pass.ClearColor = 0xFF102030;
    pass.Name = "scene";
    TestCheck(b->BeginPass(&pass) == CK_OK, "BeginPass");
    TestCheck(!f.Device->ViewClears.empty() && f.Device->ViewClears.back().Color == 0xFF102030 &&
                  f.Device->ViewClears.back().Rect.right == 64,
              "the pass clear reaches the device view");

    CKBackendPipelineState state;
    state.State.Lo = CKRST_STATE_WRITE_RGBA | CKRST_STATE_DEPTH_TEST | CKRST_STATE_DEPTH_FUNC(VXCMP_LESSEQUAL);
    state.StencilRef = 0x107; // clamped to 8 bits
    state.ScissorEnabled = TRUE;
    state.Scissor.left = 4;
    state.Scissor.top = 4;
    state.Scissor.right = 20;
    state.Scissor.bottom = 20;
    state.PointSize = 3.0f;
    b->SetPipelineState(&state);
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
    b->BindTexture(2, texture, &sampler);
    float block[CKFF_SPEC_UNIFORM_VEC4_COUNT * 4] = {0};
    b->PushConstants(CKRST_BLOCK_SPEC, block, CKFF_SPEC_UNIFORM_VEC4_COUNT);
    b->SetMarker("first");
    TestCheck(b->Draw(&draw) == CK_OK, "Draw");
    TestCheck(f.Device->Encoder.Submits.size() == 1 && f.Device->Encoder.Submits[0].Program == program &&
                  f.Device->Encoder.Submits[0].Marker == "first",
              "the draw reaches the device with its program and marker");
    TestCheck(f.Device->Encoder.LastState.Lo == state.State.Lo, "the pipeline state reaches the device");
    TestCheck(f.Device->Encoder.LastStencilRef == 0x07, "the stencil ref is clamped to 8 bits");
    TestCheck(f.Device->Encoder.ScissorEnabled && f.Device->Encoder.LastScissor.right == 20, "the scissor reaches the device");
    TestCheck(f.Device->Encoder.LastPointSize == 3.0f, "the point size reaches the device");
    TestCheck(f.Device->Encoder.TextureBindCount == 1 && f.Device->Encoder.LastTextureStage == 2 &&
                  f.Device->Encoder.LastTextureHandle == texture &&
                  f.Device->Encoder.LastTextureUniform == f.Backend->GetSamplerUniformForTests(2),
              "bound textures are set on their slot through the slot's sampler uniform");
    const CKDWORD specUniform = f.Backend->GetBlockUniformForTests(CKRST_BLOCK_SPEC);
    TestCheck(f.Device->Encoder.FloatUniforms.count(specUniform) == 1 &&
                  f.Device->Encoder.FloatUniforms[specUniform].size() == CKFF_SPEC_UNIFORM_VEC4_COUNT * 4,
              "PushConstants uploads the block through its uniform");

    // Pass 2 follows pass 1 (sequential views); the state stays sticky.
    pass.ClearFlags = 0;
    pass.Name = "overlay";
    TestCheck(b->BeginPass(&pass) == CK_OK, "second BeginPass");
    b->BindTexture(2, 0, NULL);
    TestCheck(b->Draw(&draw) == CK_OK, "second Draw");
    TestCheck(f.Device->Encoder.Submits.size() == 2 && f.Device->Encoder.Submits[1].View > f.Device->Encoder.Submits[0].View,
              "the second pass draws into a later view");
    TestCheck(f.Device->Encoder.TextureBindCount == 1, "an unbound slot is not set again");
    TestCheck(f.Device->Encoder.LastState.Lo == state.State.Lo, "the pipeline state is sticky across passes");

    // Blit inside the pass; layers are cube faces / slices and are range-checked.
    CKTextureDesc dstDesc = tex;
    dstDesc.Flags |= CKRST_TEXTURE_BLIT_DST | CKRST_TEXTURE_READBACK;
    CKDWORD dst = 0;
    TestCheck(b->CreateTexture(&dstDesc, NULL, &dst) == CK_OK, "blit destination");
    TestCheck(b->Blit(dst, 0, 0, 0, 0, texture, 0, 0, NULL) == CK_OK, "Blit");
    TestCheck(b->Blit(dst, 0, 1, 0, 0, texture, 0, 0, NULL) == CKERR_INVALIDPARAMETER, "a 2D texture has one layer");

    // Present closes the frame and fills the stats.
    CKDWORD frame = 0;
    TestCheck(b->Present(CKRST_BACKEND_PRESENT_IMMEDIATE, &frame) == CK_OK, "Present");
    TestCheck(f.Device->Frames.size() == 1 && f.Device->Frames[0] == CKRST_BACKEND_PRESENT_IMMEDIATE, "Present(IMMEDIATE) recorded");
    const CKBackendStats &stats = b->GetStats();
    TestCheck(stats.Frames == 1 && stats.Passes == 2 && stats.Draws == 2 && stats.Blits == 1, "stats of the frame");
    TestCheck(b->IsIdle(), "idle after Present");
    TestCheck(b->Present(CKRST_BACKEND_PRESENT_PRESERVE, &frame) == CK_OK && f.Device->Frames.back() == CKRST_BACKEND_PRESENT_PRESERVE,
              "Present(PRESERVE) recorded");
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
