#include "CKSdlGpuShaderPack.h"

#include "CKSdlGpuDxilCodec.h"
#include "CKSdlGpuShaderStreams.h"
#include "shaders/generated/dxbc_pack.h"
#include "shaders/generated/dxil_pack.h"
#include "shaders/generated/spirv_pack.h"

#include <SDL3/SDL_endian.h>
#include <SDL3/SDL_mutex.h>
#include <new>
#include <string.h>

// shaders/shader_pack.py defines the pack format; the decoders below and in
// CKSdlGpuDxilCodec.cpp mirror its coders step by step.

using namespace CKSdlGpuPack;

namespace {

enum { CODEC_RAW, CODEC_SPIRV, CODEC_DXIL };

const CKDWORD PACK_VERSION = 2;
const CKDWORD PACK_HEADER_SIZE = 16;
const CKDWORD MAX_DECODED_SIZE = 1u << 26;

CKDWORD Load32(const CKBYTE *p)
{
    CKDWORD value;
    memcpy(&value, p, sizeof(value));
    return SDL_Swap32LE(value);
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

int SDLCALL DecodePacks(void *formats)
{
    for (EmbeddedPack &pack : s_Packs)
        if ((SDL_GPUShaderFormat)(uintptr_t)formats & pack.Format)
            LoadPack(pack.Format);
    return 0;
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

CKSdlGpuShaderPrefetch::CKSdlGpuShaderPrefetch(SDL_GPUShaderFormat Formats) : m_Thread(NULL)
{
    SDL_GPUShaderFormat pending = 0;
    for (EmbeddedPack &pack : s_Packs)
        if ((Formats & pack.Format) && SDL_GetAtomicInt(&pack.Init.status) == SDL_INIT_STATUS_UNINITIALIZED)
            pending |= pack.Format;
    if (pending)
        m_Thread = SDL_CreateThread(DecodePacks, "CKSdlGpuShaderPack", (void *)(uintptr_t)pending);
}

CKSdlGpuShaderPrefetch::~CKSdlGpuShaderPrefetch()
{
    if (m_Thread)
        SDL_WaitThread(m_Thread, NULL);
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
