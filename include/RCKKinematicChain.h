#ifndef RCKKINEMATICCHAIN_H
#define RCKKINEMATICCHAIN_H

#include "CKRenderEngineTypes.h"
#include "CKKinematicChain.h"

class RCKKinematicChain : public CKKinematicChain {
public:

    explicit RCKKinematicChain(CKContext *Context, CKSTRING name = nullptr);
    ~RCKKinematicChain() override = default;
    CK_CLASSID GetClassID() override;

    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;

    float GetChainLength(CKBodyPart *End = nullptr) override;
    int GetChainBodyCount(CKBodyPart *End = nullptr) override;
    CKBodyPart *GetStartEffector() override;
    CKERROR SetStartEffector(CKBodyPart *start) override;
    CKBodyPart *GetEffector(int pos) override;
    CKBodyPart *GetEndEffector() override;
    CKERROR SetEndEffector(CKBodyPart *end) override;
    CKERROR IKSetEffectorPos(VxVector *pos, CK3dEntity *ref = nullptr, CKBodyPart *body = nullptr) override;

    int GetMemoryOccupation() override;
    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    // Class registration helpers
    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CKKinematicChain *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

private:
    CKBodyPart *m_StartEffector;
    CKBodyPart *m_EndEffector;
};

#endif // RCKKINEMATICCHAIN_H
