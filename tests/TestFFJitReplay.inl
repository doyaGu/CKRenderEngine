// Included by test_rasterizer3_pixels.cpp after the public rasterizer helpers.
// These asset-free fixtures replay ordered state deltas across frames. The
// value model owns geometry, texture mip data and matrices; no GPU handles are
// persisted. Baseline state is SetDiffuseState(), with identity transforms.

struct FFReplayState {
    int Stage; // -1: render state, otherwise texture stage state.
    CKDWORD State, Value;
};
struct FFReplayFrame {
    std::vector<FFReplayState> Changes;
};
enum FFReplayEffect : unsigned {
    FF_REPLAY_KEY = 1, FF_REPLAY_UNIFORM = 2, FF_REPLAY_SAMPLER = 4,
    FF_REPLAY_PIPELINE = 8, FF_REPLAY_PIXELS = 16,
};
struct FFReplayCase {
    const char *Name;
    unsigned Tolerance = 1;
    bool PositionT = false;
    bool Normals = false;
    float Rhw[3] = {0.7f, 1.3f, 0.0f};
    // Expected changes relative to frame A. Uniform checks are opt-in: an
    // ignored state may still change an unused lane in a shared constant block.
    unsigned CheckedEffects = FF_REPLAY_KEY | FF_REPLAY_SAMPLER | FF_REPLAY_PIPELINE | FF_REPLAY_PIXELS;
    std::vector<unsigned> Effects;
    VxVector Positions[3] = {VxVector(-0.93f, -0.87f, 0.1f), VxVector(0.91f, -0.79f, 0.9f),
                             VxVector(-0.13f, 0.94f, 0.45f)};
    CKDWORD Colors[3] = {0x20ffffffu, 0xe0ffffffu, 0x80ffffffu};
    float UV[3][4] = {{-0.25f, -0.125f, 0, 1}, {1.25f, -0.125f, 0, 1}, {0.5f, 1.25f, 0, 1}};
    VxMatrix TextureMatrix;
    int Textures[2] = {-1, -1}; // Indices in the immutable resource list.
    std::vector<FFReplayFrame> Frames;
    explicit FFReplayCase(const char *name) : Name(name) { Vx3DMatrixIdentity(TextureMatrix); }
};
struct FFReplayTexture {
    VX_PIXELFORMAT Format;
    unsigned Width, Height;
    CKDWORD Flags;
    std::vector<std::vector<CKDWORD>> Mips;
};
struct FFReplayImage {
    Pixels Image;
    CKFFNativeFragmentKey Key;
    CKFFSamplerLayout Layout = CKFF_SAMPLER_LAYOUT_WIDE_2D;
    std::vector<CKBYTE> Uniforms;
    std::vector<CKDWORD> Samplers, Pipeline;
    uint64_t CompileQueued = 0, PipelineQueued = 0, SynchronousRequests = 0;
};
using FFReplayImages = std::vector<std::vector<FFReplayImage>>;

std::vector<FFReplayTexture> MakeFFReplayTextures()
{
    FFReplayTexture color = {_32_ARGB8888, 4, 4, CKRST_TEXTURE_RGB | CKRST_TEXTURE_ALPHA, {}};
    for (unsigned size = 4; size; size /= 2) {
        std::vector<CKDWORD> mip;
        for (unsigned y = 0; y < size; ++y)
            for (unsigned x = 0; x < size; ++x)
                mip.push_back((x + y) % 2 ? 0xff40c080u : 0xffe08040u);
        color.Mips.push_back(mip);
    }
    FFReplayTexture bump = {_32_X8L8V8U8, 4, 4, CKRST_TEXTURE_BUMPDUDV | CKRST_TEXTURE_BUMPLUMINANCE, {}};
    bump.Mips.push_back(std::vector<CKDWORD>(16));
    for (unsigned i = 0; i < 16; ++i)
        bump.Mips[0][i] = (i % 2 ? 0x00c010f0u : 0x0080f010u);
    FFReplayTexture levels = color;
    const CKDWORD mipColors[] = {0xffc04040u, 0xff40c040u, 0xff4040c0u};
    for (unsigned mip = 0; mip < levels.Mips.size(); ++mip)
        std::fill(levels.Mips[mip].begin(), levels.Mips[mip].end(), mipColors[mip]);
    return {color, bump, levels};
}

