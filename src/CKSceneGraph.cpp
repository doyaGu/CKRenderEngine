#include "CKSceneGraph.h"
#include "CKRenderedScene.h"
#include "RCKRenderContext.h"
#include "RCKRenderManager.h"
#include "RCK3dEntity.h"
#include "RCKSprite3D.h"
#include "RCKPlace.h"
#include "CKRasterizer.h"
#include "VxRect.h"
#include "VxMath.h"
#include "VxMatrix.h"
#include "CKDebugLogger.h"

#define SCENEGRAPH_DEBUG_LOG(msg) CK_LOG("SceneGraph", msg)
#define SCENEGRAPH_DEBUG_LOG_FMT(fmt, ...) CK_LOG_FMT("SceneGraph", fmt, __VA_ARGS__)

// Helper function to swap two transparent objects
static void SwapTransparentObjects(CKTransparentObject *a, CKTransparentObject *b) {
    CKTransparentObject temp = *a;
    *a = *b;
    *b = temp;
}

// =====================================================
// CKSceneGraphNode Constructor/Destructor
// =====================================================

CKSceneGraphNode::CKSceneGraphNode(RCK3dEntity *entity) {
    // IDA @ 0x1000d6d0
    m_Entity = entity;
    m_TimeFpsCalc = 0;
    m_Flags = 0;
    m_Index = 0;
    // IDA: *(_DWORD *)&this->m_Priority = 0x27102710 means both Priority and MaxPriority = 10000
    m_Priority = 10000;
    m_MaxPriority = 10000;
    m_RenderContextMask = 0;
    m_EntityMask = 0;
    m_Parent = nullptr;
    m_ChildToBeParsedCount = 0;
}

CKSceneGraphNode::~CKSceneGraphNode() {
    // Clear children array
    SCENEGRAPH_DEBUG_LOG_FMT("~CKSceneGraphNode: Destroying node=%p entity=%p childrenCount=%d", 
                             this, m_Entity, m_Children.Size());
    m_Children.Clear();
}

// =====================================================
// CKSceneGraphNode Node Management
// =====================================================

void CKSceneGraphNode::AddNode(CKSceneGraphNode *node) {
    if (!node)
        return;

    // IDA @ 0x100770b5
    // Set the index to current children count
    node->m_Index = m_Children.Size();
    node->m_Parent = this;

    // Add to children array
    m_Children.PushBack(node);

    // Invalidate bounding box
    node->InvalidateBox(TRUE);

    // Force recalculation of render context mask hierarchy
    // IDA: if entity exists use m_InRenderContext, else use 0
    CKDWORD mask = 0;
    if (m_Entity)
        mask = m_Entity->m_InRenderContext;
    SetRenderContextMask(mask, TRUE);

    // Update entity flags
    node->EntityFlagsChanged(TRUE);

    // Check priorities
    if (node->m_MaxPriority > m_Priority || node->m_Priority > m_Priority) {
        PrioritiesChanged();
    }
}

void CKSceneGraphNode::RemoveNode(CKSceneGraphNode *node) {
    // IDA @ 0x1007715f
    node->m_Parent = nullptr;

    // Decrement child to be parsed count if node was in parsed section
    if (node->m_Index < m_ChildToBeParsedCount)
        --m_ChildToBeParsedCount;

    // Remove from children array using iterator-style removal
    int removeIndex = node->m_Index;
    m_Children.RemoveAt(removeIndex);  // XArray::Remove

    // Update indices of subsequent children
    int newIndex = removeIndex;
    for (CKSceneGraphNode **it = m_Children.Begin() + newIndex; it < m_Children.End(); ++it) {
        (*it)->m_Index = newIndex++;
    }

    // Update priorities
    PrioritiesChanged();

    // Invalidate bounding box
    InvalidateBox(TRUE);

    // Recalculate render context mask
    CKDWORD mask = 0;
    if (m_Entity)
        mask = m_Entity->m_InRenderContext;
    SetRenderContextMask(mask, TRUE);
}

