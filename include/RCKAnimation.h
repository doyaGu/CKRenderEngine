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

    // CKAnimation virtual methods
    float GetLength() override;
    float GetFrame() override;
    float GetNextFrame(float delta_t) override;
    float GetStep() override;
    void SetFrame(float frame) override;
    void SetStep(float step) override;
    void SetLength(float nbframe) override;
    CKCharacter *GetCharacter() override;
    void LinkToFrameRate(CKBOOL link, float fps = 30.0f) override;
    float GetLinkedFrameRate() override;
    CKBOOL IsLinkedToFrameRate() override;
    void SetTransitionMode(CK_ANIMATION_TRANSITION_MODE mode) override;
    CK_ANIMATION_TRANSITION_MODE GetTransitionMode() override;
    void SetSecondaryAnimationMode(CK_SECONDARYANIMATION_FLAGS mode) override;
    CK_SECONDARYANIMATION_FLAGS GetSecondaryAnimationMode() override;
    void SetCanBeInterrupt(CKBOOL can = TRUE) override;
    CKBOOL CanBeInterrupt() override;
    void SetCharacterOrientation(CKBOOL orient = TRUE) override;
    CKBOOL DoesCharacterTakeOrientation() override;
    void SetFlags(CKDWORD flags) override;
    CKDWORD GetFlags() override;
    CK3dEntity *GetRootEntity() override;
    void SetCurrentStep(float Step) override;

    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CKAnimation *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

protected:
    RCKCharacter *m_Character;
    float m_Length;
    float m_CurrentFrame;
    float m_Step;
    RCK3dEntity *m_RootEntity;
    CKDWORD m_Flags;
    float m_FrameRate;
    CK_ANIMATION_TRANSITION_MODE m_TransitionMode;
    CK_SECONDARYANIMATION_FLAGS m_SecondaryAnimationMode;
};

#endif // RCKANIMATION_H
