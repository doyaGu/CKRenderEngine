#include "RCK2dEntity.h"
#include "RCKRenderContext.h"

CK_CLASSID RCK2dEntity::m_ClassID = CKCID_2DENTITY;

CK_CLASSID RCK2dEntity::GetClassID() {
    return m_ClassID;
}

RCK2dEntity::RCK2dEntity(CKContext *Context, CKSTRING name) : RCKRenderObject(Context, name) {
    // Initialize flags with default values matching IDA decompilation:
    // CK_2DENTITY_RESERVED3 | CK_2DENTITY_RATIOOFFSET | CK_2DENTITY_CLIPTOCAMERAVIEW | CK_2DENTITY_STICKLEFT | CK_2DENTITY_STICKTOP
    m_Flags = CK_2DENTITY_RESERVED3 | CK_2DENTITY_RATIOOFFSET | CK_2DENTITY_CLIPTOCAMERAVIEW |
        CK_2DENTITY_STICKLEFT | CK_2DENTITY_STICKTOP;
    m_Parent = nullptr;
    m_HomogeneousRect = nullptr;
    m_ZOrder = 0;
    m_Material = nullptr;
    m_SourceRect = VxRect(0.0f, 0.0f, 1.0f, 1.0f);
    // Initialize m_Rect to avoid undefined values
    m_Rect = VxRect(0.0f, 0.0f, 1.0f, 1.0f);
    m_VtxPos = VxRect(0.0f, 0.0f, 0.0f, 0.0f);
    m_SrcRect = VxRect(0.0f, 0.0f, 1.0f, 1.0f);
}

RCK2dEntity::~RCK2dEntity() {
    if (m_HomogeneousRect) {
        delete m_HomogeneousRect;
        m_HomogeneousRect = nullptr;
    }
    m_Children.Clear();
    if (m_Callbacks) {
        delete m_Callbacks;
        m_Callbacks = nullptr;
    }
}

CKERROR RCK2dEntity::GetPosition(Vx2DVector &vect, CKBOOL hom, CK2dEntity *ref) {
    if (hom) {
        if (!(m_Flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) || !m_HomogeneousRect)
            return CKERR_INVALIDPARAMETER;
        vect.x = m_HomogeneousRect->left;
        vect.y = m_HomogeneousRect->top;
    } else {
        vect.x = m_Rect.left;
        vect.y = m_Rect.top;
        if (ref) {
            Vx2DVector refPos;
            ref->GetPosition(refPos);
            vect -= refPos;
        }
    }
    return CK_OK;
}

void RCK2dEntity::SetPosition(const Vx2DVector &vect, CKBOOL hom, CKBOOL KeepChildren, CK2dEntity *ref) {
    if (hom) {
        if ((m_Flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) && m_HomogeneousRect) {
            float width = m_HomogeneousRect->GetWidth();
            float height = m_HomogeneousRect->GetHeight();
            VxRect newRect(vect.x, vect.y, vect.x + width, vect.y + height);
            SetHomogeneousRect(newRect, KeepChildren);
        }
    } else {
        if (m_Flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) {
            m_Flags |= CK_2DENTITY_UPDATEHOMOGENEOUSCOORD;
        }

        Vx2DVector pos = vect;
        if (ref) {
            Vx2DVector refPos;
            ref->GetPosition(refPos);
            pos += refPos;
        }

        float width = m_Rect.GetWidth();
        float height = m_Rect.GetHeight();
        VxRect newRect(pos.x, pos.y, pos.x + width, pos.y + height);
        SetRect(newRect, KeepChildren);
    }
}

CKERROR RCK2dEntity::GetSize(Vx2DVector &vect, CKBOOL hom) {
    if (hom) {
        if (!(m_Flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) || !m_HomogeneousRect)
            return CKERR_INVALIDPARAMETER;
        vect.x = m_HomogeneousRect->GetWidth();
        vect.y = m_HomogeneousRect->GetHeight();
    } else {
        vect.x = m_Rect.GetWidth();
        vect.y = m_Rect.GetHeight();
    }
    return CK_OK;
}

