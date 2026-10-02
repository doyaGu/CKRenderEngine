#include "CKSdlGpuShaderPack.h"

#include "shaders/generated/dxbc_pack.h"
#include "shaders/generated/dxil_pack.h"
#include "shaders/generated/spirv_pack.h"

#include <SDL3/SDL_endian.h>
#include <SDL3/SDL_mutex.h>
#include <new>
#include <string.h>

// shaders/shader_pack.py defines the pack format; the decoders below mirror
// its coders step by step.

namespace {

enum { CODEC_RAW, CODEC_SPIRV, CODEC_DXIL };

const CKDWORD PACK_VERSION = 2;
const CKDWORD PACK_HEADER_SIZE = 16;
const CKDWORD MAX_DECODED_SIZE = 1u << 26;
const CKDWORD CONTEXT_COUNT = 5u << 11;     // past every context of the codecs

CKDWORD Load32(const CKBYTE *p)
{
    CKDWORD value;
    memcpy(&value, p, sizeof(value));
    return SDL_Swap32LE(value);
}

void Store32(CKBYTE *p, CKDWORD value)
{
    p[0] = (CKBYTE)value; p[1] = (CKBYTE)(value >> 8);
    p[2] = (CKBYTE)(value >> 16); p[3] = (CKBYTE)(value >> 24);
}

CKDWORD Rotl(CKDWORD value, int bits)
{
    return value << bits | value >> (32 - bits);
}

// XXH32 with seed 0, after the xxHash specification.
CKDWORD Xxh32(const CKBYTE *data, CKDWORD size)
{
    const CKDWORD P1 = 0x9E3779B1u, P2 = 0x85EBCA77u, P3 = 0xC2B2AE3Du, P4 = 0x27D4EB2Fu, P5 = 0x165667B1u;
    const CKBYTE *at = data, *end = data + size;
    CKDWORD value = P5;
    if (size >= 16) {
        CKDWORD a = P1 + P2, b = P2, c = 0, d = 0 - P1;
        for (; end - at >= 16; at += 16) {
            a = Rotl(a + Load32(at) * P2, 13) * P1;
            b = Rotl(b + Load32(at + 4) * P2, 13) * P1;
            c = Rotl(c + Load32(at + 8) * P2, 13) * P1;
            d = Rotl(d + Load32(at + 12) * P2, 13) * P1;
        }
        value = Rotl(a, 1) + Rotl(b, 7) + Rotl(c, 12) + Rotl(d, 18);
    }
    value += size;
    for (; end - at >= 4; at += 4)
        value = Rotl(value + Load32(at) * P3, 17) * P4;
    for (; at < end; ++at)
        value = Rotl(value + *at * P5, 11) * P1;
    value = (value ^ value >> 15) * P2;
    value = (value ^ value >> 13) * P3;
    return value ^ value >> 16;
}

CKQWORD Unzig(CKQWORD value)
{
    return value >> 1 ^ (0 - (value & 1));
}

// --------------------------------------------------------------------------
// LZMA1, after the reference decoder of the LZMA SDK (LzmaSpec.cpp). The
// output is the whole dictionary, so matches copy within it.

struct LzmaRange {
    const CKBYTE *In, *End;
    CKDWORD Range, Code;
    CKBOOL Failed;

    CKBYTE Next()
    {
        if (In == End) {
            Failed = TRUE;
            return 0;
        }
        return *In++;
    }

    CKBOOL Init(const CKBYTE *in, size_t size)
    {
        In = in; End = in + size;
        Range = 0xFFFFFFFFu; Code = 0; Failed = FALSE;
        const CKBOOL zero = Next() == 0;
        for (int i = 0; i < 4; ++i)
            Code = Code << 8 | Next();
        return zero && Code != Range && !Failed;
    }

    void Normalize()
    {
        if (Range < 1u << 24) {
            Range <<= 8;
            Code = Code << 8 | Next();
        }
    }

    CKDWORD Bit(CKWORD &prob)
    {
        const CKDWORD bound = (Range >> 11) * prob;
        CKDWORD bit;
        if (Code < bound) {
            prob = (CKWORD)(prob + ((2048 - prob) >> 5));
            Range = bound;
            bit = 0;
        } else {
            prob = (CKWORD)(prob - (prob >> 5));
            Code -= bound;
            Range -= bound;
            bit = 1;
        }
        Normalize();
        return bit;
    }

    CKDWORD Direct(int count)
    {
        CKDWORD result = 0;
        while (count-- > 0) {
            Range >>= 1;
            const CKDWORD bit = Code >= Range ? 1 : 0;
            if (bit)
                Code -= Range;
            Normalize();
            result = result << 1 | bit;
        }
        return result;
    }

    CKDWORD Tree(CKWORD *probs, int bits)
    {
        CKDWORD m = 1;
        for (int i = 0; i < bits; ++i)
            m = m << 1 | Bit(probs[m]);
        return m - (1u << bits);
    }

    CKDWORD ReverseTree(CKWORD *probs, int bits)
    {
        CKDWORD m = 1, symbol = 0;
        for (int i = 0; i < bits; ++i) {
            const CKDWORD bit = Bit(probs[m]);
            m = m << 1 | bit;
            symbol |= bit << i;
        }
        return symbol;
    }
};

struct LzmaLength {
    CKWORD Choice, Choice2, Low[16][8], Mid[16][8], High[256];
};

// Only probabilities, so Reset fills it as one array. Literal coders take
// 0x300 probabilities per state; packs limit lc + lp to 4.
struct LzmaModel {
    CKWORD IsMatch[12 << 4], IsRep[12], IsRepG0[12], IsRepG1[12], IsRepG2[12];
    CKWORD IsRep0Long[12 << 4], PosSlot[4][64], Pos[115], Align[16];
    LzmaLength Length, RepLength;
    CKWORD Literal[0x300 << 4];

