#ifndef RCKKEYEDANIMATION_H
#define RCKKEYEDANIMATION_H

#include "CKRenderEngineTypes.h"

#include "CKKeyedAnimation.h"

class RCKKeyedAnimation : public CKKeyedAnimation {
public:
    // TODO: Add public functions

    explicit RCKKeyedAnimation(CKContext *Context, CKSTRING name = nullptr);
    ~RCKKeyedAnimation() override;
    CK_CLASSID GetClassID() override;

    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;

    int GetMemoryOccupation() override;

    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CKKeyedAnimation *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

protected:
    CKDWORD m_Animations;
    CKDWORD field_38;
    CKBOOL m_Merged;
    float m_MergeFactor;
    RCKObjectAnimation *m_RootAnimation;
    VxVector m_Vector;
};

#endif // RCKKEYEDANIMATION_H
