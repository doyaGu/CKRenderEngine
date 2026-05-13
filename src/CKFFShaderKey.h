#ifndef CKFFSHADERKEY_H
#define CKFFSHADERKEY_H

#include "CKFFStateDesc.h"
#include "CKFFSpecializationInfo.h"
#include "CKRenderEngineTypes.h"

#include <cstddef>
#include <cstdint>

struct CKFFShaderKeyVS {
    uint64_t Bits;
    uint32_t VertexTexcoordDeclMask;
    uint8_t TexGen[CKFF_STATE_DESC_TEXTURE_STAGES];
    uint8_t TexCoordIndex[CKFF_STATE_DESC_TEXTURE_STAGES];
    uint16_t TexTransformFlags[CKFF_STATE_DESC_TEXTURE_STAGES];

    CKFFShaderKeyVS();
    explicit CKFFShaderKeyVS(const CKFFVSStateDesc &desc);

    bool GetHasPositionT() const { return (Bits & (1ull << 12)) != 0; }
    bool operator==(const CKFFShaderKeyVS &other) const;
    bool operator!=(const CKFFShaderKeyVS &other) const { return !(*this == other); }
};

struct CKFFShaderKeyFSStage {
    CKDWORD ColorOp;
    CKDWORD ColorArg0;
    CKDWORD ColorArg1;
    CKDWORD ColorArg2;
    CKDWORD AlphaOp;
    CKDWORD AlphaArg0;
    CKDWORD AlphaArg1;
    CKDWORD AlphaArg2;
    bool ResultIsTemp;
    bool ProjectedSampler;
};

struct CKFFShaderKeyFS {
    CKFFShaderKeyFSStage Stages[CKFF_STATE_DESC_TEXTURE_STAGES];
    CKDWORD LastActiveTextureStage;
    CKDWORD AlphaFunc;
    CKDWORD VertexFogMode;
    CKDWORD PixelFogMode;
    bool GlobalSpecularEnable;
    bool AlphaTestEnable;
    bool FogEnable;
    bool RangeFog;
    bool ClipEnable;
    bool FlatShade;

    CKFFShaderKeyFS();

    bool operator==(const CKFFShaderKeyFS &other) const;
    bool operator!=(const CKFFShaderKeyFS &other) const { return !(*this == other); }
};

struct CKFFShaderKey {
    CKFFShaderKeyVS VS;
    CKFFShaderKeyFS FS;

    bool operator==(const CKFFShaderKey &other) const {
        return VS == other.VS && FS == other.FS;
    }
    bool operator!=(const CKFFShaderKey &other) const { return !(*this == other); }
};

struct CKFFShaderKeyHash {
    std::size_t operator()(const CKFFShaderKey &key) const;
};

CKDWORD CKFFShaderKeyArgsMask(CKDWORD op);
bool CKFFShaderKeyArgUsesTexture(CKDWORD arg);
CKFFShaderKeyFS CKFFBuildShaderKeyFS(const CKFFFSStateDesc &desc, CKDWORD textureBoundMask);
CKFFShaderKey CKFFBuildShaderKey(const CKFFStateDesc &desc, CKDWORD textureBoundMask);
CKFFSpecializationInfo CKFFBuildSpecializationInfo(const CKFFShaderKeyFS &key);

#endif // CKFFSHADERKEY_H
