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
    bool BumpUnorm;
    bool HasTexture;
    vec4 Constant;
};

// Specialization data is uploaded per draw as u_ffSpec: each dword is split
// into four bytes carried as small integer floats (exact in fp32).
bool ckffSpecIsOptimized()
{
    return int(u_ffSpec[0].x) != 0;
}

uint ckffSpecDword(int index)
{
    vec4 b = u_ffSpec[index];
    return (uint(b.x) & uint(255)) |
           ((uint(b.y) & uint(255)) << uint(8)) |
           ((uint(b.z) & uint(255)) << uint(16)) |
           ((uint(b.w) & uint(255)) << uint(24));
}

int ckffSpecBits(uint word, int offset, int bits)
{
    uint mask = (uint(1) << uint(bits)) - uint(1);
    return int((word >> uint(offset)) & mask);
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

int ckffSpecMirrorOnceMask(int stage)
{
    if (stage < 4) return ckffSpecBits(ckffSpecDword(4), 19 + stage * 3, 3);
    return 0;
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
    params.MirrorOnceMask = int((uint(int(colorExtra.z)) >> uint(9)) & uint(7));
    params.SamplerType = ckffSpecSamplerType(stage);
    params.SamplerCompareFunc = ckffSpecSamplerCompareFunc(stage);
    params.BumpUnorm = (int(colorExtra.z) & 0x2000) != 0;
    params.HasTexture = colorParams.w > 0.5;
    params.Constant = vec4(colorExtra.w, alphaExtra.y, alphaExtra.z, alphaExtra.w);

    if (stage < 4 && ckffSpecIsOptimized()) {
        uint word = ckffSpecDword(6 + stage);
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
