#ifndef RCK2DENTITY_H
#define RCK2DENTITY_H

#include "CKBitmapData.h"
#include "RCKRenderObject.h"
#include "Vx2dVector.h"
#include "VxRect.h"
#include "CK2dEntity.h"
#include "CKContext.h"
#include "CKRenderManager.h"
#include "CKRenderContext.h"
#include "CKStateChunk.h"
#include "CKMaterial.h"
#include "CKFile.h"

class RCK2dEntity : public RCKRenderObject {
public:

#undef CK_PURE
#define CK_3DIMPLEMENTATION
#include "CK2dEntity.h"
#undef CK_3DIMPLEMENTATION

    // CKRenderObject overrides
    CKBOOL IsInRenderContext(CKRenderContext *context) override;
    CKBOOL IsRootObject() override;
    CKBOOL IsToBeRendered() override;
    void SetZOrder(int Z) override;
    int GetZOrder() override;
    CKBOOL IsToBeRenderedLast() override;
    CKBOOL AddPreRenderCallBack(CK_RENDEROBJECT_CALLBACK Function, void *Argument, CKBOOL Temp) override;
    CKBOOL RemovePreRenderCallBack(CK_RENDEROBJECT_CALLBACK Function, void *Argument) override;
    CKBOOL SetRenderCallBack(CK_RENDEROBJECT_CALLBACK Function, void *Argument) override;
    CKBOOL RemoveRenderCallBack() override;
    CKBOOL AddPostRenderCallBack(CK_RENDEROBJECT_CALLBACK Function, void *Argument, CKBOOL Temp) override;
    CKBOOL RemovePostRenderCallBack(CK_RENDEROBJECT_CALLBACK Function, void *Argument) override;
    void RemoveAllCallbacks() override;

    // Helpers
    void AddToRenderContext(CKRenderContext *context);
    void RemoveFromRenderContext(CKRenderContext *context);
    int CanBeHide();
    void GetHomogeneousRelativeRect(VxRect &rect);

    //--------------------------------------------------------
    ////               Private Part

    //----------------------------------------------------------
    // Internal functions
    CKBOOL IsHiddenByParent() override;

    explicit RCK2dEntity(CKContext *Context, CKSTRING name = nullptr);
    ~RCK2dEntity() override;
    CK_CLASSID GetClassID() override;

    void PreSave(CKFile *file, CKDWORD flags) override;
    CKStateChunk *Save(CKFile *file, CKDWORD flags) override;
    CKERROR Load(CKStateChunk *chunk, CKFile *file) override;
    void PostLoad() override;

    void PreDelete() override;
    void CheckPreDeletion() override;

    int GetMemoryOccupation() override;

    //--------------------------------------------
    // Dependencies Functions	{Secret}
    CKERROR PrepareDependencies(CKDependenciesContext &context) override;
    CKERROR RemapDependencies(CKDependenciesContext &context) override;
    CKERROR Copy(CKObject &o, CKDependenciesContext &context) override;

    static CKSTRING GetClassName();
    static int GetDependenciesCount(int mode);
    static CKSTRING GetDependencies(int i, int mode);
    static void Register();
    static RCK2dEntity *CreateInstance(CKContext *Context);
    static CK_CLASSID m_ClassID;

protected:
    VxRect m_Rect;
    VxRect *m_HomogeneousRect;
    VxRect m_VtxPos;
    VxRect m_SrcRect;
    CKDWORD m_Flags;
    CK2dEntity *m_Parent;
    CKMaterial *m_Material;
    XArray<CK2dEntity *> m_Children;
    VxRect m_SourceRect;
    CKDWORD m_ZOrder;
};

#endif // RCK2DENTITY_H
