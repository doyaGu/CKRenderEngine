#ifndef CKSDLGPU_TEXTUREDATA_H
#define CKSDLGPU_TEXTUREDATA_H

#include "VxMath.h"
#include "XArray.h"

// Decodes DXT1-5 to tightly packed BGRA8. DXT2/4 premultiplied colors are
// converted to straight alpha for the existing shader and blending paths.
// The public descriptor remains compressed; auto-mipped textures use filterable storage.
bool CKSdlGpuDecodeDXT(const VxImageDescEx &image, XArray<unsigned char> &pixels);

#endif
