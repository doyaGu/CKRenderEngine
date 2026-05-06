#ifndef CKFFSHADERKEY_H
#define CKFFSHADERKEY_H

#include <cstdint>
#include <cstring>
#include <functional>

// Fixed-function state contract key.
// The renderer uses two uber-shader programs (3D and PositionT). This key is
// kept for diagnostics/tests and must not be treated as a shader variant cache.

// ============================================================================
// Vertex Shader Key - 64 bits
// ============================================================================

struct CKFFVertexKey {
    uint64_t bits;

    CKFFVertexKey() : bits(0) {}

    // --- Vertex format (bits 0-7) ---
    void SetHasPosition(bool v)  { SetBit(0, v); }
    void SetHasNormal(bool v)    { SetBit(1, v); }
    void SetHasColor0(bool v)    { SetBit(2, v); }
    void SetHasColor1(bool v)    { SetBit(3, v); }
    void SetHasTexCoord0(bool v) { SetBit(4, v); }
    void SetHasTexCoord1(bool v) { SetBit(5, v); }
    void SetHasTexCoord2(bool v) { SetBit(6, v); }
    void SetHasPositionT(bool v) { SetBit(7, v); }

    bool GetHasPosition() const  { return GetBit(0); }
    bool GetHasNormal() const    { return GetBit(1); }
    bool GetHasColor0() const    { return GetBit(2); }
    bool GetHasColor1() const    { return GetBit(3); }
    bool GetHasTexCoord0() const { return GetBit(4); }
    bool GetHasTexCoord1() const { return GetBit(5); }
    bool GetHasTexCoord2() const { return GetBit(6); }
    bool GetHasPositionT() const { return GetBit(7); }

    // --- Lighting config (bits 8-11) ---
    void SetLightingEnabled(bool v)    { SetBit(8, v); }
    void SetNormalizeNormals(bool v)   { SetBit(9, v); }
    void SetSpecularEnabled(bool v)    { SetBit(10, v); }
    void SetLocalViewer(bool v)        { SetBit(11, v); }

    bool GetLightingEnabled() const    { return GetBit(8); }
    bool GetNormalizeNormals() const   { return GetBit(9); }
    bool GetSpecularEnabled() const    { return GetBit(10); }
    bool GetLocalViewer() const        { return GetBit(11); }

    // --- Light count (bits 12-15, 0-8) ---
    void SetLightCount(uint32_t count) { SetField(12, 4, count); }
    uint32_t GetLightCount() const     { return GetField(12, 4); }

    // --- Fog config (bits 16-19) ---
    // FogMode: 0=NONE, 1=LINEAR, 2=EXP, 3=EXP2
    void SetFogMode(uint32_t mode) { SetField(16, 2, mode); }
    uint32_t GetFogMode() const    { return GetField(16, 2); }
    void SetRangeFog(bool v)       { SetBit(18, v); }
    void SetPixelFog(bool v)       { SetBit(19, v); }
    bool GetRangeFog() const       { return GetBit(18); }
    bool GetPixelFog() const       { return GetBit(19); }

    // --- Texture coordinate generation (bits 20-31, 3 stages x 4 bits) ---
    // TexGen mode per stage: 0=PASSTHRU, 1=CAMERANORMAL, 2=CAMERAPOSITION, 3=REFLECTION, 4=SPHEREMAP
    void SetTexGen(uint32_t stage, uint32_t mode, bool hasTransform) {
        if (stage >= 3) return;
        uint32_t offset = 20 + stage * 4;
        SetField(offset, 3, mode);
        SetBit(offset + 3, hasTransform);
    }
    uint32_t GetTexGenMode(uint32_t stage) const {
        if (stage >= 3) return 0;
        return GetField(20 + stage * 4, 3);
    }
    bool GetTexGenHasTransform(uint32_t stage) const {
        if (stage >= 3) return false;
        return GetBit(20 + stage * 4 + 3);
    }

    // --- Material source selection (bits 32-39) ---
    // Source: 0=MATERIAL, 1=COLOR0, 2=COLOR1
    void SetDiffuseSource(uint32_t src)  { SetField(32, 2, src); }
    void SetAmbientSource(uint32_t src)  { SetField(34, 2, src); }
    void SetSpecularSource(uint32_t src) { SetField(36, 2, src); }
    void SetEmissiveSource(uint32_t src) { SetField(38, 2, src); }

    uint32_t GetDiffuseSource() const  { return GetField(32, 2); }
    uint32_t GetAmbientSource() const  { return GetField(34, 2); }
    uint32_t GetSpecularSource() const { return GetField(36, 2); }
    uint32_t GetEmissiveSource() const { return GetField(38, 2); }

