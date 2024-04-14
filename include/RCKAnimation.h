#ifndef RCKANIMATION_H
#define RCKANIMATION_H

#include "CKRenderEngineTypes.h"
#include "CKAnimation.h"

class RCKAnimation : public CKAnimation {
public:
    explicit RCKAnimation(CKContext *Context, CKSTRING name = nullptr);
    ~RCKAnimation() override;
    CK_CLASSID GetClassID() override;

    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;

    int GetMemoryOccupation() override;

    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CKAnimation *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

protected:
    RCKCharacter *m_Character;
    float m_Length;
    float m_Step;
    RCK3dEntity *m_RootEntity;
    CKDWORD m_Flags;
    float m_FrameRate;
};

#endif // RCKANIMATION_H
