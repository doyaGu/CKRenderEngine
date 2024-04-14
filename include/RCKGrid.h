#ifndef RCKGRID_H
#define RCKGRID_H

#include "CKRenderEngineTypes.h"

#include "CKGrid.h"

class RCKGrid : public CKGrid {
public:
    // TODO: Add public functions

    explicit RCKGrid(CKContext *Context, CKSTRING name = nullptr);
    ~RCKGrid() override;
    CK_CLASSID GetClassID() override;

    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;

    int GetMemoryOccupation() override;

    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CKGrid *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

protected:
    XArray<CKLayer *> m_Layers;
    CKDWORD m_Width;
    CKDWORD m_Length;
    CKDWORD m_Priority;
    CKDWORD m_OrientationMode;
    RCKMesh *m_Mesh;
};

#endif // RCKGRID_H
