#ifndef CKBGFXRESOURCES_H
#define CKBGFXRESOURCES_H

#include "CKRasterizerContextData.h"

#include "VxMutex.h"
#include "XArray.h"
#include "XClassArray.h"

#include <bgfx/bgfx.h>
#include <memory>
#include <string.h>

struct CKBgfxShaderRecord {
    bgfx::ShaderHandle Handle;
    CK_SHADER_STAGE Stage;
    CKShaderDesc Desc;
};

struct CKBgfxProgramRecord {
    struct UniformBinding {
        CKDWORD Slot;
        CKDWORD Count;
        bgfx::UniformHandle Handle;
    };
    struct SamplerBinding {
        CKFFSamplerBinding Desc;
        bgfx::UniformHandle Handle = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle BorderColorHandle = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle SamplerStateHandle = BGFX_INVALID_HANDLE;
        std::shared_ptr<bgfx::TextureHandle> DefaultTexture;
    };
    bgfx::ProgramHandle Handle = BGFX_INVALID_HANDLE;
    CKDWORD VertexShader = 0;
    CKDWORD PixelShader = 0;
    bool FixedFunctionBorderSampling = false;
    CKFFProgramDesc Interface;
    XClassArray<UniformBinding> Uniforms;
    XClassArray<SamplerBinding> Samplers;

    ~CKBgfxProgramRecord()
    {
        for (int i = 0; i < Uniforms.Size(); ++i)
            if (bgfx::isValid(Uniforms[i].Handle)) bgfx::destroy(Uniforms[i].Handle);
        for (int i = 0; i < Samplers.Size(); ++i) {
            if (bgfx::isValid(Samplers[i].Handle)) bgfx::destroy(Samplers[i].Handle);
            if (bgfx::isValid(Samplers[i].BorderColorHandle))
                bgfx::destroy(Samplers[i].BorderColorHandle);
            if (bgfx::isValid(Samplers[i].SamplerStateHandle))
                bgfx::destroy(Samplers[i].SamplerStateHandle);
        }
    }
};

struct CKBgfxVertexLayoutRecord {
    bgfx::VertexLayoutHandle Handle;
    bgfx::VertexLayout Layout;
};

struct CKBgfxVertexBufferRecord {
    bgfx::DynamicVertexBufferHandle Handle;
    CKDWORD Layout;
    bgfx::VertexLayout NativeLayout;
    XArray<CKBYTE> Shadow;
    CKDWORD VertexSize;
    CKDWORD VertexCount;
    CKDWORD Size;
};

struct CKBgfxIndexBufferRecord {
    bgfx::DynamicIndexBufferHandle Handle;
    XArray<CKBYTE> Shadow;
    CKBOOL Index32;
    CKDWORD IndexCount;
    CKDWORD Size;
};

enum CKBgfxTextureOrientation : int {
    CKBGFX_ORIENTATION_UNKNOWN = 0,
    CKBGFX_ORIENTATION_TOP_LEFT,
    CKBGFX_ORIENTATION_BOTTOM_LEFT,
    CKBGFX_ORIENTATION_MIXED,
};

static const CKDWORD CKBGFX_MAX_TRACKED_MIPS = 32;
static const CKDWORD CKBGFX_MAX_FRAMEBUFFER_ATTACHMENTS = 2;

struct CKBgfxTextureRecord {
    CKBgfxTextureRecord()
        : Handle(BGFX_INVALID_HANDLE), SamplerBaseHandle(BGFX_INVALID_HANDLE),
          Flags(0), Width(0), Height(0), Depth(1),
          IsDepth(FALSE), RequestedAutoMips(FALSE), MipCount(1),
          Format(bgfx::TextureFormat::Count), PixelFormat(UNKNOWN_PF), BitsPerPixel(0),
          SamplerBaseValid(FALSE), AutoMipBaseValid(FALSE)
    {
        memset(ReadbackOrientation, CKBGFX_ORIENTATION_UNKNOWN,
               sizeof(ReadbackOrientation));
    }

    ~CKBgfxTextureRecord()
    {
        if (bgfx::isValid(SamplerBaseHandle)) {
            bgfx::destroy(SamplerBaseHandle);
            SamplerBaseHandle = BGFX_INVALID_HANDLE;
        }
        delete[] AutoMipBaseDesc.Image;
        AutoMipBaseDesc.Image = NULL;
    }

    bgfx::TextureHandle Handle;
    bgfx::TextureHandle SamplerBaseHandle;
    CKDWORD Flags;
    CKDWORD Width;
    CKDWORD Height;
    CKDWORD Depth;
    CKBOOL IsDepth;
    CKBOOL RequestedAutoMips;
    CKDWORD MipCount;
    bgfx::TextureFormat::Enum Format;
    VX_PIXELFORMAT PixelFormat;
    CKDWORD BitsPerPixel;
    CKBOOL SamplerBaseValid;
    VxImageDescEx AutoMipBaseDesc;
    CKBOOL AutoMipBaseValid;
    CKBYTE ReadbackOrientation[CKBGFX_MAX_TRACKED_MIPS];
};

struct CKBgfxFrameBufferRecord {
    bgfx::FrameBufferHandle Handle;
    CKRenderTargetDesc Desc;
};

template <typename RecordT>
void CKBgfxDestroyRecord(RecordT *record)
{
    if (!record)
        return;
    if (bgfx::isValid(record->Handle))
        bgfx::destroy(record->Handle);
    delete record;
}

// Backend-local, generation-checked handle table. Reserving a reused slot
// advances its generation; exhausted generations retire the slot instead of
// allowing a stale 32-bit handle to become valid again.
template <typename RecordT>
class CKBgfxResourceTable {
public:
    explicit CKBgfxResourceTable(VxMutex &mutex) : m_Mutex(mutex) {}