    void Reset()
    {
        CKWORD *probs = &IsMatch[0];
        for (size_t i = 0; i < sizeof(LzmaModel) / sizeof(CKWORD); ++i)
            probs[i] = 1024;
    }
};

CKDWORD LzmaDecodeLength(LzmaRange &rc, LzmaLength &length, CKDWORD posState)
{
    if (!rc.Bit(length.Choice))
        return rc.Tree(length.Low[posState], 3);
    if (!rc.Bit(length.Choice2))
        return 8 + rc.Tree(length.Mid[posState], 3);
    return 16 + rc.Tree(length.High, 8);
}

CKDWORD LzmaDecodeDistance(LzmaRange &rc, LzmaModel &model, CKDWORD length)
{
    const CKDWORD slot = rc.Tree(model.PosSlot[length < 3 ? length : 3], 6);
    if (slot < 4)
        return slot;
    const int direct = (int)(slot >> 1) - 1;
    const CKDWORD distance = (2 | (slot & 1)) << direct;
    if (slot < 14)
        return distance + rc.ReverseTree(model.Pos + distance - slot, direct);
    return distance + (rc.Direct(direct - 4) << 4) + rc.ReverseTree(model.Align, 4);
}

CKBOOL LzmaDecode(const CKBYTE *in, size_t inSize, CKDWORD properties,
                  CKDWORD dictionary, CKBYTE *out, CKDWORD size)
{
    const CKDWORD lc = properties % 9, lp = properties / 9 % 5, pb = properties / 45;
    LzmaModel *model = new (std::nothrow) LzmaModel;
    if (!model)
        return FALSE;
    model->Reset();

    LzmaRange rc;
    const CKBOOL started = rc.Init(in, inSize);
    const CKDWORD pbMask = (1u << pb) - 1, lpMask = (1u << lp) - 1;
    CKDWORD pos = 0, state = 0, rep0 = 0, rep1 = 0, rep2 = 0, rep3 = 0;
    while (started && pos < size && !rc.Failed) {
        const CKDWORD posState = pos & pbMask;
        if (!rc.Bit(model->IsMatch[state << 4 | posState])) {
            const CKDWORD previous = pos ? out[pos - 1] : 0;
            CKWORD *probs = model->Literal + 0x300 * (((pos & lpMask) << lc) + (previous >> (8 - lc)));
            CKDWORD symbol = 1;
            if (state >= 7) {
                CKDWORD match = out[pos - rep0 - 1];
                do {
                    const CKDWORD matchBit = match >> 7 & 1;
                    match <<= 1;
                    const CKDWORD bit = rc.Bit(probs[((1 + matchBit) << 8) + symbol]);
                    symbol = symbol << 1 | bit;
                    if (matchBit != bit)
                        break;
                } while (symbol < 0x100);
            }
            while (symbol < 0x100)
                symbol = symbol << 1 | rc.Bit(probs[symbol]);
            out[pos++] = (CKBYTE)symbol;
            state = state < 4 ? 0 : (state < 10 ? state - 3 : state - 6);
            continue;
        }

        CKDWORD length;
        if (rc.Bit(model->IsRep[state])) {
            if (!pos)
                break;
            if (!rc.Bit(model->IsRepG0[state])) {
                if (!rc.Bit(model->IsRep0Long[state << 4 | posState])) {
                    state = state < 7 ? 9 : 11;
                    out[pos] = out[pos - rep0 - 1];
                    ++pos;
                    continue;
                }
            } else {
                CKDWORD distance;
                if (!rc.Bit(model->IsRepG1[state])) {
                    distance = rep1;
                } else {
                    if (!rc.Bit(model->IsRepG2[state])) {
                        distance = rep2;
                    } else {
                        distance = rep3;
                        rep3 = rep2;
                    }
                    rep2 = rep1;
                }
                rep1 = rep0;
                rep0 = distance;
            }
            length = LzmaDecodeLength(rc, model->RepLength, posState);
            state = state < 7 ? 8 : 11;
        } else {
            rep3 = rep2; rep2 = rep1; rep1 = rep0;
            length = LzmaDecodeLength(rc, model->Length, posState);
            state = state < 7 ? 7 : 10;
            // An end marker before the declared size fails here as well.
            rep0 = LzmaDecodeDistance(rc, *model, length);
            if (rep0 >= pos || rep0 >= dictionary)
                break;
        }
        length += 2;
        if (length > size - pos)
            break;
        const CKBYTE *from = out + pos - rep0 - 1;
        for (CKDWORD i = 0; i < length; ++i)
            out[pos + i] = from[i];
        pos += length;
    }
    delete model;
    return started && pos == size && !rc.Failed;
}

// --------------------------------------------------------------------------
// Symbol streams

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

// --------------------------------------------------------------------------
// SPIR-V

enum {
    SPIRV_SCHEMA, SPIRV_HEADER, SPIRV_OPCODE, SPIRV_COUNT, SPIRV_LITERAL,
    SPIRV_RESULT, SPIRV_ID, SPIRV_TYPE
};
enum {
    ROLE_LITERAL, ROLE_ID, ROLE_TYPE, ROLE_RESULT, ROLE_STRING,
    ROLE_LITERAL_ID, ROLE_ID_LITERAL, ROLE_ID_ID
};

class SpirvDecoder {
public:
    explicit SpirvDecoder(SymbolStreams &symbols) : m_Symbols(symbols), m_Out(NULL), m_At(0), m_Last(0) {}

