struct CKFFStageParams
{
    int ColorOp;
    int ColorArg0;
    int ColorArg1;
    int ColorArg2;
    int AlphaOp;
    int AlphaArg0;
    int AlphaArg1;
    int AlphaArg2;
    int ResultArg;
    int TexcoordTransformFlags;
    int MirrorOnceMask;
    int SamplerType;
    int SamplerCompareFunc;
    int SamplerOrdinal;
    bool BumpUnorm;
    bool HasTexture;
    int StageBlend;
    vec4 Constant;
};

// Fragment program data: u_ffProgram carries CKFF_FRAGMENT_PROGRAM_LANE_COUNT lanes
// of 24 bits, one lane per float component. Integers below 2^24 are exact in
// fp32, so int() recovers the lane and the fields are sliced with shifts. The
// field positions come from ff_fragment_program_layout.sh, generated from
// CKFFFragmentProgramLayout.def together with the C++ side.
int ckffProgramLane(int lane)
{
    vec4 v = u_ffProgram[lane / 4];
    int component = lane - (lane / 4) * 4;
    if (component == 0) return int(v.x);
    if (component == 1) return int(v.y);
    if (component == 2) return int(v.z);
    return int(v.w);
}

int ckffProgramBits(int lane, int offset, int bits)
{
    return (ckffProgramLane(lane) >> offset) & ((1 << bits) - 1);
}

#include "ff_fragment_program_layout.sh"

int ckffUnpackProgramArg(int arg)
{
    return (arg & 0x7) | ((arg & 0x18) << 1);
}

// coordParams = u_stageParams[stage * 2 + 0]: x = packed texcoord index,
// y = texture transform flags (+ MIRRORONCE / render-target flip / bump bits),
// z = has texture. constant = u_stageParams[stage * 2 + 1].
CKFFStageParams ckffReadStageParams(int stage, vec4 coordParams, vec4 constant)
{
    CKFFStageParams params;
    params.ColorOp = ckffProgramStage_COLOR_OP(stage);
    params.ColorArg0 = ckffUnpackProgramArg(ckffProgramStage_COLOR_ARG0(stage));
    params.ColorArg1 = ckffUnpackProgramArg(ckffProgramStage_COLOR_ARG1(stage));
    params.ColorArg2 = ckffUnpackProgramArg(ckffProgramStage_COLOR_ARG2(stage));
    params.AlphaOp = ckffProgramStage_ALPHA_OP(stage);
    params.AlphaArg0 = ckffUnpackProgramArg(ckffProgramStage_ALPHA_ARG0(stage));
    params.AlphaArg1 = ckffUnpackProgramArg(ckffProgramStage_ALPHA_ARG1(stage));
    params.AlphaArg2 = ckffUnpackProgramArg(ckffProgramStage_ALPHA_ARG2(stage));
    params.ResultArg = ckffProgramStage_RESULT_IS_TEMP(stage) != 0 ? 5 : 1;

    int flags = int(coordParams.y);
    if (ckffProgramStage_PROJECTED(stage) != 0) {
        flags |= 0x100;
    } else {
        flags &= ~0x100;
    }
    params.TexcoordTransformFlags = flags;
    params.MirrorOnceMask = (flags >> 9) & 7;
    params.SamplerType = ckffProgramStage_SAMPLER_TYPE(stage);
    params.SamplerCompareFunc = ckffProgramStage_SAMPLER_COMPARE_FUNC(stage);
    params.SamplerOrdinal = (ckffProgram_SAMPLER_ORDINALS() >> (stage * 3)) & 7;
    params.BumpUnorm = (flags & 0x2000) != 0;
    params.HasTexture = coordParams.z > 0.5;
    params.StageBlend = int(coordParams.w + 0.5);
    params.Constant = constant;
    return params;
}