void RCK2dEntity::SetSize(const Vx2DVector &vect, CKBOOL hom, CKBOOL KeepChildren) {
    if (hom) {
        if ((m_Flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) && m_HomogeneousRect) {
            VxRect newRect = *m_HomogeneousRect;
            newRect.right = newRect.left + vect.x;
            newRect.bottom = newRect.top + vect.y;
            SetHomogeneousRect(newRect, KeepChildren);
        }
    } else {
        if (m_Flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) {
            m_Flags |= CK_2DENTITY_UPDATEHOMOGENEOUSCOORD;
        }
        VxRect newRect = m_Rect;
        newRect.right = newRect.left + vect.x;
        newRect.bottom = newRect.top + vect.y;
        SetRect(newRect, KeepChildren);
    }
}

void RCK2dEntity::SetRect(const VxRect &rect, CKBOOL KeepChildren) {
    // Handle homogeneous coordinate system
    if (m_Flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) {
        VxRect relRect;
        GetHomogeneousRelativeRect(relRect);
        *m_HomogeneousRect = rect;
        m_HomogeneousRect->TransformToHomogeneous(relRect);
    }

    if (!KeepChildren) {
        for (auto it = m_Children.Begin(); it != m_Children.End(); ++it) {
            RCK2dEntity *child = (RCK2dEntity *) *it;
            VxRect childRect;

            if (child->m_Flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) {
                // Handle child with homogeneous coords
                childRect = *child->m_HomogeneousRect;
                VxRect childRelRect;
                child->GetHomogeneousRelativeRect(childRelRect);
                childRect.TransformFromHomogeneous(childRelRect);
            } else {
                float childWidth = child->m_Rect.GetWidth();
                float childHeight = child->m_Rect.GetHeight();

                if (child->m_Flags & CK_2DENTITY_STICKLEFT) {
                    childRect.left = child->m_Rect.left - m_Rect.left + rect.left;
                    if (child->m_Flags & CK_2DENTITY_STICKRIGHT) {
                        childRect.right = rect.right - (m_Rect.right - child->m_Rect.right);
                    } else {
                        childRect.right = childRect.left + childWidth;
                    }
                } else if (child->m_Flags & CK_2DENTITY_STICKRIGHT) {
                    childRect.right = rect.right - (m_Rect.right - child->m_Rect.right);
                    childRect.left = childRect.right - childWidth;
                } else {
                    float centerX = (child->m_Rect.left + child->m_Rect.right) * 0.5f;
                    float ratio = (centerX - m_Rect.left) / m_Rect.GetWidth();
                    float newCenter = rect.GetWidth() * ratio + rect.left;
                    childRect.left = newCenter - childWidth * 0.5f;
                    childRect.right = childRect.left + childWidth;
                }

                if (child->m_Flags & CK_2DENTITY_STICKTOP) {
                    childRect.top = child->m_Rect.top - m_Rect.top + rect.top;
                    if (child->m_Flags & CK_2DENTITY_STICKBOTTOM) {
                        childRect.bottom = rect.bottom - (m_Rect.bottom - child->m_Rect.bottom);
                    } else {
                        childRect.bottom = childRect.top + childHeight;
                    }
                } else if (child->m_Flags & CK_2DENTITY_STICKBOTTOM) {
                    childRect.bottom = rect.bottom - (m_Rect.bottom - child->m_Rect.bottom);
                    childRect.top = childRect.bottom - childHeight;
                } else {
                    float centerY = (child->m_Rect.top + child->m_Rect.bottom) * 0.5f;
                    float ratio = (centerY - m_Rect.top) / m_Rect.GetHeight();
                    float newCenter = rect.GetHeight() * ratio + rect.top;
                    childRect.top = newCenter - childHeight * 0.5f;
                    childRect.bottom = childRect.top + childHeight;
                }
            }

            child->SetRect(childRect, FALSE);
        }
    }
    m_Rect = rect;
}

void RCK2dEntity::GetRect(VxRect &rect) {
    rect = m_Rect;
}

CKERROR RCK2dEntity::SetHomogeneousRect(const VxRect &rect, CKBOOL KeepChildren) {
    if (!(m_Flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) || !m_HomogeneousRect)
        return CKERR_INVALIDPARAMETER;

    *m_HomogeneousRect = rect;
    // TODO: Convert to screen coordinates and call SetRect
    return CK_OK;
}