    // The operand roles of the opcodes of the pack, coded ahead of the shaders.
    CKBOOL ReadSchema()
    {
        m_Opcodes.Resize(0x10000);
        m_Opcodes.Memset(0);
        const CKQWORD count = m_Symbols(SPIRV_SCHEMA);
        for (CKQWORD i = 0; i < count && !m_Symbols.Failed; ++i) {
            const CKQWORD opcode = m_Symbols(SPIRV_SCHEMA), flags = m_Symbols(SPIRV_SCHEMA);
            const CKQWORD descriptors = m_Symbols(SPIRV_SCHEMA);
            if (opcode > 0xFFFF || m_Opcodes[(int)opcode] || flags >= 16 || descriptors > 0xFFFF)
                return m_Symbols.Fail();
            Entry entry = {(CKDWORD)flags, (CKDWORD)m_Descriptors.Size(), (CKDWORD)descriptors};
            for (CKQWORD k = 0; k < descriptors && !m_Symbols.Failed; ++k) {
                const CKQWORD descriptor = m_Symbols(SPIRV_SCHEMA);
                if (descriptor >= 16)
                    return m_Symbols.Fail();
                m_Descriptors.PushBack((CKBYTE)descriptor);
            }
            m_Entries.PushBack(entry);
            m_Opcodes[(int)opcode] = (CKWORD)m_Entries.Size();
        }
        return !m_Symbols.Failed;
    }

    CKBOOL Decode(CKBYTE *out, CKDWORD size)
    {
        if (size % 4 || size < 20)
            return FALSE;
        const CKDWORD count = size / 4;
        m_Out = out;
        m_At = 0;
        for (int i = 0; i < 5; ++i)
            Emit(m_Symbols(SPIRV_HEADER));
        if (m_Symbols.Failed)
            return FALSE;
        // Ids are below the bound of the header; a bitmap of the ids in the
        // recency lists spares most searches of the lists.
        const CKDWORD bound = Load32(out + 12);
        m_Listed.Resize(bound < 1u << 20 ? (int)bound : 1 << 20);
        m_Listed.Memset(0);
        m_Ids.Clear();
        m_Types.Clear();
        m_Last = 0;
        while (m_At < count && !m_Symbols.Failed) {
            const CKQWORD opcode = m_Symbols(SPIRV_OPCODE), length = m_Symbols(SPIRV_COUNT);
            if (opcode > 0xFFFF || length >= 0xFFFF || length >= count - m_At)
                return m_Symbols.Fail();
            Emit((length + 1) << 16 | opcode);
            Entry entry = {ROLE_ID << 1, 0, 0};
            if (m_Opcodes[(int)opcode])
                entry = m_Entries[m_Opcodes[(int)opcode] - 1];
            Instruction(entry, (CKDWORD)length);
        }
        return !m_Symbols.Failed && m_At == count;
    }

private:
    struct Entry {
        CKDWORD Flags;          // bit 0: type declaration; bits 1-3: role of further words
        CKDWORD First;
        CKDWORD Count;
    };

    enum { LISTED_ID = 1, LISTED_TYPE = 2 };

    void Instruction(const Entry &entry, CKDWORD count)
    {
        static const CKBYTE pairs[3][2] = {
            {ROLE_LITERAL, ROLE_ID}, {ROLE_ID, ROLE_LITERAL}, {ROLE_ID, ROLE_ID}};
        const CKBOOL typeDeclaration = entry.Flags & 1;
        CKDWORD k = 0;
        for (CKDWORD d = 0; d < entry.Count; ++d) {
            const CKDWORD descriptor = m_Descriptors[(int)(entry.First + d)];
            const CKDWORD role = descriptor & 7;
            while (k < count && !m_Symbols.Failed) {
                if (role == ROLE_STRING) {
                    // A literal string ends with the word of its terminator.
                    while (k < count && !m_Symbols.Failed) {
                        ++k;
                        if (!(Operand(ROLE_LITERAL, typeDeclaration) >> 24))
                            break;
                    }
                } else if (role >= ROLE_LITERAL_ID) {
                    for (int part = 0; part < 2; ++part) {
                        if (k < count) {
                            Operand(pairs[role - ROLE_LITERAL_ID][part], typeDeclaration);
                            ++k;
                        }
                    }
                } else {
                    Operand(role, typeDeclaration);
                    ++k;
                }
                if (!(descriptor >> 3))
                    break;
            }
        }
        for (const CKDWORD tail = entry.Flags >> 1; k < count && !m_Symbols.Failed; ++k)
            Operand(tail, typeDeclaration);
    }

    CKDWORD Operand(CKDWORD role, CKBOOL typeDeclaration)
    {
        CKQWORD word;
        if (role == ROLE_LITERAL) {
            word = m_Symbols(SPIRV_LITERAL);
        } else if (role == ROLE_RESULT) {
            word = (CKQWORD)m_Last + 1 + Unzig(m_Symbols(SPIRV_RESULT));
            if (word > 0xFFFFFFFFu)
                return Fail();
            m_Last = (CKDWORD)word;
            Touch(m_Ids, LISTED_ID, m_Last);
            if (typeDeclaration)
                Touch(m_Types, LISTED_TYPE, m_Last);
        } else if (role == ROLE_TYPE) {
            word = MoveToFront(m_Types, LISTED_TYPE, SPIRV_TYPE);
        } else {
            word = MoveToFront(m_Ids, LISTED_ID, SPIRV_ID);
        }
        return Emit(word);
    }

    CKQWORD MoveToFront(RecencyList<CKDWORD> &list, CKBYTE flag, CKDWORD context)
    {
        const CKQWORD rank = m_Symbols(context);
        CKDWORD value = 0;
        if (rank) {
            if (!list.Take(rank, value))
                return Fail();
            return value;
        }
        const CKQWORD escaped = m_Last + Unzig(m_Symbols(context));
        if (escaped > 0xFFFFFFFFu)
            return Fail();
        value = (CKDWORD)escaped;
        if (value < (CKDWORD)m_Listed.Size())
            m_Listed[(int)value] |= flag;
        list.Push(value);
        return value;
    }

