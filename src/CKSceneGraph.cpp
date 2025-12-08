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
    m_Entity = entity;
    m_TimeFpsCalc = 0;
    m_Flags = 0;
    m_Index = 0;
    m_Priority = 10000;  // 0x2710 - default priority from IDA
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
    
    SCENEGRAPH_DEBUG_LOG_FMT("AddNode START: node=%p entity=%p parent=%p parentEntity=%p parentMask=0x%X childrenBefore=%d",
                             node, node->m_Entity, this, m_Entity, m_RenderContextMask, m_Children.Size());
    
    // Set the index to current children count
    node->m_Index = m_Children.Size();
    node->m_Parent = this;
    
    // Add to children array
    m_Children.PushBack(node);
    
    // Invalidate bounding box
    node->InvalidateBox(TRUE);
    
    // Force recalculation of render context mask hierarchy (IDA @ 0x100770b5 line 10-14)
    // For root node (no entity), keep its RenderContextMask (0xFFFFFFFF), don't reset to 0
    CKDWORD maskForRecalc = m_RenderContextMask;  // Default: use current mask
    if (m_Entity) {
        maskForRecalc = m_Entity->m_InRenderContext;  // Override if entity exists
    }
    SetRenderContextMask(maskForRecalc, TRUE);  // Recalculate on PARENT
    
    // Update entity flags
    node->EntityFlagsChanged(TRUE);
    
    // Check priorities
    if (node->m_MaxPriority > m_Priority || node->m_Priority > m_Priority) {
        PrioritiesChanged();
    }
    
    SCENEGRAPH_DEBUG_LOG_FMT("AddNode: Added node (entity=%p) to parent (entity=%p), index=%d, children=%d",
                             node->m_Entity, m_Entity, node->m_Index, m_Children.Size());
}

void CKSceneGraphNode::RemoveNode(CKSceneGraphNode *node) {
    if (!node || node->m_Parent != this)
        return;
    
    // Find and remove from children array
    int index = node->m_Index;
    if (index >= 0 && index < m_Children.Size() && m_Children[index] == node) {
        // Use RemoveAt to remove by index
        for (int i = index; i < m_Children.Size() - 1; i++) {
            m_Children[i] = m_Children[i + 1];
        }
        m_Children.Resize(m_Children.Size() - 1);
        
        // Update indices of subsequent children
        for (int i = index; i < m_Children.Size(); i++) {
            if (m_Children[i])
                m_Children[i]->m_Index = i;
        }
    }
    
    node->m_Parent = nullptr;
    node->m_Index = -1;
}

void CKSceneGraphNode::PrioritiesChanged() {
    // Update max priority based on children
    CKWORD maxPriority = m_Priority;
    for (int i = 0; i < m_Children.Size(); i++) {
        if (m_Children[i]) {
            if (m_Children[i]->m_Priority > maxPriority)
                maxPriority = m_Children[i]->m_Priority;
            if (m_Children[i]->m_MaxPriority > maxPriority)
                maxPriority = m_Children[i]->m_MaxPriority;
        }
    }
    m_MaxPriority = maxPriority;
    
    // Propagate to parent
    if (m_Parent) {
        m_Parent->PrioritiesChanged();
    }
}

void CKSceneGraphNode::Clear() {
    SCENEGRAPH_DEBUG_LOG_FMT("Clear: Clearing node=%p entity=%p childrenCount=%d",
                             this, m_Entity, m_Children.Size());

    // Recursively delete children so no orphan nodes remain after a clear
    for (int i = 0; i < m_Children.Size(); ++i) {
        CKSceneGraphNode *child = m_Children[i];
        if (!child)
            continue;

        child->Clear();

        if (child->m_Entity && child->m_Entity->m_SceneGraphNode == child) {
            child->m_Entity->m_SceneGraphNode = nullptr;
        }

        delete child;
    }

    m_Children.Clear();
    m_ChildToBeParsedCount = 0;

    if (m_Entity && m_Entity->m_SceneGraphNode == this) {
        m_Entity->m_SceneGraphNode = nullptr;
    }

    m_Entity = nullptr;
    m_TimeFpsCalc = 0;
    m_Flags = 0;
    m_Index = -1;
    m_Priority = 0;
    m_MaxPriority = 0;
    m_RenderContextMask = 0;
    m_EntityMask = 0;
    m_Parent = nullptr;
}

