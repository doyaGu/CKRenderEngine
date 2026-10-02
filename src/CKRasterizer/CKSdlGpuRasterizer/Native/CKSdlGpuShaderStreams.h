#ifndef CKSDLGPU_SHADER_STREAMS_H
#define CKSDLGPU_SHADER_STREAMS_H

#include "CKTypes.h"
#include "XArray.h"
#include <string.h>

// The symbol streams of the shader packs and the coding tools their codecs
// share. shaders/shader_pack.py defines the pack format.

namespace CKSdlGpuPack {

const CKDWORD CONTEXT_COUNT = 5u << 11;     // past every context of the codecs

inline void Store32(CKBYTE *p, CKDWORD value)
{
    p[0] = (CKBYTE)value; p[1] = (CKBYTE)(value >> 8);
    p[2] = (CKBYTE)(value >> 16); p[3] = (CKBYTE)(value >> 24);
}

inline CKQWORD Unzig(CKQWORD value)
{
    return value >> 1 ^ (0 - (value & 1));
}

struct ByteCursor {
    const CKBYTE *At;
    const CKBYTE *End;

    CKBOOL Varint(CKQWORD &value)
    {
        // Most symbols are short; their bytes need no 64-bit shifts.
        CKDWORD low = 0;
        int shift = 0;
        for (; shift < 28 && At < End; shift += 7) {
            const CKBYTE byte = *At++;
            low |= (CKDWORD)(byte & 0x7F) << shift;
            if (byte < 0x80) {
                value = low;
                return TRUE;
            }
        }
        value = low;
        for (; shift < 64 && At < End; shift += 7) {
            const CKBYTE byte = *At++;
            value |= (CKQWORD)(byte & 0x7F) << shift;
            if (byte < 0x80)
                return TRUE;
        }
        return FALSE;
    }
};

// The varint streams of a pack, one per coding context. A read past a
// stream fails every later read, which ends the decoding loops.
class SymbolStreams {
public:
    CKBOOL Failed;

    SymbolStreams() : Failed(FALSE)
    {
        m_Index.Resize(CONTEXT_COUNT);
        m_Index.Memset(0);
    }

    CKBOOL Add(CKQWORD context, const CKBYTE *data, CKDWORD size)
    {
        if (context >= CONTEXT_COUNT || m_Index[(int)context])
            return FALSE;
        ByteCursor stream = {data, data + size};
        m_Streams.PushBack(stream);
        m_Index[(int)context] = (CKWORD)m_Streams.Size();
        return TRUE;
    }

    CKQWORD operator()(CKDWORD context)
    {
        ByteCursor *stream = Find(context);
        if (stream && stream->At < stream->End && *stream->At < 0x80)
            return *stream->At++;
        CKQWORD value = 0;
        if (!stream || !stream->Varint(value))
            Fail();
        return value;
    }

    const CKBYTE *Bytes(CKDWORD context, CKDWORD size)
    {
        ByteCursor *stream = Find(context);
        if (!stream || (size_t)(stream->End - stream->At) < size) {
            Fail();
            return NULL;
        }
        const CKBYTE *data = stream->At;
        stream->At += size;
        return data;
    }

    CKBOOL Consumed() const
    {
        for (const ByteCursor *stream = m_Streams.Begin(); stream != m_Streams.End(); ++stream)
            if (stream->At != stream->End)
                return FALSE;
        return TRUE;
    }

    CKBOOL Fail()
    {
        Failed = TRUE;
        return FALSE;
    }

private:
    ByteCursor *Find(CKDWORD context)
    {
        if (Failed || context >= CONTEXT_COUNT || !m_Index[(int)context])
            return NULL;
        return &m_Streams[m_Index[(int)context] - 1];
    }

    XArray<CKWORD> m_Index;     // context -> 1 + stream, 0 for none
    XArray<ByteCursor> m_Streams;
};

// A move-to-front list. Items sit in blocks of at most BLOCK_ITEMS, so that
// moving one to the front costs O(BLOCK_ITEMS + rank / BLOCK_ITEMS) rather
// than O(rank). Blocks, and the items of each, are stored back to front so
// that the front is cheap. Emptied blocks leave their storage behind until
// the list compacts, which it does once at most half the storage is in use.
template <class T>
class RecencyList {
public:
    RecencyList() : m_Size(0) {}

