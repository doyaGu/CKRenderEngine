#ifndef CKSDLGPURASTERIZER_H
#define CKSDLGPURASTERIZER_H

#include "CKRasterizer.h"

struct SDL_GPUDevice;
struct SDL_GPUTexture;

// Optional pre-creation offer. The Context borrows this device and never
// destroys it; unsupported rasterizers ignore the window property.
#define CKSDL_GPU_DEVICE_PROPERTY "CKRasterizer.SDL_gpu.device"

// Native payload accepted by CKTextureImportDesc. Both pointers are borrowed;
// Device must be the Context's device and Texture remains caller-owned.
#define CKSDL_GPU_TEXTURE_GUID CKGUID(0x6f59a44e, 0x38d72cb1)

struct CKSdlGpuTextureImportData {
    SDL_GPUDevice *Device;
    SDL_GPUTexture *Texture;
};

inline CKBOOL CKSdlGpuImportTexture(CKRasterizerContext *Context,
                                    const CKTextureDesc *Desc,
                                    SDL_GPUDevice *Device,
                                    SDL_GPUTexture *Texture,
                                    CKDWORD *OutTexture)
{
    if (OutTexture)
        *OutTexture = 0;
    if (!Context || !Desc || !Device || !Texture || !OutTexture)
        return FALSE;
    CKSdlGpuTextureImportData data = {Device, Texture};
    CKTextureImportDesc importDesc = {
        CKSDL_GPU_TEXTURE_GUID, &data, (CKDWORD)sizeof(data)};
    return Context->ImportTexture(Desc, &importDesc, OutTexture);
}

// Static registration entry. Dynamic plugins expose CKRasterizerGetInfo and
// deliberately do not advertise this symbol to linked consumers.
#if defined(CKSDL_GPU_RASTERIZER_STATIC)
void CKSdlGpuRasterizerGetInfo(CKRasterizerInfo *Info);
#endif

#endif
