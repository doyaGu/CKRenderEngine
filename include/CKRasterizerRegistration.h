#ifndef CKRASTERIZERREGISTRATION_H
#define CKRASTERIZERREGISTRATION_H

#include "CKRasterizer.h"

// Built-in fallback registration uses the same entry points as a plugin.
// CK_3D does not construct or depend on a concrete GPU rasterizer.
void CKNullRasterizerGetInfo(CKRasterizerInfo *Info);

#endif
