#ifndef CKSDLGPU_TEXTUREDATA_H
#define CKSDLGPU_TEXTUREDATA_H

#include "VxMath.h"
#include "XArray.h"

// Decodes the supplied BC1/2/3 image to tightly packed BGRA8. The public
// descriptor remains compressed; auto-mipped textures use filterable storage.
bool CKSdlGpuDecodeDXT(const VxImageDescEx &image, XArray<unsigned char> &pixels);

#endif
