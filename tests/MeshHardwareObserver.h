#ifndef MESH_HARDWARE_OBSERVER_H
#define MESH_HARDWARE_OBSERVER_H
#include <cstring>
#include "MaterialRenderObserver.h"

// Public resource-interface spy; does not execute any rasterizer backend body.
class MeshHardwareObserver : public MaterialRenderObserver {
public:
    CKVertexBufferDesc VertexDesc;
    CKIndexBufferDesc IndexDesc;
    CKBYTE VertexBytes[8192] = {};
    CKWORD IndexWords[512] = {};
    CKDWORD VertexHandle = 0, IndexHandle = 0, NextHandle = 101;
    int VertexCreates = 0, IndexCreates = 0, VertexLocks = 0, IndexLocks = 0;
    int VertexUnlocks = 0, IndexUnlocks = 0, VertexDeletes = 0, IndexDeletes = 0;
    mutable int VertexQueries = 0, IndexQueries = 0;
    bool FailVertexCreate = false, FailIndexCreate = false;
    bool FailVertexLock = false, FailIndexLock = false;
    bool FailVertexUnlock = false, FailIndexUnlock = false;
    bool FailVertexDelete = false, FailIndexDelete = false;
    CKDWORD LockedVertices = 0, LockedIndices = 0;
    CKBOOL CreateVertexBuffer(const CKVertexBufferDesc *desc, const void *data, CKDWORD *handle) override {
        ++VertexCreates;
        if (FailVertexCreate) return FALSE;
        VertexDesc = *desc; VertexHandle = NextHandle++; *handle = VertexHandle;
        std::memset(VertexBytes, 0xA5, sizeof(VertexBytes));
        if (data) {
            const std::size_t bytes = static_cast<std::size_t>(desc->m_CurrentVCount) * desc->m_VertexSize;
            EXPECT_LE(bytes, sizeof(VertexBytes));
            if (bytes <= sizeof(VertexBytes)) std::memcpy(VertexBytes, data, bytes);
        }
        return TRUE;
    }
    CKBOOL CreateIndexBuffer(const CKIndexBufferDesc *desc, const void *data, CKDWORD *handle) override {
        ++IndexCreates;
        if (FailIndexCreate) return FALSE;
        IndexDesc = *desc; IndexHandle = NextHandle++; *handle = IndexHandle;
        std::memset(IndexWords, 0xA5, sizeof(IndexWords));
        if (data) {
            EXPECT_LE(desc->m_CurrentICount, 512u);
            if (desc->m_CurrentICount <= 512u)
                std::memcpy(IndexWords, data, desc->m_CurrentICount * sizeof(CKWORD));
        }
        return TRUE;
    }
    CKBOOL GetVertexBufferDesc(CKDWORD handle, CKVertexBufferDesc *desc) const override {
        ++VertexQueries;
        if (!handle || handle != VertexHandle) return FALSE;
        *desc = VertexDesc; return TRUE;
    }
    CKBOOL GetIndexBufferDesc(CKDWORD handle, CKIndexBufferDesc *desc) const override {
        ++IndexQueries;
        if (!handle || handle != IndexHandle) return FALSE;
        *desc = IndexDesc; return TRUE;
    }
    void *LockVertexBuffer(CKDWORD handle, CKDWORD start, CKDWORD count, CKRST_LOCKFLAGS flags) override {
        ++VertexLocks; EXPECT_EQ(CKRST_LOCK_DISCARD, flags);
        if (FailVertexLock || handle != VertexHandle) return nullptr;
        EXPECT_LE(start + count, VertexDesc.m_MaxVertexCount);
        EXPECT_LE(static_cast<std::size_t>(start + count) * VertexDesc.m_VertexSize, sizeof(VertexBytes));
        if (static_cast<std::size_t>(start + count) * VertexDesc.m_VertexSize > sizeof(VertexBytes)) return nullptr;
        LockedVertices = count; return VertexBytes + start * VertexDesc.m_VertexSize;
    }
    void *LockIndexBuffer(CKDWORD handle, CKDWORD start, CKDWORD count, CKRST_LOCKFLAGS flags) override {
        ++IndexLocks; EXPECT_EQ(CKRST_LOCK_DISCARD, flags);
        if (FailIndexLock || handle != IndexHandle) return nullptr;
        EXPECT_LE(start + count, IndexDesc.m_MaxIndexCount); EXPECT_LE(start + count, 512u);
        if (start + count > 512u) return nullptr;
        LockedIndices = count; return IndexWords + start;
    }
    CKBOOL UnlockVertexBuffer(CKDWORD handle) override {
        ++VertexUnlocks; EXPECT_EQ(VertexHandle, handle); return !FailVertexUnlock;
    }
    CKBOOL UnlockIndexBuffer(CKDWORD handle) override {
        ++IndexUnlocks; EXPECT_EQ(IndexHandle, handle); return !FailIndexUnlock;
    }
    CKBOOL DeleteObject(CKRST_HANDLE handle, CKRST_OBJECTTYPE type) override {
        if (type == CKRST_OBJ_VERTEXBUFFER) {
            ++VertexDeletes;
            if (FailVertexDelete || !handle || handle != VertexHandle) return FALSE;
            VertexHandle = 0; return TRUE;
        }
        if (type == CKRST_OBJ_INDEXBUFFER) {
            ++IndexDeletes;
            if (FailIndexDelete || !handle || handle != IndexHandle) return FALSE;
            IndexHandle = 0; return TRUE;
        }
        ADD_FAILURE() << "Unexpected resource type"; return FALSE;
    }
};
#endif
