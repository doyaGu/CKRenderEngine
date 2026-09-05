#ifndef CKTRANSLATEDRASTERIZER_H
#define CKTRANSLATEDRASTERIZER_H

// Adapter-facing entry points for the CKRasterizer v3 translation core.
// Implementation types stay in CKTranslatedRasterizerInternal.h; engine and
// rasterizer adapters only need this factory interface.

#include "CKRasterizer.h"

class CKRasterizerBackendLibrary;

typedef void (*CKTranslatedLibraryCloseFunction)(CKRasterizerBackendLibrary *Library);

CKRasterizer *CKTranslatedRasterizerStart(CKRasterizerBackendLibrary *Library,
                                          CKTranslatedLibraryCloseFunction CloseLibrary);
void CKTranslatedRasterizerClose(CKRasterizer *Rasterizer);

CKRasterizer *CKTranslatedNullRasterizerStart(WIN_HANDLE AppWnd);
void CKTranslatedNullRasterizerClose(CKRasterizer *Rasterizer);
void CKTranslatedNullRasterizerGetInfo(CKRasterizerInfo *Info);

#endif // CKTRANSLATEDRASTERIZER_H
