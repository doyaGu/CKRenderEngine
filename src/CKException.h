#pragma once

#include "CKRenderConfig.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#else
typedef int LONG;
#ifndef WINAPI
#define WINAPI
#endif
#define EXCEPTION_CONTINUE_SEARCH 0
struct EXCEPTION_POINTERS;
#endif

#if CKRE_ENABLE_DEBUG_LOGGER

void CKInstallExceptionHandler();
void CKRemoveExceptionHandler();
LONG WINAPI CKExceptionHandler(EXCEPTION_POINTERS *pExceptionInfo);

#else

inline void CKInstallExceptionHandler() {}
inline void CKRemoveExceptionHandler() {}
inline LONG WINAPI CKExceptionHandler(EXCEPTION_POINTERS *) { return EXCEPTION_CONTINUE_SEARCH; }

#endif