std::vector<FFReplayCase> MakeFFReplayCases()
{
    FFReplayCase alpha("alpha-threshold");
    alpha.Frames = {{{{-1, VXRENDERSTATE_ALPHATESTENABLE, TRUE}, {-1, VXRENDERSTATE_ALPHAFUNC, VXCMP_GREATER},
                       {-1, VXRENDERSTATE_ALPHAREF, 127}}},
                    {{{-1, VXRENDERSTATE_ALPHAREF, 128}}},
                    {{{-1, VXRENDERSTATE_ALPHAFUNC, VXCMP_LESS}}},
                    {{{-1, VXRENDERSTATE_ALPHAFUNC, VXCMP_GREATER}, {-1, VXRENDERSTATE_ALPHAREF, 127}}}};
    FFReplayCase saturation("stage-saturation");
    saturation.Colors[0] = 0xff306090u;
    saturation.Colors[1] = 0xffe0b070u;
    saturation.Colors[2] = 0xff7090c0u;
    saturation.Frames = {{{{0, CKRST_TSS_OP, CKRST_TOP_MODULATE4X}, {0, CKRST_TSS_ARG1, CKRST_TA_DIFFUSE},
                            {0, CKRST_TSS_ARG2, CKRST_TA_CONSTANT}, {0, CKRST_TSS_CONSTANT, 0xffbfbfbfu},
                            {1, CKRST_TSS_OP, CKRST_TOP_MODULATE}, {1, CKRST_TSS_ARG1, CKRST_TA_CURRENT},
                            {1, CKRST_TSS_ARG2, CKRST_TA_CONSTANT}, {1, CKRST_TSS_CONSTANT, 0xff404040u},
                            {1, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1}, {1, CKRST_TSS_AARG1, CKRST_TA_CURRENT}}},
                         {{{0, CKRST_TSS_CONSTANT, 0xff606060u}}}, {{{0, CKRST_TSS_CONSTANT, 0xffbfbfbfu}}}};
    FFReplayCase border("filter-border");
    border.Textures[0] = 0;
    border.Frames = {{{{0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE},
                       {0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_LINEAR}, {0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_LINEAR},
                       {0, CKRST_TSS_ADDRESS, VXTEXTURE_ADDRESSBORDER}, {0, CKRST_TSS_BORDERCOLOR, 0xff8040c0u}}},
                    {{{0, CKRST_TSS_BORDERCOLOR, 0xff40c080u}}}, {{{0, CKRST_TSS_BORDERCOLOR, 0xff8040c0u}}}};
    FFReplayCase projected = border;
    projected.Name = "projected-coordinates";
    projected.TextureMatrix[0][2] = 0.4f;
    projected.TextureMatrix[3][2] = 0.8f;
    projected.Frames[0].Changes.push_back({0, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_COUNT3 | CKRST_TTF_PROJECTED});
    projected.Frames[1].Changes = {{0, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_COUNT2}};
    projected.Frames[2].Changes = {{0, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_COUNT3 | CKRST_TTF_PROJECTED}};
    FFReplayCase fog("pixel-fog");
    fog.Frames = {{{{-1, VXRENDERSTATE_FOGENABLE, TRUE}, {-1, VXRENDERSTATE_FOGVERTEXMODE, VXFOG_NONE},
                    {-1, VXRENDERSTATE_FOGPIXELMODE, VXFOG_LINEAR}, {-1, VXRENDERSTATE_FOGSTART, FloatBits(0.15f)},
                    {-1, VXRENDERSTATE_FOGEND, FloatBits(0.75f)}, {-1, VXRENDERSTATE_FOGCOLOR, 0xff204080u}}},
                 {{{-1, VXRENDERSTATE_FOGEND, FloatBits(0.9f)}}}, {{{-1, VXRENDERSTATE_FOGEND, FloatBits(0.75f)}}}};
    FFReplayCase bump("bump-luminance");
    bump.Textures[0] = 1;
    bump.Textures[1] = 0;
    bump.Frames = {{{{0, CKRST_TSS_OP, CKRST_TOP_BUMPENVMAPLUMINANCE}, {0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE},
                     {0, CKRST_TSS_BUMPENVMAT00, FloatBits(0.5f)}, {0, CKRST_TSS_BUMPENVMAT11, FloatBits(0.5f)},
                     {0, CKRST_TSS_BUMPENVLSCALE, FloatBits(1.0f)}, {0, CKRST_TSS_BUMPENVLOFFSET, FloatBits(0.0f)},
                     {1, CKRST_TSS_OP, CKRST_TOP_SELECTARG1}, {1, CKRST_TSS_ARG1, CKRST_TA_TEXTURE},
                     {1, CKRST_TSS_AOP, CKRST_TOP_SELECTARG1}, {1, CKRST_TSS_AARG1, CKRST_TA_CURRENT},
                     {1, CKRST_TSS_TEXCOORDINDEX, 0}}},
                  {{{0, CKRST_TSS_BUMPENVLSCALE, FloatBits(0.5f)}}}, {{{0, CKRST_TSS_BUMPENVLSCALE, FloatBits(1.0f)}}}};
    const unsigned color = FF_REPLAY_PIXELS, uniform = FF_REPLAY_UNIFORM;
    alpha.Effects = {0, color | uniform, color | uniform | FF_REPLAY_KEY, 0};
    saturation.Effects = border.Effects = fog.Effects = bump.Effects = {0, color | uniform, 0};
    projected.Effects = {0, color | uniform | FF_REPLAY_KEY, 0};
    for (auto *fixture : {&alpha, &saturation, &border, &projected, &fog, &bump})
        fixture->CheckedEffects |= uniform;

    FFReplayCase factor("texture-factor");
    factor.Frames = {{{{0, CKRST_TSS_ARG1, CKRST_TA_TFACTOR}, {-1, VXRENDERSTATE_TEXTUREFACTOR, 0xff80c040u}}},
                     {{{-1, VXRENDERSTATE_TEXTUREFACTOR, 0xff4080c0u}}},
                     {{{-1, VXRENDERSTATE_TEXTUREFACTOR, 0xff80c040u}}}};
    factor.CheckedEffects |= uniform;
    factor.Effects = {0, color | uniform, 0};
    FFReplayCase filter("native-filter");
    filter.Textures[0] = 0;
    filter.Frames = {{{{0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE}}},
                     {{{0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_LINEAR}}},
                     {{{0, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_NEAREST}}}};
    filter.Effects = {0, color | FF_REPLAY_SAMPLER, 0};
    FFReplayCase blend("output-blend");
    blend.Frames = {{{{-1, VXRENDERSTATE_ALPHABLENDENABLE, TRUE}, {-1, VXRENDERSTATE_SRCBLEND, VXBLEND_ONE},
                      {-1, VXRENDERSTATE_DESTBLEND, VXBLEND_ZERO}}},
                    {{{-1, VXRENDERSTATE_SRCBLEND, VXBLEND_SRCALPHA}, {-1, VXRENDERSTATE_DESTBLEND, VXBLEND_INVSRCALPHA}}},
                    {{{-1, VXRENDERSTATE_SRCBLEND, VXBLEND_ONE}, {-1, VXRENDERSTATE_DESTBLEND, VXBLEND_ZERO}}}};
    blend.Effects = {0, color | FF_REPLAY_PIPELINE, 0};
    FFReplayCase depth("depth-function");
    depth.Frames = {{{{-1, VXRENDERSTATE_ZENABLE, TRUE}, {-1, VXRENDERSTATE_ZWRITEENABLE, TRUE},
                      {-1, VXRENDERSTATE_ZFUNC, VXCMP_LESS}}},
                    {{{-1, VXRENDERSTATE_ZFUNC, VXCMP_NEVER}}}, {{{-1, VXRENDERSTATE_ZFUNC, VXCMP_LESS}}}};
    depth.Effects = {0, color | FF_REPLAY_PIPELINE, 0};
    FFReplayCase ignoredAlpha("disabled-alpha");
    ignoredAlpha.Frames = {{{{-1, VXRENDERSTATE_ALPHAFUNC, VXCMP_GREATER}, {-1, VXRENDERSTATE_ALPHAREF, 64}}},
                           {{{-1, VXRENDERSTATE_ALPHAFUNC, VXCMP_LESS}, {-1, VXRENDERSTATE_ALPHAREF, 192}}},
                           {{{-1, VXRENDERSTATE_ALPHAFUNC, VXCMP_GREATER}, {-1, VXRENDERSTATE_ALPHAREF, 64}}}};
    ignoredAlpha.Effects = {0, 0, 0};
    FFReplayCase ignoredStage("inactive-stage");
    ignoredStage.Frames = {{{{7, CKRST_TSS_OP, CKRST_TOP_MODULATE}, {7, CKRST_TSS_CONSTANT, 0xff4080c0u}}},
                           {{{7, CKRST_TSS_OP, CKRST_TOP_ADD}, {7, CKRST_TSS_CONSTANT, 0xffc08040u},
                             {7, CKRST_TSS_ARG1, CKRST_TA_TEXTURE}, {7, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_LINEAR}}},
                           {{{7, CKRST_TSS_OP, CKRST_TOP_MODULATE}, {7, CKRST_TSS_CONSTANT, 0xff4080c0u},
                             {7, CKRST_TSS_ARG1, 0}, {7, CKRST_TSS_MINFILTER, 0}}}};
    ignoredStage.Effects = {0, 0, 0};
    FFReplayCase bias("nonzero-lod-bias");
    bias.Textures[0] = 2;
    bias.UV[0][0] = bias.UV[0][1] = 0;
    bias.UV[1][0] = 12; bias.UV[1][1] = 0;
    bias.UV[2][0] = 0; bias.UV[2][1] = 12;
    bias.Frames = {{{{0, CKRST_TSS_ARG1, CKRST_TA_TEXTURE}, {0, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_MIPNEAREST},
                     {0, CKRST_TSS_MIPMAPLODBIAS, FloatBits(1.0f)}}},
                   {{{0, CKRST_TSS_MIPMAPLODBIAS, FloatBits(2.0f)}}},
                   {{{0, CKRST_TSS_MIPMAPLODBIAS, FloatBits(1.0f)}}}};
    bias.CheckedEffects |= uniform;
    bias.Effects = {0, color | uniform | FF_REPLAY_SAMPLER, 0};
    FFReplayCase zeroBias = bias;
    zeroBias.Name = "zero-lod-bias";
    zeroBias.Frames[1].Changes[0].Value = FloatBits(0.0f);
    zeroBias.Effects[1] |= FF_REPLAY_KEY;
    FFReplayCase screenBorder = border;
    screenBorder.Name = "positiont-border";
    screenBorder.PositionT = true;
    FFReplayCase screenProject = screenBorder;
    screenProject.Name = "positiont-projection";
    screenProject.UV[0][3] = 0.7f;
    screenProject.UV[1][3] = 1.3f;
    screenProject.UV[2][3] = 0.9f;
    screenProject.Frames[0].Changes.push_back({0, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_COUNT4 | CKRST_TTF_PROJECTED});
    screenProject.Frames[1].Changes = {{0, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_COUNT2}};
    screenProject.Frames[2].Changes = {{0, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_COUNT4 | CKRST_TTF_PROJECTED}};
    screenProject.Effects = {0, color | uniform | FF_REPLAY_KEY, 0};
    FFReplayCase screenAffine = screenBorder;
    screenAffine.Name = "positiont-affine";
    screenAffine.Frames[0].Changes.push_back({-1, VXRENDERSTATE_TEXTUREPERSPECTIVE, TRUE});
    screenAffine.Frames[1].Changes = {{-1, VXRENDERSTATE_TEXTUREPERSPECTIVE, FALSE}};
    screenAffine.Frames[2].Changes = {{-1, VXRENDERSTATE_TEXTUREPERSPECTIVE, TRUE}};
    screenAffine.Effects = {0, color | uniform | FF_REPLAY_KEY, 0};
    FFReplayCase screenIndex = screenBorder;
    screenIndex.Name = "positiont-coordinate-index";
    screenIndex.Frames[0].Changes.push_back({0, CKRST_TSS_TEXCOORDINDEX, 0});
    screenIndex.Frames[1].Changes = {{0, CKRST_TSS_TEXCOORDINDEX, 1}};
    screenIndex.Frames[2].Changes = {{0, CKRST_TSS_TEXCOORDINDEX, 0}};
    screenIndex.Effects = {0, color | uniform, 0};
    FFReplayCase screenFog = fog;
    screenFog.Name = "positiont-pixel-fog";
    screenFog.PositionT = true;
    FFReplayCase vertexFog = fog;
    vertexFog.Name = "3d-vertex-fog";
    vertexFog.Frames[0].Changes.push_back({-1, VXRENDERSTATE_FOGDENSITY, FloatBits(0.0f)});
    vertexFog.Frames[0].Changes.push_back({-1, VXRENDERSTATE_RANGEFOGENABLE, FALSE});
    vertexFog.Frames[0].Changes.push_back({-1, VXRENDERSTATE_FOGPIXELMODE, VXFOG_NONE});
    vertexFog.Frames[0].Changes.push_back({-1, VXRENDERSTATE_FOGVERTEXMODE, VXFOG_LINEAR});
    vertexFog.Frames[1].Changes = {{-1, VXRENDERSTATE_FOGVERTEXMODE, VXFOG_EXP2},
                                  {-1, VXRENDERSTATE_FOGDENSITY, FloatBits(1.1f)},
                                  {-1, VXRENDERSTATE_RANGEFOGENABLE, TRUE}};
    vertexFog.Frames[2].Changes = {{-1, VXRENDERSTATE_FOGVERTEXMODE, VXFOG_LINEAR},
                                  {-1, VXRENDERSTATE_FOGDENSITY, FloatBits(0.0f)},
                                  {-1, VXRENDERSTATE_RANGEFOGENABLE, FALSE}};
    vertexFog.Effects = {0, color | uniform, 0};
    std::vector<FFReplayCase> result = {alpha, saturation, border, projected, fog, bump, factor, filter, blend, depth,
        ignoredAlpha, ignoredStage, bias, zeroBias, screenBorder, screenProject, screenAffine, screenIndex, screenFog, vertexFog};
    const char *names[] = {"3d-texgen-normal", "3d-texgen-position", "3d-texgen-reflection", "3d-texgen-sphere"};
    for (unsigned mode = 1; mode <= 4; ++mode) {
        FFReplayCase texgen = border;
        texgen.Name = names[mode - 1]; texgen.Normals = true;
        texgen.TextureMatrix[0][0] = 0.37f; texgen.TextureMatrix[1][1] = 0.41f;
        texgen.TextureMatrix[3][0] = 0.3f; texgen.TextureMatrix[3][1] = 0.2f;
        texgen.Frames[0].Changes.push_back({0, CKRST_TSS_TEXCOORDINDEX, mode << 16});
        texgen.Frames[0].Changes.push_back({0, CKRST_TSS_TEXTURETRANSFORMFLAGS, CKRST_TTF_COUNT2});
        texgen.Frames[0].Changes.push_back({-1, VXRENDERSTATE_NORMALIZENORMALS, TRUE});
        texgen.Frames[1].Changes = {{0, CKRST_TSS_TEXCOORDINDEX, 0}};
        texgen.Frames[2].Changes = {{0, CKRST_TSS_TEXCOORDINDEX, mode << 16}};
        texgen.Effects = {0, color | uniform, 0};
        result.push_back(texgen);
    }
    return result;
}