void CKSceneGraphNode::Check() {
    // Validation/debug check
}

// Check if this node should be parsed for rendering
CKBOOL CKSceneGraphNode::IsToBeParsed() {
    if (m_EntityMask == 0) {
        SCENEGRAPH_DEBUG_LOG_FMT("IsToBeParsed: FALSE - m_EntityMask=0, node=%p entity=%p", this, m_Entity);
        return FALSE;
    }
    
    if (m_Entity) {
        if ((m_Entity->m_MoveableFlags & VX_MOVEABLE_VISIBLE) != 0) {
            SCENEGRAPH_DEBUG_LOG_FMT("IsToBeParsed: TRUE - VX_MOVEABLE_VISIBLE set, node=%p entity=%p flags=0x%X", 
                                     this, m_Entity, m_Entity->m_MoveableFlags);
            return TRUE;
        }
        if ((m_Entity->m_MoveableFlags & VX_MOVEABLE_HIERARCHICALHIDE) != 0) {
            SCENEGRAPH_DEBUG_LOG_FMT("IsToBeParsed: FALSE - HIERARCHICALHIDE set, node=%p entity=%p", this, m_Entity);
            return FALSE;
        }
    }
    
    CKBOOL result = m_ChildToBeParsedCount > 0;
    SCENEGRAPH_DEBUG_LOG_FMT("IsToBeParsed: %d - based on childCount=%d, node=%p entity=%p", 
                             result, m_ChildToBeParsedCount, this, m_Entity);
    return result;
}

void CKSceneGraphNode::SetRenderContextMask(CKDWORD mask, CKBOOL force) {
    SCENEGRAPH_DEBUG_LOG_FMT("SetRenderContextMask START: node=%p entity=%p oldRenderMask=0x%X newMask=0x%X force=%d",
                             this, m_Entity, m_RenderContextMask, mask, force);
    
    if (mask != m_RenderContextMask || force) {
        m_RenderContextMask = mask;
        
        // Update entity mask up the hierarchy
        for (CKSceneGraphNode *node = this; node != nullptr; node = node->m_Parent) {
            CKDWORD oldEntityMask = node->m_EntityMask;
            node->m_EntityMask = node->m_RenderContextMask;
            
            // Combine with children's masks
            for (int i = 0; i < node->m_Children.Size(); i++) {
                if (node->m_Children[i]) {
                    SCENEGRAPH_DEBUG_LOG_FMT("SetRenderContextMask: Combining child[%d] EntityMask=0x%X", 
                                             i, node->m_Children[i]->m_EntityMask);
                    node->m_EntityMask |= node->m_Children[i]->m_EntityMask;
                }
            }
            
            SCENEGRAPH_DEBUG_LOG_FMT("SetRenderContextMask: node=%p entity=%p oldEntityMask=0x%X → newEntityMask=0x%X parent=%p",
                                     node, node->m_Entity, oldEntityMask, node->m_EntityMask, node->m_Parent);
            
            // Stop if mask didn't change or we're at root (IDA @ 0x1007784f line 15-16)
            if (node->m_EntityMask == oldEntityMask || node->m_Parent == nullptr) {
                SCENEGRAPH_DEBUG_LOG_FMT("SetRenderContextMask: BREAK - maskSame=%d isRoot=%d",
                                         node->m_EntityMask == oldEntityMask, node->m_Parent == nullptr);
                break;
            }
            
            // Update flags if mask changed (called BEFORE break check per IDA line 17)
            SCENEGRAPH_DEBUG_LOG_FMT("SetRenderContextMask: Calling EntityFlagsChanged on node=%p", node);
            node->EntityFlagsChanged(TRUE);
        }
    } else {
        SCENEGRAPH_DEBUG_LOG_FMT("SetRenderContextMask: SKIPPED - mask same and not forced");
    }
}

