#include "CKBgfxRasterizer.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

CKBgfxRasterizerDriver::CKBgfxRasterizerDriver(CKBgfxRasterizer *owner)
{
    m_Owner = owner;
    m_Hardware = TRUE;
    m_CapsUpToDate = FALSE;
    m_DriverIndex = 0;
    m_Desc = "bgfx D3D11 Driver";

    DEVMODEA dm;
    memset(&dm, 0, sizeof(dm));
    dm.dmSize = sizeof(dm);

    int modeIndex = 0;
    while (EnumDisplaySettingsA(NULL, modeIndex, &dm))
    {
        if (dm.dmBitsPerPel >= 16 && dm.dmPelsWidth >= 640 && dm.dmPelsHeight >= 400)
        {
            VxDisplayMode vdm;
            vdm.Width = (int)dm.dmPelsWidth;
            vdm.Height = (int)dm.dmPelsHeight;
            vdm.Bpp = (int)dm.dmBitsPerPel;
            vdm.RefreshRate = (int)dm.dmDisplayFrequency;
            if (!m_DisplayModes.IsHere(vdm))
                m_DisplayModes.PushBack(vdm);
        }
        ++modeIndex;
    }

    if (m_DisplayModes.Size() == 0)
    {
        VxDisplayMode fallback;
        fallback.Width = 640;
        fallback.Height = 480;
        fallback.Bpp = 32;
        fallback.RefreshRate = 60;
        m_DisplayModes.PushBack(fallback);
    }

    CKTextureDesc texDesc;
    texDesc.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
    VxPixelFormat2ImageDesc(_32_ARGB8888, texDesc.Format);
    m_TextureFormats.PushBack(texDesc);

    CKTextureDesc texDescBGRA;
    texDescBGRA.Flags = CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
    VxPixelFormat2ImageDesc(_32_BGRA8888, texDescBGRA.Format);
    m_TextureFormats.PushBack(texDescBGRA);

    memset(&m_3DCaps, 0, sizeof(m_3DCaps));
    m_3DCaps.MinTextureWidth = 1;
    m_3DCaps.MinTextureHeight = 1;
    m_3DCaps.MaxTextureWidth = 16384;
    m_3DCaps.MaxTextureHeight = 16384;
    m_3DCaps.MaxTextureRatio = 16384;
    m_3DCaps.MaxClipPlanes = 6;
    m_3DCaps.MaxActiveLights = 8;
    m_3DCaps.MaxNumberBlendStage = 8;
    m_3DCaps.MaxNumberTextureStage = 8;
    m_3DCaps.TextureFilterCaps = CKRST_TFILTERCAPS_NEAREST
                               | CKRST_TFILTERCAPS_LINEAR
                               | CKRST_TFILTERCAPS_MIPNEAREST
                               | CKRST_TFILTERCAPS_MIPLINEAR
                               | CKRST_TFILTERCAPS_LINEARMIPNEAREST
                               | CKRST_TFILTERCAPS_LINEARMIPLINEAR
                               | CKRST_TFILTERCAPS_ANISOTROPIC;
    m_3DCaps.TextureAddressCaps = CKRST_TADDRESSCAPS_WRAP
                                | CKRST_TADDRESSCAPS_MIRROR
                                | CKRST_TADDRESSCAPS_CLAMP
                                | CKRST_TADDRESSCAPS_BORDER
                                | CKRST_TADDRESSCAPS_INDEPENDENTUV;
    m_3DCaps.CKRasterizerSpecificCaps = CKRST_SPECIFICCAPS_CANDOVERTEXBUFFER
                                      | CKRST_SPECIFICCAPS_CANDOINDEXBUFFER
                                      | CKRST_SPECIFICCAPS_COPYTEXTURE
                                      | CKRST_SPECIFICCAPS_HARDWARETL
                                      | CKRST_SPECIFICCAPS_SUPPORTSHADERS;

    memset(&m_2DCaps, 0, sizeof(m_2DCaps));
    m_2DCaps.Caps = CKRST_2DCAPS_WINDOWED | CKRST_2DCAPS_3D | CKRST_2DCAPS_GDI;

    m_CapsUpToDate = TRUE;
}

