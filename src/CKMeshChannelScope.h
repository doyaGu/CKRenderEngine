#ifndef CKMESHCHANNELSCOPE_H
#define CKMESHCHANNELSCOPE_H

#include "RCKMaterial.h"
#include "RCKRenderContext.h"

class CKMeshChannelMaterialScope {
public:
    CKMeshChannelMaterialScope(RCKMaterial *material, VXBLEND_MODE source, VXBLEND_MODE dest)
        : m_Material(material) {
        m_Material->PatchForChannelRender(source, dest, m_Source, m_Dest, m_Flags);
    }
    ~CKMeshChannelMaterialScope() {
        m_Material->RestoreAfterChannelRender(m_Source, m_Dest, m_Flags);
    }
private:
    CKMeshChannelMaterialScope(const CKMeshChannelMaterialScope &) = delete;
    CKMeshChannelMaterialScope &operator=(const CKMeshChannelMaterialScope &) = delete;
    RCKMaterial *m_Material;
    VXBLEND_MODE m_Source;
    VXBLEND_MODE m_Dest;
    CKDWORD m_Flags;
};

// Restore the context's cached projection as well as the rasterizer state.
class CKMeshChannelProjectionScope {
public:
    explicit CKMeshChannelProjectionScope(RCKRenderContext *context)
        : m_Context(context), m_Projection(context->GetProjectionTransformationMatrix()) {
        VxMatrix biased = m_Projection;
        biased[3][2] *= 1.001f;
        m_Context->SetProjectionTransformationMatrix(biased);
    }
    ~CKMeshChannelProjectionScope() {
        m_Context->SetProjectionTransformationMatrix(m_Projection);
    }
private:
    CKMeshChannelProjectionScope(const CKMeshChannelProjectionScope &) = delete;
    CKMeshChannelProjectionScope &operator=(const CKMeshChannelProjectionScope &) = delete;
    RCKRenderContext *m_Context;
    VxMatrix m_Projection;
};

#endif