void CKSceneGraphNode::PrioritiesChanged() {
    // IDA @ 0x10076fdd: Iterate upward updating m_Priority based on children
    for (CKSceneGraphNode *node = this; node != nullptr; node = node->m_Parent) {
        CKWORD priority = 0;
        
        // Find maximum priority among children
        CKSceneGraphNode **it = node->m_Children.Begin();
        CKSceneGraphNode **end = node->m_Children.End();
        while (it < end) {
            CKSceneGraphNode *child = *it;
            if (child->m_Priority > priority)
                priority = child->m_Priority;
            if (child->m_MaxPriority > priority)
                priority = child->m_MaxPriority;
            ++it;
        }
        
        // Stop if priority hasn't changed
        if (node->m_Priority == priority)
            break;
        
        // Update flags and priority
        node->m_Flags |= 0x10;  // Need sort flag
        node->m_Priority = priority;
    }
}

// Check if this node should be parsed for rendering
// IDA @ 0x1007766d
CKBOOL CKSceneGraphNode::IsToBeParsed() {
    if (m_EntityMask == 0)
        return FALSE;

    if ((m_Entity->m_MoveableFlags & VX_MOVEABLE_VISIBLE) != 0)
        return TRUE;

    if ((m_Entity->m_MoveableFlags & VX_MOVEABLE_HIERARCHICALHIDE) != 0)
        return FALSE;

    return m_ChildToBeParsedCount;
}

// IDA @ 0x1007784f
void CKSceneGraphNode::SetRenderContextMask(CKDWORD mask, CKBOOL force) {
    if (mask != m_RenderContextMask || force) {
        m_RenderContextMask = mask;
        
        // Update entity mask up the hierarchy
        for (CKSceneGraphNode *node = this; node != nullptr; node = node->m_Parent) {
            CKDWORD oldEntityMask = node->m_EntityMask;
            node->m_EntityMask = node->m_RenderContextMask;
            
            // Combine with children's masks using iterator style
            CKSceneGraphNode **it = node->m_Children.Begin();
            CKSceneGraphNode **end = node->m_Children.End();
            while (it < end) {
                node->m_EntityMask |= (*it)->m_EntityMask;
                ++it;
            }
            
            // Stop if mask didn't change or we're at root
            if (node->m_EntityMask == oldEntityMask || node->m_Parent == nullptr)
                break;
            
            // Update flags if mask changed (before next iteration)
            node->EntityFlagsChanged(TRUE);
        }
    }
}

// IDA @ 0x10077426
void CKSceneGraphNode::EntityFlagsChanged(CKBOOL updateParent) {
    // IDA @ 0x10077426 - Safety check: if no parent, nothing to do
    if (!m_Parent)
        return;
    
    SCENEGRAPH_DEBUG_LOG_FMT("EntityFlagsChanged: entity=%p index=%d parent=%p parentChildCount=%d parentToBeParsed=%d",
                              m_Entity, m_Index, m_Parent, m_Parent->m_Children.Size(),
                              m_Parent->m_ChildToBeParsedCount);
    
    // Check if parent has children
    if (m_Parent->m_Children.Size() == 0)
        return;
    
    CKBOOL shouldParse = IsToBeParsed();
    SCENEGRAPH_DEBUG_LOG_FMT("EntityFlagsChanged: entity=%p shouldParse=%d", m_Entity, shouldParse);
    
    if (shouldParse) {
        // Node should be parsed
        if (m_Index >= m_Parent->m_ChildToBeParsedCount) {
            // Need to move to parsed section
            if (m_Parent->m_Children.Size() == 1) {
                m_Parent->m_ChildToBeParsedCount = 1;
                SCENEGRAPH_DEBUG_LOG_FMT("EntityFlagsChanged: entity=%p ADDED to parse (single child), parent count now=%d",
                                         m_Entity, m_Parent->m_ChildToBeParsedCount);
                if (updateParent && m_Parent->m_Parent)
                    m_Parent->EntityFlagsChanged(TRUE);
            } else {
                if (m_Index > m_Parent->m_ChildToBeParsedCount) {
                    // Swap with first non-parsed node
                    int swapIndex = m_Parent->m_ChildToBeParsedCount;
                    CKSceneGraphNode *other = m_Parent->m_Children[swapIndex];
                    m_Parent->m_Children[m_Index] = other;
                    m_Parent->m_Children[swapIndex] = this;
                    other->m_Index = m_Index;
                    m_Index = swapIndex;
                }
                ++m_Parent->m_ChildToBeParsedCount;
                SCENEGRAPH_DEBUG_LOG_FMT("EntityFlagsChanged: entity=%p ADDED to parse, parent count now=%d",
                                         m_Entity, m_Parent->m_ChildToBeParsedCount);
                if (updateParent && m_Parent->m_Parent)
                    m_Parent->EntityFlagsChanged(TRUE);
            }
            m_Parent->m_Flags |= 0x10; // Need sort
        }
    } else {
        // Node should not be parsed
        if (m_Index < m_Parent->m_ChildToBeParsedCount) {
            // Need to move out of parsed section
            if (m_Index < m_Parent->m_ChildToBeParsedCount - 1) {
                // Swap with last parsed node
                int swapIndex = m_Parent->m_ChildToBeParsedCount - 1;
                CKSceneGraphNode *other = m_Parent->m_Children[swapIndex];
                m_Parent->m_Children[m_Index] = other;
                m_Parent->m_Children[swapIndex] = this;
                other->m_Index = m_Index;
                m_Index = swapIndex;
            }
            --m_Parent->m_ChildToBeParsedCount;
            if (updateParent && m_Parent->m_Parent)
                m_Parent->EntityFlagsChanged(TRUE);
            m_Parent->m_Flags |= 0x10; // Need sort
        }
    }
}

