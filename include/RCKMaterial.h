#ifndef RCKMATERIAL_H
#define RCKMATERIAL_H

#include "CKRenderEngineTypes.h"

#include "CKMaterial.h"

class RCKMaterial : public CKMaterial {
public:
    // TODO: Add public functions

    explicit RCKMaterial(CKContext *Context, CKSTRING name = nullptr);
    ~RCKMaterial() override;
    CK_CLASSID GetClassID() override;

    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;

    int GetMemoryOccupation() override;

    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CKMaterial *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

protected:
    CKTexture *m_Textures[4];
    CKMaterialData m_MaterialData;
    VxColor m_SpecularColor;
    VXTEXTURE_BLENDMODE m_TextureBlendMode;
    VXTEXTURE_FILTERMODE m_TextureMinMode;
    VXTEXTURE_FILTERMODE m_TextureMagMode;
    VXTEXTURE_BLENDMODE m_SourceBlend;
    VXTEXTURE_BLENDMODE m_DestBlend;
    CKDWORD m_ShadeMode;
    VXTEXTURE_BLENDMODE m_FillMode;
    VXTEXTURE_FILTERMODE m_TextureAddressMode;
    VXTEXTURE_FILTERMODE m_TextureBorderColor;
    VXBLEND_MODE m_Flags;
    VXBLEND_MODE m_AlphaRef;
    CKSprite3DBatch *m_Sprite3DBatch;
    CK_MATERIALCALLBACK m_Fct;
    void *m_CallbackArgument;
    CKParameter *m_EffectParameter;
};

#endif // RCKMATERIAL_H
