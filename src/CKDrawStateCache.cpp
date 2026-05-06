#include "CKDrawStateCache.h"
#include <cstdlib>

static bool EnvEnabled(const char *name) {
    const char *value = std::getenv(name);
    if (!value || !*value)
        return false;
    return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
}

static CKDWORD RemapCullMode(CKDWORD mode) {
    if (mode == VXCULL_NONE)
        return 0;
    if (mode == VXCULL_CW)
        return 1;
    if (mode == VXCULL_CCW)
        return 2;
    return 2;
}

static CKDWORD RemapFillMode(CKDWORD mode) {
    if (mode == VXFILL_WIREFRAME)
        return 1;
    if (mode == VXFILL_POINT)
        return 2;
    return 0;
}

static void FixupBlendPair(CKDWORD &src, CKDWORD &dst) {
    if (src == VXBLEND_BOTHSRCALPHA) {
        src = VXBLEND_SRCALPHA;
        dst = VXBLEND_INVSRCALPHA;
    } else if (src == VXBLEND_BOTHINVSRCALPHA) {
        src = VXBLEND_INVSRCALPHA;
        dst = VXBLEND_SRCALPHA;
    }
}

CKDrawStateCache::CKDrawStateCache() : m_DirtyMask(0xFFFFFFFF), m_LastTopology(VX_TRIANGLELIST) {
    m_CachedState = {0, 0, 0};
    SetDefaults();
}

void CKDrawStateCache::SetDefaults() {
    memset(m_States, 0, sizeof(m_States));
    m_States[VXRENDERSTATE_ZENABLE] = TRUE;
    m_States[VXRENDERSTATE_ZWRITEENABLE] = TRUE;
    m_States[VXRENDERSTATE_ZFUNC] = VXCMP_LESSEQUAL;
    m_States[VXRENDERSTATE_CULLMODE] = VXCULL_CCW;
    m_States[VXRENDERSTATE_FILLMODE] = VXFILL_SOLID;
    m_States[VXRENDERSTATE_SHADEMODE] = VXSHADE_GOURAUD;
    m_States[VXRENDERSTATE_ALPHABLENDENABLE] = FALSE;
    m_States[VXRENDERSTATE_SRCBLEND] = VXBLEND_ONE;
    m_States[VXRENDERSTATE_DESTBLEND] = VXBLEND_ZERO;
    m_States[VXRENDERSTATE_ALPHATESTENABLE] = FALSE;
    m_States[VXRENDERSTATE_ALPHAFUNC] = VXCMP_ALWAYS;
    m_States[VXRENDERSTATE_ALPHAREF] = 0;
    m_States[VXRENDERSTATE_LIGHTING] = TRUE;
    m_States[VXRENDERSTATE_COLORVERTEX] = TRUE;
    m_States[VXRENDERSTATE_LOCALVIEWER] = TRUE;
    m_States[VXRENDERSTATE_DIFFUSEFROMVERTEX] = TRUE;
    m_States[VXRENDERSTATE_SPECULARFROMVERTEX] = TRUE;
    m_States[VXRENDERSTATE_AMBIENTFROMVERTEX] = FALSE;
    m_States[VXRENDERSTATE_EMISSIVEFROMVERTEX] = FALSE;
    m_States[VXRENDERSTATE_SPECULARENABLE] = FALSE;
    m_States[VXRENDERSTATE_NORMALIZENORMALS] = TRUE;
    m_States[VXRENDERSTATE_FOGENABLE] = FALSE;
    m_States[VXRENDERSTATE_DITHERENABLE] = FALSE;
    m_States[VXRENDERSTATE_TEXTUREPERSPECTIVE] = TRUE;
    m_DirtyMask = 0xFFFFFFFF;
}

void CKDrawStateCache::Reset() {
    SetDefaults();
}

void CKDrawStateCache::SetRenderState(VXRENDERSTATETYPE state, CKDWORD value) {
    if ((CKDWORD)state >= CKFF_RS_COUNT)
        return;

    if (m_States[state] == value)
        return;

    m_States[state] = value;

    switch (state) {
    case VXRENDERSTATE_ALPHABLENDENABLE:
    case VXRENDERSTATE_SRCBLEND:
    case VXRENDERSTATE_DESTBLEND:
        m_DirtyMask |= CKFF_DIRTY_BLEND;
        break;
    case VXRENDERSTATE_ZENABLE:
    case VXRENDERSTATE_ZWRITEENABLE:
    case VXRENDERSTATE_ZFUNC:
        m_DirtyMask |= CKFF_DIRTY_DEPTH;
        break;
    case VXRENDERSTATE_CULLMODE:
    case VXRENDERSTATE_FILLMODE:
        m_DirtyMask |= CKFF_DIRTY_RASTER;
        break;
    default:
        break;
    }
}

CKDWORD CKDrawStateCache::GetRenderState(VXRENDERSTATETYPE state) const {
    if ((CKDWORD)state >= CKFF_RS_COUNT)
        return 0;
    return m_States[state];
}

CKDrawState CKDrawStateCache::BuildDrawState(VXPRIMITIVETYPE topology) {
    if (m_DirtyMask == 0 && topology == m_LastTopology)
        return m_CachedState;

    m_LastTopology = topology;

    uint32_t lo = CKRST_STATE_WRITE_RGBA;
    uint32_t mid = 0;
    uint32_t hi = 0;

    // Depth
    if (m_States[VXRENDERSTATE_ZENABLE]) {
        lo |= CKRST_STATE_DEPTH_TEST;
        lo |= CKRST_STATE_DEPTH_FUNC(m_States[VXRENDERSTATE_ZFUNC]);
    }
    if (m_States[VXRENDERSTATE_ZWRITEENABLE])
        lo |= CKRST_STATE_DEPTH_WRITE;

    // Cull
    CKDWORD cullMode = m_States[VXRENDERSTATE_CULLMODE];
    if (EnvEnabled("CK2_3D_DEBUG_NO_CULL"))
        cullMode = VXCULL_NONE;
    lo |= CKRST_STATE_CULL(RemapCullMode(cullMode));

    // Fill mode
    lo |= CKRST_STATE_FILLMODE(RemapFillMode(m_States[VXRENDERSTATE_FILLMODE]));

    // Blend
    if (m_States[VXRENDERSTATE_ALPHABLENDENABLE]) {
        CKDWORD srcBlend = m_States[VXRENDERSTATE_SRCBLEND];
        CKDWORD dstBlend = m_States[VXRENDERSTATE_DESTBLEND];
        FixupBlendPair(srcBlend, dstBlend);
        lo |= CKRST_STATE_BLEND(srcBlend, dstBlend);
    }

    // Topology
    mid |= CKRST_STATE_PT(topology);

    // Match D3D/Virtools winding: clockwise is front-facing by default.
    // VXCULL_CCW maps to culling counter-clockwise back faces.

    m_CachedState.Lo = lo;
    m_CachedState.Mid = mid;
    m_CachedState.Hi = hi;
    m_DirtyMask = 0;

    return m_CachedState;
}