// Capture values, not revision numbers or object handles: independent contexts
// and newly allocated textures must produce the same dependency observations.
void CaptureFFReplayDependencies(const CKFFDraw &draw, FFReplayImage &image)
{
    for (CKDWORD block : {CKRST_BLOCK_DRAW_PARAMS, CKRST_BLOCK_BUMP_ENV, CKRST_BLOCK_STAGE_PARAMS}) {
        const auto &bytes = (*draw.Constants)[block].Bytes;
        image.Uniforms.insert(image.Uniforms.end(), bytes.Begin(), bytes.End());
    }
    for (CKDWORD slot = 0; slot < CKFF_TEXTURE_SLOT_COUNT; ++slot) {
        const auto &binding = draw.Textures->NativeBindings[slot];
        if (!binding.Texture) continue;
        const auto &s = binding.Sampler;
        // Border color is shader metadata. The other fields below form SDL's
        // sampler cache input for the ordinary color textures in these cases.
        const CKDWORD words[] = {slot, unsigned(s.MinFilter), unsigned(s.MagFilter), unsigned(s.MipFilter),
            unsigned(s.AddressU), unsigned(s.AddressV), unsigned(s.AddressW), unsigned(s.CompareFunc),
            s.MinMipLevel, s.MaxAnisotropy, FloatBits(s.MipLodBias)};
        image.Samplers.insert(image.Samplers.end(), std::begin(words), std::end(words));
        const CKDWORD metadata[] = {slot, s.BorderColor, binding.ShaderState, s.ShaderAnisotropy};
        const auto *bytes = reinterpret_cast<const CKBYTE *>(metadata);
        image.Uniforms.insert(image.Uniforms.end(), bytes, bytes + sizeof(metadata));
    }
    const auto &p = draw.Pipeline;
    image.Pipeline = {draw.VertexFormat, p.State.Lo, p.State.Mid, p.State.Hi,
                      p.StencilReadMask & 255u, p.StencilWriteMask & 255u, unsigned(p.DepthClipEnabled)};
    // Render-target formats, sample count and the secondary vertex stream are
    // fixed across these sequences. Dynamic stencil ref/scissor aren't PSO keys.
}

