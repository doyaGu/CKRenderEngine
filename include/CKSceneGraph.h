#ifndef CKSCENEGRAPH_H
#define CKSCENEGRAPH_H

#include "CKRenderEngineTypes.h"

class RCKRenderContext;
class RCKPlace;

// Forward declaration
struct CKSceneGraphNode;

// Transparent object entry for sorting
struct CKTransparentObject {
    CKSceneGraphNode *m_Node; // The scene graph node
    float m_ZhMax;            // Maximum Z in homogeneous coordinates (for depth sorting)
    float m_ZhMin;            // Minimum Z in homogeneous coordinates
};

struct CKSceneGraphNode {
    explicit CKSceneGraphNode(RCK3dEntity *entity = nullptr);
    ~CKSceneGraphNode();

    void AddNode(CKSceneGraphNode *node);
    void RemoveNode(CKSceneGraphNode *node);
    void PrioritiesChanged();
    void SetRenderContextMask(CKDWORD mask, CKBOOL force);
    void EntityFlagsChanged(CKBOOL updateParent);
    void InvalidateBox(CKBOOL propagate);
    void SetPriority(CKWORD priority, CKBOOL propagate);
    CKBOOL IsToBeParsed();
    CKDWORD Rebuild();
    CKDWORD ComputeHierarchicalBox();

    // Rendering traversal
    void NoTestsTraversal(RCKRenderContext *dev, CKDWORD flags);
    void SetAsPotentiallyVisible();
    void SetAsInsideFrustum();
    void SortNodes();
    void ClearTransparentFlags();
    CKBOOL CheckHierarchyFrustum();

    RCK3dEntity *m_Entity;
    CKDWORD m_TimeFpsCalc;
    CKDWORD m_Flags;
    int m_Index;
    VxBbox m_Bbox;
    CKWORD m_Priority;
    CKWORD m_MaxPriority;
    CKDWORD m_RenderContextMask;
    CKDWORD m_EntityMask;
    CKSceneGraphNode *m_Parent;
    XArray<CKSceneGraphNode *> m_Children;
    int m_ChildToBeParsedCount;
};

struct CKSceneGraphRootNode : CKSceneGraphNode {
    void RenderTransparentObjects(RCKRenderContext *rc, CKDWORD flags);
    void SortTransparentObjects(RCKRenderContext *rc, CKDWORD flags);
    void AddTransparentObject(CKSceneGraphNode *node);

    void Clear();
    void Check();

    XClassArray<CKTransparentObject> m_TransparentObjects; // Transparent objects for sorting
};

#endif // CKSCENEGRAPH_H
