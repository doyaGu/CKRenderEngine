#ifndef RCKTARGETLIGHT_H
#define RCKTARGETLIGHT_H

#include "CKTargetLight.h"

class RCKTargetLight : public CKTargetLight {
public:
    // TODO: Add public functions

    explicit RCKTargetLight(CKContext *Context, CKSTRING name = nullptr);
    ~RCKTargetLight() override;
    CK_CLASSID GetClassID() override;

    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;

    int GetMemoryOccupation() override;

    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CKTargetLight *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

protected:
    // TODO: Add fields
};

#endif // RCKTARGETLIGHT_H