void RunFFReplay(CKSdlGpuRasterizerContext *ctx, const std::vector<FFReplayCase> &cases,
                  const std::vector<FFReplayTexture> &resources, FFReplayImages &images)
{
    std::vector<CKDWORD> handles;
    for (const auto &texture : resources) {
        CKTextureDesc desc;
        VxPixelFormat2ImageDesc(texture.Format, desc.Format);
        desc.Format.Width = texture.Width;
        desc.Format.Height = texture.Height;
        desc.Format.BytesPerLine = texture.Width * 4;
        desc.MipMapCount = unsigned(texture.Mips.size());
        desc.Flags = texture.Flags;
        CKDWORD handle = 0;
        TestCheck(ctx->CreateTexture(&desc, &handle), "create replay texture");
        handles.push_back(handle);
        VxImageDescEx image = desc.Format;
        for (unsigned mip = 0; mip < texture.Mips.size(); ++mip) {
            image.Image = reinterpret_cast<XBYTE *>(const_cast<CKDWORD *>(texture.Mips[mip].data()));
            TestCheck(ctx->LoadTexture(handle, image, mip, CKRST_CUBEFACE_XPOS, NULL), "upload replay mip");
            image.Width = std::max(1, image.Width / 2);
            image.Height = std::max(1, image.Height / 2);
            image.BytesPerLine = image.Width * 4;
        }
    }
    images.clear();
    for (const auto &fixture : cases) {
        SetDiffuseState(ctx);
        VxMatrix identity;
        Vx3DMatrixIdentity(identity);
        ctx->SetTransformMatrix(VXMATRIX_WORLD, identity);
        ctx->SetTransformMatrix(VXMATRIX_TEXTURE0, fixture.TextureMatrix);
        for (int stage = 0; stage < 2; ++stage) {
            ctx->SetTexture(fixture.Textures[stage] < 0 ? 0 : handles[fixture.Textures[stage]], stage);
            ctx->SetTextureStageState(stage, CKRST_TSS_MINFILTER, VXTEXTUREFILTER_NEAREST);
            ctx->SetTextureStageState(stage, CKRST_TSS_MAGFILTER, VXTEXTUREFILTER_NEAREST);
        }
        std::vector<FFReplayImage> frames;
        for (const auto &frame : fixture.Frames) {
            const auto before = ctx->GetFFJitStats();
            for (const auto &state : frame.Changes) {
                if (state.Stage < 0) ctx->SetRenderState((VXRENDERSTATETYPE)state.State, state.Value);
                else ctx->SetTextureStageState(state.Stage, (CKRST_TEXTURESTAGESTATETYPE)state.State, state.Value);
            }
            BeginFrame(ctx, CKRST_CTXCLEAR_COLOR | CKRST_CTXCLEAR_DEPTH);
            TestCheck(ctx->Clear(CKRST_CTXCLEAR_COLOR, 0x000d0713u, 1.0f, 0, 0, NULL), "replay sentinel clear");
            float uv[3][4];
            memcpy(uv, fixture.UV, sizeof(uv));
            if (fixture.PositionT) {
                float screen[3][4];
                for (int v = 0; v < 3; ++v) {
                    screen[v][0] = (fixture.Positions[v].x + 1.0f) * kWidth * 0.5f - 0.5f;
                    screen[v][1] = (1.0f - fixture.Positions[v].y) * kHeight * 0.5f - 0.5f;
                    screen[v][2] = fixture.Positions[v].z;
                    screen[v][3] = fixture.Rhw[v];
                }
                TestCheck(DrawTexturedPositionTTriangle(ctx, screen, fixture.Colors, uv), "POSITIONT replay draw");
            } else if (fixture.Normals) {
                VxVector normals[3] = {VxVector(0.3f, 0.2f, 0.9f), VxVector(-0.2f, 0.4f, 0.8f), VxVector(0.1f, -0.4f, 0.9f)};
                VxDrawPrimitiveData data = {};
                data.VertexCount = 3; data.Flags = CKRST_DP_TR_CL_VCT | CKRST_DP_LIGHT;
                data.PositionPtr = const_cast<VxVector *>(fixture.Positions); data.PositionStride = sizeof(VxVector);
                data.NormalPtr = normals; data.NormalStride = sizeof(VxVector);
                data.ColorPtr = const_cast<CKDWORD *>(fixture.Colors); data.ColorStride = sizeof(CKDWORD);
                data.TexCoordPtr = uv; data.TexCoordStride = sizeof(uv[0]);
                TestCheck(ctx->DrawPrimitive(VX_TRIANGLELIST, NULL, 0, &data), "3D texgen replay draw");
            } else {
                TestCheck(DrawTexturedTriangle(ctx, fixture.Positions, fixture.Colors, uv), "replay draw");
            }
            FFReplayImage result;
            const CKFFDraw &draw = ctx->GetFFPipelineForTests()->GetDraw();
            TestCheck(draw.ProgramContext && draw.Textures && draw.Constants, "replay exposes prepared inputs");
            CKSdlGpuFFFragmentArtifactKey artifact;
            TestCheck(CKSdlGpuBuildFFFragmentArtifactKey(draw.Textures->SamplerLayoutPlan,
                        draw.Textures->RequiresShaderSampling, artifact), "replay artifact");
            result.Layout = artifact.SamplerLayout;
            result.Key = CKFFNativeFragmentDrawKey(draw.ProgramContext->FragmentProgram, *draw.Constants,
                                                   artifact.UsesShaderSampling != FALSE, artifact.ComparisonResourceCount);
            CKFFCanonicalizeNativeFragmentKey(result.Key, result.Layout);
            CaptureFFReplayDependencies(draw, result);
            EndFrame(ctx);
            ReadBackbuffer(ctx, result.Image);
            const auto after = ctx->GetFFJitStats();
            result.CompileQueued = after.CompileQueued - before.CompileQueued;
            result.PipelineQueued = after.PipelineQueued - before.PipelineQueued;
            result.SynchronousRequests = after.SynchronousRequests - before.SynchronousRequests;
            frames.push_back(result);
        }
        images.push_back(frames);
    }
    SetDiffuseState(ctx);
    for (CKDWORD handle : handles) TestCheck(ctx->DeleteObject(handle, CKRST_OBJ_TEXTURE), "delete replay texture");
}

