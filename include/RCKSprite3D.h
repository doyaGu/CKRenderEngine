#ifndef RCKSPRITE3D_H
#define RCKSPRITE3D_H

#include "CKRenderEngineTypes.h"

#include "RCK3dEntity.h"

class RCKSprite3D : public RCK3dEntity {
public:

#undef CK_PURE
#define CK_3DIMPLEMENTATION
#include "CKSprite3D.h"
#undef CK_3DIMPLEMENTATION

    // Non-virtual helper method for batch rendering
    void FillBatch(CKSprite3DBatch *batch);

    explicit RCKSprite3D(CKContext *Context, CKSTRING name = nullptr);
    ~RCKSprite3D() override;
    CK_CLASSID GetClassID() override;

    void PreSave(CKFile *file, CKDWORD flags) override;
    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;

    int GetMemoryOccupation() override;

    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CKSprite3D *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

protected:
    RCKMaterial *m_Material;
    CKDWORD m_Mode;
    Vx2DVector m_Offset;
    VxRect m_Rect;
};

#endif // RCKSPRITE3D_H
