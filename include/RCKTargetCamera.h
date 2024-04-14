#ifndef RCKTARGETCAMERA_H
#define RCKTARGETCAMERA_H

#include "CKTargetCamera.h"

class RCKTargetCamera : public CKTargetCamera {
public:
    // TODO: Add public functions

    explicit RCKTargetCamera(CKContext *Context, CKSTRING name = nullptr);
    ~RCKTargetCamera() override;
    CK_CLASSID GetClassID() override;

    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;

    int GetMemoryOccupation() override;

    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static CKTargetCamera *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

protected:
    // TODO: Add fields
};

#endif // RCKTARGETCAMERA_H