    void Touch(RecencyList<CKDWORD> &list, CKBYTE flag, CKDWORD value)
    {
        if (value < (CKDWORD)m_Listed.Size() && !(m_Listed[(int)value] & flag)) {
            m_Listed[(int)value] |= flag;
            list.Push(value);
        } else {
            list.Touch(value);
        }
    }

    CKDWORD Emit(CKQWORD word)
    {
        if (word > 0xFFFFFFFFu)
            return Fail();
        if (!m_Symbols.Failed)
            Store32(m_Out + 4 * m_At++, (CKDWORD)word);
        return (CKDWORD)word;
    }

    CKDWORD Fail()
    {
        m_Symbols.Fail();
        return 0;
    }

    SymbolStreams &m_Symbols;
    XArray<CKWORD> m_Opcodes;           // opcode -> 1 + entry, 0 for none
    XArray<Entry> m_Entries;
    XArray<CKBYTE> m_Descriptors;       // role | repeated << 3
    CKBYTE *m_Out;
    CKDWORD m_At;
    CKDWORD m_Last;                     // the last result id
    RecencyList<CKDWORD> m_Ids, m_Types;
    XArray<CKBYTE> m_Listed;
};

// --------------------------------------------------------------------------
// DXIL

enum {
    DXIL_CONTAINER, DXIL_SPLIT, DXIL_BLOCK, DXIL_DEFINE, DXIL_VALUE,
    DXIL_GLOBAL, DXIL_LOCAL, DXIL_BB, DXIL_TYPE, DXIL_FORWARD_TYPE
};
enum { DXIL_EVENT = 1, DXIL_ABBREV, DXIL_COUNT, DXIL_LITERAL };
enum { EVENT_END, EVENT_ENTER, EVENT_DEFINE, EVENT_RECORD };
enum { OP_LITERAL, OP_FIXED, OP_VBR, OP_ARRAY, OP_CHAR6, OP_BLOB };

// LLVM 3.7 block and record ids of the value numbering.
const CKQWORD BLOCK_TOP = ~(CKQWORD)0, BLOCK_INFO = 0, BLOCK_MODULE = 8;
const CKQWORD BLOCK_CONSTANTS = 11, BLOCK_FUNCTION = 12, BLOCK_TYPES = 17;
const CKQWORD TYPE_VOID = 2, TYPE_FUNCTION = 21;
const CKQWORD NO_VALUE_CODES = 1ull << 1 | 1ull << 10 | 1ull << 11 | 1ull << 12 | 1ull << 15 |
                               1ull << 24 | 1ull << 31 | 1ull << 33 | 1ull << 35 | 1ull << 36 |
                               1ull << 39 | 1ull << 42 | 1ull << 44 | 1ull << 45;
const CKQWORD TERMINATOR_CODES = 1ull << 10 | 1ull << 11 | 1ull << 12 | 1ull << 13 |
                                 1ull << 15 | 1ull << 31 | 1ull << 39;

CKBOOL InCodes(CKQWORD codes, CKQWORD code)
{
    return code < 64 && (codes >> code & 1);
}

CKDWORD Context(CKDWORD kind, CKQWORD block, CKQWORD code = 0)
{
    return kind << 11 | (CKDWORD)(block < 31 ? block : 31) << 6 | (CKDWORD)(code < 63 ? code : 63);
}

int Char6Index(CKQWORD c)
{
    if (c >= 'a' && c <= 'z') return (int)(c - 'a');
    if (c >= 'A' && c <= 'Z') return (int)(c - 'A') + 26;
    if (c >= '0' && c <= '9') return (int)(c - '0') + 52;
    return c == '.' ? 62 : (c == '_' ? 63 : -1);
}

// Rebuilds the LLVM bitstream of a DXIL container from its events, numbering
// values like the bitcode writer to predict the operands of instructions.
class DxilDecoder {
public:
    explicit DxilDecoder(SymbolStreams &symbols) : m_Symbols(symbols) {}

    CKBOOL Decode(CKBYTE *out, CKDWORD size)
    {
        const CKQWORD start = m_Symbols(DXIL_SPLIT), length = m_Symbols(DXIL_SPLIT);
        if (m_Symbols.Failed || length > size || start > size - length)
            return FALSE;
        const CKDWORD rest = size - (CKDWORD)length;
        const CKBYTE *container = m_Symbols.Bytes(DXIL_CONTAINER, rest);
        if (!container)
            return FALSE;
        memcpy(out, container, (size_t)start);
        memcpy(out + start + length, container + start, (size_t)(rest - start));

        m_Bits = out + start; m_Capacity = (CKDWORD)length;
        m_Byte = 0; m_Acc = 0; m_Pending = 0;
        Scope top = {BLOCK_TOP, 2, 0, 0};
        m_Scopes.Resize(0); m_Scopes.PushBack(top);
        m_Abbrevs.Resize(0); m_Specs.Resize(0); m_SpecOps.Resize(0);
        m_BlockInfo.Resize(0); m_BlockInfoTarget = BLOCK_TOP;
        m_Types.Resize(0); m_Bodies.Resize(0); m_Body = 0;
        m_Globals = m_Constants = m_Valno = m_Local = m_Bb = 0;
        m_Ids.Clear();
        const CKQWORD end = length * 8;
        while (!m_Symbols.Failed && !(m_Scopes.Size() == 1 && Position() >= end))
            Event();
        return !m_Symbols.Failed && Position() == end;
    }

private:
    struct Scope {
        CKQWORD Block;
        CKDWORD Width;          // abbreviation id width
        CKDWORD Abbrevs;        // first abbreviation of the block in m_Abbrevs
        CKDWORD Length;         // offset of the length word of the block
    };
    struct Spec {
        CKDWORD First;          // operands in m_SpecOps, value << 3 | encoding
        CKDWORD Count;
    };
    struct BlockInfo {
        CKQWORD Block;
        CKDWORD Spec;
    };
    struct Type {
        CKQWORD Code;
        CKDWORD Count;
        CKQWORD Op1;            // the return type of a function type
    };

