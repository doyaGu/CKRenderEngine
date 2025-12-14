#ifndef RCKKINEMATICCHAIN_H
#define RCKKINEMATICCHAIN_H

#include "CKRenderEngineTypes.h"
#include "CKKinematicChain.h"

/**
 * @brief Internal structure for IK chain body data.
 * Size: 116 bytes per body part in the chain.
 * Used during IKSetEffectorPos for joint constraint evaluation.
 */
struct CKIKChainBodyData {
    CKDWORD m_JointFlags;          // offset 0: Joint constraint flags (bits 0-2 = axis locks, 4-6 = limits enabled)
    VxVector m_MinAngles;          // offset 4: Minimum rotation angles per axis
    VxVector m_MaxAngles;          // offset 16: Maximum rotation angles per axis
    float m_Reserved[6];           // offset 28: Reserved/padding
    CKBodyPart *m_BodyPart;        // offset 40: Pointer to body part
    float m_TransformData[16];     // offset 44: Transform matrix data (4x4)
    CKDWORD m_IsLocked;            // offset 108: Whether this joint is locked
    CKDWORD m_WasAtLimit;          // offset 112: Whether joint was at limit during last solve
};

class RCKKinematicChain : public CKKinematicChain {
public:
    explicit RCKKinematicChain(CKContext *Context, CKSTRING name = nullptr);
    ~RCKKinematicChain() override;
    
    CK_CLASSID GetClassID() override;

    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;

    void CheckPreDeletion() override;
    int GetMemoryOccupation() override;
    CKBOOL IsObjectUsed(CKObject *obj, CK_CLASSID cid) override;
    CKERROR RemapDependencies(CKDependenciesContext &context) override;
    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    // CKKinematicChain virtual methods
    float GetChainLength(CKBodyPart *End = nullptr) override;
    int GetChainBodyCount(CKBodyPart *End = nullptr) override;
    CKBodyPart *GetStartEffector() override;
    CKERROR SetStartEffector(CKBodyPart *start) override;
    CKBodyPart *GetEffector(int pos) override;
    CKBodyPart *GetEndEffector() override;
    CKERROR SetEndEffector(CKBodyPart *end) override;
    CKERROR IKSetEffectorPos(VxVector *pos, CK3dEntity *ref = nullptr, CKBodyPart *body = nullptr) override;

    // Class registration helpers
    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CKKinematicChain *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

private:
    // IK helper method - performs rotation iteration for IK solving
    CKERROR IKRotateToward(VxVector *targetDelta, CKBodyPart *endBody);

    CKBodyPart *m_StartEffector;   // offset +20
    CKBodyPart *m_EndEffector;     // offset +24
    CKDWORD m_ChainBodyCount;      // offset +28: Number of body parts in chain
    CKIKChainBodyData *m_ChainData; // offset +32: Pointer to IK chain body data array
};

#endif // RCKKINEMATICCHAIN_H