    void Clear()
    {
        m_Items.Resize(0);
        m_Blocks.Resize(0);
        m_Size = 0;
    }

    void Push(T value)
    {
        if (m_Blocks.Size() == 0 || m_Blocks.Back().Count == BLOCK_ITEMS)
            AddBlock();
        Block &front = m_Blocks.Back();
        m_Items[(int)(front.First + front.Count++)] = value;
        ++m_Size;
    }

    // Moves the item of the given rank, 1 for the front, to the front.
    CKBOOL Take(CKQWORD rank, T &value)
    {
        if (rank == 0 || rank > m_Size)
            return FALSE;
        int block = m_Blocks.Size() - 1;
        CKDWORD remaining = (CKDWORD)rank;
        while (remaining > m_Blocks[block].Count)
            remaining -= m_Blocks[block--].Count;
        value = Remove(block, m_Blocks[block].Count - remaining);
        Push(value);
        return TRUE;
    }

    // Moves the value to the front, adding it if it is missing.
    void Touch(T value)
    {
        for (int block = m_Blocks.Size() - 1; block >= 0; --block) {
            const T *items = &m_Items[(int)m_Blocks[block].First];
            for (CKDWORD i = m_Blocks[block].Count; i-- > 0;) {
                if (items[i] == value) {
                    Remove(block, i);
                    Push(value);
                    return;
                }
            }
        }
        Push(value);
    }

private:
    enum { BLOCK_ITEMS = 32 };

    struct Block {
        CKDWORD First;
        CKDWORD Count;
    };

    T Remove(int block, CKDWORD index)
    {
        Block &from = m_Blocks[block];
        T *item = &m_Items[(int)(from.First + index)];
        const T value = *item;
        for (T *last = item + (from.Count - index - 1); item != last; ++item)
            item[0] = item[1];
        --m_Size;
        if (--from.Count == 0 && block != m_Blocks.Size() - 1)
            m_Blocks.RemoveAt(block);
        return value;
    }

    void AddBlock()
    {
        if ((CKDWORD)m_Items.Size() >= 2 * (m_Size + BLOCK_ITEMS))
            Compact();
        if (m_Blocks.Size() && m_Blocks.Back().Count < BLOCK_ITEMS)
            return;
        const int first = m_Items.Size();
        if (first + BLOCK_ITEMS > m_Items.Allocated())
            m_Items.Reserve(2 * first + BLOCK_ITEMS);
        m_Items.Resize(first + BLOCK_ITEMS);
        const Block block = {(CKDWORD)first, 0};
        m_Blocks.PushBack(block);
    }

    // Refills the blocks in order from the back, leaving the front block
    // the only one with room.
    void Compact()
    {
        m_Scratch.Resize(0);
        for (const Block *block = m_Blocks.Begin(); block != m_Blocks.End(); ++block)
            for (CKDWORD i = 0; i < block->Count; ++i)
                m_Scratch.PushBack(m_Items[(int)(block->First + i)]);
        m_Items.Resize(0);
        m_Blocks.Resize(0);
        for (CKDWORD at = 0; at < (CKDWORD)m_Scratch.Size(); at += BLOCK_ITEMS) {
            const CKDWORD count = (CKDWORD)m_Scratch.Size() - at;
            const Block block = {at, count < BLOCK_ITEMS ? count : (CKDWORD)BLOCK_ITEMS};
            m_Blocks.PushBack(block);
        }
        m_Items.Resize((int)(m_Blocks.Size() * BLOCK_ITEMS));
        if (m_Scratch.Size())
            memcpy(m_Items.Begin(), m_Scratch.Begin(), m_Scratch.Size() * sizeof(T));
    }

    XArray<T> m_Items;
    XArray<Block> m_Blocks;
    XArray<T> m_Scratch;
    CKDWORD m_Size;
};

} // namespace CKSdlGpuPack

#endif
