#ifndef CKRE_FFP_BENCHMARK_WORKLOAD_H
#define CKRE_FFP_BENCHMARK_WORKLOAD_H

#include "CKFixedFunctionPipeline.h"
#include "CKRasterizer.h"

#include <array>
#include <cstring>

namespace CKFFBenchmark {

constexpr CKDWORD DrawsPerFrame = 44;
constexpr CKDWORD VerticesPerDraw = 20;
constexpr CKDWORD IndicesPerDraw = 42;
constexpr CKDWORD PackedVertexStride = 48;
constexpr CKDWORD TransientBytesPerDraw =
    VerticesPerDraw * PackedVertexStride + IndicesPerDraw * sizeof(CKWORD);
constexpr CKDWORD TransientBytesPerFrame = DrawsPerFrame * TransientBytesPerDraw;
constexpr CKDWORD VertexFormat =
    CKRST_DP_TRANSFORM | CKRST_DP_LIGHT | CKRST_DP_DIFFUSE |
    CKRST_DP_SPECULAR | CKRST_DP_STAGES0;
constexpr CKDWORD TextureFlags =
    CKRST_TEXTURE_VALID | CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;

static_assert(TransientBytesPerFrame == 45936,
              "the transient workload must stay at 45,936 bytes per frame");

struct NoopObserver {
    void operator()(CKDWORD) const {}
};

struct MaterialProfile {
    CKMaterialRenderState RenderState;
    CKDWORD TextureOp;
    CKDWORD Address;
    CKDWORD Filter;
    CKBOOL AlphaTest;
    CKDWORD AlphaFunc;
    CKDWORD AlphaRef;
};

struct PublicResources {
    std::array<CKDWORD, 4> Textures{};
    CKDWORD VertexBuffer = 0;
    CKDWORD IndexBuffer = 0;
};

class Workload {
public:
    Workload()
    {
        for (CKDWORD i = 0; i < VerticesPerDraw; ++i) {
            const float x = static_cast<float>(i % 5) * 0.25f - 0.5f;
            const float y = static_cast<float>(i / 5) * 0.25f - 0.375f;
            Positions[i] = VxVector(x, y, static_cast<float>(i & 1) * 0.05f);
            Normals[i] = VxVector(0.0f, 0.0f, 1.0f);
            Texcoords2[i][0] = static_cast<float>(i % 5) * 0.25f;
            Texcoords2[i][1] = static_cast<float>(i / 5) / 3.0f;
            Texcoords3[i][0] = Texcoords2[i][0];
            Texcoords3[i][1] = Texcoords2[i][1];
            Texcoords3[i][2] = static_cast<float>((i * 3) % 7) * 0.125f;
            Diffuse[i] = 0xff000000u | (((i * 37u) & 0xffu) << 16) |
                         (((i * 67u) & 0xffu) << 8) |
                         ((i * 97u) & 0xffu);
            Specular[i] = 0x20000000u | (((i * 17u) & 0xffu) << 16) |
                          (((i * 29u) & 0xffu) << 8) |
                          ((i * 43u) & 0xffu);
        }
        for (CKDWORD triangle = 0; triangle < IndicesPerDraw / 3; ++triangle) {
            Indices[triangle * 3 + 0] = 0;
            Indices[triangle * 3 + 1] = static_cast<CKWORD>(1 + triangle);
            Indices[triangle * 3 + 2] = static_cast<CKWORD>(2 + triangle);
        }
        for (CKDWORD draw = 0; draw < DrawsPerFrame; ++draw) {
            Worlds[draw].SetIdentity();
            Worlds[draw][3][0] = static_cast<float>(draw % 11) * 0.125f;
            Worlds[draw][3][1] = static_cast<float>(draw / 11) * 0.125f;
        }
        InitProfiles();
        InitLight();
    }

    VxDrawPrimitiveData PrimitiveData(bool general) const
    {
        VxDrawPrimitiveData data = {};
        data.VertexCount = static_cast<int>(VerticesPerDraw);
        data.Flags = VertexFormat;
        data.PositionPtr = const_cast<VxVector *>(Positions.data());
        data.PositionStride = sizeof(VxVector);
        data.NormalPtr = const_cast<VxVector *>(Normals.data());
        data.NormalStride = sizeof(VxVector);
        data.ColorPtr = const_cast<CKDWORD *>(Diffuse.data());
        data.ColorStride = sizeof(CKDWORD);
        data.SpecularColorPtr = const_cast<CKDWORD *>(Specular.data());
        data.SpecularColorStride = sizeof(CKDWORD);
        data.TexCoordPtr = general
            ? static_cast<void *>(const_cast<float *>(&Texcoords3[0][0]))
            : static_cast<void *>(const_cast<float *>(&Texcoords2[0][0]));
        data.TexCoordStride = general ? sizeof(Texcoords3[0]) : sizeof(Texcoords2[0]);
        return data;
    }