void CKSceneGraphNode::EntityFlagsChanged(CKBOOL updateParent) {
    SCENEGRAPH_DEBUG_LOG_FMT("EntityFlagsChanged: node=%p entity=%p IsToBeParsed=%d index=%d parentChildCount=%d",
                             this, m_Entity, IsToBeParsed(), m_Index, 
                             m_Parent ? m_Parent->m_ChildToBeParsedCount : -1);
    
    if (!m_Parent || m_Parent->m_Children.Size() == 0)
        return;
    
    if (IsToBeParsed()) {
        // Node should be parsed
        if (m_Index >= m_Parent->m_ChildToBeParsedCount) {
            // Need to move to parsed section
            if (m_Parent->m_Children.Size() == 1) {
                m_Parent->m_ChildToBeParsedCount = 1;
                SCENEGRAPH_DEBUG_LOG_FMT("EntityFlagsChanged: Updated parent childCount to 1");
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
                    SCENEGRAPH_DEBUG_LOG_FMT("EntityFlagsChanged: Swapped node at %d with %d", swapIndex, m_Index);
                }
                m_Parent->m_ChildToBeParsedCount++;
                SCENEGRAPH_DEBUG_LOG_FMT("EntityFlagsChanged: Incremented parent childCount to %d", m_Parent->m_ChildToBeParsedCount);
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
                SCENEGRAPH_DEBUG_LOG_FMT("EntityFlagsChanged: Swapped node out, old index %d new index %d", m_Index, swapIndex);
            }
            m_Parent->m_ChildToBeParsedCount--;
            SCENEGRAPH_DEBUG_LOG_FMT("EntityFlagsChanged: Decremented parent childCount to %d", m_Parent->m_ChildToBeParsedCount);
            if (updateParent && m_Parent->m_Parent)
                m_Parent->EntityFlagsChanged(TRUE);
            m_Parent->m_Flags |= 0x10; // Need sort
        }
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

void CKSceneGraphNode::SetAsPotentiallyVisible() {
    m_Flags |= 0x01; // Mark as potentially visible
}

void CKSceneGraphNode::SetAsInsideFrustum() {
    m_Flags |= 0x02; // Mark as inside frustum
}

void CKSceneGraphNode::SortNodes() {
    // Sort children by priority for rendering order
    // Simple bubble sort for now
    int n = m_ChildToBeParsedCount;
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < n - i - 1; j++) {
            if (m_Children[j]->m_Priority > m_Children[j + 1]->m_Priority) {
                CKSceneGraphNode *temp = m_Children[j];
                m_Children[j] = m_Children[j + 1];
                m_Children[j + 1] = temp;
            }
        }
    }
    m_Flags &= ~0x10; // Clear sort needed flag
}

void CKSceneGraphNode::ClearTransparentFlags() {
    // Clear the "in transparent list" flag for this node and all children
    m_Flags &= ~0x20;
    for (int i = 0; i < m_Children.Size(); i++) {
        if (m_Children[i])
            m_Children[i]->ClearTransparentFlags();
    }
}

CKBOOL CKSceneGraphNode::CheckHierarchyFrustum() {
    // Check if entity passes hierarchy frustum test
    // Returns TRUE if the entity is fully inside the frustum
    if ((m_Flags & 0x02) != 0) // Already marked as inside frustum
        return TRUE;
    return FALSE;
}

