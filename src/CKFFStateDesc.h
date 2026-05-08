#ifndef CKFFSTATEDESC_H
#define CKFFSTATEDESC_H

#include <cstdint>

// Fixed-function state description.
// The renderer uses two uber-shader programs (3D and PositionT). This state
// description is kept for diagnostics/tests and must not be treated as a shader
// variant cache.

static const uint32_t CKFF_STATE_DESC_TEXTURE_STAGES = 8;

// ============================================================================
// Vertex State Description
// ============================================================================

struct CKFFVSStateDesc {
    uint64_t bits;
    uint8_t TexGen[CKFF_STATE_DESC_TEXTURE_STAGES];

    CKFFVSStateDesc() : bits(0), TexGen{} {}

    // --- Vertex format ---
    void SetHasPosition(bool v)  { SetBit(0, v); }
    void SetHasNormal(bool v)    { SetBit(1, v); }
    void SetHasColor0(bool v)    { SetBit(2, v); }
    void SetHasColor1(bool v)    { SetBit(3, v); }
    void SetHasTexCoord0(bool v) { SetBit(4, v); }
    void SetHasTexCoord1(bool v) { SetBit(5, v); }
    void SetHasTexCoord2(bool v) { SetBit(6, v); }
    void SetHasTexCoord3(bool v) { SetBit(7, v); }
    void SetHasTexCoord4(bool v) { SetBit(8, v); }
    void SetHasTexCoord5(bool v) { SetBit(9, v); }
    void SetHasTexCoord6(bool v) { SetBit(10, v); }
    void SetHasTexCoord7(bool v) { SetBit(11, v); }
    void SetHasPositionT(bool v) { SetBit(12, v); }
    void SetHasTexCoord(uint32_t stage, bool v) {
        if (stage >= CKFF_STATE_DESC_TEXTURE_STAGES)
            return;
        SetBit(4 + stage, v);
    }

    bool GetHasPosition() const  { return GetBit(0); }
    bool GetHasNormal() const    { return GetBit(1); }
    bool GetHasColor0() const    { return GetBit(2); }
    bool GetHasColor1() const    { return GetBit(3); }
    bool GetHasTexCoord0() const { return GetBit(4); }
    bool GetHasTexCoord1() const { return GetBit(5); }
    bool GetHasTexCoord2() const { return GetBit(6); }
    bool GetHasTexCoord3() const { return GetBit(7); }
    bool GetHasTexCoord4() const { return GetBit(8); }
    bool GetHasTexCoord5() const { return GetBit(9); }
    bool GetHasTexCoord6() const { return GetBit(10); }
    bool GetHasTexCoord7() const { return GetBit(11); }
    bool GetHasPositionT() const { return GetBit(12); }
    bool GetHasTexCoord(uint32_t stage) const {
        if (stage >= CKFF_STATE_DESC_TEXTURE_STAGES)
            return false;
        return GetBit(4 + stage);
    }

    // --- Lighting config (bits 13-16) ---
    void SetLightingEnabled(bool v)    { SetBit(13, v); }
    void SetNormalizeNormals(bool v)   { SetBit(14, v); }
    void SetSpecularEnabled(bool v)    { SetBit(15, v); }
    void SetLocalViewer(bool v)        { SetBit(16, v); }

    bool GetLightingEnabled() const    { return GetBit(13); }
    bool GetNormalizeNormals() const   { return GetBit(14); }
    bool GetSpecularEnabled() const    { return GetBit(15); }
    bool GetLocalViewer() const        { return GetBit(16); }

    // --- Light count (bits 17-20, 0-8) ---
    void SetLightCount(uint32_t count) { SetField(17, 4, count); }
    uint32_t GetLightCount() const     { return GetField(17, 4); }

    // --- Fog config (bits 21-24) ---
    // FogMode: 0=NONE, 1=LINEAR, 2=EXP, 3=EXP2
    void SetFogMode(uint32_t mode) { SetField(21, 2, mode); }
    uint32_t GetFogMode() const    { return GetField(21, 2); }
    void SetRangeFog(bool v)       { SetBit(23, v); }
    void SetPixelFog(bool v)       { SetBit(24, v); }
    bool GetRangeFog() const       { return GetBit(23); }
    bool GetPixelFog() const       { return GetBit(24); }

