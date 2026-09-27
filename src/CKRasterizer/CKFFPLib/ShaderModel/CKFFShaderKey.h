#ifndef CKFFSHADERKEY_H
#define CKFFSHADERKEY_H

#include "CKFFStateDesc.h"
#include "CKFFConstants.h"
#include "CKFFSpecializationInfo.h"
#include "CKRenderEngineTypes.h"

#include <stddef.h>
#include <stdint.h>

struct CKFFShaderKeyVS {
    uint64_t Bits;
    uint32_t VertexTexcoordDeclMask;
    uint8_t TexGen[CKFF_STATE_DESC_TEXTURE_STAGES];
    uint8_t TexCoordIndex[CKFF_STATE_DESC_TEXTURE_STAGES];
    uint16_t TexTransformFlags[CKFF_STATE_DESC_TEXTURE_STAGES];

    CKFFShaderKeyVS();
    explicit CKFFShaderKeyVS(const CKFFVSStateDesc &desc);

    bool GetHasPositionT() const { return (Bits & (1ull << 12)) != 0; }
    bool GetVertexClipping() const { return (Bits & (1ull << 34)) != 0; }
    bool GetPointSprite() const { return (Bits & (1ull << 40)) != 0; }
    bool GetPointOffset() const { return (Bits & (1ull << 41)) != 0; }
    bool GetPointOffsetWeight() const { return (Bits & (1ull << 42)) != 0; }
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
    bool HasTexture;
    bool ProjectedSampler;
    CKDWORD SamplerType;
    CKDWORD SamplerCompareFunc;
    CKDWORD MirrorOnceMask;
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
    bool FlatShade;
    bool DitherEnable;
    CKDWORD ColorTargetFormat;

    CKFFShaderKeyFS();

    bool operator==(const CKFFShaderKeyFS &other) const;
    bool operator!=(const CKFFShaderKeyFS &other) const { return !(*this == other); }
};

struct CKFFSamplerStageSlot {
    CKBYTE Ordinal;
    CKBYTE NativeSlot;

    CKFFSamplerStageSlot() : Ordinal(0), NativeSlot(0) {}
};

// Complete sampler placement for one fixed-function fragment program. The
// shader key is resolved once into this plan; texture binding, native program
// selection and fragment constants all consume the same result.
struct CKFFSamplerLayoutPlan {
    CKFFSamplerLayout Layout;
    CKBYTE CompareSamplerCount;
    CKFFSamplerStageSlot Stages[CKFF_MAX_TEXTURE_STAGES];

    CKFFSamplerLayoutPlan()
        : Layout(CKFF_SAMPLER_LAYOUT_WIDE_2D),
          CompareSamplerCount(0), Stages() {}
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
    size_t operator()(const CKFFShaderKey &key) const;
};

CKDWORD CKFFShaderKeyArgsMask(CKDWORD op);
bool CKFFShaderKeyArgUsesTexture(CKDWORD arg);
bool CKFFShaderKeyStageUsesTexture(const CKFFShaderKeyFSStage &stage,
                                   CKDWORD previousColorOp,
                                   CKDWORD previousAlphaOp);
CKFFShaderKeyFS CKFFBuildShaderKeyFS(const CKFFFSStateDesc &desc, CKDWORD textureBoundMask);
CKFFSamplerLayoutPlan CKFFBuildSamplerLayoutPlan(const CKFFShaderKeyFS &key);
CKFFShaderKey CKFFBuildShaderKey(const CKFFStateDesc &desc, CKDWORD textureBoundMask);
CKFFSpecializationInfo CKFFBuildSpecializationInfo(const CKFFShaderKeyFS &key);

#endif // CKFFSHADERKEY_H