void SaveFFReplay(const FFReplayCase &fixture, const std::vector<FFReplayTexture> &resources,
                   const FFReplayImage &image, const char *name, unsigned frame)
{
    const char *directory = GetEnvValue("CKRE_FF_JIT_ARTIFACTS");
    if (!directory) directory = "ffjit-failures";
    SDL_CreateDirectory(directory);
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s-replay.txt", directory, name);
    FILE *file = fopen(path, "wb");
    if (!file) { printf("  could not save %s\n", path); return; }
    fprintf(file, "FFJit replay v2\ncase %s\nframe %u\nsize %d %d\nclear 000d0713\ntolerance %u\n",
            fixture.Name, frame, kWidth, kHeight, fixture.Tolerance);
    fprintf(file, "baseline SetDiffuseState; identity world/view/projection; nearest min/mag stages 0,1\n");
    fprintf(file, "positiont %u rhw %a %a %a; screen xy from NDC with half-pixel offset\n",
            unsigned(fixture.PositionT), fixture.Rhw[0], fixture.Rhw[1], fixture.Rhw[2]);
    for (int v = 0; v < 3; ++v)
        fprintf(file, "vertex %a %a %a %08x uv %a %a %a %a\n", fixture.Positions[v].x,
                fixture.Positions[v].y, fixture.Positions[v].z, unsigned(fixture.Colors[v]),
                fixture.UV[v][0], fixture.UV[v][1], fixture.UV[v][2], fixture.UV[v][3]);
    for (int row = 0; row < 4; ++row)
        fprintf(file, "texture-matrix %a %a %a %a\n", fixture.TextureMatrix[row][0], fixture.TextureMatrix[row][1],
                fixture.TextureMatrix[row][2], fixture.TextureMatrix[row][3]);
    fprintf(file, "texture-slots %d %d\n", fixture.Textures[0], fixture.Textures[1]);
    for (const auto &texture : resources) {
        fprintf(file, "texture %u %u %u flags %08x\n", unsigned(texture.Format), texture.Width, texture.Height, unsigned(texture.Flags));
        for (const auto &mip : texture.Mips) {
            fprintf(file, "mip");
            for (CKDWORD texel : mip) fprintf(file, " %08x", unsigned(texel));
            fprintf(file, "\n");
        }
    }
    for (unsigned i = 0; i < fixture.Frames.size(); ++i) {
        const auto &step = fixture.Frames[i];
        fprintf(file, "frame effects %x checked %x\n", fixture.Effects[i], fixture.CheckedEffects);
        for (const auto &state : step.Changes)
            fprintf(file, "state %d %u %08x\n", state.Stage, unsigned(state.State), unsigned(state.Value));
    }
    fprintf(file, "layout %u\nkey", unsigned(image.Layout));
    for (unsigned i = 0; i < CKFF_FRAGMENT_PROGRAM_LANE_COUNT; ++i)
        fprintf(file, " %08x", unsigned(image.Key.Program.Lanes()[i]));
    for (CKDWORD word : image.Key.Switches) fprintf(file, " %08x", unsigned(word));
    fprintf(file, "\nsamplers");
    for (CKDWORD word : image.Samplers) fprintf(file, " %08x", unsigned(word));
    fprintf(file, "\npipeline");
    for (CKDWORD word : image.Pipeline) fprintf(file, " %08x", unsigned(word));
    fprintf(file, "\nuniform-bytes");
    for (CKBYTE byte : image.Uniforms) fprintf(file, " %02x", unsigned(byte));
    CKJitFragmentShader ir;
    if (CKFFCompileNativeFragmentProgram(image.Key, image.Layout, ir))
        fprintf(file, "\n%s\n", CKJitDump(ir).CStr());
    fclose(file);
}

