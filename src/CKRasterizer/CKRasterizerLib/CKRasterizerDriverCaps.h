#ifndef CKRASTERIZERDRIVERCAPS_H
#define CKRASTERIZERDRIVERCAPS_H

#include "CKRasterizer.h"

// Publishes conservative discovery data before a Context has opened a device.
// The concrete driver replaces Caps and texture formats with its final snapshot
// after successful native initialization.
void CKRSTInitializeDriverCaps(
    XArray<VxDisplayMode> &DisplayModes,
    XClassArray<CKTextureDesc> &TextureFormats,
    CKRasterizerNativeCapsDesc &Caps);

#endif
