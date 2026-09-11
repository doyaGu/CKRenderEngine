#ifndef CKRASTERIZERDRIVERCAPS_H
#define CKRASTERIZERDRIVERCAPS_H

#include "CKRasterizer.h"

// Initializes the common legacy capability baseline and enumerates host display
// modes. A concrete driver lowers the numeric limits after its device is ready.
void CKRSTInitializeDriverCaps(CKRasterizerDriver *driver);

#endif