// IDA @ 0x10076f50
void CKSceneGraphNode::SetPriority(CKWORD priority, CKBOOL propagate) {
    // Clamp priority to [-10000, 10000] range
    int p = (int)priority;
    if (p < -10000)
        p = -10000;
    else if (p > 10000)
        p = 10000;
    
    // Store as offset (priority + 10000) so range becomes [0, 20000]
    CKWORD newMaxPriority = (CKWORD)(p + 10000);
    
    if (m_MaxPriority == newMaxPriority || !m_Parent) {
        // No change or no parent - just update
        m_MaxPriority = newMaxPriority;
    } else {
        // Priority changed and has parent - mark parent for resort
        m_Parent->m_Flags |= 0x10;
        m_MaxPriority = newMaxPriority;
        m_Parent->PrioritiesChanged();
    }
}

void CKSceneGraphNode::InvalidateBox(CKBOOL propagate) {
    // Clear the "box valid" flags (0xC = 0x4 | 0x8)
    m_Flags &= ~0xC;

    // Propagate to parents if requested
    if (propagate) {
        for (CKSceneGraphNode *parent = m_Parent;
             parent && (parent->m_Flags & 0x8) != 0;
             parent = parent->m_Parent) {
            parent->m_Flags &= ~0xC;
        }
    }
}

// IDA @ 0x1000d250: Clear visibility test flags (bits 0,1)
void CKSceneGraphNode::SetAsPotentiallyVisible() {
    m_Flags &= ~3u; // Clear bits 0 and 1
}

// IDA @ 0x1000d270: Set as inside frustum (bit 0)
void CKSceneGraphNode::SetAsInsideFrustum() {
    m_Flags |= 1u; // Set bit 0
}

void CKSceneGraphNode::SortNodes() {
    m_Flags &= ~0x10;

    if (m_ChildToBeParsedCount < 2)
        return;

    CKSceneGraphNode **begin = m_Children.Begin();
    CKSceneGraphNode **end = begin + m_ChildToBeParsedCount;

    CKBOOL noSwaps = TRUE;
    for (CKSceneGraphNode **i = begin + 1; i < end; ++i) {
        for (CKSceneGraphNode **curr = end - 1; curr >= i; --curr) {
            CKSceneGraphNode **prev = curr - 1;

            CKDWORD currPriority = *reinterpret_cast<CKDWORD *>(&(*curr)->m_Priority);
            CKDWORD prevPriority = *reinterpret_cast<CKDWORD *>(&(*prev)->m_Priority);

            if (currPriority > prevPriority) {
                CKSceneGraphNode *tmp = *curr;
                *curr = *prev;
                *prev = tmp;

                int prevIndex = (*curr)->m_Index;
                (*curr)->m_Index = (*prev)->m_Index;
                (*prev)->m_Index = prevIndex;

                noSwaps = FALSE;
            }
        }

        if (noSwaps)
            break;
        noSwaps = TRUE;
    }
}

