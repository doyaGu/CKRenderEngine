struct CKFFTextureStageProgram
{
    int ColorOp;
    int ColorArg0;
    int ColorArg1;
    int ColorArg2;
    int AlphaOp;
    int AlphaArg0;
    int AlphaArg1;
    int AlphaArg2;
    bool ResultIsTemp;
    int SamplerType;
    bool Projected;
    int SamplerCompareFunc;
    int SamplerOrdinal;
};

struct CKFFGlobalFragmentProgram
{
    int LastActiveTextureStage;
    bool AlphaTestEnabled;
    int AlphaFunc;
    bool FogEnabled;
    int VertexFogMode;
    int PixelFogMode;
    bool RangeFog;
    bool FlatShade;
    bool GlobalSpecularEnabled;
    int SamplerOrdinals;
};

struct CKFFStageParams
{
    int TexcoordTransformFlags;
    int MirrorOnceMask;
    bool BumpUnorm;
    bool HasTexture;
    int StageBlend;
    vec4 Constant;
};

// Fragment program data: u_ffProgram carries CKFF_FRAGMENT_PROGRAM_LANE_COUNT
// lanes of 24 bits, one lane per float component. Integers below 2^24 are
// exact in fp32. Field shifts and masks come from CKFFFragmentProgramLayout.def
// through this generated include.
#include "ff_fragment_program_layout.sh"

int ckffUnpackProgramArg(int arg)
{
    return (arg & 0x7) | ((arg & 0x18) << 1);
}

CKFFGlobalFragmentProgram ckffDecodeGlobalFragmentProgram()
{
    CKFFGlobalFragmentProgram program;
    vec4 words = u_ffProgram[4];
    int globalWord = int(words.x);
    program.LastActiveTextureStage =
        (globalWord >> CKFF_FRAGMENT_PROGRAM_LAST_ACTIVE_TEXTURE_STAGE_SHIFT) &
        CKFF_FRAGMENT_PROGRAM_LAST_ACTIVE_TEXTURE_STAGE_MASK;
    program.AlphaTestEnabled =
        ((globalWord >> CKFF_FRAGMENT_PROGRAM_ALPHA_TEST_ENABLED_SHIFT) &
         CKFF_FRAGMENT_PROGRAM_ALPHA_TEST_ENABLED_MASK) != 0;
    program.AlphaFunc =
        (globalWord >> CKFF_FRAGMENT_PROGRAM_ALPHA_FUNC_SHIFT) &
        CKFF_FRAGMENT_PROGRAM_ALPHA_FUNC_MASK;
    program.FogEnabled =
        ((globalWord >> CKFF_FRAGMENT_PROGRAM_FOG_ENABLED_SHIFT) &
         CKFF_FRAGMENT_PROGRAM_FOG_ENABLED_MASK) != 0;
    program.VertexFogMode =
        (globalWord >> CKFF_FRAGMENT_PROGRAM_VERTEX_FOG_MODE_SHIFT) &
        CKFF_FRAGMENT_PROGRAM_VERTEX_FOG_MODE_MASK;
    program.PixelFogMode =
        (globalWord >> CKFF_FRAGMENT_PROGRAM_PIXEL_FOG_MODE_SHIFT) &
        CKFF_FRAGMENT_PROGRAM_PIXEL_FOG_MODE_MASK;
    program.RangeFog =
        ((globalWord >> CKFF_FRAGMENT_PROGRAM_RANGE_FOG_SHIFT) &
         CKFF_FRAGMENT_PROGRAM_RANGE_FOG_MASK) != 0;
    program.FlatShade =
        ((globalWord >> CKFF_FRAGMENT_PROGRAM_FLAT_SHADE_SHIFT) &
         CKFF_FRAGMENT_PROGRAM_FLAT_SHADE_MASK) != 0;
    program.GlobalSpecularEnabled =
        ((globalWord >> CKFF_FRAGMENT_PROGRAM_GLOBAL_SPECULAR_ENABLED_SHIFT) &
         CKFF_FRAGMENT_PROGRAM_GLOBAL_SPECULAR_ENABLED_MASK) != 0;
    program.SamplerOrdinals = int(words.y);
    return program;
}