CKERROR RCK2dEntity::GetHomogeneousRect(VxRect &rect) {
    if ((m_Flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) && m_HomogeneousRect) {
        rect = *m_HomogeneousRect;
        return CK_OK;
    }
    return CKERR_INVALIDPARAMETER;
}

void RCK2dEntity::GetHomogeneousRelativeRect(VxRect &rect) {
    CKRenderContext *ctx = m_Context->GetPlayerRenderContext();
    RCK2dEntity *parent = (RCK2dEntity *) GetParent();

    if (parent) {
        parent->GetRect(rect);
        return;
    }

    if (IsRatioOffset()) {
        if (ctx) {
            ctx->GetViewRect(rect);
        }
    } else {
        if (ctx) {
            ctx->GetWindowRect(rect, FALSE);
        }
    }

    // Translate rect so that its top-left is at origin
    Vx2DVector offset(-rect.left, -rect.top);
    rect.Translate(offset);
}

void RCK2dEntity::SetSourceRect(VxRect &rect) {
    m_SourceRect = rect;
}

void RCK2dEntity::GetSourceRect(VxRect &rect) {
    rect = m_SourceRect;
}

void RCK2dEntity::UseSourceRect(CKBOOL Use) {
    if (Use)
        m_Flags |= CK_2DENTITY_USESRCRECT;
    else
        m_Flags &= ~CK_2DENTITY_USESRCRECT;
}

CKBOOL RCK2dEntity::IsUsingSourceRect() {
    return (m_Flags & CK_2DENTITY_USESRCRECT) != 0;
}

void RCK2dEntity::SetPickable(CKBOOL Pick) {
    if (Pick)
        m_Flags &= ~CK_2DENTITY_NOTPICKABLE;
    else
        m_Flags |= CK_2DENTITY_NOTPICKABLE;
}

CKBOOL RCK2dEntity::IsPickable() {
    return (m_Flags & CK_2DENTITY_NOTPICKABLE) == 0;
}

void RCK2dEntity::SetBackground(CKBOOL back) {
    CKRenderContext *rc = m_Context->GetPlayerRenderContext();
    CK2dEntity *root = nullptr;

    if (rc) {
        root = rc->Get2dRoot(back ? 1 : 0);
    }

    if (back) {
        if (!GetParent() && !(m_Flags & CK_2DENTITY_BACKGROUND)) {
            SetParent(root);
            m_Flags |= CK_2DENTITY_BACKGROUND;
        }
    } else {
        if (!GetParent() && (m_Flags & CK_2DENTITY_BACKGROUND)) {
            SetParent(root);
            m_Flags &= ~CK_2DENTITY_BACKGROUND;
        }
    }
}

CKBOOL RCK2dEntity::IsBackground() {
    return (m_Flags & CK_2DENTITY_BACKGROUND) != 0;
}

void RCK2dEntity::SetClipToParent(CKBOOL clip) {
    if (clip)
        m_Flags |= CK_2DENTITY_CLIPTOPARENT;
    else
        m_Flags &= ~CK_2DENTITY_CLIPTOPARENT;
}

CKBOOL RCK2dEntity::IsClipToParent() {
    return (m_Flags & CK_2DENTITY_CLIPTOPARENT) != 0;
}

void RCK2dEntity::SetFlags(CKDWORD Flags) {
    m_Flags = Flags;
}

void RCK2dEntity::ModifyFlags(CKDWORD add, CKDWORD remove) {
    m_Flags |= add;
    m_Flags &= ~remove;
}

CKDWORD RCK2dEntity::GetFlags() {
    return m_Flags;
}

void RCK2dEntity::EnableRatioOffset(CKBOOL Ratio) {
    if (Ratio)
        m_Flags |= CK_2DENTITY_RATIOOFFSET;
    else
        m_Flags &= ~CK_2DENTITY_RATIOOFFSET;
}

CKBOOL RCK2dEntity::IsRatioOffset() {
    return (m_Flags & CK_2DENTITY_RATIOOFFSET) != 0;
}