    void Event()
    {
        const Scope scope = m_Scopes.Back();
        const CKQWORD symbol = m_Symbols(Context(DXIL_EVENT, scope.Block));
        if (symbol == EVENT_END)
            End(scope);
        else if (symbol == EVENT_ENTER)
            Enter(scope);
        else if (symbol == EVENT_DEFINE)
            Define(scope);
        else
            Record(scope, symbol - EVENT_RECORD);
    }

    void End(const Scope &scope)
    {
        if (m_Scopes.Size() == 1) {
            m_Symbols.Fail();
            return;
        }
        if (scope.Block == BLOCK_CONSTANTS && m_Scopes.Size() == 3 && m_Scopes[1].Block == BLOCK_MODULE)
            m_Constants = m_Valno - m_Globals;
        Write(0, scope.Width);
        Align();
        if (!m_Symbols.Failed)
            Store32(m_Bits + scope.Length, (m_Byte - scope.Length - 4) / 4);
        m_Abbrevs.Resize((int)scope.Abbrevs);
        m_Scopes.PopBack();
    }

    void Enter(const Scope &scope)
    {
        const CKQWORD block = m_Symbols(DXIL_BLOCK), width = m_Symbols(DXIL_BLOCK);
        if (width < 1 || width > 32) {
            m_Symbols.Fail();
            return;
        }
        if (block == BLOCK_FUNCTION) {
            CKQWORD args = 0;
            if (m_Body < (CKDWORD)m_Bodies.Size()) {
                const CKQWORD fn = m_Bodies[(int)m_Body];
                if (fn < (CKQWORD)m_Types.Size() && m_Types[(int)fn].Code == TYPE_FUNCTION)
                    args = m_Types[(int)fn].Count > 2 ? m_Types[(int)fn].Count - 2 : 0;
            }
            ++m_Body;
            m_Local = m_Globals + m_Constants;
            m_Valno = m_Local + args;
            m_Ids.Clear();
            m_Bb = 0;
        } else if (block == BLOCK_CONSTANTS && m_Scopes.Size() == 2 && m_Scopes[1].Block == BLOCK_MODULE) {
            m_Valno = m_Globals;
        }
        Write(EVENT_ENTER, scope.Width);
        Vbr(block, 8);
        Vbr(width, 4);
        Align();
        Scope inner = {block, (CKDWORD)width, (CKDWORD)m_Abbrevs.Size(), m_Byte};
        Write(0, 32);
        m_Scopes.PushBack(inner);
        for (const BlockInfo *info = m_BlockInfo.Begin(); info != m_BlockInfo.End(); ++info)
            if (info->Block == block)
                m_Abbrevs.PushBack(info->Spec);
    }

    void Define(const Scope &scope)
    {
        const CKQWORD count = m_Symbols(DXIL_DEFINE);
        Write(EVENT_DEFINE, scope.Width);
        Vbr(count, 5);
        Spec spec = {(CKDWORD)m_SpecOps.Size(), 0};
        for (CKQWORD k = 0; k < count && !m_Symbols.Failed; ++k) {
            const CKQWORD op = m_Symbols(DXIL_DEFINE);
            const CKDWORD encoding = (CKDWORD)(op & 7);
            m_SpecOps.PushBack(op);
            ++spec.Count;
            if (encoding == OP_LITERAL) {
                Write(1, 1);
                Vbr(op >> 3, 8);
            } else {
                Write(0, 1);
                Write(encoding, 3);
                if (encoding == OP_FIXED || encoding == OP_VBR)
                    Vbr(op >> 3, 5);
            }
        }
        m_Specs.PushBack(spec);
        const CKDWORD id = (CKDWORD)m_Specs.Size() - 1;
        if (scope.Block != BLOCK_INFO) {
            m_Abbrevs.PushBack(id);
        } else if (m_BlockInfoTarget != BLOCK_TOP) {
            BlockInfo info = {m_BlockInfoTarget, id};
            m_BlockInfo.PushBack(info);
        }
    }

    void Record(const Scope &scope, CKQWORD code)
    {
        const CKQWORD abbrev = m_Symbols(Context(DXIL_ABBREV, scope.Block, code));
        const Spec *spec = NULL;
        if (abbrev) {
            if (abbrev > (CKQWORD)m_Abbrevs.Size() - scope.Abbrevs) {
                m_Symbols.Fail();
                return;
            }
            spec = &m_Specs[(int)m_Abbrevs[(int)(scope.Abbrevs + abbrev - 1)]];
        }
        if (!spec || VariableCount(*spec)) {
            m_Count = m_Symbols(Context(DXIL_COUNT, scope.Block, code));
        } else if (spec->Count) {
            m_Count = spec->Count - 1;
        } else {
            m_Symbols.Fail();
            return;
        }
        m_Fields.Resize(0);
        if (scope.Block == BLOCK_FUNCTION)
            Instruction(code);
        Rest(Context(DXIL_LITERAL, scope.Block, code));

        const CKDWORD count = (CKDWORD)m_Fields.Size();
        if (scope.Block == BLOCK_TYPES) {
            if (code != 1 && code != 19) {          // NUMENTRY, STRUCT_NAME
                Type type = {code, count, count > 1 ? m_Fields[1] : 0};
                m_Types.PushBack(type);
            }
        } else if (scope.Block == BLOCK_MODULE) {
            if (code == 7 || code == 8 || code == 9 || code == 14) {  // globals, functions, aliases
                ++m_Globals;
                if (code == 8 && count > 2 && m_Fields[2] == 0)
                    m_Bodies.PushBack(m_Fields[0]);
            }
        } else if (scope.Block == BLOCK_CONSTANTS && code != 1) {   // SETTYPE
            ++m_Valno;
        }

        Write(3 + abbrev, scope.Width);
        if (!spec) {
            Vbr(code, 6);
            Vbr(count, 6);
            for (CKDWORD i = 0; i < count; ++i)
                Vbr(m_Fields[(int)i], 6);
        } else {
            Abbreviated(*spec, code);
        }
        if (scope.Block == BLOCK_INFO && code == 1 && count)  // SETBID
            m_BlockInfoTarget = m_Fields[0];
    }

