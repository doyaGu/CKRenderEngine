#ifndef CK_RENDER_CONFIGURATION_H
#define CK_RENDER_CONFIGURATION_H

#include "CKTypes.h"

#if defined(_WIN32) && defined(CKRE_DYNAMIC_RENDER_ENGINE)
#define CKRE_CONFIGURATION_EXPORT __declspec(dllexport)
#else
#define CKRE_CONFIGURATION_EXPORT
#endif

// Select an owned configuration for this context before creating render contexts.
// The file is parsed immediately. Failure preserves the previous configuration.
// NULL restores legacy defaults (CKRE_SETTINGS_FILE, module sibling, current directory).
// Existing resources keep their properties; defaults apply to future resources.
extern "C" CKRE_CONFIGURATION_EXPORT CKERROR CKSetRenderSettingsFile(CKContext *context, const char *filename);

#endif