CKBOOL RCK2dEntity::SetParent(CK2dEntity *parent) {
    if (parent == (CK2dEntity *) this) return FALSE;

    for (CK2dEntity *p = parent; p; p = p->GetParent()) {
        if (p == (CK2dEntity *) this) return FALSE;
    }

    RCK2dEntity *oldParent = (RCK2dEntity *) m_Parent;
    RCK2dEntity *newParent = (RCK2dEntity *) parent;

    if (oldParent) {
        oldParent->m_Children.Remove((CK2dEntity *) this);
    }

    m_Parent = parent;

    if (newParent) {
        newParent->m_Children.PushBack((CK2dEntity *) this);
        if (newParent->IsBackground()) {
            m_Flags |= CK_2DENTITY_BACKGROUND;
        } else {
            m_Flags &= ~CK_2DENTITY_BACKGROUND;
        }
    } else {
        CKRenderContext *rc = m_Context->GetPlayerRenderContext();
        if (rc) {
            RCK2dEntity *root = (RCK2dEntity *) rc->Get2dRoot(IsBackground() ? 1 : 0);
            if (root) {
                root->m_Children.PushBack((CK2dEntity *) this);
                m_Parent = (CK2dEntity *) root;
            }
        }
    }
    return TRUE;
}

CK2dEntity *RCK2dEntity::GetParent() const {
    return m_Parent;
}

int RCK2dEntity::GetChildrenCount() const {
    return m_Children.Size();
}

CK2dEntity *RCK2dEntity::GetChild(int i) const {
    if (i >= 0 && i < m_Children.Size())
        return (CK2dEntity *) m_Children[i];
    return nullptr;
}

CK2dEntity *RCK2dEntity::HierarchyParser(CK2dEntity *current) const {
    if (current) {
        if (current->GetChildrenCount() > 0)
            return current->GetChild(0);

        CK2dEntity *p = current;
        while (p) {
            if (p == (CK2dEntity *) this) return nullptr;

            CK2dEntity *parent = p->GetParent();
            if (!parent) break;

            int count = parent->GetChildrenCount();
            for (int i = 0; i < count; ++i) {
                if (parent->GetChild(i) == p) {
                    if (i + 1 < count)
                        return parent->GetChild(i + 1);
                    break;
                }
            }
            p = parent;
        }
    } else {
        if (GetChildrenCount() > 0)
            return GetChild(0);
    }
    return nullptr;
}

void RCK2dEntity::SetMaterial(CKMaterial *mat) {
    m_Material = mat;
}

CKMaterial *RCK2dEntity::GetMaterial() {
    return m_Material;
}

void RCK2dEntity::SetHomogeneousCoordinates(CKBOOL Coord) {
    if (Coord) {
        if (!(m_Flags & CK_2DENTITY_USEHOMOGENEOUSCOORD)) {
            m_Flags |= CK_2DENTITY_USEHOMOGENEOUSCOORD;
            if (!m_HomogeneousRect)
                m_HomogeneousRect = new VxRect();
        }
    } else {
        if (m_Flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) {
            m_Flags &= ~CK_2DENTITY_USEHOMOGENEOUSCOORD;
            if (m_HomogeneousRect) {
                delete m_HomogeneousRect;
                m_HomogeneousRect = nullptr;
            }
        }
    }
}

CKBOOL RCK2dEntity::IsHomogeneousCoordinates() {
    return (m_Flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) != 0;
}

void RCK2dEntity::EnableClipToCamera(CKBOOL Clip) {
    if (Clip)
        m_Flags |= CK_2DENTITY_CLIPTOCAMERAVIEW;
    else
        m_Flags &= ~CK_2DENTITY_CLIPTOCAMERAVIEW;
}

CKBOOL RCK2dEntity::IsClippedToCamera() {
    return (m_Flags & CK_2DENTITY_CLIPTOCAMERAVIEW) != 0;
}

CKERROR RCK2dEntity::Render(CKRenderContext *context) {
    Draw(context);
    for (auto it = m_Children.Begin(); it != m_Children.End(); ++it) {
        ((CK2dEntity *) *it)->Render(context);
    }
    return CK_OK;
}

CKERROR RCK2dEntity::Draw(CKRenderContext *context) {
    return CK_OK;
}

void RCK2dEntity::GetExtents(VxRect &srcrect, VxRect &rect) {
    srcrect = m_SourceRect;
    rect = m_Rect;
}

void RCK2dEntity::SetExtents(const VxRect &srcrect, const VxRect &rect) {
    m_SourceRect = srcrect;
    m_Rect = rect;
}

