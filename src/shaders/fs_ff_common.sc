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

bool ckffSpecIsOptimized()
{
    return int(u_ffSpec[0].x) != 0;
}

int ckffSpecDword(int index)
{
    return int(u_ffSpec[index].x);
}

int ckffSpecBits(int word, int offset, int bits)
{
    int mask = (1 << bits) - 1;
    return (word >> offset) & mask;
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
        params.ColorArg1 = ckffSpecBits(word, 5, 5);
        params.ColorArg2 = ckffSpecBits(word, 10, 5);
        params.AlphaOp = ckffSpecBits(word, 15, 5);
        params.AlphaArg1 = ckffSpecBits(word, 20, 5);
        params.AlphaArg2 = ckffSpecBits(word, 25, 5);
        params.ResultArg = ckffSpecBits(word, 30, 1) != 0 ? 5 : 1;
    }

    return params;
}
