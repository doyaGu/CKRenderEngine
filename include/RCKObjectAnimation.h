#ifndef RCKOBJECTANIMATION_H
#define RCKOBJECTANIMATION_H

#include "CKRenderEngineTypes.h"

#include "CKObjectAnimation.h"

class RCKObjectAnimation : public CKObjectAnimation {
public:
    // TODO: Add public functions

    explicit RCKObjectAnimation(CKContext *Context, CKSTRING name = nullptr);
    ~RCKObjectAnimation() override;
    CK_CLASSID GetClassID() override;

    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;

    int GetMemoryOccupation() override;

    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CKObjectAnimation *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

protected:
    CKKeyframeData *m_KeyframeData;
    CKDWORD m_Flags;
    RCK3dEntity *m_Entity;
    float m_CurrentStep;
    float m_MergeFactor;
    RCKObjectAnimation *m_Anim1;
    RCKObjectAnimation *m_Anim2;
    CKDWORD m_field_38;
    RCKKeyedAnimation *m_field_3C;
};

#endif // RCKOBJECTANIMATION_H
