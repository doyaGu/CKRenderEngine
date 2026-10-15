#ifndef CKMESHRENDERSCOPE_H
#define CKMESHRENDERSCOPE_H

#include "CKRenderEngineTypes.h"
#include "CKEnums.h"

// Only nested draws of the same mesh restore an enclosing draw's transient
// plan. Authored channel flags and ordinary top-level results remain visible.
class CKMeshRenderScope {
public:
    CKMeshRenderScope(RCKMesh *mesh, XArray<int> &activeChannels,
                      XClassArray<VxMaterialChannel> &channels, CKDWORD &drawFlags)
        : m_Previous(Current()), m_Mesh(mesh), m_ActiveChannels(activeChannels),
          m_Channels(channels), m_DrawFlags(drawFlags), m_SavedDrawFlags(drawFlags),
          m_Nested(FALSE) {
        for (CKMeshRenderScope *scope = m_Previous; scope; scope = scope->m_Previous) {
            if (scope->m_Mesh == mesh) {
                m_Nested = TRUE;
                break;
            }
        }
        if (m_Nested) {
            m_ActiveChannels.Swap(m_SavedActive);
            m_SavedChannelFlags.Resize(m_Channels.Size());
            for (int c = 0; c < m_Channels.Size(); ++c)
                m_SavedChannelFlags[c] = m_Channels[c].m_Flags & (VXCHANNEL_MONO | VXCHANNEL_LAST);
        }
        Current() = this;
    }
    // A direct group consumes an existing plan rather than rebuilding it.
    void KeepActiveChannels() {
        if (m_Nested)
            m_ActiveChannels = m_SavedActive;
    }
    CKBOOL IsNested() const { return m_Nested; }
    ~CKMeshRenderScope() {
        if (m_Nested) {
            m_ActiveChannels.Swap(m_SavedActive);
            for (int c = 0; c < m_Channels.Size() && c < m_SavedChannelFlags.Size(); ++c)
                m_Channels[c].m_Flags = (m_Channels[c].m_Flags & ~(VXCHANNEL_MONO | VXCHANNEL_LAST)) |
                    m_SavedChannelFlags[c];
            m_DrawFlags = m_SavedDrawFlags;
        }
        Current() = m_Previous;
    }
private:
    CKMeshRenderScope(const CKMeshRenderScope &) = delete;
    CKMeshRenderScope &operator=(const CKMeshRenderScope &) = delete;
    static CKMeshRenderScope *&Current() {
        static thread_local CKMeshRenderScope *current = nullptr;
        return current;
    }
    CKMeshRenderScope *m_Previous;
    RCKMesh *m_Mesh;
    XArray<int> &m_ActiveChannels;
    XClassArray<VxMaterialChannel> &m_Channels;
    CKDWORD &m_DrawFlags;
    CKDWORD m_SavedDrawFlags;
    CKBOOL m_Nested;
    XArray<int> m_SavedActive;
    XArray<CKDWORD> m_SavedChannelFlags;
};

#endif