void CKSceneGraphNode::NoTestsTraversal(RCKRenderContext *dev, CKDWORD flags) {
    CKBOOL clipRectSet = FALSE;

    // Update stats
    dev->m_SceneTraversalCalls++;

    // Mark visibility
    SetAsPotentiallyVisible();
    SetAsInsideFrustum();

    // Sort nodes if needed
    if ((m_Flags & 0x10) != 0)
        SortNodes();

    // Handle Place viewport clipping
    if (m_Entity && m_Entity->GetClassID() == CKCID_PLACE) {
        RCKPlace *place = (RCKPlace *) m_Entity;
        VxRect &clip = place->ViewportClip();
        if (!clip.IsNull()) {
            // Check if clip rect is valid (within 0-1 range)
            VxRect unitRect(0.0f, 0.0f, 1.0f, 1.0f);
            // TODO: Transform and set clip rect
            clipRectSet = TRUE;
        }
    }

    // Mark character as rendered
    if (m_Entity && m_Entity->GetClassID() == CKCID_CHARACTER) {
        m_Entity->ModifyMoveableFlags(VX_MOVEABLE_CHARACTERRENDERED, 0);
    }

    // Render the entity
    if ((dev->m_MaskFree & m_RenderContextMask) != 0 && m_Entity && m_Entity->IsToBeRendered()) {
        m_Entity->ModifyMoveableFlags(0, VX_MOVEABLE_EXTENTSUPTODATE);

        if (m_Entity->IsToBeRenderedLast()) {
            // Queue for render last pass (transparent objects)
            m_TimeFpsCalc = dev->m_TimeFpsCalc;
            CKSceneGraphRootNode *root = &dev->m_RenderManager->m_CKSceneGraphRootNode;
            root->AddTransparentObject(this);
        } else {
            // Update timing stats
            dev->m_Stats.SceneTraversalTime += dev->m_SceneTraversalTimeProfiler.Current();

            // Render the entity
            m_Entity->Render((CKRenderContext *) dev, flags);

            dev->m_SceneTraversalTimeProfiler.Reset();
        }
    }

    // Traverse children
    for (int i = 0; i < m_ChildToBeParsedCount; i++) {
        CKSceneGraphNode *child = m_Children[i];
        if (child && (dev->m_MaskFree & child->m_EntityMask) != 0) {
            child->NoTestsTraversal(dev, flags);
        }
    }

    // Restore viewport if we changed it
    if (clipRectSet) {
        dev->m_RasterizerContext->SetViewport(&dev->m_ViewportData);
        dev->m_RasterizerContext->SetTransformMatrix(VXMATRIX_PROJECTION, dev->m_ProjectionMatrix);
    }
}

