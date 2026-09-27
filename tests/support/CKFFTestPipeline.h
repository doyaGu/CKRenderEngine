#ifndef CKRE_CKFFTESTPIPELINE_H
#define CKRE_CKFFTESTPIPELINE_H

#include "CKFixedFunctionPipeline.h"
#include "CKFFShaderCache.h"
#include "CKVertexLayoutCache.h"
#include "CKRasterizerContextData.h"
#include "XHashTable.h"

#include <string.h>

// Test-owned native cache for the recording device. CKFFPLib itself resolves
// only CPU shader variants and fragment-program data.
class CKFFTestShaderCache : public CKFFShaderCache {
public:
    CKFFTestShaderCache()
    {
        memset(m_Programs, 0, sizeof(m_Programs));
        memset(m_VertexShaders, 0, sizeof(m_VertexShaders));
        memset(m_PixelShaders, 0, sizeof(m_PixelShaders));
    }

    bool Init(const CKRasterizerDeviceCaps &caps, const CKFFShaderSet &shaders)
    {
        memset(m_Programs, 0, sizeof(m_Programs));
        memset(m_VertexShaders, 0, sizeof(m_VertexShaders));
        memset(m_PixelShaders, 0, sizeof(m_PixelShaders));
        CKRasterizerTargetDesc target;
        target.ShaderFormat = caps.ShaderFormat;
        target.ShaderProfile = caps.ShaderProfile;
        target.HomogeneousDepth = caps.HomogeneousDepth;
        target.OriginBottomLeft = caps.OriginBottomLeft;
        return CKFFShaderCache::Init(target, shaders);
    }

    template <class DeviceT>
    void Shutdown(DeviceT *device)
    {
        if (device) {
            for (CKDWORD variant = 0;
                 variant < CKFF_PROGRAM_VARIANT_COUNT; ++variant) {
                for (CKDWORD layout = 0; layout < CKFF_SAMPLER_LAYOUT_COUNT; ++layout)
                    if (m_Programs[variant][layout])
                        device->DestroyObject(m_Programs[variant][layout], CKRST_OBJ_PROGRAM);
                if (m_VertexShaders[variant])
                    device->DestroyObject(m_VertexShaders[variant], CKRST_OBJ_SHADER);
            }
            for (CKDWORD layout = 0; layout < CKFF_SAMPLER_LAYOUT_COUNT; ++layout)
                if (m_PixelShaders[layout])
                    device->DestroyObject(m_PixelShaders[layout], CKRST_OBJ_SHADER);
        }
        memset(m_Programs, 0, sizeof(m_Programs));
        memset(m_VertexShaders, 0, sizeof(m_VertexShaders));
        memset(m_PixelShaders, 0, sizeof(m_PixelShaders));
        CKFFShaderCache::Shutdown();
    }

    template <class DeviceT>
    CKFFProgramBinding GetProgram(DeviceT *device,
                                  const CKFFShaderKey &key)
    {
        return GetProgram(device, key, CKFFBuildSamplerLayoutPlan(key.FS));
    }

    template <class DeviceT>
    CKFFProgramBinding GetProgram(
        DeviceT *device, const CKFFShaderKey &key,
        const CKFFSamplerLayoutPlan &samplerLayoutPlan)
    {
        if (!device)
            return CKFFProgramBinding();
        const CKFFProgramSelection selection = ResolveProgram(
            key, samplerLayoutPlan);
        const CKDWORD variant = (CKDWORD)selection.Variant;
        const CKFFSamplerLayout selectedLayout =
            selection.SamplerLayoutPlan.Layout;
        const CKDWORD layout = (CKDWORD)selectedLayout;
        if (variant >= CKFF_PROGRAM_VARIANT_COUNT)
            return CKFFProgramBinding();
        if (!m_PixelShaders[layout] &&
            device->CreateShader(&GetPixelShader(), &m_PixelShaders[layout]) != CK_OK)
            return CKFFProgramBinding();
        if (!m_VertexShaders[variant] &&
            device->CreateShader(&GetVertexShader(selection.Variant),
                                 &m_VertexShaders[variant]) != CK_OK)
            return CKFFProgramBinding();
        if (!m_Programs[variant][layout]) {
            const CKBOOL positionT =
                selection.Variant == CKFF_PROGRAM_POSITIONT ||
                selection.Variant == CKFF_PROGRAM_POSITIONT_CLIP;
            const CKFFProgramDesc desc = CKFFBuildProgramInterface(
                m_VertexShaders[variant], m_PixelShaders[layout],
                GetShaderFormat(), FALSE, positionT,
                selectedLayout);
            if (device->CreateProgram(&desc, &m_Programs[variant][layout]) != CK_OK)
                return CKFFProgramBinding();
        }
        return CKFFProgramBinding(m_Programs[variant][layout],
                                  selection.FragmentProgram);
    }