    CKBOOL VariableCount(const Spec &spec) const
    {
        for (CKDWORD k = 0; k < spec.Count; ++k) {
            const CKQWORD encoding = m_SpecOps[(int)(spec.First + k)] & 7;
            if (encoding == OP_ARRAY || encoding == OP_BLOB)
                return TRUE;
        }
        return FALSE;
    }

    // Writes the code and fields of a record through its abbreviation.
    void Abbreviated(const Spec &spec, CKQWORD code)
    {
        const CKDWORD total = 1 + (CKDWORD)m_Fields.Size();
        CKDWORD at = 0;
        for (CKDWORD k = 0; k < spec.Count && !m_Symbols.Failed; ++k) {
            const CKQWORD op = m_SpecOps[(int)(spec.First + k)];
            const CKDWORD encoding = (CKDWORD)(op & 7);
            if (encoding == OP_ARRAY) {
                if (k + 1 >= spec.Count) {
                    m_Symbols.Fail();
                    return;
                }
                const CKQWORD element = m_SpecOps[(int)(spec.First + k + 1)];
                Vbr(total - at, 6);
                for (; at < total; ++at)
                    Scalar((CKDWORD)(element & 7), element >> 3, Value(code, at));
                return;
            }
            if (encoding == OP_BLOB) {
                Vbr(total - at, 6);
                Align();
                for (; at < total; ++at)
                    Write(Value(code, at), 8);
                Align();
                return;
            }
            if (at >= total || (encoding == OP_LITERAL && Value(code, at) != op >> 3)) {
                m_Symbols.Fail();
                return;
            }
            if (encoding != OP_LITERAL)
                Scalar(encoding, op >> 3, Value(code, at));
            ++at;
        }
        if (at != total)
            m_Symbols.Fail();
    }

    CKQWORD Value(CKQWORD code, CKDWORD at) const
    {
        return at ? m_Fields[(int)at - 1] : code;
    }

    void Scalar(CKDWORD encoding, CKQWORD width, CKQWORD value)
    {
        if (encoding == OP_FIXED && width <= 64) {
            if (width > 32) {
                Write(value & 0xFFFFFFFFu, 32);
                Write(value >> 32, (CKDWORD)width - 32);
            } else {
                Write(value, (CKDWORD)width);
            }
        } else if (encoding == OP_VBR && width <= 32) {
            Vbr(value, (CKDWORD)width);
        } else if (encoding == OP_CHAR6 && Char6Index(value) >= 0) {
            Write((CKQWORD)Char6Index(value), 6);
        } else {
            m_Symbols.Fail();
        }
    }

    // Instruction operands, following the FUNCTION_BLOCK records of LLVM 3.7.
    void Instruction(CKQWORD code)
    {
        const CKDWORD literal = Context(DXIL_LITERAL, BLOCK_FUNCTION, code);
        CKBOOL produces = !InCodes(NO_VALUE_CODES, code);
        switch (code) {
        case 34: {      // CALL [attrs, cc, fnty, callee, args...]
            CKQWORD cc = 0, type = 0;
            Literal(literal);
            const CKBOOL typed = Literal(literal, &cc) && (cc & 1 << 15) && Literal(DXIL_TYPE, &type);
            TypedValue();
            while (More())
                RelativeValue(FALSE);
            produces = !typed || !ReturnsVoid(type);
            break;
        }
        case 16:        // PHI [ty, (value, bb)*]
            Literal(DXIL_TYPE);
            while (More(2)) {
                RelativeValue(TRUE);
                BasicBlock();
            }
            break;
        case 2:         // BINOP [a+ty, b, ...]
        case 28:        // CMP2
            TypedValue();
            RelativeValue(FALSE);
            break;
        case 3:         // CAST [a+ty, ty, ...]
        case 20:        // LOAD
            TypedValue();
            Literal(DXIL_TYPE);
            break;
        case 7:         // INSERTELT [a+ty, b, index+ty]
        case 29:        // VSELECT [a+ty, b, cond+ty]
            TypedValue();
            RelativeValue(FALSE);
            TypedValue();
            break;
        case 6:         // EXTRACTELT [a+ty, index+ty]
        case 27:        // INSERTVAL [a+ty, b+ty, ...]
        case 44:        // STORE
            TypedValue();
            TypedValue();
            break;
        case 26:        // EXTRACTVAL [a+ty, indices...]
            TypedValue();
            break;
        case 8:         // SHUFFLEVEC [a+ty, b, mask]
            TypedValue();
            RelativeValue(FALSE);
            RelativeValue(FALSE);
            break;
        case 43:        // GEP [inbounds, ty, (value+ty)*]
            Literal(literal);
            Literal(DXIL_TYPE);
            while (More())
                TypedValue();
            break;
        case 11:        // BR [bb, (bb, cond)]
            BasicBlock();
            if (m_Count == 3) {
                BasicBlock();
                RelativeValue(FALSE);
            }
            break;
        case 10:        // RET [(value+ty)*]
            while (More())
                TypedValue();
            break;
        case 12:        // SWITCH [ty, cond, ...]
            Literal(DXIL_TYPE);
            RelativeValue(FALSE);
            break;
        default:
            break;
        }
        if (InCodes(TERMINATOR_CODES, code))
            ++m_Bb;
        if (produces)
            ++m_Valno;
    }