    // --- Texture coordinate generation (8 stages x 4 bits) ---
    // TexGen mode per stage: 0=PASSTHRU, 1=CAMERANORMAL, 2=CAMERAPOSITION, 3=REFLECTION, 4=SPHEREMAP
    void SetTexGen(uint32_t stage, uint32_t mode, bool hasTransform) {
        if (stage >= CKFF_STATE_DESC_TEXTURE_STAGES) return;
        TexGen[stage] = (uint8_t)((mode & 7u) | (hasTransform ? 8u : 0u));
    }
    uint32_t GetTexGenMode(uint32_t stage) const {
        if (stage >= CKFF_STATE_DESC_TEXTURE_STAGES) return 0;
        return TexGen[stage] & 7u;
    }
    bool GetTexGenHasTransform(uint32_t stage) const {
        if (stage >= CKFF_STATE_DESC_TEXTURE_STAGES) return false;
        return (TexGen[stage] & 8u) != 0;
    }

    // --- Material source selection (bits 25-32) ---
    // Source: 0=MATERIAL, 1=COLOR0, 2=COLOR1
    void SetDiffuseSource(uint32_t src)  { SetField(25, 2, src); }
    void SetAmbientSource(uint32_t src)  { SetField(27, 2, src); }
    void SetSpecularSource(uint32_t src) { SetField(29, 2, src); }
    void SetEmissiveSource(uint32_t src) { SetField(31, 2, src); }

    uint32_t GetDiffuseSource() const  { return GetField(25, 2); }
    uint32_t GetAmbientSource() const  { return GetField(27, 2); }
    uint32_t GetSpecularSource() const { return GetField(29, 2); }
    uint32_t GetEmissiveSource() const { return GetField(31, 2); }

    bool operator==(const CKFFVSStateDesc &o) const {
        if (bits != o.bits)
            return false;
        for (uint32_t stage = 0; stage < CKFF_STATE_DESC_TEXTURE_STAGES; ++stage) {
            if (TexGen[stage] != o.TexGen[stage])
                return false;
        }
        return true;
    }
    bool operator!=(const CKFFVSStateDesc &o) const { return !(*this == o); }

private:
    void SetBit(uint32_t pos, bool v) {
        if (v) bits |= (1ULL << pos);
        else   bits &= ~(1ULL << pos);
    }
    bool GetBit(uint32_t pos) const {
        return (bits >> pos) & 1;
    }
    void SetField(uint32_t pos, uint32_t width, uint32_t value) {
        uint64_t mask = ((1ULL << width) - 1) << pos;
        bits = (bits & ~mask) | (((uint64_t)value & ((1ULL << width) - 1)) << pos);
    }
    uint32_t GetField(uint32_t pos, uint32_t width) const {
        return (uint32_t)((bits >> pos) & ((1ULL << width) - 1));
    }
};

// ============================================================================
// Fragment State Description
// ============================================================================

struct CKFFFSStateDesc {
    struct Stage {
        uint32_t ColorOp = 0;
        uint32_t ColorArg1 = 0;
        uint32_t ColorArg2 = 0;
        uint32_t AlphaOp = 0;
        bool ResultIsTemp = false;

        bool operator==(const Stage &o) const {
            return ColorOp == o.ColorOp &&
                   ColorArg1 == o.ColorArg1 &&
                   ColorArg2 == o.ColorArg2 &&
                   AlphaOp == o.AlphaOp &&
                   ResultIsTemp == o.ResultIsTemp;
        }
        bool operator!=(const Stage &o) const { return !(*this == o); }
    };

    uint64_t bits;
    Stage Stages[CKFF_STATE_DESC_TEXTURE_STAGES];

    CKFFFSStateDesc() : bits(0), Stages{} {}

    void SetStageColorOp(uint32_t stage, uint32_t op) {
        if (stage >= CKFF_STATE_DESC_TEXTURE_STAGES) return;
        Stages[stage].ColorOp = op;
    }
    void SetStageColorArg1(uint32_t stage, uint32_t arg) {
        if (stage >= CKFF_STATE_DESC_TEXTURE_STAGES) return;
        Stages[stage].ColorArg1 = arg;
    }
    void SetStageColorArg2(uint32_t stage, uint32_t arg) {
        if (stage >= CKFF_STATE_DESC_TEXTURE_STAGES) return;
        Stages[stage].ColorArg2 = arg;
    }
    void SetStageAlphaOp(uint32_t stage, uint32_t op) {
        if (stage >= CKFF_STATE_DESC_TEXTURE_STAGES) return;
        Stages[stage].AlphaOp = op;
    }
    void SetStageResultIsTemp(uint32_t stage, bool v) {
        if (stage >= CKFF_STATE_DESC_TEXTURE_STAGES) return;
        Stages[stage].ResultIsTemp = v;
    }