void RCK2dEntity::RestoreInitialSize() {
}

CKBOOL RCK2dEntity::IsHiddenByParent() {
    CK2dEntity *p = m_Parent;
    while (p) {
        if (!p->IsVisible()) return TRUE;
        p = p->GetParent();
    }
    return FALSE;
}

// CKRenderObject implementation
void RCK2dEntity::AddToRenderContext(CKRenderContext *context) {
    RCKRenderContext *dev = (RCKRenderContext *) context;
    m_InRenderContext |= dev->m_MaskFree;
}

void RCK2dEntity::RemoveFromRenderContext(CKRenderContext *context) {
    RCKRenderContext *dev = (RCKRenderContext *) context;
    m_InRenderContext &= ~dev->m_MaskFree;
}

int RCK2dEntity::CanBeHide() {
    return 2;
}

CKBOOL RCK2dEntity::IsInRenderContext(CKRenderContext *context) {
    RCKRenderContext *dev = (RCKRenderContext *) context;
    return (dev->m_MaskFree & m_InRenderContext) != 0;
}

CKBOOL RCK2dEntity::IsRootObject() {
    return m_Parent == nullptr;
}

CKBOOL RCK2dEntity::IsToBeRendered() {
    return IsVisible();
}

void RCK2dEntity::SetZOrder(int Z) {
    m_ZOrder = Z;
}

int RCK2dEntity::GetZOrder() {
    return m_ZOrder;
}

CKBOOL RCK2dEntity::IsToBeRenderedLast() {
    return FALSE;
}

CKBOOL RCK2dEntity::AddPreRenderCallBack(CK_RENDEROBJECT_CALLBACK Function, void *Argument, CKBOOL Temp) {
    if (!m_Callbacks) {
        m_Callbacks = new CKCallbacksContainer();
    }
    return m_Callbacks->AddPreCallback((void *) Function, Argument, Temp, m_Context->GetRenderManager());
}

CKBOOL RCK2dEntity::RemovePreRenderCallBack(CK_RENDEROBJECT_CALLBACK Function, void *Argument) {
    if (!m_Callbacks) {
        return FALSE;
    }
    return m_Callbacks->RemovePreCallback((void *) Function, Argument);
}

CKBOOL RCK2dEntity::SetRenderCallBack(CK_RENDEROBJECT_CALLBACK Function, void *Argument) {
    if (!m_Callbacks) {
        m_Callbacks = new CKCallbacksContainer();
    }
    return m_Callbacks->SetCallBack((void *) Function, Argument);
}

CKBOOL RCK2dEntity::RemoveRenderCallBack() {
    if (!m_Callbacks) {
        return FALSE;
    }
    return m_Callbacks->RemoveCallBack();
}

CKBOOL RCK2dEntity::AddPostRenderCallBack(CK_RENDEROBJECT_CALLBACK Function, void *Argument, CKBOOL Temp) {
    if (!m_Callbacks) {
        m_Callbacks = new CKCallbacksContainer();
    }
    return m_Callbacks->AddPostCallback((void *) Function, Argument, Temp, m_Context->GetRenderManager());
}

CKBOOL RCK2dEntity::RemovePostRenderCallBack(CK_RENDEROBJECT_CALLBACK Function, void *Argument) {
    if (!m_Callbacks) {
        return FALSE;
    }
    return m_Callbacks->RemovePostCallback((void *) Function, Argument);
}

void RCK2dEntity::RemoveAllCallbacks() {
    if (m_Callbacks) {
        m_Callbacks->Clear();
    }
}

