#ifndef CKRASTERIZERCAPSBASELINE_H
#define CKRASTERIZERCAPSBASELINE_H

// Capability baseline shared by rasterizer drivers.
//
// The baseline is the Vx3DCapsDesc / Vx2DCapsDesc snapshot reported by the
// original CKDX8Rasterizer.dll on the reference machine. It is captured by
// ckre_scene_capture --caps-json into tests/reference/caps-baseline.json and
// turned into CKRasterizerCapsBaseline.generated.h by
// tools/gen_caps_baseline_header.py. Drivers fill m_3DCaps / m_2DCaps from
// it and only lower numeric fields to their real backend limits.

#include "VxDefines.h"
#include "CKTypes.h"

// Returns TRUE and fills both structures when a generated baseline is
// compiled in. Returns FALSE (structures untouched) otherwise.
CKBOOL CKRSTGetCapsBaseline(Vx3DCapsDesc *Caps3D, Vx2DCapsDesc *Caps2D);

// Human-readable origin of the baseline ("CKDX8Rasterizer.dll, <driver>,
// <date>"), or NULL when no baseline is compiled in.
const char *CKRSTGetCapsBaselineSource();

// Lowers numeric limits to the values reported by the concrete driver: every numeric
// field of Caps is clamped to the corresponding field of Limits when Limits
// is smaller (and non-zero); bit fields are left untouched.
void CKRSTLowerCapsToLimits(Vx3DCapsDesc *Caps, const Vx3DCapsDesc *Limits);

#endif // CKRASTERIZERCAPSBASELINE_H
