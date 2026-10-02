#ifndef CKFFSAMPLERLAYOUT_H
#define CKFFSAMPLERLAYOUT_H

// Each native fragment shader exposes sixteen samplers. Three layouts cover
// every mix of the eight logical texture stages: when one non-2D type needs
// more than four slots, at most three stages remain for the other two types.
#define CKFF_NARROW_SAMPLER_COUNT 4
#define CKFF_WIDE_SAMPLER_COUNT 8

enum CKFFSamplerLayout {
#define CKFF_SAMPLER_LAYOUT(name, value, twoD, cube, volume) \
    CKFF_SAMPLER_LAYOUT_##name = value,
#include "CKFFSamplerLayout.def"
#undef CKFF_SAMPLER_LAYOUT
    CKFF_SAMPLER_LAYOUT_COUNT,
};

#endif // CKFFSAMPLERLAYOUT_H
