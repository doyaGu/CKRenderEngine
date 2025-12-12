#ifndef RCKPLACE_H
#define RCKPLACE_H

#include "RCK3dEntity.h"

// Portal entry - pair of destination place and portal geometry
struct CKPortalEntry {
    CKPlace *place;
    CK3dEntity *portal;
};

class RCKPlace : public RCK3dEntity {
public:

#undef CK_PURE
#define CK_3DIMPLEMENTATION
#include "CKPlace.h"
#undef CK_3DIMPLEMENTATION

    explicit RCKPlace(CKContext *Context, CKSTRING name = nullptr);
    ~RCKPlace() override;
    CK_CLASSID GetClassID() override;

    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;

    int GetMemoryOccupation() override;

    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CKPlace *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

protected:
    XSArray<CKPortalEntry> m_Portals;
    CK_ID m_AuxObjectId;  // auxiliary serialized reference (legacy)
    CK_ID m_DefaultCamera;
    VxRect m_ClippingRect;
};

#endif // RCKPLACE_H