CKBgfxRasterizerDriver::~CKBgfxRasterizerDriver()
{
    for (int i = 0; i < m_Contexts.Size(); ++i)
        delete m_Contexts[i];
    m_Contexts.Clear();
}

CKRasterizerContext *CKBgfxRasterizerDriver::CreateContext()
{
    auto *ctx = new CKBgfxRasterizerContext(this);
    m_Contexts.PushBack(ctx);
    return ctx;
}

CKBOOL CKBgfxRasterizerDriver::DestroyContext(CKRasterizerContext *Context)
{
    for (int i = 0; i < m_Contexts.Size(); ++i)
    {
        if (m_Contexts[i] == Context)
        {
            delete m_Contexts[i];
            m_Contexts.RemoveAt(i);
            return TRUE;
        }
    }
    return FALSE;
}

CKERROR CKBgfxRasterizerDriver::GetProgrammableCaps(VxProgCapsDesc &Caps)
{
    memset(&Caps, 0, sizeof(Caps));

    const bgfx::Caps *bgfxCaps = bgfx::getCaps();
    if (bgfxCaps)
    {
        Caps.MaxRenderViews = (CKWORD)bgfxCaps->limits.maxViews;
        Caps.MaxEncoders = bgfxCaps->limits.maxEncoders;
        Caps.MaxVertexStreams = bgfxCaps->limits.maxVertexStreams;
        Caps.MaxTextureStages = bgfxCaps->limits.maxTextureSamplers;
        Caps.MaxComputeBindings = bgfxCaps->limits.maxTextureSamplers;
        Caps.MaxUniforms = bgfxCaps->limits.maxUniforms;
        Caps.MaxFrameBuffers = bgfxCaps->limits.maxFrameBuffers;
        Caps.MaxColorAttachments = bgfxCaps->limits.maxFBAttachments;
        Caps.MaxTransientVertexBufferSize = bgfxCaps->limits.maxTransientVbSize;
        Caps.MaxTransientIndexBufferSize = bgfxCaps->limits.maxTransientIbSize;
        Caps.MaxTransientInstanceBufferSize = bgfxCaps->limits.maxTransientVbSize;
        Caps.MaxInstanceCount = bgfxCaps->limits.maxDrawCalls;
        Caps.MaxTransforms = CKRST_MAX_TRANSFORMS;
        Caps.MaxOcclusionQueries = bgfxCaps->limits.maxOcclusionQueries;
        Caps.MaxIndirectBuffers = 128;
        Caps.MaxComputeWorkGroupSize[0] = 65535;
        Caps.MaxComputeWorkGroupSize[1] = 65535;
        Caps.MaxComputeWorkGroupSize[2] = 65535;

        switch (bgfxCaps->rendererType)
        {
        case bgfx::RendererType::Direct3D11: Caps.MaxShaderModel = 50; break;
        case bgfx::RendererType::Direct3D12: Caps.MaxShaderModel = 60; break;
        case bgfx::RendererType::Vulkan:     Caps.MaxShaderModel = 60; break;
        case bgfx::RendererType::OpenGL:     Caps.MaxShaderModel = 40; break;
        default:                             Caps.MaxShaderModel = 40; break;
        }

        uint64_t s = bgfxCaps->supported;
        Caps.Caps = CKRST_PROGCAPS_VERTEXSHADER
                  | CKRST_PROGCAPS_PIXELSHADER
                  | CKRST_PROGCAPS_RENDER_VIEWS
                  | CKRST_PROGCAPS_FRAMEBUFFER
                  | CKRST_PROGCAPS_TRANSIENT_BUFFERS
                  | CKRST_PROGCAPS_SCISSOR
                  | CKRST_PROGCAPS_BUFFER_UPDATE
                  | CKRST_PROGCAPS_TEXTURE_UPDATE
                  | CKRST_PROGCAPS_TRANSFORM_CACHE
                  | CKRST_PROGCAPS_BLEND_EQUATION;

        if (s & BGFX_CAPS_INSTANCING)
            Caps.Caps |= CKRST_PROGCAPS_INSTANCING;
        if (s & BGFX_CAPS_TEXTURE_READ_BACK)
            Caps.Caps |= CKRST_PROGCAPS_READBACK;
        if (s & BGFX_CAPS_TEXTURE_BLIT)
            Caps.Caps |= CKRST_PROGCAPS_BLIT;
        if (s & BGFX_CAPS_INDEX32)
            Caps.Caps |= CKRST_PROGCAPS_INDEX32;
        if (s & BGFX_CAPS_TEXTURE_COMPARE_ALL)
            Caps.Caps |= CKRST_PROGCAPS_TEXTURE_COMPARISON;
        if (s & BGFX_CAPS_RENDERER_MULTITHREADED)
            Caps.Caps |= CKRST_PROGCAPS_MULTITHREADED_SUBMIT;
        if (s & BGFX_CAPS_COMPUTE)
            Caps.Caps |= CKRST_PROGCAPS_COMPUTE;
        if (s & BGFX_CAPS_OCCLUSION_QUERY)
            Caps.Caps |= CKRST_PROGCAPS_OCCLUSION_QUERY;
        if (s & BGFX_CAPS_DRAW_INDIRECT)
            Caps.Caps |= CKRST_PROGCAPS_DRAW_INDIRECT;
        Caps.Caps |= CKRST_PROGCAPS_TEXTURE_CUBE;
        if (s & BGFX_CAPS_IMAGE_RW)
            Caps.Caps |= CKRST_PROGCAPS_IMAGE_RW;

        if (bgfxCaps->formats[bgfx::TextureFormat::D16] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER
         || bgfxCaps->formats[bgfx::TextureFormat::D24] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER
         || bgfxCaps->formats[bgfx::TextureFormat::D24S8] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER
         || bgfxCaps->formats[bgfx::TextureFormat::D32F] & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER)
            Caps.Caps |= CKRST_PROGCAPS_DEPTH_TEXTURE;
    }
    else
    {
        Caps.MaxRenderViews = CKRST_MAX_RENDER_VIEWS;
        Caps.MaxEncoders = CKRST_MAX_ENCODERS;
        Caps.MaxVertexStreams = CKRST_MAX_VERTEX_STREAMS;
        Caps.MaxTextureStages = CKRST_MAX_TEXTURE_STAGES;
        Caps.MaxComputeBindings = 8;
        Caps.MaxUniforms = 128;
        Caps.MaxFrameBuffers = 64;
        Caps.MaxColorAttachments = 4;
        Caps.MaxTransientVertexBufferSize = 1 << 20;
        Caps.MaxTransientIndexBufferSize = 1 << 20;
        Caps.MaxTransientInstanceBufferSize = 1 << 20;
        Caps.MaxInstanceCount = 4096;
        Caps.MaxTransforms = CKRST_MAX_TRANSFORMS;
        Caps.MaxShaderModel = 50;
        Caps.MaxOcclusionQueries = 256;
        Caps.MaxIndirectBuffers = 64;
        Caps.MaxComputeWorkGroupSize[0] = 65535;
        Caps.MaxComputeWorkGroupSize[1] = 65535;
        Caps.MaxComputeWorkGroupSize[2] = 65535;
        Caps.Caps = CKRST_PROGCAPS_VERTEXSHADER
                  | CKRST_PROGCAPS_PIXELSHADER
                  | CKRST_PROGCAPS_RENDER_VIEWS
                  | CKRST_PROGCAPS_FRAMEBUFFER
                  | CKRST_PROGCAPS_TRANSIENT_BUFFERS
                  | CKRST_PROGCAPS_SCISSOR
                  | CKRST_PROGCAPS_INSTANCING
                  | CKRST_PROGCAPS_BUFFER_UPDATE
                  | CKRST_PROGCAPS_TEXTURE_UPDATE
                  | CKRST_PROGCAPS_BLEND_EQUATION
                  | CKRST_PROGCAPS_BLIT
                  | CKRST_PROGCAPS_TRANSFORM_CACHE
                  | CKRST_PROGCAPS_INDEX32
                  | CKRST_PROGCAPS_DEPTH_TEXTURE
                  | CKRST_PROGCAPS_COMPUTE
                  | CKRST_PROGCAPS_OCCLUSION_QUERY
                  | CKRST_PROGCAPS_DRAW_INDIRECT
                  | CKRST_PROGCAPS_TEXTURE_CUBE
                  | CKRST_PROGCAPS_IMAGE_RW;
    }

    return CK_OK;
}
