#ifndef RCKCHARACTER_H
#define RCKCHARACTER_H

#include "RCK3dEntity.h"

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
    // TODO: Add fields
};

#endif // RCKCHARACTER_H