void CKSceneGraphRootNode::RenderTransparents(RCKRenderContext *rc, CKDWORD flags) {
    SCENEGRAPH_DEBUG_LOG_FMT("RenderTransparents: node=%p entity=%p childCount=%d", 
                             this, m_Entity, m_ChildToBeParsedCount);
    
    // Update stats
    rc->m_SceneTraversalCalls++;

    // Mark as potentially visible
    SetAsPotentiallyVisible();

    // If no children to parse, handle leaf node
    if (m_ChildToBeParsedCount == 0) {
        if (m_Entity && m_Entity->IsToBeRendered() &&
            (rc->m_MaskFree & m_RenderContextMask) != 0 &&
            m_Entity->IsInViewFrustrum((CKRenderContext *) rc, flags)) {
            if (m_Entity->IsToBeRenderedLast()) {
                m_TimeFpsCalc = rc->m_TimeFpsCalc;
                rc->m_RenderManager->m_CKSceneGraphRootNode.AddTransparentObject(this);
                SCENEGRAPH_DEBUG_LOG_FMT("Leaf: Added transparent, entity=%p", m_Entity);
            } else {
                rc->m_Stats.SceneTraversalTime += rc->m_SceneTraversalTimeProfiler.Current();

                // Set flag bit for transparent render
                CKDWORD renderFlags = flags;
                renderFlags |= 0x100; // BYTE1(flags) | 1

                SCENEGRAPH_DEBUG_LOG_FMT("Leaf: Calling Render(), entity=%p flags=0x%X", m_Entity, renderFlags);
                m_Entity->Render((CKRenderContext *) rc, renderFlags);

                rc->m_SceneTraversalTimeProfiler.Reset();
            }
        } else {
            SCENEGRAPH_DEBUG_LOG_FMT("Leaf: SKIPPED render - entity=%p IsToBeRendered=%d MaskTest=%d InFrustrum=%d", 
                                     m_Entity, m_Entity ? m_Entity->IsToBeRendered() : -1,
                                     (rc->m_MaskFree & m_RenderContextMask) != 0,
                                     m_Entity ? m_Entity->IsInViewFrustrum((CKRenderContext *) rc, flags) : -1);
        }
        return;
    }

    // Sort nodes if needed
    if ((m_Flags & 0x10) != 0)
        SortNodes();

    CKBOOL clipRectSet = FALSE;
    CKBOOL entityInFrustum = FALSE;

    if (m_Entity) {
        // Clear extents flag
        m_Entity->ModifyMoveableFlags(0, VX_MOVEABLE_EXTENTSUPTODATE);

        // Check view frustum
        if (!m_Entity->IsInViewFrustrumHierarchic((CKRenderContext *) rc)) {
            // Special handling for characters - expand bbox
            if (m_Entity->GetClassID() == CKCID_CHARACTER) {
                VxBbox expandedBox = m_Bbox;
                expandedBox.Max *= 2.0f;
                expandedBox.Min *= 2.0f;

                if (rc->m_RasterizerContext->ComputeBoxVisibility(expandedBox, TRUE, nullptr)) {
                    m_Entity->ModifyMoveableFlags(VX_MOVEABLE_CHARACTERRENDERED, 0);
                }
            }
            // Clear transparent flags for this subtree
            ClearTransparentFlags();
            return;
        }

        // Handle Place viewport clipping
        if (m_Entity->GetClassID() == CKCID_PLACE) {
            RCKPlace *place = (RCKPlace *) m_Entity;
            VxRect &clip = place->ViewportClip();
            if (!clip.IsNull()) {
                VxRect unitRect(0.0f, 0.0f, 1.0f, 1.0f);
                if (clip.IsInside(unitRect)) {
                    clipRectSet = TRUE;

                    // Transform clip rect from homogeneous to screen coordinates
                    VxRect screenClip = clip;
                    float viewX = (float) rc->m_ViewportData.ViewX;
                    float viewY = (float) rc->m_ViewportData.ViewY;
                    float viewRight = viewX + (float) rc->m_ViewportData.ViewWidth;
                    float viewBottom = viewY + (float) rc->m_ViewportData.ViewHeight;
                    VxRect viewport(viewX, viewY, viewRight, viewBottom);
                    screenClip.TransformFromHomogeneous(viewport);
                    rc->SetClipRect(&screenClip);
                }
            }
        }

        // Mark character as rendered
        if (m_Entity->GetClassID() == CKCID_CHARACTER) {
            m_Entity->ModifyMoveableFlags(VX_MOVEABLE_CHARACTERRENDERED, 0);
        }

        // Check if entity passes hierarchy test
        entityInFrustum = CheckHierarchyFrustum();

        // Render if needed
        if ((rc->m_MaskFree & m_RenderContextMask) != 0 && m_Entity->IsToBeRendered()) {
            if (m_Entity->IsToBeRenderedLast()) {
                if (entityInFrustum || m_Entity->IsInViewFrustrum((CKRenderContext *) rc, flags)) {
                    m_TimeFpsCalc = rc->m_TimeFpsCalc;
                    rc->m_RenderManager->m_CKSceneGraphRootNode.AddTransparentObject(this);
                }
            } else {
                rc->m_Stats.SceneTraversalTime += rc->m_SceneTraversalTimeProfiler.Current();
                m_Entity->Render((CKRenderContext *) rc, flags);
                rc->m_SceneTraversalTimeProfiler.Reset();
            }
        }
    }

    // Traverse children
    if (entityInFrustum) {
        // Entity passed frustum test, children likely visible - use no-tests traversal
        for (int i = 0; i < m_ChildToBeParsedCount; i++) {
            CKSceneGraphNode *child = m_Children[i];
            if (child && (rc->m_MaskFree & child->m_EntityMask) != 0) {
                child->NoTestsTraversal(rc, flags);
            }
        }
    } else {
        // Need to test each child
        for (int i = 0; i < m_ChildToBeParsedCount; i++) {
            CKSceneGraphRootNode *child = (CKSceneGraphRootNode *) m_Children[i];
            if (child && (rc->m_MaskFree & child->m_EntityMask) != 0) {
                child->RenderTransparents(rc, flags);
            }
        }
    }

    // Restore viewport if we changed it
    if (clipRectSet) {
        rc->m_RasterizerContext->SetViewport(&rc->m_ViewportData);
        rc->m_RasterizerContext->SetTransformMatrix(VXMATRIX_PROJECTION, rc->m_ProjectionMatrix);
    }
}

