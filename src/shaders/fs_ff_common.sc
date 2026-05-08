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
    bool HasTexture;
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

CKFFStageParams ckffReadStageParams(int stage, vec4 colorParams, vec4 alphaParams, vec4 colorExtra, vec4 alphaExtra)
{
    CKFFStageParams params;
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
    params.HasTexture = colorParams.w > 0.5;

    if (stage < 4 && ckffSpecIsOptimized()) {
        int word = ckffSpecDword(6 + stage);
        params.ColorOp = ckffSpecBits(word, 0, 5);
        params.ColorArg1 = ckffUnpackSpecArg(ckffSpecBits(word, 5, 5));
        params.ColorArg2 = ckffUnpackSpecArg(ckffSpecBits(word, 10, 5));
        params.AlphaOp = ckffSpecBits(word, 15, 5);
        params.AlphaArg1 = ckffUnpackSpecArg(ckffSpecBits(word, 20, 5));
        params.AlphaArg2 = ckffUnpackSpecArg(ckffSpecBits(word, 25, 5));
        params.ResultArg = ckffSpecBits(word, 30, 1) != 0 ? 5 : 1;
    }

    return params;
}
