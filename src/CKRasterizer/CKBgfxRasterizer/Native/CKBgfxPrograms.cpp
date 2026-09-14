// CKBgfxRasterizerContext shader, vertex-layout and program creation.

#include "CKBgfxRasterizerContext.h"
#include "CKBgfxResources.h"
#include "CKBgfxInternal.h"
#include "CKRasterizerValidation.h"

#include <stdint.h>

static bool CKBgfxSamplerBindingsEqual(
    const CKFFSamplerBinding &left,
    const CKFFSamplerBinding &right)
{
    return left.Name == right.Name &&
        left.NativeSlot == right.NativeSlot &&
        left.Slot == right.Slot &&
        left.Dimension == right.Dimension &&
        left.DefaultColor == right.DefaultColor;
}

CKERROR CKBgfxRasterizerContext::CreateShader(const CKShaderDesc *Desc,
                                               CKDWORD *OutShader)
{
    if (!OutShader)
        return CKERR_INVALIDPARAMETER;
    *OutShader = 0;
    if (!m_BgfxInitialized || !m_BgfxCreated || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (!Desc)
        return CKERR_INVALIDPARAMETER;
    if (Desc->Stage != CKRST_SHADER_VERTEX && Desc->Stage != CKRST_SHADER_PIXEL)
        return CKERR_INVALIDPARAMETER;
    if (!Desc->Code || Desc->CodeSize == 0) {
        CKBgfxLogf("CreateShader",
                   "invalid shader blob shader=%u stage=%u code=%p size=%u",
                   0u,
                   Desc ? (unsigned)Desc->Stage : 0u,
                   Desc ? Desc->Code : NULL,
                   Desc ? Desc->CodeSize : 0u);
        return CKERR_INVALIDPARAMETER;
    }
    if (Desc->Format != CKRST_SHADER_FORMAT_BGFX ||
        Desc->Profile != m_Caps.ShaderProfile) {
        CKBgfxLogf("CreateShader",
                   "shader target mismatch shader=%u stage=%u descFormat=0x%08X descProfile=%s(0x%08X) targetFormat=0x%08X targetProfile=%s(0x%08X)",
                   0u,
                   (unsigned)Desc->Stage,
                   Desc->Format,
                   CKBgfxShaderProfileName(Desc->Profile),
                   Desc->Profile,
                    CKRST_SHADER_FORMAT_BGFX,
                    CKBgfxShaderProfileName(m_Caps.ShaderProfile),
                    m_Caps.ShaderProfile);
        return CKERR_INVALIDPARAMETER;
    }

    const bgfx::Memory *mem = bgfx::copy(Desc->Code, Desc->CodeSize);
    bgfx::ShaderHandle handle = bgfx::createShader(mem);
    if (!bgfx::isValid(handle)) {
        CKBgfxLogf("CreateShader",
                   "bgfx::createShader failed shader=%u stage=%u profile=%s(0x%08X) size=%u",
                   0u,
                   (unsigned)Desc->Stage,
                   CKBgfxShaderProfileName(Desc->Profile),
                   Desc->Profile,
                   Desc->CodeSize);
        return CKERR_INVALIDPARAMETER;
    }

    auto *rec = new CKBgfxShaderRecord();
    rec->Handle = handle;
    rec->Stage = Desc->Stage;
    rec->Desc = *Desc;

    const CKDWORD shader = m_Resources->Shaders.Insert(
        rec, m_CapsDesc.MaxShaders);
    if (shader == 0) {
        CKBgfxDestroyRecord(rec);
        return CKERR_OUTOFMEMORY;
    }
    *OutShader = shader;

    return CK_OK;
}
CKERROR CKBgfxRasterizerContext::CreateVertexLayout(const CKVertexLayoutDesc *Desc,
                                                     CKDWORD *OutLayout)
{
    if (!OutLayout)
        return CKERR_INVALIDPARAMETER;
    *OutLayout = 0;
    if (!m_BgfxInitialized || !m_BgfxCreated || !IsApiThread())
        return CKERR_INVALIDOPERATION;
    if (CKRasterizerValidateVertexLayout(Desc) != CK_OK ||
        m_RendererType == bgfx::RendererType::Count)
        return CKERR_INVALIDPARAMETER;

    CKDWORD order[CKRST_ATTRIB_COUNT];
    bgfx::Attrib::Enum nativeAttribs[CKRST_ATTRIB_COUNT];
    bgfx::AttribType::Enum nativeTypes[CKRST_ATTRIB_COUNT];
    CKBOOL usedAttribs[CKRST_ATTRIB_COUNT] = {};
    for (CKDWORD i = 0; i < Desc->ElementCount; ++i)
    {
        const CKVertexElementDesc &elem = Desc->Elements[i];
        if ((CKDWORD)elem.Attrib >= CKRST_ATTRIB_COUNT ||
            usedAttribs[elem.Attrib] || elem.Count < 1 || elem.Count > 4 ||
            elem.Offset >= Desc->Stride ||
            !CKBgfxTryAttrib(elem.Attrib, nativeAttribs[i]) ||
            !CKBgfxTryAttribType(elem.Type, nativeTypes[i]))
            return CKERR_INVALIDPARAMETER;
        if (elem.Type == CKRST_ATTRIBTYPE_HALF &&
            (m_CapsDesc.Features & CKRST_DEVCAPS_VERTEX_ATTRIB_HALF) == 0)
            return CKERR_NOTIMPLEMENTED;
        if (elem.Type == CKRST_ATTRIBTYPE_UINT10 &&
            (m_CapsDesc.Features & CKRST_DEVCAPS_VERTEX_ATTRIB_UINT10) == 0)
            return CKERR_NOTIMPLEMENTED;
        usedAttribs[elem.Attrib] = TRUE;
        order[i] = i;
    }

    for (CKDWORD i = 1; i < Desc->ElementCount; ++i)
    {
        const CKDWORD value = order[i];
        CKDWORD j = i;
        while (j > 0 &&
               Desc->Elements[order[j - 1]].Offset > Desc->Elements[value].Offset)
        {
            order[j] = order[j - 1];
            --j;
        }
        order[j] = value;
    }

    bgfx::VertexLayout bgfxLayout;
    bgfxLayout.begin(m_RendererType);
    CKDWORD cursor = 0;
    for (CKDWORD sortedIndex = 0; sortedIndex < Desc->ElementCount; ++sortedIndex)
    {
        const CKDWORD sourceIndex = order[sortedIndex];
        const CKVertexElementDesc &elem = Desc->Elements[sourceIndex];
        if (elem.Offset < cursor)
            return CKERR_INVALIDPARAMETER;
        CKDWORD gap = elem.Offset - cursor;
        while (gap > 0)
        {
            const uint8_t chunk = (uint8_t)XMin(gap, (CKDWORD)UINT8_MAX);
            bgfxLayout.skip(chunk);
            gap -= chunk;
        }
        bgfxLayout.add(nativeAttribs[sourceIndex], elem.Count,
                       nativeTypes[sourceIndex], elem.Normalized != FALSE,
                       elem.AsInt != FALSE);
        cursor = bgfxLayout.getStride();
        if (cursor > Desc->Stride)
            return CKERR_INVALIDPARAMETER;
    }

    CKDWORD tail = Desc->Stride - cursor;
    while (tail > 0)
    {
        const uint8_t chunk = (uint8_t)XMin(tail, (CKDWORD)UINT8_MAX);
        bgfxLayout.skip(chunk);
        tail -= chunk;
    }
    bgfxLayout.end();

    if (bgfxLayout.getStride() != Desc->Stride)
        return CKERR_INVALIDPARAMETER;
    for (CKDWORD i = 0; i < Desc->ElementCount; ++i)
    {
        const CKVertexElementDesc &elem = Desc->Elements[i];
        uint8_t count = 0;
        bgfx::AttribType::Enum type = bgfx::AttribType::Count;
        bool normalized = false;
        bool asInt = false;
        bgfxLayout.decode(nativeAttribs[i], count, type, normalized, asInt);
        if (bgfxLayout.getOffset(nativeAttribs[i]) != elem.Offset ||
            count != elem.Count || type != nativeTypes[i] ||
            normalized != (elem.Normalized != FALSE) ||
            asInt != (elem.AsInt != FALSE))
            return CKERR_INVALIDPARAMETER;
    }

    bgfx::VertexLayoutHandle handle = bgfx::createVertexLayout(bgfxLayout);
    if (!bgfx::isValid(handle))
        return CKERR_OUTOFMEMORY;

    auto *rec = new CKBgfxVertexLayoutRecord();
    rec->Handle = handle;
    rec->Layout = bgfxLayout;

    const CKDWORD layout = m_Resources->VertexLayouts.Insert(
        rec, m_CapsDesc.MaxVertexLayouts);
    if (layout == 0) {
        CKBgfxDestroyRecord(rec);
        return CKERR_OUTOFMEMORY;
    }
    *OutLayout = layout;

    return CK_OK;
}

CKERROR CKBgfxRasterizerContext::CreateProgram(const CKFFProgramDesc *Desc, CKDWORD *Out)
{
    if (!Out)
        return CKERR_INVALIDPARAMETER;
    *Out = 0;
    if (!IsReady())
        return CKERR_INVALIDOPERATION;
    if (!Desc)
        return CKERR_INVALIDPARAMETER;

    const CKDWORD VertexShader = Desc->VertexShader;
    const CKDWORD PixelShader = Desc->PixelShader;
    CKBgfxShaderRecord *vs = GetShader(VertexShader);
    CKBgfxShaderRecord *ps = GetShader(PixelShader);
    if (!vs || !ps || vs->Stage != CKRST_SHADER_VERTEX || ps->Stage != CKRST_SHADER_PIXEL) {
        CKBgfxLogf("CreateProgram", "missing shaders vs=%p(h=%u) ps=%p(h=%u) shadersSize=%d",
                   vs, VertexShader, ps, PixelShader, m_Resources->Shaders.SlotCount());
        return CKERR_INVALIDPARAMETER;
    }

    const CKERROR validation = CKFFValidateProgram(*Desc, vs->Desc, ps->Desc);
    if (validation != CK_OK) return validation;
    // bgfx names and texture units are shared between stages. Compile their
    // declarations once; a draw never reflects or searches resource names.
    typedef XSHashTable<const CKFFUniformBinding *, XString> UniformTable;
    typedef XSHashTable<const CKFFSamplerBinding *, XString> SamplerTable;
    typedef XSHashTable<const CKFFSamplerBinding *, CKDWORD> TextureUnitTable;
    UniformTable uniforms;
    SamplerTable samplers;
    TextureUnitTable textureUnits;
    for (int i = 0; i < Desc->Uniforms.Size(); ++i) {
        const CKFFUniformBinding &binding = Desc->Uniforms[i];
        if (binding.Name.IsEmpty()) return CKERR_INVALIDPARAMETER;
        const CKFFUniformBinding *const *existing =
            uniforms.FindPtr(binding.Name);
        if (existing) {
            if ((*existing)->Slot != binding.Slot ||
                (*existing)->Type != binding.Type ||
                (*existing)->Count != binding.Count)
                return CKERR_INVALIDPARAMETER;
        } else if (!uniforms.Insert(binding.Name, &binding, FALSE)) {
            return CKERR_OUTOFMEMORY;
        }
    }
    for (int i = 0; i < Desc->Samplers.Size(); ++i) {
        const CKFFSamplerBinding &binding = Desc->Samplers[i];
        if (binding.Name.IsEmpty() || uniforms.IsHere(binding.Name) ||
            binding.NativeSlot >= m_CapsDesc.MaxTextureBindings) return CKERR_INVALIDPARAMETER;
        const CKFFSamplerBinding *const *named =
            samplers.FindPtr(binding.Name);
        const CKFFSamplerBinding *const *unit =
            textureUnits.FindPtr(binding.NativeSlot);
        if ((named && !CKBgfxSamplerBindingsEqual(**named, binding)) ||
            (unit && !CKBgfxSamplerBindingsEqual(**unit, binding)))
            return CKERR_INVALIDPARAMETER;
        if (!named && !samplers.Insert(binding.Name, &binding, FALSE))
            return CKERR_OUTOFMEMORY;
        if (!unit && !textureUnits.Insert(
                         binding.NativeSlot, &binding, FALSE))
            return CKERR_OUTOFMEMORY;
    }

    bgfx::ProgramHandle handle = bgfx::createProgram(vs->Handle, ps->Handle, false);
    if (!bgfx::isValid(handle)) {
        CKBgfxLogf("CreateProgram", "bgfx::createProgram failed vs.idx=%u ps.idx=%u",
                   vs->Handle.idx, ps->Handle.idx);
        return CKERR_INVALIDPARAMETER;
    }

    auto *rec = new CKBgfxProgramRecord();
    rec->Handle = handle;
    rec->VertexShader = VertexShader;
    rec->PixelShader = PixelShader;
    rec->Interface = *Desc;
    rec->Uniforms.Reserve(uniforms.Size());
    rec->Samplers.Reserve(samplers.Size());
    for (UniformTable::Iterator entry = uniforms.Begin();
         entry != uniforms.End(); ++entry) {
        const CKFFUniformBinding &binding = **entry;
        auto uniform = bgfx::createUniform(binding.Name.CStr(),
            binding.Type == CKFF_UNIFORM_MAT4 ? bgfx::UniformType::Mat4 : bgfx::UniformType::Vec4,
            (uint16_t)binding.Count);
        if (!bgfx::isValid(uniform)) { CKBgfxDestroyRecord(rec); return CKERR_OUTOFMEMORY; }
        CKBgfxProgramRecord::UniformBinding nativeBinding;
        nativeBinding.Slot = binding.Slot;
        nativeBinding.Count = binding.Count;
        nativeBinding.Handle = uniform;
        rec->Uniforms.PushBack(nativeBinding);
        auto &constants = m_ConstantData[binding.Slot];
        if ((CKDWORD)constants.Size() < binding.Size()) constants.Resize((int)binding.Size());
    }
    for (SamplerTable::Iterator entry = samplers.Begin();
         entry != samplers.End(); ++entry) {
        const CKFFSamplerBinding &binding = **entry;
        const auto texture = GetDefaultTexture(binding);
        if (!texture) { CKBgfxDestroyRecord(rec); return CKERR_OUTOFMEMORY; }
        const auto uniform = bgfx::createUniform(binding.Name.CStr(), bgfx::UniformType::Sampler);
        if (!bgfx::isValid(uniform)) { CKBgfxDestroyRecord(rec); return CKERR_OUTOFMEMORY; }
        CKBgfxProgramRecord::SamplerBinding nativeBinding;
        nativeBinding.Desc = binding;
        nativeBinding.Handle = uniform;
        nativeBinding.DefaultTexture = texture;
        rec->Samplers.PushBack(nativeBinding);
    }

    const CKDWORD program = m_Resources->Programs.Insert(
        rec, m_CapsDesc.MaxPrograms);
    if (program == 0) {
        CKBgfxDestroyRecord(rec);
        return CKERR_OUTOFMEMORY;
    }
    *Out = program;
    TraceProgramMap((CKSTRING)"create", program, rec);
    return CK_OK;
}