    uint32_t GetStageColorOp(uint32_t stage) const {
        if (stage >= CKFF_STATE_DESC_TEXTURE_STAGES) return 0;
        return Stages[stage].ColorOp;
    }
    uint32_t GetStageColorArg1(uint32_t stage) const {
        if (stage >= CKFF_STATE_DESC_TEXTURE_STAGES) return 0;
        return Stages[stage].ColorArg1;
    }
    uint32_t GetStageColorArg2(uint32_t stage) const {
        if (stage >= CKFF_STATE_DESC_TEXTURE_STAGES) return 0;
        return Stages[stage].ColorArg2;
    }
    uint32_t GetStageAlphaOp(uint32_t stage) const {
        if (stage >= CKFF_STATE_DESC_TEXTURE_STAGES) return 0;
        return Stages[stage].AlphaOp;
    }
    bool GetStageResultIsTemp(uint32_t stage) const {
        if (stage >= CKFF_STATE_DESC_TEXTURE_STAGES) return false;
        return Stages[stage].ResultIsTemp;
    }

    // --- Global flags (bits 0-2) ---
    void SetSpecularAdd(bool v)     { SetBit(0, v); }
    void SetAlphaTestEnabled(bool v){ SetBit(1, v); }
    void SetFogEnabled(bool v)      { SetBit(2, v); }

    bool GetSpecularAdd() const      { return GetBit(0); }
    bool GetAlphaTestEnabled() const { return GetBit(1); }
    bool GetFogEnabled() const       { return GetBit(2); }

    // --- Alpha func (bits 3-6) ---
    void SetAlphaFunc(uint32_t func) { SetField(3, 4, func); }
    uint32_t GetAlphaFunc() const    { return GetField(3, 4); }

    bool operator==(const CKFFFSStateDesc &o) const {
        if (bits != o.bits)
            return false;
        for (uint32_t stage = 0; stage < CKFF_STATE_DESC_TEXTURE_STAGES; ++stage) {
            if (Stages[stage] != o.Stages[stage])
                return false;
        }
        return true;
    }
    bool operator!=(const CKFFFSStateDesc &o) const { return !(*this == o); }

private:
    void SetBit(uint32_t pos, bool v) {
        if (v) bits |= (1ULL << pos);
        else   bits &= ~(1ULL << pos);
    }
    bool GetBit(uint32_t pos) const {
        return (bits >> pos) & 1;
    }
    void SetField(uint32_t pos, uint32_t width, uint32_t value) {
        uint64_t mask = ((1ULL << width) - 1) << pos;
        bits = (bits & ~mask) | (((uint64_t)value & ((1ULL << width) - 1)) << pos);
    }
    uint32_t GetField(uint32_t pos, uint32_t width) const {
        return (uint32_t)((bits >> pos) & ((1ULL << width) - 1));
    }
};

// ============================================================================
// Combined State Description
// ============================================================================

struct CKFFStateDesc {
    CKFFVSStateDesc VS;
    CKFFFSStateDesc FS;

    CKFFStateDesc() = default;

    bool operator==(const CKFFStateDesc &o) const {
        return VS == o.VS && FS == o.FS;
    }
    bool operator!=(const CKFFStateDesc &o) const {
        return !(*this == o);
    }
};

// ============================================================================
// Texture stage argument encoding for fragment state description
// ============================================================================

enum CKFFTexArg : uint32_t {
    CKFF_TA_DIFFUSE  = 0,
    CKFF_TA_CURRENT  = 1,
    CKFF_TA_TEXTURE  = 2,
    CKFF_TA_TFACTOR  = 3,
    CKFF_TA_SPECULAR = 4,
    CKFF_TA_TEMP     = 5,
};

// Texture generation modes for vertex state description
enum CKFFTexGen : uint32_t {
    CKFF_TG_PASSTHRU        = 0,
    CKFF_TG_CAMERANORMAL    = 1,
    CKFF_TG_CAMERAPOSITION  = 2,
    CKFF_TG_REFLECTION      = 3,
    CKFF_TG_SPHEREMAP       = 4,
};

// Material source modes
enum CKFFMaterialSource : uint32_t {
    CKFF_MS_MATERIAL = 0,
    CKFF_MS_COLOR0   = 1,
    CKFF_MS_COLOR1   = 2,
};

#endif // CKFFSTATEDESC_H
