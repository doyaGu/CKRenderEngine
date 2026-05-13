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
    int SamplerType;
    int SamplerCompareFunc;
    bool HasTexture;
    vec4 Constant;
};

#if defined(CKFF_FULL_SPECIALIZED)
#ifndef CKFF_SPEC_DWORD0
#define CKFF_SPEC_DWORD0 0
#endif
#ifndef CKFF_SPEC_DWORD1
#define CKFF_SPEC_DWORD1 0
#endif
#ifndef CKFF_SPEC_DWORD2
#define CKFF_SPEC_DWORD2 0
#endif
#ifndef CKFF_SPEC_DWORD3
#define CKFF_SPEC_DWORD3 0
#endif
#ifndef CKFF_SPEC_DWORD4
#define CKFF_SPEC_DWORD4 0
#endif
#ifndef CKFF_SPEC_DWORD5
#define CKFF_SPEC_DWORD5 0
#endif
#ifndef CKFF_SPEC_DWORD6
#define CKFF_SPEC_DWORD6 0
#endif
#ifndef CKFF_SPEC_DWORD7
#define CKFF_SPEC_DWORD7 0
#endif
#ifndef CKFF_SPEC_DWORD8
#define CKFF_SPEC_DWORD8 0
#endif
#ifndef CKFF_SPEC_DWORD9
#define CKFF_SPEC_DWORD9 0
#endif
#ifndef CKFF_FS_STAGE0_HAS_TEXTURE
#define CKFF_FS_STAGE0_HAS_TEXTURE 0
#endif
#ifndef CKFF_FS_STAGE1_HAS_TEXTURE
#define CKFF_FS_STAGE1_HAS_TEXTURE 0
#endif
#ifndef CKFF_FS_STAGE2_HAS_TEXTURE
#define CKFF_FS_STAGE2_HAS_TEXTURE 0
#endif
#ifndef CKFF_FS_STAGE3_HAS_TEXTURE
#define CKFF_FS_STAGE3_HAS_TEXTURE 0
#endif
#endif

bool ckffSpecIsOptimized()
{
#if defined(CKFF_FULL_SPECIALIZED)
    return true;
#else
    return int(u_ffSpec[0].x) != 0;
#endif
}

int ckffSpecDword(int index)
{
#if defined(CKFF_FULL_SPECIALIZED)
    if (index == 0) return CKFF_SPEC_DWORD0;
    if (index == 1) return CKFF_SPEC_DWORD1;
    if (index == 2) return CKFF_SPEC_DWORD2;
    if (index == 3) return CKFF_SPEC_DWORD3;
    if (index == 4) return CKFF_SPEC_DWORD4;
    if (index == 5) return CKFF_SPEC_DWORD5;
    if (index == 6) return CKFF_SPEC_DWORD6;
    if (index == 7) return CKFF_SPEC_DWORD7;
    if (index == 8) return CKFF_SPEC_DWORD8;
    if (index == 9) return CKFF_SPEC_DWORD9;
    return 0;
#else
    vec4 b = u_ffSpec[index];
    return int(b.x) | (int(b.y) << 8) | (int(b.z) << 16) | (int(b.w) << 24);
#endif
}

int ckffSpecBits(int word, int offset, int bits)
{
    int mask = (1 << bits) - 1;
    return (word >> offset) & mask;
}

int ckffUnpackSpecArg(int arg)
{
    return (arg & 0x7) | ((arg & 0x18) << 1);
}

int ckffSpecLastActiveTextureStage()
{
    return ckffSpecBits(ckffSpecDword(4), 16, 3);
}

bool ckffSpecGlobalSpecularEnabled()
{
    return ckffSpecBits(ckffSpecDword(6), 31, 1) != 0;
}

int ckffSpecProjectedSamplerMask()
{
    return ckffSpecBits(ckffSpecDword(5), 0, 4);
}

bool ckffSpecStageHasTexture(int stage)
{
#if defined(CKFF_FULL_SPECIALIZED)
    if (stage == 0) return CKFF_FS_STAGE0_HAS_TEXTURE != 0;
    if (stage == 1) return CKFF_FS_STAGE1_HAS_TEXTURE != 0;
    if (stage == 2) return CKFF_FS_STAGE2_HAS_TEXTURE != 0;
    if (stage == 3) return CKFF_FS_STAGE3_HAS_TEXTURE != 0;
    return false;
#else
    return false;
#endif
}