void CheckFFReplayImages(const char *mode, const std::vector<FFReplayCase> &cases,
                         const std::vector<FFReplayTexture> &resources,
                         const FFReplayImages &actual, const FFReplayImages &expected)
{
    const CKBYTE clear[4] = {0x13, 0x07, 0x0d, 0x00};
    const auto sameKey = [](const FFReplayImage &a, const FFReplayImage &b) {
        return a.Layout == b.Layout && a.Key == b.Key;
    };
    TestCheck(actual.size() == cases.size() && expected.size() == cases.size(), "replay case count");
    for (unsigned c = 0; c < cases.size(); ++c) {
        TestCheck(actual[c].size() == cases[c].Frames.size() && expected[c].size() == cases[c].Frames.size(), "replay frame count");
        TestCheck(cases[c].Effects.size() == cases[c].Frames.size(), "each replay frame declares its dependencies");
        for (unsigned f = 0; f < cases[c].Frames.size(); ++f) {
            char name[128];
            snprintf(name, sizeof(name), "%s-%s-%u", mode, cases[c].Name, f);
            const Pixels &a = actual[c][f].Image, &e = expected[c][f].Image;
            TestCheck(a.Width == kWidth && a.Height == kHeight && e.Width == kWidth && e.Height == kHeight &&
                          a.Data.Size() == kWidth * kHeight * 4 && e.Data.Size() == kWidth * kHeight * 4,
                      "replay has complete images");
            const auto diff = FFImageDiff::Compare(e.Data.Begin(), a.Data.Begin(), kWidth, kHeight, cases[c].Tolerance, clear);
            const auto &initial = actual[c][0], &current = actual[c][f];
            const auto change = FFImageDiff::Compare(initial.Image.Data.Begin(), a.Data.Begin(), kWidth, kHeight, 0, clear);
            const unsigned effects = (sameKey(initial, current) ? 0 : FF_REPLAY_KEY) |
                (initial.Uniforms == current.Uniforms ? 0 : FF_REPLAY_UNIFORM) |
                (initial.Samplers == current.Samplers ? 0 : FF_REPLAY_SAMPLER) |
                (initial.Pipeline == current.Pipeline ? 0 : FF_REPLAY_PIPELINE) |
                (change.Matches() ? 0 : FF_REPLAY_PIXELS);
            const bool dependencies = (effects & cases[c].CheckedEffects) == cases[c].Effects[f];
            if (!diff.Matches() || !dependencies || EnvFlagEnabled("CKRE_FF_JIT_SAVE_REPLAYS"))
                SaveFFReplay(cases[c], resources, actual[c][f], name, f);
            CheckMatchingImage(name, a, e, cases[c].Tolerance, clear);
            TestCheck(sameKey(actual[c][f], expected[c][f]), "reference and JIT resolve the same canonical key");
            TestCheckf(dependencies, "%s frame %u: dependency effects %x, expected %x (checked %x)",
                       cases[c].Name, f, effects, cases[c].Effects[f], cases[c].CheckedEffects);
            if (f != 0 && sameKey(actual[c][f - 1], current)) {
                TestCheck(current.CompileQueued == 0, "unchanged canonical key queues no compilation");
                if (actual[c][f - 1].Pipeline == current.Pipeline)
                    TestCheck(current.PipelineQueued == 0 && current.SynchronousRequests == 0,
                              "uniform, sampler and irrelevant changes reuse the pipeline");
            }
        }
        // A restored state must reproduce A exactly. Per-frame expectations
        // above distinguish observable changes from deliberately irrelevant ones.
        CheckMatchingImage(cases[c].Name, actual[c].back().Image, actual[c].front().Image, 0, clear);
    }
}

