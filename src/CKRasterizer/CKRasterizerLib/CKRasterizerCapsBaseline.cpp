#include "CKRasterizerCapsBaseline.h"

#include <string.h>

#if defined(__has_include)
#if __has_include("CKRasterizerCapsBaseline.generated.h")
#include "CKRasterizerCapsBaseline.generated.h"
#define CKRST_HAVE_CAPS_BASELINE 1
#endif
#endif

#ifndef CKRST_HAVE_CAPS_BASELINE
#define CKRST_HAVE_CAPS_BASELINE 0
#endif

CKBOOL CKRSTGetCapsBaseline(Vx3DCapsDesc *Caps3D, Vx2DCapsDesc *Caps2D)
{
#if CKRST_HAVE_CAPS_BASELINE
    static const Vx3DCapsDesc s_Baseline3D = CKRST_CAPS_BASELINE_3D_INIT;
    static const Vx2DCapsDesc s_Baseline2D = CKRST_CAPS_BASELINE_2D_INIT;
    if (Caps3D)
        memcpy(Caps3D, &s_Baseline3D, sizeof(Vx3DCapsDesc));
    if (Caps2D)
        memcpy(Caps2D, &s_Baseline2D, sizeof(Vx2DCapsDesc));
    return TRUE;
#else
    (void)Caps3D;
    (void)Caps2D;
    return FALSE;
#endif
}

const char *CKRSTGetCapsBaselineSource()
{
#if CKRST_HAVE_CAPS_BASELINE
    return CKRST_CAPS_BASELINE_SOURCE;
#else
    return NULL;
#endif
}

static void LowerField(XDWORD &Value, XDWORD Limit)
{
    if (Limit != 0 && Limit < Value)
        Value = Limit;
}

void CKRSTLowerCapsToLimits(Vx3DCapsDesc *Caps, const Vx3DCapsDesc *Limits)
{
    if (!Caps || !Limits)
        return;
    LowerField(Caps->MaxTextureWidth, Limits->MaxTextureWidth);
    LowerField(Caps->MaxTextureHeight, Limits->MaxTextureHeight);
    LowerField(Caps->MaxClipPlanes, Limits->MaxClipPlanes);
    LowerField(Caps->MaxActiveLights, Limits->MaxActiveLights);
    LowerField(Caps->MaxNumberBlendStage, Limits->MaxNumberBlendStage);
    LowerField(Caps->MaxNumberTextureStage, Limits->MaxNumberTextureStage);
    LowerField(Caps->MaxTextureRatio, Limits->MaxTextureRatio);
    // Minimum sizes may only grow.
    if (Limits->MinTextureWidth > Caps->MinTextureWidth)
        Caps->MinTextureWidth = Limits->MinTextureWidth;
    if (Limits->MinTextureHeight > Caps->MinTextureHeight)
        Caps->MinTextureHeight = Limits->MinTextureHeight;
}