void CKSceneGraphRootNode::SortTransparentObjects(RCKRenderContext *dev, CKDWORD flags) {
    int count = m_TransparentObjects.Size();
    if (count == 0)
        return;

    CKDWORD timeFpsCalc = dev->m_TimeFpsCalc;

    // Check if sorting is enabled
    if (dev->m_RenderManager->m_SortTransparentObjects.Value && count > 1) {
        // Reset profiler
        dev->m_TransparentObjectsSortTimeProfiler.Reset();

        // Update matrices for projection
        dev->m_RasterizerContext->UpdateMatrices(2);
        VxMatrix viewProjMatrix = dev->m_RasterizerContext->m_ViewProjMatrix;

        // Calculate Z extents for each transparent object
        CKTransparentObject *it = m_TransparentObjects.Begin();
        while (it != m_TransparentObjects.End()) {
            CKSceneGraphNode *node = it->m_Node;

            // Check if object is valid for this render context
            if ((dev->m_MaskFree & node->m_RenderContextMask) != 0 &&
                node->m_TimeFpsCalc == timeFpsCalc) {
                RCK3dEntity *entity = node->m_Entity;

                // For Sprite3D, update bounding box
                if (CKIsChildClassOf(entity, CKCID_SPRITE3D)) {
                    entity->ModifyMoveableFlags(0, 4); // Clear flag
                    entity->UpdateBox(TRUE);
                }

                // Compute model-view-projection matrix
                VxMatrix mvp;
                Vx3DMultiplyMatrix4(mvp, viewProjMatrix, entity->GetWorldMatrix());

                // Project bounding box to get Z extents
                const VxBbox &bbox = entity->GetBoundingBox(FALSE);
                float zhMin, zhMax;
                VxProjectBoxZExtents(mvp, bbox, zhMin, zhMax);
                it->m_ZhMin = zhMin;
                it->m_ZhMax = zhMax;

                ++it;
            } else {
                // Remove from list - object not valid
                node->m_Flags &= ~0x20; // Clear "in transparent list" flag
                it = m_TransparentObjects.Remove(it);
            }
        }

        // Sort transparent objects if any remain
        if (m_TransparentObjects.Size() > 0) {
            // Get camera position for tie-breaking
            VxVector cameraPos = *(VxVector *) &dev->m_RenderedScene->GetRootEntity()->GetWorldMatrix()[3];

            CKTransparentObject *begin = m_TransparentObjects.Begin();
            CKTransparentObject *end = m_TransparentObjects.End();

            // Bubble sort with multiple passes until stable
            CKBOOL sorted = FALSE;
            for (CKTransparentObject *i = begin + 1; !sorted && i != end; ++i) {
                sorted = TRUE;

                for (CKTransparentObject *k = end - 1; k != begin; --k) {
                    CKTransparentObject *prev = k - 1;

                    // Compare by priority first
                    if (k->m_Node->m_MaxPriority > prev->m_Node->m_MaxPriority) {
                        SwapTransparentObjects(k, prev);
                        sorted = FALSE;
                        continue;
                    }

                    // If same priority, compare by Z depth
                    if (k->m_Node->m_MaxPriority == prev->m_Node->m_MaxPriority) {
                        // Check if Z ranges overlap
                        if (prev->m_ZhMax < k->m_ZhMin) {
                            // prev is completely in front, no swap needed
                            continue;
                        }

                        if (k->m_ZhMax < prev->m_ZhMin) {
                            // k is completely in front, swap
                            SwapTransparentObjects(k, prev);
                            sorted = FALSE;
                            continue;
                        }

                        // Overlapping Z ranges - use bounding box comparison for tie-breaking
                        // Sort by maximum Z (back to front)
                        if (prev->m_ZhMax + 0.00000011920929f < k->m_ZhMax) {
                            SwapTransparentObjects(k, prev);
                            sorted = FALSE;
                        }
                    }
                }
            }

            // Record sort time
            dev->m_Stats.TransparentObjectsSortTime = dev->m_TransparentObjectsSortTimeProfiler.Current();

            // Render all transparent objects in sorted order
            for (CKTransparentObject *j = m_TransparentObjects.Begin();
                 j != m_TransparentObjects.End(); ++j) {
                j->m_Node->m_Entity->Render((CKRenderContext *) dev, flags);
            }
        }
    } else {
        // No sorting - render in order, checking validity
        CKTransparentObject *it = m_TransparentObjects.Begin();
        while (it != m_TransparentObjects.End()) {
            CKSceneGraphNode *node = it->m_Node;

            if ((dev->m_MaskFree & node->m_RenderContextMask) != 0) {
                if (node->m_TimeFpsCalc == timeFpsCalc) {
                    // Render the entity
                    node->m_Entity->Render((CKRenderContext *) dev, flags);
                    ++it;
                } else {
                    // Remove from list
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