    void ConfigureCore(CKFixedFunctionPipeline &pipeline,
                       const std::array<CKDWORD, 4> &textures) const
    {
        VxMatrix identity;
        identity.SetIdentity();
        pipeline.SetTransform(VXMATRIX_VIEW, identity);
        pipeline.SetTransform(VXMATRIX_PROJECTION, identity);
        pipeline.SetRenderState(VXRENDERSTATE_LIGHTING, TRUE);
        pipeline.SetLight(0, &Light);
        pipeline.EnableLight(0, TRUE);
        CKViewportData viewport = {};
        viewport.ViewWidth = 640;
        viewport.ViewHeight = 480;
        viewport.ViewZMax = 1.0f;
        pipeline.SetViewport(viewport);
        pipeline.SetTargetExtents(640, 480, 640, 480);
        ApplyCoreProfile(pipeline, textures, 0);
    }

    void ConfigurePublic(CKRasterizerContext &context,
                         const PublicResources &resources) const
    {
        VxMatrix identity;
        identity.SetIdentity();
        context.SetTransformMatrix(VXMATRIX_VIEW, identity);
        context.SetTransformMatrix(VXMATRIX_PROJECTION, identity);
        context.SetRenderState(VXRENDERSTATE_LIGHTING, TRUE);
        context.SetLight(0, &Light);
        context.EnableLight(0, TRUE);
        CKViewportData viewport = {};
        viewport.ViewWidth = 640;
        viewport.ViewHeight = 480;
        viewport.ViewZMax = 1.0f;
        context.SetViewport(&viewport);
        ApplyPublicProfile(context, resources.Textures, 0);
    }

    void ApplyCoreProfile(CKFixedFunctionPipeline &pipeline,
                          const std::array<CKDWORD, 4> &textures,
                          CKDWORD profileIndex) const
    {
        const MaterialProfile &profile = Profiles[profileIndex & 7u];
        pipeline.ApplyMaterial(profile.RenderState);
        pipeline.SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, profile.AlphaTest);
        pipeline.SetRenderState(VXRENDERSTATE_ALPHAFUNC, profile.AlphaFunc);
        pipeline.SetRenderState(VXRENDERSTATE_ALPHAREF, profile.AlphaRef);
        pipeline.SetTexture(0, textures[profileIndex & 3u], TextureFlags);
        ApplyCoreTextureState(pipeline, profile);
    }

    void ApplyPublicProfile(CKRasterizerContext &context,
                            const std::array<CKDWORD, 4> &textures,
                            CKDWORD profileIndex) const
    {
        const MaterialProfile &profile = Profiles[profileIndex & 7u];
        context.ApplyMaterial(profile.RenderState);
        context.SetRenderState(VXRENDERSTATE_ALPHATESTENABLE, profile.AlphaTest);
        context.SetRenderState(VXRENDERSTATE_ALPHAFUNC, profile.AlphaFunc);
        context.SetRenderState(VXRENDERSTATE_ALPHAREF, profile.AlphaRef);
        context.SetTexture(textures[profileIndex & 3u], 0);
        ApplyPublicTextureState(context, profile);
    }

    template <typename Observer = NoopObserver>
    bool RunCoreVertexBufferFrame(CKFixedFunctionPipeline &pipeline,
                                  const std::array<CKDWORD, 4> &textures,
                                  CKDWORD vertexBuffer,
                                  CKDWORD indexBuffer,
                                  CKDWORD vertexLayout,
                                  CKDWORD formatFlags,
                                  bool cycleMaterials,
                                  Observer observer = Observer()) const
    {
        for (CKDWORD draw = 0; draw < DrawsPerFrame; ++draw) {
            if (cycleMaterials)
                ApplyCoreProfile(pipeline, textures, draw);
            pipeline.SetTransform(VXMATRIX_WORLD, Worlds[draw]);
            if (!pipeline.DrawVertexBuffer(
                    VX_TRIANGLELIST, vertexBuffer, indexBuffer, 0,
                    VerticesPerDraw, 0, IndicesPerDraw,
                    VertexFormat, formatFlags, vertexLayout))
                return false;
            observer(draw);
        }
        return true;
    }

    template <typename Observer = NoopObserver>
    bool RunCoreTransientFrame(CKFixedFunctionPipeline &pipeline,
                               bool general,
                               Observer observer = Observer()) const
    {
        VxDrawPrimitiveData data = PrimitiveData(general);
        for (CKDWORD draw = 0; draw < DrawsPerFrame; ++draw) {
            pipeline.SetTransform(VXMATRIX_WORLD, Worlds[draw]);
            if (!pipeline.DrawPrimitive(
                    VX_TRIANGLELIST, const_cast<CKWORD *>(Indices.data()),
                    static_cast<int>(IndicesPerDraw), &data))
                return false;
            observer(draw);
        }
        return true;
    }

