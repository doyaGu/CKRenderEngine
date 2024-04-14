#ifndef CKSCENEGRAPH_H
#define CKSCENEGRAPH_H

#include "CKRenderEngineTypes.h"

struct CKSceneGraphNode {
public:
    void Clear();
    void Check();

protected:
    RCK3dEntity *m_Entity;
    CKDWORD m_field_4;
    CKDWORD m_Flag;
    CKDWORD m_ChildCount;
    VxBbox m_Bbox;
    CKWORD m_field_28;
    CKWORD m_Priority;
    CKDWORD m_field_2C;
    CKDWORD m_field_30;
    CKSceneGraphNode *m_Parent;
    XArray<CKSceneGraphNode> m_Children;
    CKDWORD m_field_44;
};

struct CKSceneGraphRootNode : public CKSceneGraphNode {
    CKDWORD field_48;
};

#endif // CKSCENEGRAPH_H