    CKDWORD Insert(RecordT *record, CKDWORD maxHandles)
    {
        if (!record)
            return 0;
        VxMutexLock lock(m_Mutex);
        if (m_Slots.Size() == 0)
            m_Slots.PushBack(Slot());
        for (int i = 1; i < m_Slots.Size(); ++i) {
            Slot &slot = m_Slots[i];
            if (!slot.Record && !slot.Retired) {
                if (slot.Generation == 0xffffu) {
                    slot.Retired = TRUE;
                    continue;
                }
                ++slot.Generation;
                slot.Record = record;
                return Encode((CKDWORD)i, slot.Generation);
            }
        }
        const CKDWORD handleLimit = maxHandles != 0
            ? XMin(maxHandles, SLOT_MASK)
            : SLOT_MASK;
        if ((CKDWORD)(m_Slots.Size() - 1) >= handleLimit)
            return 0;
        Slot slot;
        slot.Record = record;
        slot.Generation = 1;
        m_Slots.PushBack(slot);
        return Encode((CKDWORD)(m_Slots.Size() - 1), slot.Generation);
    }

    RecordT *Get(CKDWORD handle) const
    {
        VxMutexLock lock(m_Mutex);
        const CKDWORD index = HandleSlot(handle);
        if (index == 0 || (int)index >= m_Slots.Size())
            return NULL;
        const Slot &slot = m_Slots[index];
        return slot.Generation == HandleGeneration(handle) ? slot.Record : NULL;
    }

    RecordT *Remove(CKDWORD handle)
    {
        VxMutexLock lock(m_Mutex);
        const CKDWORD index = HandleSlot(handle);
        if (index == 0 || (int)index >= m_Slots.Size())
            return NULL;
        Slot &slot = m_Slots[index];
        if (slot.Generation != HandleGeneration(handle))
            return NULL;
        RecordT *record = slot.Record;
        slot.Record = NULL;
        return record;
    }

    CKBOOL IsAlive(CKDWORD handle) const
    {
        VxMutexLock lock(m_Mutex);
        const CKDWORD index = HandleSlot(handle);
        if (index == 0 || (int)index >= m_Slots.Size())
            return FALSE;
        const Slot &slot = m_Slots[index];
        return slot.Record && slot.Generation == HandleGeneration(handle)
            ? TRUE : FALSE;
    }

    void DestroyAll()
    {
        VxMutexLock lock(m_Mutex);
        for (int i = 0; i < m_Slots.Size(); ++i) {
            CKBgfxDestroyRecord(m_Slots[i].Record);
            m_Slots[i].Record = NULL;
        }
    }

    int SlotCount() const
    {
        VxMutexLock lock(m_Mutex);
        return m_Slots.Size();
    }

private:
    struct Slot {
        Slot() : Record(NULL), Generation(0), Retired(FALSE) {}

        RecordT *Record;
        CKWORD Generation;
        CKBOOL Retired;
    };

    static const CKDWORD SLOT_MASK = 0xffffu;
    static CKDWORD Encode(CKDWORD slot, CKWORD generation)
    {
        return ((CKDWORD)generation << 16) | slot;
    }
    static CKDWORD HandleSlot(CKDWORD handle) { return handle & SLOT_MASK; }
    static CKWORD HandleGeneration(CKDWORD handle) { return (CKWORD)(handle >> 16); }

    XArray<Slot> m_Slots;
    VxMutex &m_Mutex;
};

class CKBgfxResources {
private:
    mutable VxMutex m_TableMutex;

public:
    CKBgfxResources()
        : Shaders(m_TableMutex), Programs(m_TableMutex), VertexLayouts(m_TableMutex),
          VertexBuffers(m_TableMutex), IndexBuffers(m_TableMutex), Textures(m_TableMutex),
          FrameBuffers(m_TableMutex) {}

    CKBOOL IsAlive(CKDWORD object, CKDWORD type) const
    {
        switch (type) {
        case CKRST_OBJ_TEXTURE:      return Textures.IsAlive(object);
        case CKRST_OBJ_VERTEXBUFFER: return VertexBuffers.IsAlive(object);
        case CKRST_OBJ_INDEXBUFFER:  return IndexBuffers.IsAlive(object);
        case CKRST_OBJ_SHADER:       return Shaders.IsAlive(object);
        case CKRST_OBJ_PROGRAM:      return Programs.IsAlive(object);
        case CKRST_OBJ_VERTEXLAYOUT: return VertexLayouts.IsAlive(object);
        case CKRST_OBJ_FRAMEBUFFER:  return FrameBuffers.IsAlive(object);
        default:                     return FALSE;
        }
    }

    void DestroyAll()
    {
        FrameBuffers.DestroyAll();
        Textures.DestroyAll();
        Programs.DestroyAll();
        Shaders.DestroyAll();
        VertexLayouts.DestroyAll();
        VertexBuffers.DestroyAll();
        IndexBuffers.DestroyAll();
    }

    CKBgfxResourceTable<CKBgfxShaderRecord> Shaders;
    CKBgfxResourceTable<CKBgfxProgramRecord> Programs;
    CKBgfxResourceTable<CKBgfxVertexLayoutRecord> VertexLayouts;
    CKBgfxResourceTable<CKBgfxVertexBufferRecord> VertexBuffers;
    CKBgfxResourceTable<CKBgfxIndexBufferRecord> IndexBuffers;
    CKBgfxResourceTable<CKBgfxTextureRecord> Textures;
    CKBgfxResourceTable<CKBgfxFrameBufferRecord> FrameBuffers;
};

#endif // CKBGFXRESOURCES_H