    template <typename Observer = NoopObserver>
    bool RunCoreTransientMaterialFrame(CKFixedFunctionPipeline &pipeline,
                                       bool general,
                                       const std::array<CKDWORD, 4> &textures,
                                       Observer observer = Observer()) const
    {
        VxDrawPrimitiveData data = PrimitiveData(general);
        for (CKDWORD draw = 0; draw < DrawsPerFrame; ++draw) {
            ApplyCoreProfile(pipeline, textures, draw);
            pipeline.SetTransform(VXMATRIX_WORLD, Worlds[draw]);
            if (!pipeline.DrawPrimitive(
                    VX_TRIANGLELIST, const_cast<CKWORD *>(Indices.data()),
                    static_cast<int>(IndicesPerDraw), &data))
                return false;
            observer(draw);
        }
        return true;
    }

    template <typename Observer = NoopObserver>
    bool RunTranslatedVertexBufferFrame(CKRasterizerContext &context,
                                        const PublicResources &resources,
                                        Observer observer = Observer()) const
    {
        if (!context.BeginScene())
            return false;
        for (CKDWORD draw = 0; draw < DrawsPerFrame; ++draw) {
            ApplyPublicProfile(context, resources.Textures, draw);
            if (!context.SetTransformMatrix(VXMATRIX_WORLD, Worlds[draw]) ||
                !context.DrawPrimitiveVBIB(
                    VX_TRIANGLELIST, resources.VertexBuffer,
                    resources.IndexBuffer, 0, VerticesPerDraw,
                    0, static_cast<int>(IndicesPerDraw)))
                return false;
            observer(draw);
        }
        return context.EndScene() && context.BackToFront(FALSE);
    }

    bool CreatePublicResources(CKRasterizerContext &context,
                               PublicResources &resources) const
    {
        CKTextureDesc textureDesc = {};
        VxPixelFormat2ImageDesc(_32_ARGB8888, textureDesc.Format);
        textureDesc.Format.Width = 4;
        textureDesc.Format.Height = 4;
        textureDesc.MipMapCount = 1;
        textureDesc.Flags = CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA;
        for (CKDWORD &texture : resources.Textures) {
            if (!context.CreateTexture(&textureDesc, &texture) || texture == 0)
                return false;
        }

        CKVertexBufferDesc vertexDesc = {};
        vertexDesc.m_VertexFormat = VertexFormat;
        vertexDesc.m_MaxVertexCount = VerticesPerDraw;
        vertexDesc.m_CurrentVCount = VerticesPerDraw;
        if (!context.CreateVertexBuffer(&vertexDesc, nullptr,
                                        &resources.VertexBuffer) ||
            resources.VertexBuffer == 0)
            return false;

        CKIndexBufferDesc indexDesc = {};
        indexDesc.m_MaxIndexCount = IndicesPerDraw;
        indexDesc.m_CurrentICount = IndicesPerDraw;
        return context.CreateIndexBuffer(&indexDesc, nullptr,
                                         &resources.IndexBuffer) &&
               resources.IndexBuffer != 0;
    }