void CKSceneGraphNode::ClearTransparentFlags() {
    // IDA @ 0x100789A0
    // Clear flag 0x01 and set flag 0x02
    m_Flags &= ~1u;
    m_Flags |= 2u;
    
    // Only traverse children that are to be parsed (not all children)
    int i = 0;
    CKSceneGraphNode **it = m_Children.Begin();
    while (i < m_ChildToBeParsedCount) {
        (*it)->ClearTransparentFlags();
        ++it;
        ++i;
    }
}

CKBOOL CKSceneGraphNode::CheckHierarchyFrustum() {
    // IDA @ 0x1000D290: return this->m_Flags & 1
    // Check flag 0x01 to determine if hierarchy frustum test passed
    return (m_Flags & 0x01) != 0;
}

void CKSceneGraphNode::NoTestsTraversal(RCKRenderContext *dev, CKDWORD flags) {
    CKBOOL clipRectSet = FALSE;

    dev->m_SceneTraversalCalls++;

    SetAsPotentiallyVisible();
    SetAsInsideFrustum();

    if ((m_Flags & 0x10) != 0)
        SortNodes();

    if (m_Entity->GetClassID() == CKCID_PLACE) {
        RCKPlace *place = (RCKPlace *)m_Entity;
        VxRect &clip = place->ViewportClip();
        if (!clip.IsNull()) {
            VxRect unit(0.0f, 0.0f, 1.0f, 1.0f);
            if (clip.IsInside(unit)) {
                clipRectSet = TRUE;

                VxRect rect = clip;
                float right = (float)dev->m_ViewportData.ViewX + (float)dev->m_ViewportData.ViewWidth;
                float bottom = (float)dev->m_ViewportData.ViewY + (float)dev->m_ViewportData.ViewHeight;
                VxRect viewport((float)dev->m_ViewportData.ViewX, (float)dev->m_ViewportData.ViewY, right, bottom);
                rect.TransformFromHomogeneous(viewport);
                dev->SetClipRect(&rect);
            }
        }
    }

    if (m_Entity->GetClassID() == CKCID_CHARACTER)
        m_Entity->m_MoveableFlags |= 0x20000000;

    if ((m_EntityMask & dev->m_MaskFree) != 0 && m_Entity->IsToBeRendered()) {
        if (m_Entity->IsToBeRenderedLast()) {
            m_TimeFpsCalc = dev->m_TimeFpsCalc;
            dev->m_RenderManager->m_SceneGraphRootNode.AddTransparentObject(this);
        } else {
            dev->m_Stats.SceneTraversalTime += dev->m_SceneTraversalTimeProfiler.Current();
            m_Entity->Render((CKRenderContext *)dev, flags);
            dev->m_SceneTraversalTimeProfiler.Reset();
        }
    }

    CKSceneGraphNode **it = m_Children.Begin();
    for (int parsed = 0; parsed < m_ChildToBeParsedCount; ++parsed, ++it) {
        if ((dev->m_MaskFree & (*it)->m_EntityMask) != 0)
            (*it)->NoTestsTraversal(dev, flags);
    }

    if (clipRectSet) {
        dev->m_RasterizerContext->SetViewport(&dev->m_ViewportData);
        dev->m_RasterizerContext->SetTransformMatrix(VXMATRIX_PROJECTION, dev->m_ProjectionMatrix);
    }
}