CKERROR RCK2dEntity::Load(CKStateChunk *chunk, CKFile *file) {
    if (!chunk) return CKERR_INVALIDPARAMETER;

    CKERROR result = CKBeObject::Load(chunk, file);
    if (result != CK_OK) return result;

    if (chunk->GetDataVersion() >= 5) {
        // New format (version 5+)
        if (chunk->SeekIdentifier(0x10F000)) {
            CKDWORD saveFlags = chunk->ReadDword();
            m_Flags = saveFlags & 0xFFF8F7FF; // Mask out save-only flags

            // Allocate homogeneous rect if needed
            if ((m_Flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) && !m_HomogeneousRect) {
                m_HomogeneousRect = new VxRect();
            }

            // Read rect
            if (m_Flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) {
                chunk->ReadAndFillBuffer_LEndian(sizeof(VxRect), m_HomogeneousRect);
            } else {
                chunk->ReadAndFillBuffer_LEndian(sizeof(VxRect), &m_Rect);
            }

            // Read source rect conditionally
            if (saveFlags & 0x10000) {
                chunk->ReadAndFillBuffer_LEndian(sizeof(VxRect), &m_SourceRect);
            } else if (GetClassID() == CKCID_2DENTITY) {
                m_SourceRect = VxRect(0.0f, 0.0f, 1.0f, 1.0f);
            } else {
                m_SourceRect = VxRect(0.0f, 0.0f, 0.0f, 0.0f);
            }

            // Read ZOrder conditionally
            if (saveFlags & 0x20000) {
                m_ZOrder = chunk->ReadInt();
            } else {
                m_ZOrder = 0;
            }

            // Read parent conditionally
            if (saveFlags & 0x40000) {
                CK2dEntity *parent = (CK2dEntity *) chunk->ReadObject(m_Context);
                if (file) {
                    m_Parent = parent;
                } else {
                    SetParent(parent);
                }
            } else {
                SetParent(nullptr);
            }
        }

        // Read material for CK2dEntity only
        if (GetClassID() == CKCID_2DENTITY && chunk->SeekIdentifier(0x200000)) {
            m_Material = (CKMaterial *) chunk->ReadObject(m_Context);
        } else {
            m_Material = nullptr;
        }
    } else {
        // Old format (version < 5)
        if (chunk->SeekIdentifier(0x4000)) {
            m_Flags = chunk->ReadDword();
            if (!(m_Flags & 1)) {
                m_Flags |= 1;
                Show(CKHIDE);
            }
            m_Flags &= ~CK_2DENTITY_UPDATEHOMOGENEOUSCOORD;
            m_Flags |= 0x280000;
        }

        if ((m_Flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) && !m_HomogeneousRect) {
            m_HomogeneousRect = new VxRect();
        }

        if (chunk->SeekIdentifier(0x8000)) {
            if (m_Flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) {
                m_HomogeneousRect->left = chunk->ReadFloat();
                m_HomogeneousRect->top = chunk->ReadFloat();
            } else {
                int x = chunk->ReadInt();
                int y = chunk->ReadInt();
                m_Rect.left = (float) x;
                m_Rect.top = (float) y;
            }
        }

        if (chunk->SeekIdentifier(0x2000)) {
            if (m_Flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) {
                float w = chunk->ReadFloat();
                float h = chunk->ReadFloat();
                m_HomogeneousRect->right = m_HomogeneousRect->left + w;
                m_HomogeneousRect->bottom = m_HomogeneousRect->top + h;
            } else {
                int w = chunk->ReadInt();
                int h = chunk->ReadInt();
                m_Rect.right = m_Rect.left + (float) w;
                m_Rect.bottom = m_Rect.top + (float) h;
            }
        }

        if (chunk->SeekIdentifier(0x1000)) {
            m_SourceRect.right = (float) chunk->ReadInt();
            m_SourceRect.left = (float) chunk->ReadInt();
            m_SourceRect.top = (float) chunk->ReadInt();
            m_SourceRect.bottom = (float) chunk->ReadInt();
        }

        if (chunk->SeekIdentifier(0x100000)) {
            m_ZOrder = chunk->ReadInt();
        }
    }

    // Transform from homogeneous to screen coords if needed
    if (m_Flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) {
        VxRect tempRect = *m_HomogeneousRect;
        VxRect relRect;
        GetHomogeneousRelativeRect(relRect);
        tempRect.TransformFromHomogeneous(relRect);
        m_Rect = tempRect;
    }

    return CK_OK;
}

void RCK2dEntity::PostLoad(void) {
    if (m_Parent) {
        ((RCK2dEntity *) m_Parent)->m_Children.PushBack((CK2dEntity *) this);

        CKRenderManager *rm = m_Context->GetRenderManager();
        int count = rm->GetRenderContextCount();
        for (int i = 0; i < count; ++i) {
            CKRenderContext *rc = rm->GetRenderContext(i);
            rc->RemoveObject(this);
        }
    }
    CKObject::PostLoad();
}

