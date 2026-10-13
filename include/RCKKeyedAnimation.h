#ifndef RCKKEYEDANIMATION_H
#define RCKKEYEDANIMATION_H

#include "CKRenderEngineTypes.h"

#include "CKKeyedAnimation.h"
#include "RCKAnimation.h"

class RCKKeyedAnimation : public RCKAnimation {
    friend class RCKCharacter;  // Allow RCKCharacter to access protected members
public:

#undef CK_PURE
#define CK_3DIMPLEMENTATION
#include "CKKeyedAnimation.h"
#undef CK_3DIMPLEMENTATION

    explicit RCKKeyedAnimation(CKContext *Context, CKSTRING name = nullptr);
    ~RCKKeyedAnimation() override;
    CK_CLASSID GetClassID() override;

    // CKObject virtual methods
    void CheckPreDeletion() override;
    CKBOOL IsObjectUsed(CKObject *obj, CK_CLASSID cid) override;
    void PreSave(CKFile *file, CKDWORD flags) override;

    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;

    int GetMemoryOccupation() override;

    CKERROR PrepareDependencies(CKDependenciesContext &context) override;
    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;
    CKERROR RemapDependencies(CKDependenciesContext &context) override;

    // Override CKAnimation methods with RCKKeyedAnimation-specific implementations
    void SetLength(float nbframe) override;
    void SetFrame(float frame) override;
    void SetStep(float step) override;
    void CenterAnimation(float frame) override;
    float GetMergeFactor() override;
    void SetMergeFactor(float factor) override;
    CKBOOL IsMerged() override;
    CKAnimation *CreateMergedAnimation(CKAnimation *anim2, CKBOOL dynamic = FALSE) override;
    float CreateTransition(CKAnimation *in, CKAnimation *out, CKDWORD OutTransitionMode, float length, float FrameTo) override;

    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CKKeyedAnimation *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

    // Internal helpers used to match original CK2_3D.dll serialization behavior.
    RCKObjectAnimation *GetRootAnimationInternal() const { return m_RootAnimation; }
    const VxVector &GetRootVectorInternal() const { return m_Vector; }

    void UpdateRootEntity();

    static void SetParentKeyedAnimation(RCKObjectAnimation *objAnim, RCKKeyedAnimation *parent);

protected:
    void SortAnimations();
    static float ComputeAnimationContribution(RCKObjectAnimation *animation);

    // Original Virtools 2.1 Win32 reference offsets; SDL layout is platform-dependent.
    XSObjectPointerArray m_Animations; // Native +0x34/+0x38 array begin/end
    CKBOOL m_Merged;                   // Native +0x3C
    float m_MergeFactor;               // Native +0x40
    RCKObjectAnimation *m_RootAnimation; // Native +0x44
    VxVector m_Vector;                 // Native +0x48..0x53 (12 bytes)
};

#endif // RCKKEYEDANIMATION_H
