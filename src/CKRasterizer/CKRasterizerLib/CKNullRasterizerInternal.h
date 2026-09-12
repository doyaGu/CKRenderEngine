#ifndef CKNULLRASTERIZERINTERNAL_H
#define CKNULLRASTERIZERINTERNAL_H

#include "CKRasterizer.h"

CKRasterizerContext *CKNullCreateRasterizerContext(CKRasterizerDriver *driver);
CKBOOL CKNullDestroyRasterizerContext(CKRasterizerContext *context);

#endif // CKNULLRASTERIZERINTERNAL_H
