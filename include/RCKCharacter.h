#ifndef RCKCHARACTER_H
#define RCKCHARACTER_H

#include "RCK3dEntity.h"
#include "XObjectArray.h"

typedef enum CK_SECONDARYANIMATION_RUNTIME_MODE {
    CKSECONDARYANIMATIONRUNTIME_STARTINGWARP = 1,
    CKSECONDARYANIMATIONRUNTIME_PLAYING = 2,
    CKSECONDARYANIMATIONRUNTIME_STOPPINGWARP = 3,
} CK_SECONDARYANIMATION_RUNTIME_MODE;

// Native 2.1 uses seven 32-bit fields (28-byte stride): ID +0, warper +4,
// flags +8, warp length +12, start frame +16, mode +20, loop count +24.
// This SDL record retains an additional reserved word and a native pointer;
// its size/offsets depend on architecture and are not native ABI guarantees.
struct CKSecondaryAnimation {
    CK_ID AnimID;                              // Public source animation identity
    RCKKeyedAnimation *Animation;              // Reusable transition, retained outside the active prefix
    CKDWORD Flags;                             // CK_SECONDARYANIMATION_FLAGS, unshifted
    float WarpLength;                          // Requested transition duration in frames
    CKDWORD Padding;                           // SDL live-return target ID for mixed clocks/held main, otherwise 0; not a native field
    CKDWORD StartingFrameBits;                 // Bit-preserved float starting frame
    CK_SECONDARYANIMATION_RUNTIME_MODE Mode;   // 0 unused, 1 starting warp, 2 playing, 3 stopping warp
    CKDWORD LoopCountRemaining;                // Requested remaining loops for LOOPNTIMES

    float GetStartingFrame() const {
        float frame;
        memcpy(&frame, &StartingFrameBits, sizeof(frame));
        return frame;
    }

    void SetStartingFrame(float frame) {
        memcpy(&StartingFrameBits, &frame, sizeof(frame));
    }
};

class RCKCharacter : public RCK3dEntity {
public:

#undef CK_PURE
#define CK_3DIMPLEMENTATION
#include "CKCharacter.h"
#undef CK_3DIMPLEMENTATION

    //--------------------------------------------------------
    ////               Private Part

    //----------------------------------------------------------
    // Internal functions

    explicit RCKCharacter(CKContext *Context, CKSTRING name = nullptr);
    ~RCKCharacter() override;
    CK_CLASSID GetClassID() override;

    void PreSave(CKFile *file, CKDWORD flags) override;
    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;

    void CheckPreDeletion() override;

    int GetMemoryOccupation() override;
    CKBOOL IsObjectUsed(CKObject *o, CK_CLASSID cid) override;

    void Show(CK_OBJECT_SHOWOPTION show) override;

    //--------------------------------------------
    // Dependencies Functions	{Secret}
    CKERROR PrepareDependencies(CKDependenciesContext &context) override;
    CKERROR RemapDependencies(CKDependenciesContext &context) override;
    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    void AddToScene(CKScene *scene, CKBOOL dependencies = TRUE) override;
    void RemoveFromScene(CKScene *scene, CKBOOL dependencies = TRUE) override;

    const VxBbox &GetBoundingBox(CKBOOL Local = FALSE) override;
    CKBOOL GetBaryCenter(VxVector *Pos) override;
    float GetRadius() override;

    //--------------------------------------------
    // Class Registering	{Secret}
    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CKCharacter *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

protected:
    void PreDeleteBodyPartsForAnimation(CKAnimation *anim);
    void FindFloorReference();
    void RemoveSecondaryAnimationAt(int index);

    XSObjectPointerArray m_BodyParts;      // Stores CKObject* (body parts)
    XSObjectPointerArray m_Animations;     // Stores CKObject* (animations)
    CKSecondaryAnimation *m_SecondaryAnimations;
    CKWORD m_SecondaryAnimationsCount;
    CKWORD m_SecondaryAnimationsAllocated;
    RCKBodyPart *m_RootBodyPart;
    RCKKeyedAnimation *m_ActiveAnimation;
    RCKAnimation *m_AnimDest;
    RCKKeyedAnimation *m_Warper;
    float m_FrameDest;
    CKDWORD field_1D4;
    RCK3dEntity *m_FloorRef;
    float m_AnimationLevelOfDetail;
    float m_FrameSrc;
    RCKAnimation *m_AnimSrc;
    CKDWORD m_TransitionMode;

private:
    RCKCharacter(const RCKCharacter &) = delete;
    RCKCharacter &operator=(const RCKCharacter &) = delete;
};

#endif // RCKCHARACTER_H
