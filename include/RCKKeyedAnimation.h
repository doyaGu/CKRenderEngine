#ifndef RCKKEYEDANIMATION_H
#define RCKKEYEDANIMATION_H

#include "CKRenderEngineTypes.h"

#include "CKKeyedAnimation.h"
#include "RCKAnimation.h"

class RCKKeyedAnimation : public RCKAnimation {
public:

#undef CK_PURE
#define CK_3DIMPLEMENTATION
#include "CKKeyedAnimation.h"
#undef CK_3DIMPLEMENTATION

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
    XSObjectPointerArray m_Animations;
    CKDWORD field_38;
    CKBOOL m_Merged;
    float m_MergeFactor;
    RCKObjectAnimation *m_RootAnimation;
    VxVector m_Vector;
};

#endif // RCKKEYEDANIMATION_H