void CKSceneGraphRootNode::RenderTransparentObjects(RCKRenderContext *rc, CKDWORD flags) {
    rc->m_SceneTraversalCalls++;

    SetAsPotentiallyVisible();

    CKBOOL clipRectSet = FALSE;
    CKBOOL hierarchyVisible = FALSE;

    if (m_ChildToBeParsedCount != 0) {
        if ((m_Flags & 0x10) != 0)
            SortNodes();

        if (m_Entity) {
            m_Entity->ModifyMoveableFlags(0x20, 0);

            if (!m_Entity->IsInViewFrustrumHierarchic((CKRenderContext *)rc)) {
                if (m_Entity->GetClassID() == CKCID_CHARACTER) {
                    VxBbox expanded = m_Bbox;
                    expanded.Max *= 2.0f;
                    expanded.Min *= 2.0f;

                    if (rc->m_RasterizerContext->ComputeBoxVisibility(expanded, TRUE, nullptr))
                        m_Entity->m_MoveableFlags |= 0x20000000;
                }

                ClearTransparentFlags();
                return;
            }

            if (m_Entity->GetClassID() == CKCID_PLACE) {
                RCKPlace *place = (RCKPlace *)m_Entity;
                VxRect &clip = place->ViewportClip();
                if (!clip.IsNull()) {
                    VxRect unit(0.0f, 0.0f, 1.0f, 1.0f);
                    if (clip.IsInside(unit)) {
                        clipRectSet = TRUE;

                        VxRect rect = clip;
                        float right = (float)rc->m_ViewportData.ViewX + (float)rc->m_ViewportData.ViewWidth;
                        float bottom = (float)rc->m_ViewportData.ViewY + (float)rc->m_ViewportData.ViewHeight;
                        VxRect viewport((float)rc->m_ViewportData.ViewX, (float)rc->m_ViewportData.ViewY, right, bottom);
                        rect.TransformFromHomogeneous(viewport);
                        rc->SetClipRect(&rect);
                    }
                }
            }

            if (m_Entity->GetClassID() == CKCID_CHARACTER)
                m_Entity->m_MoveableFlags |= 0x20000000;

            hierarchyVisible = CheckHierarchyFrustum();

            if ((rc->m_MaskFree & m_RenderContextMask) != 0 && m_Entity->IsToBeRendered()) {
                if (m_Entity->IsToBeRenderedLast()) {
                    if (hierarchyVisible || m_Entity->IsInViewFrustrum((CKRenderContext *)rc, flags)) {
                        m_TimeFpsCalc = rc->m_TimeFpsCalc;
                        rc->m_RenderManager->m_SceneGraphRootNode.AddTransparentObject(this);
                    }
                } else {
                    rc->m_Stats.SceneTraversalTime += rc->m_SceneTraversalTimeProfiler.Current();
                    m_Entity->Render((CKRenderContext *)rc, flags);
                    rc->m_SceneTraversalTimeProfiler.Reset();
                }
            }
        }

        if (hierarchyVisible) {
            CKSceneGraphNode **it = m_Children.Begin();
            for (int parsed = 0; parsed < m_ChildToBeParsedCount; ++parsed, ++it) {
                if ((rc->m_MaskFree & (*it)->m_EntityMask) != 0)
                    (*it)->NoTestsTraversal(rc, flags);
            }
        } else {
            CKSceneGraphRootNode **it = (CKSceneGraphRootNode **)m_Children.Begin();
            for (int parsed = 0; parsed < m_ChildToBeParsedCount; ++parsed, ++it) {
                if ((rc->m_MaskFree & (*it)->m_EntityMask) != 0)
                    (*it)->RenderTransparentObjects(rc, flags);
            }
        }

        if (clipRectSet) {
            rc->m_RasterizerContext->SetViewport(&rc->m_ViewportData);
            rc->m_RasterizerContext->SetTransformMatrix(VXMATRIX_PROJECTION, rc->m_ProjectionMatrix);
        }

        return;
    }

    if (!m_Entity)
        return;

    if (!m_Entity->IsToBeRendered())
        return;

    if ((m_RenderContextMask & rc->m_MaskFree) == 0)
        return;

    if (!m_Entity->IsInViewFrustrum((CKRenderContext *)rc, flags))
        return;

    if (m_Entity->IsToBeRenderedLast()) {
        m_TimeFpsCalc = rc->m_TimeFpsCalc;
        rc->m_RenderManager->m_SceneGraphRootNode.AddTransparentObject(this);
    } else {
        rc->m_Stats.SceneTraversalTime += rc->m_SceneTraversalTimeProfiler.Current();
        CKDWORD renderFlags = flags | 0x100;
        m_Entity->Render((CKRenderContext *)rc, renderFlags);
        rc->m_SceneTraversalTimeProfiler.Reset();
    }
}