void CheckFFJitStatistics(const CKSdlGpuFFJitStats &stats)
{
    TestCheck(stats.PositionTReady + stats.UnlitReady <= stats.PipelineReady,
              "generated vertex selections are a subset of ready JIT selections");
    TestCheck(stats.Requests == stats.Specialized + stats.Unavailable + stats.Capacity + stats.Rejected + stats.QueueDeferred,
              "every program request has one resolution outcome");
    TestCheck(stats.PipelineSelections == stats.PipelineReady + stats.ShaderPending + stats.PipelinePending +
                  stats.PipelineDeferred + stats.PipelineFailed,
              "every specialized pipeline selection has one outcome");
    TestCheck(stats.CompileCompleted <= stats.CompileQueued && stats.CompileFailed <= stats.CompileCompleted &&
                  stats.PipelineCompleted <= stats.PipelineQueued && stats.PipelineBuildFailed <= stats.PipelineCompleted,
              "background completion counts are bounded by submissions");
    TestCheck(stats.PipelinePrewarmDeferred <= stats.PipelineQueueDeferred &&
                  stats.PipelineDeferred <= stats.PipelineQueueDeferred && stats.PipelinePendingPeak <= 128,
              "pipeline deferrals and outstanding jobs respect their bounds");
    TestCheck(stats.CompileMaxNs <= stats.CompileNs && stats.PipelineMaxNs <= stats.PipelineNs &&
                  stats.SynchronousMaxNs <= stats.SynchronousNs, "maximum duration is bounded by total duration");
}

