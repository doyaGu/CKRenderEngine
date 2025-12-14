#ifndef RCKCHARACTER_H
#define RCKCHARACTER_H

#include "RCK3dEntity.h"
#include "XObjectArray.h"

struct CKSecondaryAnimation {
  CK_ID AnimID;
  RCKKeyedAnimation *Animation;
  CKDWORD Flags;
  float WarpLength;
  CKDWORD field_14;
  CK_SECONDARYANIMATION_FLAGS Mode;
  CKDWORD field_1C;
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
    int IsObjectUsed(CKObject *o, CK_CLASSID cid) override;

    void Show(CK_OBJECT_SHOWOPTION show) override;

    //--------------------------------------------
    // Dependencies Functions	{Secret}
    CKERROR PrepareDependencies(CKDependenciesContext &context) override;
    CKERROR RemapDependencies(CKDependenciesContext &context) override;
    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    void AddToScene(CKScene *scene, CKBOOL dependencies = TRUE) override;
    void RemoveFromScene(CKScene *scene, CKBOOL dependencies = TRUE) override;

    void ApplyPatchForOlderVersion(int NbObject, CKFileObject *FileObjects) override;

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
};

#endif // RCKCHARACTER_H