bool ckffSpecAlphaTestEnabled()
{
    return ckffSpecBits(ckffSpecDword(5), 4, 1) != 0;
}

int ckffSpecAlphaFunc()
{
    return ckffSpecBits(ckffSpecDword(5), 5, 4);
}

bool ckffSpecFogEnabled()
{
    return ckffSpecBits(ckffSpecDword(5), 9, 1) != 0;
}

int ckffSpecVertexFogMode()
{
    return ckffSpecBits(ckffSpecDword(5), 10, 2);
}

int ckffSpecPixelFogMode()
{
    return ckffSpecBits(ckffSpecDword(5), 12, 2);
}

bool ckffSpecRangeFog()
{
    return ckffSpecBits(ckffSpecDword(5), 14, 1) != 0;
}

bool ckffSpecFlatShade()
{
    return ckffSpecBits(ckffSpecDword(5), 15, 1) != 0;
}

int ckffSpecSamplerType(int stage)
{
    return ckffSpecBits(ckffSpecDword(5), 16 + stage * 2, 2);
}

int ckffSpecSamplerCompareFunc(int stage)
{
    return ckffSpecBits(ckffSpecDword(3), stage * 4, 4);
}

CKFFStageParams ckffReadStageParams(int stage, vec4 colorParams, vec4 alphaParams, vec4 colorExtra, vec4 alphaExtra)
{
    CKFFStageParams params;
#if defined(CKFF_FULL_SPECIALIZED)
    params.ColorOp = 0;
    params.ColorArg0 = 0;
    params.ColorArg1 = 0;
    params.ColorArg2 = 0;
    params.AlphaOp = 0;
    params.AlphaArg0 = 0;
    params.AlphaArg1 = 0;
    params.AlphaArg2 = 0;
    params.ResultArg = 1;
    params.TexcoordTransformFlags = 0;
    params.SamplerType = ckffSpecSamplerType(stage);
    params.SamplerCompareFunc = ckffSpecSamplerCompareFunc(stage);
    params.HasTexture = ckffSpecStageHasTexture(stage);
    params.Constant = vec4(colorExtra.w, alphaExtra.y, alphaExtra.z, alphaExtra.w);
#else
    params.ColorOp = int(colorParams.x);
    params.ColorArg0 = int(colorExtra.x);
    params.ColorArg1 = int(colorParams.y);
    params.ColorArg2 = int(colorParams.z);
    params.AlphaOp = int(alphaParams.x);
    params.AlphaArg0 = int(alphaExtra.x);
    params.AlphaArg1 = int(alphaParams.y);
    params.AlphaArg2 = int(alphaParams.z);
    params.ResultArg = int(alphaParams.w);
    params.TexcoordTransformFlags = int(colorExtra.z);
    params.SamplerType = ckffSpecSamplerType(stage);
    params.SamplerCompareFunc = ckffSpecSamplerCompareFunc(stage);
    params.HasTexture = colorParams.w > 0.5;
    params.Constant = vec4(colorExtra.w, alphaExtra.y, alphaExtra.z, alphaExtra.w);
#endif

    if (stage < 4 && ckffSpecIsOptimized()) {
        int word = ckffSpecDword(6 + stage);
        params.ColorOp = ckffSpecBits(word, 0, 5);
        params.ColorArg0 = ckffUnpackSpecArg(ckffSpecBits(ckffSpecDword(1), stage * 5, 5));
        params.ColorArg1 = ckffUnpackSpecArg(ckffSpecBits(word, 5, 5));
        params.ColorArg2 = ckffUnpackSpecArg(ckffSpecBits(word, 10, 5));
        params.AlphaOp = ckffSpecBits(word, 15, 5);
        params.AlphaArg0 = ckffUnpackSpecArg(ckffSpecBits(ckffSpecDword(2), stage * 5, 5));
        params.AlphaArg1 = ckffUnpackSpecArg(ckffSpecBits(word, 20, 5));
        params.AlphaArg2 = ckffUnpackSpecArg(ckffSpecBits(word, 25, 5));
        params.ResultArg = ckffSpecBits(word, 30, 1) != 0 ? 5 : 1;
        if ((ckffSpecProjectedSamplerMask() & (1 << stage)) != 0) {
            params.TexcoordTransformFlags |= 0x100;
        } else {
            params.TexcoordTransformFlags &= ~0x100;
        }
    }

    return params;
}