void CheckFFReplay(Backend &backend, const std::vector<FFReplayCase> &cases,
                   const std::vector<FFReplayTexture> &resources, const FFReplayImages &reference)
{
    auto *ctx = static_cast<CKSdlGpuRasterizerContext *>(backend.Context);
    FFReplayImages images;
    RunFFReplay(ctx, cases, resources, images);
    CheckFFReplayImages("cold", cases, resources, images, reference);
    if (!ctx->IsFFJitEnabledForTests())
        return;
    TestCheck(ctx->FinishBackgroundWorkForTests(30000), "replay background work finishes");
    const auto before = ctx->GetFFJitStats();
    TestCheck(before.CompileCompleted == before.CompileQueued && before.PipelineCompleted == before.PipelineQueued,
              "drained worker publishes every compilation and pipeline completion");
    RunFFReplay(ctx, cases, resources, images);
    const auto after = ctx->GetFFJitStats();
    CKSdlGpuFFJitSnapshotV1 snapshot;
    TestCheck(CKSdlGpuQueryFFJitSnapshotV1(ctx, sizeof(snapshot), &snapshot) == 1,
              "live context exposes the optional JIT snapshot");
#define CKSDL_GPU_CHECK_SNAPSHOT(Name) TestCheck(snapshot.Name == after.Name, "snapshot counter " #Name);
    CKSDL_GPU_FF_JIT_SNAPSHOT_V1_FIELDS(CKSDL_GPU_CHECK_SNAPSHOT)
#undef CKSDL_GPU_CHECK_SNAPSHOT
    struct QueryFromWorker {
        const CKRasterizerContext *Context;
        static int Run(void *user) {
            auto *query = static_cast<QueryFromWorker *>(user);
            CKSdlGpuFFJitSnapshotV1 other;
            other.Requests = 123;
            return !CKSdlGpuQueryFFJitSnapshotV1(query->Context, sizeof(other), &other) && other.Requests == 123 ? 0 : 1;
        }
    } query = {ctx};
    SDL_Thread *queryThread = SDL_CreateThread(QueryFromWorker::Run, "JitSnapshotThreadTest", &query);
    int queryResult = -1;
    TestCheck(queryThread != nullptr, "snapshot thread test starts");
    if (queryThread) SDL_WaitThread(queryThread, &queryResult);
    TestCheck(queryResult == 0, "JIT snapshot refuses access from a worker thread");
    CheckFFReplayImages("warm", cases, resources, images, reference);
    CheckFFJitStatistics(after);
    TestCheck(after.CompileQueued == before.CompileQueued && after.PipelineQueued == before.PipelineQueued &&
                  after.SynchronousRequests == before.SynchronousRequests, "warm replay creates no shaders or pipelines");
    if (after.Specialized) {
        TestCheck(after.PipelineReady > before.PipelineReady && after.ShaderPending == before.ShaderPending &&
                      after.PipelinePending == before.PipelinePending && after.PipelineDeferred == before.PipelineDeferred &&
                      after.PipelineFailed == before.PipelineFailed,
                  "warm replay selects ready JIT pipelines without falling back");
        TestCheck(after.VertexCompileFailed == 0, "POSITIONT companions compile successfully");
        const bool hasPositionT = std::any_of(cases.begin(), cases.end(), [](const FFReplayCase &c) { return c.PositionT; });
        const char *vertexSetting = GetEnvValue("CKRE_SDL_GPU_FF_VERTEX_JIT");
        if (hasPositionT && (!vertexSetting || strcmp(vertexSetting, "0") != 0)) {
            const auto counts = ctx->CountFFJitProgramsForTests();
            TestCheck(after.VertexCompileCompleted > 0 && counts.PositionTPrograms > 0 && counts.PositionTPipelines > 0,
                      "POSITIONT replays execute generated vertex pipelines");
        }
        TestCheck(after.CompileFailed == 0 && after.PipelineBuildFailed == 0 && after.Rejected == 0 && after.Capacity == 0,
                  "replay has no rejected or capacity-limited programs");
    }
    printf("  FF replay: %u cases, full BGRA and coverage; cold/warm and A-B-A passed\n", unsigned(cases.size()));
    printf("  FF JIT CPU ms: compile total=%.3f max=%.3f; background pipeline total=%.3f max=%.3f; synchronous total=%.3f max=%.3f\n",
           after.CompileNs / 1e6, after.CompileMaxNs / 1e6, after.PipelineNs / 1e6, after.PipelineMaxNs / 1e6,
           after.SynchronousNs / 1e6, after.SynchronousMaxNs / 1e6);
    printf("  FF JIT selections: ready=%llu shader-pending=%llu pipeline-pending=%llu deferred=%llu failed=%llu; requests unavailable=%llu capacity=%llu rejected=%llu queue-deferred=%llu\n",
           (unsigned long long)after.PipelineReady, (unsigned long long)after.ShaderPending,
           (unsigned long long)after.PipelinePending, (unsigned long long)after.PipelineDeferred, (unsigned long long)after.PipelineFailed,
           (unsigned long long)after.Unavailable, (unsigned long long)after.Capacity,
           (unsigned long long)after.Rejected, (unsigned long long)after.QueueDeferred);
}