    size_t CachedProgramCount() const
    {
        size_t count = 0;
        for (CKDWORD variant = 0;
             variant < CKFF_PROGRAM_VARIANT_COUNT; ++variant) {
            for (CKDWORD layout = 0; layout < CKFF_SAMPLER_LAYOUT_COUNT; ++layout)
                if (m_Programs[variant][layout])
                    ++count;
        }
        return count;
    }

private:
    CKDWORD m_Programs[CKFF_PROGRAM_VARIANT_COUNT][CKFF_SAMPLER_LAYOUT_COUNT];
    CKDWORD m_VertexShaders[CKFF_PROGRAM_VARIANT_COUNT];
    CKDWORD m_PixelShaders[CKFF_SAMPLER_LAYOUT_COUNT];
};

// Test-only runner for unit tests that exercise CKFFPLib without constructing
// a complete CKRasterizerContext. Production contexts perform these steps
// directly with their own native caches.
template <class DeviceT>
class CKFFTestPipelineT : public CKFixedFunctionPipeline {
public:
    CKFFTestPipelineT() : m_Device(NULL) {}
    ~CKFFTestPipelineT() { Shutdown(); }

    bool Init(DeviceT *device, const CKFFShaderSet &shaders)
    {
        if (!device)
            return false;
        Shutdown();
        const CKRasterizerDeviceCaps &caps = device->GetCaps();
        if (!m_Shaders.Init(caps, shaders))
            return false;
        m_Layouts.Clear();
        if (!CKFixedFunctionPipeline::Init(
                caps.Features, caps.MaxTextureBindings,
                m_Shaders.GetTargetFlags() |
                    (caps.ShaderFormat == CKRST_SHADER_FORMAT_BGFX
                         ? CKRST_SHADER_TARGET_BORDER_COLOR_UNIFORM : 0u))) {
            ClearLayouts();
            m_Shaders.Shutdown(device);
            return false;
        }
        m_Device = device;
        return true;
    }

    CKERROR PrepareShutdown() const
    {
        return m_Device && !m_Device->IsIdle()
            ? CKERR_INVALIDOPERATION : CK_OK;
    }

    CKERROR Shutdown()
    {
        const CKERROR status = PrepareShutdown();
        if (status != CK_OK)
            return status;
        CKFixedFunctionPipeline::Shutdown();
        ClearLayouts();
        m_Shaders.Shutdown(m_Device);
        m_Device = NULL;
        return CK_OK;
    }

    CKDWORD ResolveVertexLayout(CKDWORD formatFlags)
    {
        return GetLayout(formatFlags);
    }

    size_t CachedProgramCount() const
    {
        return m_Shaders.CachedProgramCount();
    }

    CKBOOL DrawPrimitive(VXPRIMITIVETYPE type, CKWORD *indices,
                         int indexCount, VxDrawPrimitiveData *data)
    {
        return PreparePrimitive(type, indices, indexCount, data) && Submit();
    }

    CKBOOL DrawVertexBuffer(VXPRIMITIVETYPE type, CKDWORD vb, CKDWORD ib,
                            CKDWORD baseVertex, CKDWORD vertexCount,
                            CKDWORD startIndex, CKDWORD indexCount,
                            CKDWORD dpFlags, CKDWORD formatFlags,
                            CKDWORD vertexLayout,
                            const CKWORD *indices = NULL)
    {
        return PrepareVertexBuffer(type, vb, ib, baseVertex, vertexCount,
                                   startIndex, indexCount, dpFlags,
                                   formatFlags, vertexLayout, indices) &&
               Submit();
    }

private:
    CKDWORD GetLayout(CKDWORD formatFlags)
    {
        CKDWORD layout = 0;
        if (m_Layouts.LookUp(formatFlags, layout))
            return layout;
        CKVertexElementDesc elements[20];
        CKVertexLayoutDesc desc;
        if (!m_Device || !CKFFVertexLayout::BuildLayout(
                formatFlags, elements,
                (CKDWORD)(sizeof(elements) / sizeof(elements[0])), desc) ||
            m_Device->CreateVertexLayout(&desc, &layout) != CK_OK)
            return 0;
        m_Layouts.Insert(formatFlags, layout);
        return layout;
    }