void CKSceneGraphRootNode::SortTransparentObjects(RCKRenderContext *dev, CKDWORD flags) {
    int count = m_TransparentObjects.Size();
    if (count == 0)
        return;

    CKDWORD timeFpsCalc = dev->m_TimeFpsCalc;

    if (dev->m_RenderManager->m_SortTransparentObjects.Value && count > 1) {
        dev->m_TransparentObjectsSortTimeProfiler.Reset();

        dev->m_RasterizerContext->UpdateMatrices(2);
        VxMatrix viewProj = dev->m_RasterizerContext->m_ViewProjMatrix;

        CKTransparentObject *it = m_TransparentObjects.Begin();
        while (it != m_TransparentObjects.End()) {
            CKSceneGraphNode *node = it->m_Node;

            if ((dev->m_MaskFree & node->m_RenderContextMask) != 0 && node->m_TimeFpsCalc == timeFpsCalc) {
                RCK3dEntity *entity = node->m_Entity;

                if (CKIsChildClassOf(entity, CKCID_SPRITE3D)) {
                    entity->ModifyMoveableFlags(0, 4);
                    entity->UpdateBox(TRUE);
                }

                VxMatrix mvp;
                Vx3DMultiplyMatrix4(mvp, viewProj, entity->GetWorldMatrix());

                const VxBbox &bbox = entity->GetBoundingBox(FALSE);
                VxProjectBoxZExtents(mvp, bbox, it->m_ZhMin, it->m_ZhMax);

                ++it;
            } else {
                node->m_Flags &= ~0x20;
                it = m_TransparentObjects.Remove(it);
            }
        }

        if (m_TransparentObjects.Size() > 0) {
            CKTransparentObject *begin = m_TransparentObjects.Begin();
            CKTransparentObject *end = m_TransparentObjects.End();

            CKBOOL noSwaps = TRUE;
            for (CKTransparentObject *i = begin + 1; i != end; ++i) {
                for (CKTransparentObject *k = end - 1; k != (i - 1); --k) {
                    CKTransparentObject *prev = k - 1;

                    if (k->m_Node->m_MaxPriority > prev->m_Node->m_MaxPriority) {
                        SwapTransparentObjects(k, prev);
                        noSwaps = FALSE;
                        continue;
                    }

                    if (k->m_Node->m_MaxPriority == prev->m_Node->m_MaxPriority && prev->m_ZhMax < k->m_ZhMin) {
                        if (k->m_ZhMax < prev->m_ZhMin) {
                            int cmp = (k->m_ZhMax > prev->m_ZhMax) ? 1 : ((k->m_ZhMax < prev->m_ZhMax) ? -1 : 0);
                            if (cmp < 0) {
                                SwapTransparentObjects(k, prev);
                                noSwaps = FALSE;
                                continue;
                            }
                        }

                        if (prev->m_ZhMax + 0.00000011920929f < k->m_ZhMax) {
                            SwapTransparentObjects(k, prev);
                            noSwaps = FALSE;
                        }
                    }
                }

                if (noSwaps)
                    break;
                noSwaps = TRUE;
            }

            dev->m_Stats.TransparentObjectsSortTime = dev->m_TransparentObjectsSortTimeProfiler.Current();

            for (CKTransparentObject *renderIt = m_TransparentObjects.Begin(); renderIt != m_TransparentObjects.End(); ++renderIt)
                renderIt->m_Node->m_Entity->Render((CKRenderContext *)dev, flags);
        }
    } else {
        CKTransparentObject *it = m_TransparentObjects.Begin();
        while (it != m_TransparentObjects.End()) {
            CKSceneGraphNode *node = it->m_Node;

            if ((dev->m_MaskFree & node->m_RenderContextMask) != 0) {
                if (node->m_TimeFpsCalc == timeFpsCalc) {
                    node->m_Entity->Render((CKRenderContext *)dev, flags);
                    ++it;
                } else {
                    node->m_Flags &= ~0x20;
                    it = m_TransparentObjects.Remove(it);
                }
            } else {
                ++it;
            }
        }
    }
}