    CKBOOL ReturnsVoid(CKQWORD type) const
    {
        if (type >= (CKQWORD)m_Types.Size())
            return FALSE;
        const Type &function = m_Types[(int)type];
        if (function.Code != TYPE_FUNCTION || function.Count < 2 || function.Op1 >= (CKQWORD)m_Types.Size())
            return FALSE;
        return m_Types[(int)function.Op1].Code == TYPE_VOID;
    }

    CKBOOL More(CKQWORD n = 1) const
    {
        return !m_Symbols.Failed && (CKQWORD)m_Fields.Size() + n <= m_Count;
    }

    CKBOOL Literal(CKDWORD context, CKQWORD *value = NULL)
    {
        if (!More())
            return FALSE;
        const CKQWORD field = m_Symbols(context);
        m_Fields.PushBack(field);
        if (value)
            *value = field;
        return TRUE;
    }

    void Rest(CKDWORD context)
    {
        while (More())
            Literal(context);
    }

    // A value operand relative to the next value number: a recent value by
    // rank, a global or a raw relative number.
    CKBOOL RelativeValue(CKBOOL isSigned, CKQWORD *rawOut = NULL)
    {
        if (!More())
            return FALSE;
        const CKQWORD symbol = m_Symbols(DXIL_VALUE);
        CKQWORD raw, value;
        if (symbol == 1) {
            raw = m_Symbols(DXIL_LOCAL);
            const CKQWORD distance = isSigned ? raw >> 1 : raw;
            if (!(isSigned && (raw & 1)) && distance > 0 && distance <= m_Valno)
                m_Ids.Push(m_Valno - distance);
        } else {
            if (symbol == 0)
                value = m_Symbols(DXIL_GLOBAL);
            else if (!m_Ids.Take(symbol - 1, value))
                return m_Symbols.Fail();
            if (value > m_Valno)
                return m_Symbols.Fail();
            if (symbol == 0)
                m_Ids.Push(value);
            raw = isSigned ? (m_Valno - value) << 1 : m_Valno - value;
        }
        m_Fields.PushBack(raw);
        if (rawOut)
            *rawOut = raw;
        return TRUE;
    }

    // A value with its type when it is a forward reference.
    void TypedValue()
    {
        CKQWORD raw;
        if (RelativeValue(FALSE, &raw) && (raw == 0 || raw >= 1u << 31))
            Literal(DXIL_FORWARD_TYPE);
    }

    void BasicBlock()
    {
        if (More())
            m_Fields.PushBack(m_Bb + Unzig(m_Symbols(DXIL_BB)));
    }

    CKQWORD Position() const
    {
        return (CKQWORD)m_Byte * 8 + m_Pending;
    }

    // Appends a field of up to 32 bits, which the value must fit. Fewer
    // than 8 bits stay pending, so 32-bit arithmetic suffices.
    void Write(CKQWORD value, CKDWORD bits)
    {
        const CKDWORD word = (CKDWORD)value;
        if (bits > 32 || (CKDWORD)(value >> 32) || (bits < 32 && word >> bits)) {
            m_Symbols.Fail();
            return;
        }
        CKDWORD low = m_Acc | word << m_Pending;
        CKDWORD high = m_Pending ? word >> (32 - m_Pending) : 0;
        for (m_Pending += bits; m_Pending >= 8; m_Pending -= 8) {
            if (m_Byte >= m_Capacity) {
                m_Symbols.Fail();
                return;
            }
            m_Bits[m_Byte++] = (CKBYTE)low;
            low = low >> 8 | high << 24;
            high >>= 8;
        }
        m_Acc = low;
    }

    void Vbr(CKQWORD value, CKDWORD bits)
    {
        if (bits < 2 || bits > 32) {
            m_Symbols.Fail();
            return;
        }
        const CKDWORD payload = bits - 1, high = 1u << payload;
        for (; value >> 32 && !m_Symbols.Failed; value >>= payload)
            Write(((CKDWORD)value & (high - 1)) | high, bits);
        CKDWORD word = (CKDWORD)value;
        for (; word >= high && !m_Symbols.Failed; word >>= payload)
            Write((word & (high - 1)) | high, bits);
        Write(word, bits);
    }

    void Align()
    {
        Write(0, (CKDWORD)(0 - Position()) & 31);
    }

    SymbolStreams &m_Symbols;

    CKBYTE *m_Bits;
    CKDWORD m_Capacity;
    CKDWORD m_Byte;
    CKDWORD m_Acc;
    CKDWORD m_Pending;

    XArray<Scope> m_Scopes;
    XArray<CKDWORD> m_Abbrevs;          // abbreviations in scope, by block
    XArray<Spec> m_Specs;
    XArray<CKQWORD> m_SpecOps;
    XArray<BlockInfo> m_BlockInfo;
    CKQWORD m_BlockInfoTarget;

    XArray<Type> m_Types;
    XArray<CKQWORD> m_Bodies;           // types of the functions with bodies
    CKDWORD m_Body;
    CKQWORD m_Globals, m_Constants, m_Valno, m_Local, m_Bb;
    RecencyList<CKQWORD> m_Ids;