    std::array<VxVector, VerticesPerDraw> Positions{};
    std::array<VxVector, VerticesPerDraw> Normals{};
    float Texcoords2[VerticesPerDraw][2]{};
    float Texcoords3[VerticesPerDraw][3]{};
    std::array<CKDWORD, VerticesPerDraw> Diffuse{};
    std::array<CKDWORD, VerticesPerDraw> Specular{};
    std::array<CKWORD, IndicesPerDraw> Indices{};
    std::array<VxMatrix, DrawsPerFrame> Worlds{};
    std::array<MaterialProfile, 8> Profiles{};
    CKLightData Light{};

private:
    void InitProfiles()
    {
        static const CKDWORD textureOps[8] = {
            CKRST_TOP_MODULATE, CKRST_TOP_ADD,
            CKRST_TOP_MODULATE2X, CKRST_TOP_SELECTARG1,
            CKRST_TOP_MODULATE, CKRST_TOP_ADD,
            CKRST_TOP_MODULATE, CKRST_TOP_MODULATE2X};
        static const CKDWORD addresses[8] = {
            VXTEXTURE_ADDRESSWRAP, VXTEXTURE_ADDRESSWRAP,
            VXTEXTURE_ADDRESSCLAMP, VXTEXTURE_ADDRESSMIRROR,
            VXTEXTURE_ADDRESSWRAP, VXTEXTURE_ADDRESSCLAMP,
            VXTEXTURE_ADDRESSWRAP, VXTEXTURE_ADDRESSMIRROR};
        for (CKDWORD i = 0; i < Profiles.size(); ++i) {
            MaterialProfile &profile = Profiles[i];
            std::memset(&profile.RenderState, 0, sizeof(profile.RenderState));
            const float r = 0.25f + static_cast<float>(i & 3u) * 0.15f;
            const float g = 0.30f + static_cast<float>((i + 1u) & 3u) * 0.12f;
            const float b = 0.35f + static_cast<float>((i + 2u) & 3u) * 0.10f;
            profile.RenderState.Material.Diffuse = VxColor(r, g, b, 0.75f);
            profile.RenderState.Material.Ambient = VxColor(0.2f, 0.2f, 0.2f, 1.0f);
            profile.RenderState.Material.Specular = VxColor(0.3f, 0.3f, 0.3f, 1.0f);
            profile.RenderState.Material.Emissive = VxColor(0.0f, 0.0f, 0.0f, 1.0f);
            profile.RenderState.Material.SpecularPower = 4.0f + static_cast<float>(i);
            profile.RenderState.CullMode = VXCULL_CCW;
            profile.RenderState.FillMode = VXFILL_SOLID;
            profile.RenderState.ShadeMode = VXSHADE_GOURAUD;
            profile.RenderState.AlphaBlend = (i == 4 || i == 5 || i == 7) ? TRUE : FALSE;
            profile.RenderState.SourceBlend = i == 5 ? VXBLEND_ONE : VXBLEND_SRCALPHA;
            profile.RenderState.DestBlend = i == 5 ? VXBLEND_ONE : VXBLEND_INVSRCALPHA;
            profile.RenderState.ZWrite = (i < 4 || i == 6) ? TRUE : FALSE;
            profile.RenderState.ZFunc = VXCMP_LESSEQUAL;
            profile.TextureOp = textureOps[i];
            profile.Address = addresses[i];
            profile.Filter = i == 2 ? VXTEXTUREFILTER_NEAREST : VXTEXTUREFILTER_LINEAR;
            profile.AlphaTest = (i == 6 || i == 7) ? TRUE : FALSE;
            profile.AlphaFunc = i == 7 ? VXCMP_GREATEREQUAL : VXCMP_GREATER;
            profile.AlphaRef = i == 7 ? 0x40u : 0x80u;
        }
    }

    void InitLight()
    {
        std::memset(&Light, 0, sizeof(Light));
        Light.Type = VX_LIGHTDIREC;
        Light.Diffuse = VxColor(0.9f, 0.85f, 0.8f, 1.0f);
        Light.Specular = VxColor(0.5f, 0.5f, 0.5f, 1.0f);
        Light.Ambient = VxColor(0.1f, 0.1f, 0.1f, 1.0f);
        Light.Direction = VxVector(0.0f, 0.0f, -1.0f);
        Light.Range = 1000.0f;
        Light.Attenuation0 = 1.0f;
    }

    static void ApplyCoreTextureState(CKFixedFunctionPipeline &pipeline,
                                      const MaterialProfile &profile)
    {
        pipeline.SetTextureStageState(0, CKRST_TSS_OP, profile.TextureOp);
        pipeline.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
        pipeline.SetTextureStageState(0, CKRST_TSS_ARG2, CKRST_TA_CURRENT);
        pipeline.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_MODULATE);
        pipeline.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);
        pipeline.SetTextureStageState(0, CKRST_TSS_AARG2, CKRST_TA_CURRENT);
        pipeline.SetTextureStageState(0, CKRST_TSS_ADDRESS, profile.Address);
        pipeline.SetTextureStageState(0, CKRST_TSS_MINFILTER, profile.Filter);
        pipeline.SetTextureStageState(0, CKRST_TSS_MAGFILTER, profile.Filter);
    }

    static void ApplyPublicTextureState(CKRasterizerContext &context,
                                        const MaterialProfile &profile)
    {
        context.SetTextureStageState(0, CKRST_TSS_OP, profile.TextureOp);
        context.SetTextureStageState(0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE);
        context.SetTextureStageState(0, CKRST_TSS_ARG2, CKRST_TA_CURRENT);
        context.SetTextureStageState(0, CKRST_TSS_AOP, CKRST_TOP_MODULATE);
        context.SetTextureStageState(0, CKRST_TSS_AARG1, CKRST_TA_TEXTURE);
        context.SetTextureStageState(0, CKRST_TSS_AARG2, CKRST_TA_CURRENT);
        context.SetTextureStageState(0, CKRST_TSS_ADDRESS, profile.Address);
        context.SetTextureStageState(0, CKRST_TSS_MINFILTER, profile.Filter);
        context.SetTextureStageState(0, CKRST_TSS_MAGFILTER, profile.Filter);
    }
};

} // namespace CKFFBenchmark

#endif // CKRE_FFP_BENCHMARK_WORKLOAD_H