void CKSceneGraphRootNode::AddTransparentObject(CKSceneGraphNode *node) {
    // Check if already in the list (flag 0x20)
    if ((node->m_Flags & 0x20) == 0) {
        CKTransparentObject obj;
        obj.m_Node = node;
        obj.m_ZhMax = 0.0f;
        obj.m_ZhMin = 0.0f;
        m_TransparentObjects.PushBack(obj);
        node->m_Flags |= 0x20; // Mark as added to transparent list
    }
}

void CKSceneGraphRootNode::Clear() {
    m_Children.Clear();
    m_ChildToBeParsedCount = 0;
    m_Index = 0;
    m_Flags = 0;
    m_Priority = 10000;
    m_MaxPriority = 10000;
    m_TransparentObjects.Resize(0);
}

void CKSceneGraphRootNode::Check() {
    CKTransparentObject *it = m_TransparentObjects.Begin();
    while (it != m_TransparentObjects.End()) {
        CKSceneGraphNode *node = it->m_Node;
        RCK3dEntity *entity = node->m_Entity;
        if (entity && entity->GetObjectFlags() & 0x10) {
            it = m_TransparentObjects.Remove(it);
        } else {
            ++it;
        }
    }
}

CKDWORD CKSceneGraphNode::Rebuild() {
    int childCount = m_Children.Size();
    m_EntityMask = m_RenderContextMask;

    if (childCount == 0)
        return m_EntityMask;

    if (childCount == 1) {
        CKSceneGraphNode *child = m_Children[0];
        m_EntityMask |= child->Rebuild();
        m_ChildToBeParsedCount = child->IsToBeParsed() ? 1 : 0;
        return m_EntityMask;
    }

    m_Flags |= 0x10;

    XArray<CKSceneGraphNode *> reordered;
    reordered.Resize(childCount);

    int parsedIndex = 0;
    int unparsedIndex = childCount - 1;

    for (CKSceneGraphNode **it = m_Children.Begin(); it < m_Children.End(); ++it) {
        CKSceneGraphNode *child = *it;
        m_EntityMask |= child->Rebuild();

        if (child->IsToBeParsed()) {
            child->m_Index = parsedIndex;
            reordered[parsedIndex++] = child;
        } else {
            child->m_Index = unparsedIndex;
            reordered[unparsedIndex--] = child;
        }
    }

    m_ChildToBeParsedCount = parsedIndex;

    for (int i = 0; i < childCount; ++i)
        m_Children[i] = reordered[i];

    return m_EntityMask;
}

CKDWORD CKSceneGraphNode::ComputeHierarchicalBox() {
    if ((m_Flags & 0x08) != 0)
        return m_Flags & 0x04;

    m_Flags |= 0x08;

    m_Entity->UpdateBox(TRUE);

    if ((m_Entity->m_MoveableFlags & VX_MOVEABLE_BOXVALID) != 0) {
        m_Bbox = m_Entity->m_WorldBoundingBox;

        CKSceneGraphNode **it = m_Children.Begin();
        while (it < m_Children.End()) {
            if ((*it)->ComputeHierarchicalBox())
                m_Bbox.Merge((*it)->m_Bbox);
            ++it;
        }

        m_Flags |= 0x04;
    } else {
        CKSceneGraphNode **it = m_Children.Begin();
        while (it < m_Children.End()) {
            if ((*it)->ComputeHierarchicalBox()) {
                m_Bbox = (*it)->m_Bbox;
                m_Flags |= 0x04;
                ++it;
                break;
            }
            ++it;
        }

        while (it < m_Children.End()) {
            if ((*it)->ComputeHierarchicalBox())
                m_Bbox.Merge((*it)->m_Bbox);
            ++it;
        }
    }

    return m_Flags & 0x04;
}