    void ClearLayouts()
    {
        if (m_Device) {
            for (XHashTable<CKDWORD, CKDWORD>::Iterator it = m_Layouts.Begin();
                 it != m_Layouts.End(); ++it)
                m_Device->DestroyObject(*it, CKRST_OBJ_VERTEXLAYOUT);
        }
        m_Layouts.Clear();
    }

    CKBOOL Submit()
    {
        if (!m_Device)
            return FinishDraw(CKERR_INVALIDOPERATION, 0);
        const CKFFDraw &draw = GetDraw();
        if (draw.SkipSubmit)
            return TRUE;
        const CKFFProgramBinding binding = m_Shaders.GetProgram(
            m_Device, draw.ShaderKey, draw.Textures.SamplerLayoutPlan);
        const CKDWORD layout = GetLayout(draw.VertexFormat);
        if (!binding.Program || !layout)
            return FinishDraw(CKERR_INVALIDOPERATION, 0);

        CKFFTextureBindings textures;
        for (CKDWORD i = 0; i < draw.Textures.ActiveTextureCount; ++i) {
            const CKFFTextureBinding &source = draw.Textures.Bindings[i];
            if (!source.Texture || source.Stage >= CKFF_TEXTURE_SLOT_COUNT)
                continue;
            textures[source.Stage].Texture = source.Texture;
            textures[source.Stage].Sampler = source.Sampler;
        }

        CKDrawCommand nativeDraw;
        nativeDraw.Pipeline = draw.Pipeline;
        nativeDraw.Textures = &textures;
        nativeDraw.Constants = draw.Constants;
        nativeDraw.Marker = draw.Marker;
        nativeDraw.Program = binding.Program;
        nativeDraw.Layout = layout;
        nativeDraw.VertexBuffer = draw.VertexBuffer;
        nativeDraw.StartVertex = draw.StartVertex;
        nativeDraw.VertexCount = draw.VertexCount;
        nativeDraw.IndexBuffer = draw.IndexBuffer;
        nativeDraw.StartIndex = draw.StartIndex;
        nativeDraw.IndexCount = draw.IndexCount;
        nativeDraw.SortKey = draw.SortKey;

        CKTransientVertexData vertices;
        CKTransientIndexData indices;
        if (draw.Vertices) {
            if (!m_Device->AllocTransientVertices(
                    draw.VertexCount, layout, &vertices) ||
                vertices.Stride != draw.VertexStride)
                return FinishDraw(CKERR_OUTOFMEMORY, 0);
            memcpy(vertices.Data, draw.Vertices,
                   (size_t)draw.VertexCount * draw.VertexStride);
            nativeDraw.VertexBuffer = 0;
            nativeDraw.StartVertex = 0;
            nativeDraw.TransientVertices = &vertices;
        }
        if (draw.Indices) {
            if (!m_Device->AllocTransientIndices(
                    draw.IndexCount, draw.Index32, &indices))
                return FinishDraw(CKERR_OUTOFMEMORY, 0);
            memcpy(indices.Data, draw.Indices,
                   (size_t)draw.IndexCount * (draw.Index32 ? 4u : 2u));
            nativeDraw.IndexBuffer = 0;
            nativeDraw.StartIndex = 0;
            nativeDraw.TransientIndices = &indices;
        }

        const CKERROR error = m_Device->Draw(&nativeDraw);
        return FinishDraw(error, m_Device->GetDrawApproximationMask());
    }

    DeviceT *m_Device;
    CKFFTestShaderCache m_Shaders;
    XHashTable<CKDWORD, CKDWORD> m_Layouts;
};

class CKRecordingBackend;
typedef CKFFTestPipelineT<CKRecordingBackend> CKFFTestPipeline;

#endif // CKRE_CKFFTESTPIPELINE_H