void RCK2dEntity::PreSave(CKFile *file, CKDWORD flags) {
    CKBeObject::PreSave(file, flags);
    if ((flags & 0x200000) && m_Material) {
        file->SaveObject(m_Material, flags);
    }
    if (flags & 0x400000) {
        for (auto it = m_Children.Begin(); it != m_Children.End(); ++it) {
            file->SaveObject(*it, flags);
        }
    }
}

CKStateChunk *RCK2dEntity::Save(CKFile *file, CKDWORD flags) {
    CKStateChunk *chunk = CreateCKStateChunk(CKCID_2DENTITY, file);
    CKStateChunk *baseChunk = CKBeObject::Save(file, flags);
    if (!chunk) return baseChunk;

    chunk->StartWrite();
    chunk->AddChunkAndDelete(baseChunk);

    chunk->WriteIdentifier(0x10F000);

    // Build save flags
    CKDWORD saveFlags = m_Flags;
    if (m_SourceRect.top != 0.0f || m_SourceRect.left != 0.0f ||
        m_SourceRect.bottom != 0.0f || m_SourceRect.right != 0.0f) {
        saveFlags |= 0x10000;
    }
    if (m_ZOrder) {
        saveFlags |= 0x20000;
    }
    if (m_Parent) {
        saveFlags |= 0x40000;
    }

    chunk->WriteDword(saveFlags);

    // Write rect - homogeneous or screen coords
    if (m_Flags & CK_2DENTITY_USEHOMOGENEOUSCOORD) {
        chunk->WriteBufferNoSize_LEndian(sizeof(VxRect), m_HomogeneousRect);
    } else {
        chunk->WriteBufferNoSize_LEndian(sizeof(VxRect), &m_Rect);
    }

    // Conditionally write source rect
    if (saveFlags & 0x10000) {
        chunk->WriteBufferNoSize_LEndian(sizeof(VxRect), &m_SourceRect);
    }

    // Conditionally write ZOrder
    if (saveFlags & 0x20000) {
        chunk->WriteInt(m_ZOrder);
    }

    // Conditionally write parent
    if (saveFlags & 0x40000) {
        chunk->WriteObject(m_Parent);
    }

    // Write material if present
    if (m_Material) {
        chunk->WriteIdentifier(0x200000);
        chunk->WriteObject(m_Material);
    }

    if (GetClassID() == CKCID_2DENTITY) {
        chunk->CloseChunk();
    } else {
        chunk->UpdateDataSize();
    }
    return chunk;
}

void RCK2dEntity::PreDelete() {
    CKBeObject::PreDelete();
}

void RCK2dEntity::CheckPreDeletion() {
    CKObject::CheckPreDeletion();
}

int RCK2dEntity::GetMemoryOccupation() {
    return sizeof(RCK2dEntity) + m_Children.Size() * sizeof(CK2dEntity *);
}

CKERROR RCK2dEntity::PrepareDependencies(CKDependenciesContext &context) {
    return CKBeObject::PrepareDependencies(context);
}

CKERROR RCK2dEntity::RemapDependencies(CKDependenciesContext &context) {
    return CKBeObject::RemapDependencies(context);
}

CKERROR RCK2dEntity::Copy(CKObject &o, CKDependenciesContext &context) {
    return CKBeObject::Copy(o, context);
}

CKSTRING RCK2dEntity::GetClassName() {
    return (CKSTRING) "2dEntity";
}

int RCK2dEntity::GetDependenciesCount(int mode) {
    return 0;
}

CKSTRING RCK2dEntity::GetDependencies(int i, int mode) {
    return nullptr;
}

void RCK2dEntity::Register() {
    // Based on IDA decompilation
    CKClassNeedNotificationFrom(m_ClassID, CKCID_MATERIAL);
    CKClassRegisterAssociatedParameter(m_ClassID, CKPGUID_2DENTITY);
    CKClassRegisterDefaultDependencies(m_ClassID, 3, CK_DEPENDENCIES_COPY);
    CKClassRegisterDefaultDependencies(m_ClassID, 3, CK_DEPENDENCIES_SAVE);
}

RCK2dEntity *RCK2dEntity::CreateInstance(CKContext *Context) {
    return new RCK2dEntity(Context);
}
