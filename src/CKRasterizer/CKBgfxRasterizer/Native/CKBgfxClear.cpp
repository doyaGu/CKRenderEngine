#include "CKBgfxRasterizerContext.h"

// Direct3D 11 partial clears draw a rect; builds without its shaders have no
// Direct3D 11 renderer.
#if CKBGFX_SHADER_DX11
#include "shaders/native/vs_rect_clear.h"
#include "shaders/native/fs_rect_clear.h"
#endif

CKERROR CKBgfxRasterizerContext::PrepareRectClear()
{
#if CKBGFX_SHADER_DX11
    if (bgfx::isValid(m_RectClearProgram)) return CK_OK;
    const bgfx::ShaderHandle vertex = bgfx::createShader(bgfx::makeRef(ck_vs_rect_clear_dx11, sizeof(ck_vs_rect_clear_dx11)));
    const bgfx::ShaderHandle fragment = bgfx::createShader(bgfx::makeRef(ck_fs_rect_clear_dx11, sizeof(ck_fs_rect_clear_dx11)));
    if (bgfx::isValid(vertex) && bgfx::isValid(fragment))
        m_RectClearProgram = bgfx::createProgram(vertex, fragment, true);
    else {
        if (bgfx::isValid(vertex)) bgfx::destroy(vertex);
        if (bgfx::isValid(fragment)) bgfx::destroy(fragment);
    }
    static const float vertices[][3] = {{-1, -1, 0}, {1, -1, 0}, {-1, 1, 0}, {1, 1, 0}};
    bgfx::VertexLayout layout;
    layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float).end();
    m_RectClearVertices = bgfx::createVertexBuffer(bgfx::makeRef(vertices, sizeof(vertices)), layout);
    m_RectClearColor = bgfx::createUniform("u_ckRectClearColor", bgfx::UniformType::Vec4);
    m_RectClearDepth = bgfx::createUniform("u_ckRectClearDepth", bgfx::UniformType::Vec4);
    if (!bgfx::isValid(m_RectClearProgram) || !bgfx::isValid(m_RectClearVertices) ||
        !bgfx::isValid(m_RectClearColor) || !bgfx::isValid(m_RectClearDepth)) {
        ReleaseRectClear();
        return CKERR_OUTOFMEMORY;
    }
    return CK_OK;
#else
    return CKERR_NOTIMPLEMENTED;
#endif
}

void CKBgfxRasterizerContext::EncodeRectClear(bgfx::ViewId View, const CKRenderPassDesc &Desc)
{
    bgfx::discard(BGFX_DISCARD_ALL);
    const float color[] = {float((Desc.ClearColor >> 16) & 255) / 255.0f,
        float((Desc.ClearColor >> 8) & 255) / 255.0f, float(Desc.ClearColor & 255) / 255.0f,
        float((Desc.ClearColor >> 24) & 255) / 255.0f};
    const float depth[] = {Desc.ClearZ, 0, 0, 0};
    bgfx::setUniform(m_RectClearColor, color);
    bgfx::setUniform(m_RectClearDepth, depth);
    uint64_t state = BGFX_STATE_MSAA | BGFX_STATE_PT_TRISTRIP;
    if (Desc.ClearFlags & CKRST_CTXCLEAR_COLOR) state |= BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
    if (Desc.ClearFlags & CKRST_CTXCLEAR_DEPTH) state |= BGFX_STATE_DEPTH_TEST_ALWAYS | BGFX_STATE_WRITE_Z;
    uint32_t stencil = BGFX_STENCIL_NONE;
    if (Desc.ClearFlags & CKRST_CTXCLEAR_STENCIL)
        stencil = BGFX_STENCIL_TEST_ALWAYS | BGFX_STENCIL_FUNC_REF(Desc.ClearStencil) |
            BGFX_STENCIL_FUNC_RMASK(0xff) | BGFX_STENCIL_OP_FAIL_S_REPLACE |
            BGFX_STENCIL_OP_FAIL_Z_REPLACE | BGFX_STENCIL_OP_PASS_Z_REPLACE;
    bgfx::setState(state);
    bgfx::setStencil(stencil, stencil);
    bgfx::setScissor(UINT16_MAX);
    bgfx::setVertexBuffer(0, m_RectClearVertices);
    bgfx::submit(View, m_RectClearProgram, 0, BGFX_DISCARD_ALL);
}

void CKBgfxRasterizerContext::ReleaseRectClear()
{
    if (bgfx::isValid(m_RectClearProgram)) bgfx::destroy(m_RectClearProgram);
    if (bgfx::isValid(m_RectClearVertices)) bgfx::destroy(m_RectClearVertices);
    if (bgfx::isValid(m_RectClearColor)) bgfx::destroy(m_RectClearColor);
    if (bgfx::isValid(m_RectClearDepth)) bgfx::destroy(m_RectClearDepth);
    m_RectClearProgram = BGFX_INVALID_HANDLE;
    m_RectClearVertices = BGFX_INVALID_HANDLE;
    m_RectClearColor = BGFX_INVALID_HANDLE;
    m_RectClearDepth = BGFX_INVALID_HANDLE;
}
