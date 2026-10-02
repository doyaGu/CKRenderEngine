#include "CKSdlGpuDxilCodec.h"

#include <new>
#include <string.h>

// shaders/shader_pack.py defines the pack format; this decoder mirrors its
// DXIL coder step by step.

namespace CKSdlGpuPack {

namespace {

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

} // namespace

// Rebuilds the LLVM bitstream of a DXIL container from its events, numbering
// values like the bitcode writer to predict the operands of instructions.
class DxilBitcodeDecoder {
public:
    explicit DxilBitcodeDecoder(SymbolStreams &symbols) : m_Symbols(symbols) {}

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

DxilDecoder::~DxilDecoder()
{
    delete m_Bitcode;
}

CKBOOL DxilDecoder::Decode(CKBYTE *out, CKDWORD size)
{
    if (!m_Bitcode)
        m_Bitcode = new (std::nothrow) DxilBitcodeDecoder(m_Symbols);
    return m_Bitcode && m_Bitcode->Decode(out, size);
}

} // namespace CKSdlGpuPack