CKFFTextureStageProgram ckffDecodeTextureStageProgram(
    int stage, int samplerOrdinals)
{
    CKFFTextureStageProgram program;
    vec4 packedWords = u_ffProgram[stage / 2];
    int colorWord = int(packedWords.x);
    int alphaWord = int(packedWords.y);
    if ((stage & 1) != 0) {
        colorWord = int(packedWords.z);
        alphaWord = int(packedWords.w);
    }

    program.ColorOp =
        (colorWord >> CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_OP_SHIFT) &
        CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_OP_MASK;
    program.ColorArg0 = ckffUnpackProgramArg(
        (colorWord >> CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_ARG0_SHIFT) &
        CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_ARG0_MASK);
    program.ColorArg1 = ckffUnpackProgramArg(
        (colorWord >> CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_ARG1_SHIFT) &
        CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_ARG1_MASK);
    program.ColorArg2 = ckffUnpackProgramArg(
        (colorWord >> CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_ARG2_SHIFT) &
        CKFF_FRAGMENT_PROGRAM_STAGE_COLOR_ARG2_MASK);
    program.ResultIsTemp =
        ((colorWord >> CKFF_FRAGMENT_PROGRAM_STAGE_RESULT_IS_TEMP_SHIFT) &
         CKFF_FRAGMENT_PROGRAM_STAGE_RESULT_IS_TEMP_MASK) != 0;
    program.SamplerType =
        (colorWord >> CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE_SHIFT) &
        CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_TYPE_MASK;
    program.Projected =
        ((colorWord >> CKFF_FRAGMENT_PROGRAM_STAGE_PROJECTED_SHIFT) &
         CKFF_FRAGMENT_PROGRAM_STAGE_PROJECTED_MASK) != 0;

    program.AlphaOp =
        (alphaWord >> CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_OP_SHIFT) &
        CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_OP_MASK;
    program.AlphaArg0 = ckffUnpackProgramArg(
        (alphaWord >> CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_ARG0_SHIFT) &
        CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_ARG0_MASK);
    program.AlphaArg1 = ckffUnpackProgramArg(
        (alphaWord >> CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_ARG1_SHIFT) &
        CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_ARG1_MASK);
    program.AlphaArg2 = ckffUnpackProgramArg(
        (alphaWord >> CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_ARG2_SHIFT) &
        CKFF_FRAGMENT_PROGRAM_STAGE_ALPHA_ARG2_MASK);
    program.SamplerCompareFunc =
        (alphaWord >> CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC_SHIFT) &
        CKFF_FRAGMENT_PROGRAM_STAGE_SAMPLER_COMPARE_FUNC_MASK;
    program.SamplerOrdinal = (samplerOrdinals >> (stage * 3)) & 7;
    return program;
}

// coordParams = u_stageParams[stage * 2 + 0]: x = packed texcoord index,
// y = texture transform flags (+ MIRRORONCE / render-target flip / bump bits),
// z = has texture. constant = u_stageParams[stage * 2 + 1].
CKFFStageParams ckffReadStageParams(bool projected, vec4 coordParams,
                                    vec4 constant)
{
    CKFFStageParams params;
    int flags = int(coordParams.y);
    if (projected) {
        flags |= 0x100;
    } else {
        flags &= ~0x100;
    }
    params.TexcoordTransformFlags = flags;
    params.MirrorOnceMask = (flags >> 9) & 7;
    params.BumpUnorm = (flags & 0x2000) != 0;
    params.HasTexture = coordParams.z > 0.5;
    params.StageBlend = int(coordParams.w + 0.5);
    params.Constant = constant;
    return params;
}
