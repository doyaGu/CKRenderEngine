#ifndef RCKBODYPART_H
#define RCKBODYPART_H

#include "RCK3dObject.h"

class RCKBodyPart : public RCK3dObject {
public:
    // TODO: Add public functions

    explicit RCKBodyPart(CKContext *Context, CKSTRING name = nullptr);
    ~RCKBodyPart() override;
    CK_CLASSID GetClassID() override;

    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;

    int GetMemoryOccupation() override;

    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CKBodyPart *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

protected:
    CKDWORD field_1A8;
    CKDWORD field_1AC;
    CKDWORD field_1B0;
    CKDWORD field_1B4;
    CKDWORD field_1B8;
    CKDWORD field_1BC;
    CKDWORD field_1C0;
    CKDWORD field_1C4;
    CKDWORD field_1C8;
    CKDWORD field_1CC;
    CKDWORD field_1D0;
    CKDWORD field_1D4;
};

#endif // RCKBODYPART_H