    bool operator==(const CKFFVertexKey &o) const { return bits == o.bits; }
    bool operator!=(const CKFFVertexKey &o) const { return bits != o.bits; }

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
// Fragment Shader Key - 64 bits
// ============================================================================

struct CKFFFragmentKey {
    uint64_t bits;

    CKFFFragmentKey() : bits(0) {}

    // --- Texture stage config (bits 0-47, 3 stages x 16 bits) ---
    // Per stage layout (16 bits):
    //   Bits 0-4:   ColorOp (5 bits, CKRST_TOP_* value, 0=DISABLE)
    //   Bits 5-7:   ColorArg1 (3 bits: TEXTURE=0, CURRENT=1, DIFFUSE=2, SPECULAR=3, TFACTOR=4)
    //   Bits 8-10:  ColorArg2 (3 bits)
    //   Bits 11-14: AlphaOp (4 bits)
    //   Bit 15:     ResultIsTemp

    void SetStageColorOp(uint32_t stage, uint32_t op) {
        if (stage >= 3) return;
        SetField(stage * 16, 5, op);
    }
    void SetStageColorArg1(uint32_t stage, uint32_t arg) {
        if (stage >= 3) return;
        SetField(stage * 16 + 5, 3, arg);
    }
    void SetStageColorArg2(uint32_t stage, uint32_t arg) {
        if (stage >= 3) return;
        SetField(stage * 16 + 8, 3, arg);
    }
    void SetStageAlphaOp(uint32_t stage, uint32_t op) {
        if (stage >= 3) return;
        SetField(stage * 16 + 11, 4, op);
    }
    void SetStageResultIsTemp(uint32_t stage, bool v) {
        if (stage >= 3) return;
        SetBit(stage * 16 + 15, v);
    }

    uint32_t GetStageColorOp(uint32_t stage) const {
        if (stage >= 3) return 0;
        return GetField(stage * 16, 5);
    }
    uint32_t GetStageColorArg1(uint32_t stage) const {
        if (stage >= 3) return 0;
        return GetField(stage * 16 + 5, 3);
    }
    uint32_t GetStageColorArg2(uint32_t stage) const {
        if (stage >= 3) return 0;
        return GetField(stage * 16 + 8, 3);
    }
    uint32_t GetStageAlphaOp(uint32_t stage) const {
        if (stage >= 3) return 0;
        return GetField(stage * 16 + 11, 4);
    }
    bool GetStageResultIsTemp(uint32_t stage) const {
        if (stage >= 3) return false;
        return GetBit(stage * 16 + 15);
    }

    // --- Global flags (bits 48-51) ---
    void SetSpecularAdd(bool v)     { SetBit(48, v); }
    void SetAlphaTestEnabled(bool v){ SetBit(49, v); }
    void SetFogEnabled(bool v)      { SetBit(50, v); }

    bool GetSpecularAdd() const      { return GetBit(48); }
    bool GetAlphaTestEnabled() const { return GetBit(49); }
    bool GetFogEnabled() const       { return GetBit(50); }

    // --- Alpha func (bits 52-55) ---
    void SetAlphaFunc(uint32_t func) { SetField(52, 4, func); }
    uint32_t GetAlphaFunc() const    { return GetField(52, 4); }

    bool operator==(const CKFFFragmentKey &o) const { return bits == o.bits; }
    bool operator!=(const CKFFFragmentKey &o) const { return bits != o.bits; }

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
// Combined Shader Key - 128 bits
// ============================================================================

struct CKFFShaderKey {
    CKFFVertexKey VS;
    CKFFFragmentKey FS;

    CKFFShaderKey() = default;

    bool operator==(const CKFFShaderKey &o) const {
        return VS == o.VS && FS == o.FS;
    }
    bool operator!=(const CKFFShaderKey &o) const {
        return !(*this == o);
    }
};

// Hash support for unordered_map
namespace std {
    template<> struct hash<CKFFShaderKey> {
        size_t operator()(const CKFFShaderKey &k) const {
            size_t h1 = hash<uint64_t>()(k.VS.bits);
            size_t h2 = hash<uint64_t>()(k.FS.bits);
            const size_t magic = sizeof(size_t) >= 8
                ? static_cast<size_t>(0x9e3779b97f4a7c15ULL)
                : static_cast<size_t>(0x9e3779b9U);
            return h1 ^ (h2 + magic + (h1 << 6) + (h1 >> 2));
        }
    };
}

// ============================================================================
// Texture stage argument encoding for fragment key
// ============================================================================

enum CKFFTexArg : uint32_t {
    CKFF_TA_TEXTURE  = 0,
    CKFF_TA_CURRENT  = 1,
    CKFF_TA_DIFFUSE  = 2,
    CKFF_TA_SPECULAR = 3,
    CKFF_TA_TFACTOR  = 4,
};

// Texture generation modes for vertex key
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

#endif // CKFFSHADERKEY_H
