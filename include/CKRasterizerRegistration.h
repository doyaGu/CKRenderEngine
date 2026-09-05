#ifndef CKRASTERIZERREGISTRATION_H
#define CKRASTERIZERREGISTRATION_H

#include "CKRasterizer.h"

// Built-in fallback registration uses the same v3 entry points as a plugin.
// CK_3D does not construct or depend on the translation core or a GPU backend.
void CKNullRasterizerGetInfo(CKRasterizerInfo *Info);

#endif
