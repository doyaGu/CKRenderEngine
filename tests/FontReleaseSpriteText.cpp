#include "FontReleaseObserver.h"
#include "RCKSpriteText.h"
#include "CKStateChunk.h"
#include "CKBitmapData.h"

// Link the actual sprite-text implementation with an observed deletion boundary.
// Other engine objects still come from CK2_3DStatic; its sprite-text object is unused.
#define VxDeleteFont CkreFontTestDeleteFont
#include "../src/CKSpriteText.cpp"
#undef VxDeleteFont