    XArray<CKQWORD> m_Fields;           // the operands of the current record
    CKQWORD m_Count;
};

// --------------------------------------------------------------------------
// Packs

CKBOOL DecodePack(const CKBYTE *pack, size_t size, CKSdlGpuShaderArtifacts &out)
{
    if (!pack || size < PACK_HEADER_SIZE || memcmp(pack, "CKSP", 4) != 0)
        return FALSE;
    const CKDWORD codec = pack[5], properties = pack[6];
    const CKDWORD dictionary = Load32(pack + 8), payloadSize = Load32(pack + 12);
    if (pack[4] != PACK_VERSION || codec > CODEC_DXIL || properties >= 225 ||
        properties % 9 + properties / 9 % 5 > 4 || payloadSize > MAX_DECODED_SIZE)
        return FALSE;
    XArray<CKBYTE> payload;
    payload.Resize((int)payloadSize);
    if (!LzmaDecode(pack + PACK_HEADER_SIZE, size - PACK_HEADER_SIZE, properties, dictionary,
                    payload.Begin(), payloadSize))
        return FALSE;

    // Shader sizes, stream contexts and sizes, shader checksums, streams.
    ByteCursor directory = {payload.Begin(), payload.End()};
    CKQWORD shaders, streams, value;
    if (!directory.Varint(shaders) || shaders > payloadSize)
        return FALSE;
    CKDWORD total = 0, present = 0;
    out.Offsets.PushBack(0);
    for (CKQWORD i = 0; i < shaders; ++i) {
        if (!directory.Varint(value) || value > MAX_DECODED_SIZE - total)
            return FALSE;
        total += (CKDWORD)value;
        present += value ? 1 : 0;
        out.Offsets.PushBack(total);
    }
    if (!directory.Varint(streams) || streams > payloadSize)
        return FALSE;
    XArray<CKQWORD> layout;
    CKQWORD data = 0;
    for (CKQWORD i = 0; i < 2 * streams; ++i) {
        if (!directory.Varint(value))
            return FALSE;
        layout.PushBack(value);
        if (i & 1)
            data += value;
    }
    const CKBYTE *checksums = directory.At;
    if ((size_t)(directory.End - checksums) < 4 * (size_t)present)
        return FALSE;
    const CKBYTE *at = checksums + 4 * (size_t)present;
    if (data != (CKQWORD)(directory.End - at))
        return FALSE;
    SymbolStreams symbols;
    for (int i = 0; i < layout.Size(); i += 2) {
        if (!symbols.Add(layout[i], at, (CKDWORD)layout[i + 1]))
            return FALSE;
        at += layout[i + 1];
    }

    out.Code.Resize((int)total);
    SpirvDecoder spirv(symbols);
    DxilDecoder dxil(symbols);
    if (codec == CODEC_SPIRV && !spirv.ReadSchema())
        return FALSE;
    for (CKQWORD i = 0; i < shaders; ++i) {
        const CKDWORD begin = out.Offsets[(int)i], length = out.Offsets[(int)i + 1] - begin;
        if (!length)
            continue;
        CKBYTE *code = out.Code.Begin() + begin;
        CKBOOL decoded;
        if (codec == CODEC_SPIRV) {
            decoded = spirv.Decode(code, length);
        } else if (codec == CODEC_DXIL) {
            decoded = dxil.Decode(code, length);
        } else {
            const CKBYTE *raw = symbols.Bytes(0, length);
            decoded = raw != NULL;
            if (raw)
                memcpy(code, raw, length);
        }
        if (!decoded || symbols.Failed || Xxh32(code, length) != Load32(checksums))
            return FALSE;
        checksums += 4;
    }
    return symbols.Consumed();
}

struct EmbeddedPack {
    EmbeddedPack(SDL_GPUShaderFormat format, const CKBYTE *data, size_t size)
        : Format(format), Data(data), Size(size), Init(), Valid(FALSE) {}

    SDL_GPUShaderFormat Format;
    const CKBYTE *Data;
    size_t Size;
    SDL_InitState Init;
    CKBOOL Valid;
    CKSdlGpuShaderArtifacts Artifacts;
};

EmbeddedPack s_Packs[] = {
    {SDL_GPU_SHADERFORMAT_DXIL, s_sdl_dxil_pack, sizeof(s_sdl_dxil_pack)},
    {SDL_GPU_SHADERFORMAT_SPIRV, s_sdl_spirv_pack, sizeof(s_sdl_spirv_pack)},
    {SDL_GPU_SHADERFORMAT_DXBC, s_sdl_dxbc_pack, sizeof(s_sdl_dxbc_pack)},
};

const EmbeddedPack *LoadPack(SDL_GPUShaderFormat format)
{
    for (EmbeddedPack &pack : s_Packs) {
        if (pack.Format != format)
            continue;
        if (SDL_ShouldInit(&pack.Init)) {
            pack.Valid = CKSdlGpuDecodeShaderPack(pack.Data, pack.Size, pack.Artifacts) &&
                         pack.Artifacts.Offsets.Size() == CKSDL_SHADER_COUNT + 1;
            SDL_SetInitialized(&pack.Init, true);
        }
        return pack.Valid ? &pack : NULL;
    }
    return NULL;
}

} // namespace

CKBOOL CKSdlGpuDecodeShaderPack(const CKBYTE *Pack, size_t Size, CKSdlGpuShaderArtifacts &Out)
{
    Out.Code.Clear();
    Out.Offsets.Clear();
    if (DecodePack(Pack, Size, Out))
        return TRUE;
    Out.Code.Clear();
    Out.Offsets.Clear();
    return FALSE;
}

CKBOOL CKSdlGpuLoadShaders(SDL_GPUShaderFormat Format)
{
    return LoadPack(Format) != NULL;
}

CKBOOL CKSdlGpuShaderCode(SDL_GPUShaderFormat Format, CKSdlShader Shader,
                          const CKBYTE *&Code, CKDWORD &Size)
{
    Code = NULL;
    Size = 0;
    const EmbeddedPack *pack = LoadPack(Format);
    if (!pack || Shader < 0 || Shader >= CKSDL_SHADER_COUNT)
        return FALSE;
    const CKDWORD begin = pack->Artifacts.Offsets[Shader], end = pack->Artifacts.Offsets[Shader + 1];
    if (begin == end)
        return FALSE;
    Code = pack->Artifacts.Code.Begin() + begin;
    Size = end - begin;
    return TRUE;
}
