#ifndef RCKRENDEROBJECT_H
#define RCKRENDEROBJECT_H

#include "CKRenderEngineTypes.h"

#include "CKRenderObject.h"

class RCKRenderObject : public CKRenderObject {
public:
    // TODO: Add public functions

    RCKRenderObject() {}
    explicit RCKRenderObject(CKContext *Context, CKSTRING name = nullptr);
    ~RCKRenderObject() override;
    CK_CLASSID GetClassID() override;

    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;

    int GetMemoryOccupation() override;

    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CKRenderObject *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

protected:
    CKBOOL m_InRenderContext;
    CKCallbacksContainer *m_Callbacks;
};

#endif // RCKRENDEROBJECT_H
