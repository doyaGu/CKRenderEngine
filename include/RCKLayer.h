#ifndef RCKLAYER_H
#define RCKLAYER_H

#include "CKRenderEngineTypes.h"

#include "CKLayer.h"

class RCKLayer : public CKLayer {
public:
    // TODO: Add public functions

    explicit RCKLayer(CKContext *Context, CKSTRING name = nullptr);
    ~RCKLayer() override;
    CK_CLASSID GetClassID() override;

    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;

    int GetMemoryOccupation() override;

    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CKLayer *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

protected:
    RCKGrid *m_Grid;
    CKDWORD m_Type;
    CKDWORD m_Format;
    CKDWORD m_Flags;
    void *m_SquareArray;
};

#endif // RCKLAYER_H
