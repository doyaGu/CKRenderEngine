#ifndef RCKSPRITE_H
#define RCKSPRITE_H

#include "CKSprite.h"

class RCKSprite : public CKSprite {
public:
    // TODO: Add public functions

    explicit RCKSprite(CKContext *Context, CKSTRING name = nullptr);
    ~RCKSprite() override;
    CK_CLASSID GetClassID() override;

    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;

    int GetMemoryOccupation() override;

    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CKSprite *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

protected:
    CKBitmapData m_BitmapData;
    VX_PIXELFORMAT m_PixelFormat;
    CKRasterizerContext *m_RasterizerContext;
    CKDWORD m_ObjectIndex;
};

#endif // RCKSPRITE_H
