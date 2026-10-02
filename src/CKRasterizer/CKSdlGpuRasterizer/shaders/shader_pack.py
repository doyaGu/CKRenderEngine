"""Compact packs of the precompiled SDL GPU shaders.

A pack holds every artifact of one shader format. The artifacts are variants
of a few programs, but each one numbers its ids, values and types on its own,
which hides most of what they share from a generic compressor. The codecs
therefore first rewrite the artifacts into streams of small symbols that
repeat across variants:

* SPIR-V instructions are split into opcodes, word counts, literals, result
  ids relative to the previous result, and id operands coded by their
  move-to-front rank. Operand roles come from a schema stored in the pack, so
  decoders need no SPIR-V grammar.
* DXIL containers are kept as they are except for the LLVM bitcode, which is
  parsed into records. Relative value operands become move-to-front ranks
  over absolute value numbers, branch targets become relative to the current
  basic block, and the remaining fields go to streams per block and record
  code.
* DXBC containers are stored as they are.

All streams of a pack are compressed together as raw LZMA1. decode() mirrors
CKSdlGpuShaderPack.cpp, and encode() proves that every pack decodes to its
artifacts before returning it.

Pack layout (little endian):
    'CKSP', u8 version, u8 codec, u8 LZMA properties, u8 0,
    u32 dictionary size, u32 decoded size, LZMA1 payload.
The decoded payload starts with varints: the shader count, then per shader
its size (0 when absent), then the stream count and per stream its context
and size. XXH32 checksums (u32, seed 0) of the present shaders and the
streams in that order follow.
"""
from __future__ import annotations

import itertools
import json
import lzma
import struct

MAGIC = b"CKSP"
VERSION = 2
RAW, SPIRV, DXIL = range(3)
CODECS = {"dxbc": RAW, "spirv": SPIRV, "dxil": DXIL}

# SPIR-V stream contexts.
SPIRV_SCHEMA, SPIRV_HEADER, SPIRV_OPCODE, SPIRV_COUNT, SPIRV_LITERAL, \
    SPIRV_RESULT, SPIRV_ID, SPIRV_TYPE = range(8)
# SPIR-V operand roles: literal, id, result type, result, literal string and
# the pairs literal-id, id-literal and id-id.
ROLE_LITERAL, ROLE_ID, ROLE_TYPE, ROLE_RESULT, ROLE_STRING, \
    ROLE_LITERAL_ID, ROLE_ID_LITERAL, ROLE_ID_ID = range(8)
_PAIRS = {ROLE_LITERAL_ID: (ROLE_LITERAL, ROLE_ID),
          ROLE_ID_LITERAL: (ROLE_ID, ROLE_LITERAL),
          ROLE_ID_ID: (ROLE_ID, ROLE_ID)}

# DXIL stream contexts. Contexts of a block and record code are
# kind << 11 | block << 6 | code; the last block and code slots are shared by
# all larger ones, and the last block slot also stands for the top level.
DXIL_CONTAINER, DXIL_SPLIT, DXIL_BLOCK, DXIL_DEFINE, DXIL_VALUE, \
    DXIL_GLOBAL, DXIL_LOCAL, DXIL_BB, DXIL_TYPE, DXIL_FORWARD_TYPE = range(10)
DXIL_EVENT, DXIL_ABBREV, DXIL_COUNT, DXIL_LITERAL = range(1, 5)

# Bitstream events: END_BLOCK, ENTER_SUBBLOCK, DEFINE_ABBREV and records.
END, ENTER, DEFINE, RECORD = range(4)
# Abbreviation operand encodings; 0 marks a literal operand.
LITERAL, FIXED, VBR, ARRAY, CHAR6, BLOB = range(6)
_CHAR6 = b"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._"
_CHAR6_INDEX = {c: i for i, c in enumerate(_CHAR6)}
BITCODE_MAGIC = b"BC\xc0\xde"

# LLVM 3.7 module, block and record ids used by the value numbering.
_MODULE, _CONSTANTS, _FUNCTION, _TYPES = 8, 11, 12, 17
_MODULE_VALUES = (7, 8, 9, 14)              # global variable, function, aliases
_TYPE_RECORDS_WITHOUT_TYPE = (1, 19)        # NUMENTRY, STRUCT_NAME
_TYPE_VOID, _TYPE_FUNCTION = 2, 21
_SETTYPE = 1
_NO_VALUE = frozenset((1, 10, 11, 12, 15, 24, 31, 33, 35, 36, 39, 42, 44, 45))
_TERMINATORS = frozenset((10, 11, 12, 13, 15, 31, 39))


_XXH_PRIMES = (0x9E3779B1, 0x85EBCA77, 0xC2B2AE3D, 0x27D4EB2F, 0x165667B1)


def _rotl(value: int, bits: int) -> int:
    return (value << bits | value >> (32 - bits)) & 0xFFFFFFFF


def xxh32(data: bytes) -> int:
    """XXH32 of the data with seed 0, after the xxHash specification."""
    p1, p2, p3, p4, p5 = _XXH_PRIMES
    size, at = len(data), 0
    if size >= 16:
        lanes = [(p1 + p2) & 0xFFFFFFFF, p2, 0, -p1 & 0xFFFFFFFF]
        words = struct.unpack_from(f"<{size // 16 * 4}I", data)
        for i, word in enumerate(words):
            lanes[i & 3] = _rotl((lanes[i & 3] + word * p2) & 0xFFFFFFFF, 13) * p1 & 0xFFFFFFFF
        value = _rotl(lanes[0], 1) + _rotl(lanes[1], 7) + _rotl(lanes[2], 12) + _rotl(lanes[3], 18)
        at = len(words) * 4
    else:
        value = p5
    value = (value + size) & 0xFFFFFFFF
    for (word,) in struct.iter_unpack("<I", data[at:at + (size - at) // 4 * 4]):
        value = _rotl((value + word * p3) & 0xFFFFFFFF, 17) * p4 & 0xFFFFFFFF
    for byte in data[size - (size - at) % 4:]:
        value = _rotl((value + byte * p5) & 0xFFFFFFFF, 11) * p1 & 0xFFFFFFFF
    value = (value ^ value >> 15) * p2 & 0xFFFFFFFF
    value = (value ^ value >> 13) * p3 & 0xFFFFFFFF
    return value ^ value >> 16


def _zig(value: int) -> int:
    return value << 1 if value >= 0 else ((-value) << 1) - 1


def _unzig(value: int) -> int:
    return value >> 1 if not value & 1 else -((value + 1) >> 1)


def _context(kind: int, block: int, code: int = 0) -> int:
    block = block if 0 <= block < 31 else 31
    return kind << 11 | block << 6 | (code if code < 63 else 63)


class _Encoder:
    """Collects coded symbols into streams; returns each value it codes."""
    encoding = True

    def __init__(self):
        self.streams = {}

    def __call__(self, context, value):
        out = self.streams.get(context)
        if out is None:
            out = self.streams[context] = bytearray()
        v = value
        while v >= 0x80:
            out.append(v & 0x7F | 0x80)
            v >>= 7
        out.append(v)
        return value

    def bytes(self, context, data):
        self.streams.setdefault(context, bytearray()).extend(data)
        return bytes(data)


class _Decoder:
    """Reads coded symbols back from the streams of a pack."""
    encoding = False

    def __init__(self, streams):
        self.streams = streams
        self.positions = dict.fromkeys(streams, 0)

    def __call__(self, context, value=None):
        data, pos = self.streams[context], self.positions[context]
        value = shift = 0
        while True:
            byte = data[pos]
            pos += 1
            value |= (byte & 0x7F) << shift
            if byte < 0x80:
                break
            shift += 7
        self.positions[context] = pos
        return value

    def bytes(self, context, size):
        pos = self.positions[context]
        self.positions[context] = pos + size
        data = self.streams[context][pos:pos + size]
        if len(data) != size:
            raise ValueError("shader pack stream overrun")
        return bytes(data)


def _move_to_front(coder, items, context, value, base):
    """Codes value by its rank in items, or escapes it relative to base."""
    if coder.encoding:
        try:
            rank = items.index(value) + 1
        except ValueError:
            rank = 0
        coder(context, rank)
        if not rank:
            coder(context, _zig(value - base))
    else:
        rank = coder(context)
        value = items[rank - 1] if rank else base + _unzig(coder(context))
    if rank:
        del items[rank - 1]
    items.insert(0, value)
    return value


def _touch(items, value):
    try:
        items.remove(value)
    except ValueError:
        pass
    items.insert(0, value)


# --------------------------------------------------------------------------
# SPIR-V

def spirv_schema(grammar_path, opcodes):
    """Operand roles of the given opcodes, from the SPIR-V core grammar.

    An entry is (flags, descriptors): flags bit 0 marks type declarations and
    bits 1-3 hold the role of words beyond the listed operands, which are
    the parameters of enumerants; a descriptor is role | repeated << 3.
    """
    grammar = json.loads(open(grammar_path, encoding="utf-8").read())
    kinds = {kind["kind"]: kind for kind in grammar["operand_kinds"]}

    def is_id(kind):
        return kinds.get(kind, {}).get("category") == "Id"

    def role(kind):
        if kind == "IdResultType":
            return ROLE_TYPE
        if kind == "IdResult":
            return ROLE_RESULT
        if kind == "LiteralString":
            return ROLE_STRING
        if kind == "PairLiteralIntegerIdRef":
            return ROLE_LITERAL_ID
        if kind == "PairIdRefLiteralInteger":
            return ROLE_ID_LITERAL
        if kind == "PairIdRefIdRef":
            return ROLE_ID_ID
        return ROLE_ID if is_id(kind) else ROLE_LITERAL

    schema = {}
    for instruction in grammar["instructions"]:
        opcode = instruction["opcode"]
        if opcode not in opcodes:
            continue
        operands = instruction.get("operands", [])
        descriptors = tuple(role(o["kind"]) | (o.get("quantifier") == "*") << 3
                            for o in operands)
        # Enumerant parameters follow the listed operands; code them as the
        # role most of the parameters of the instruction's enumerants have.
        ids = literals = 0
        for operand in operands:
            for enumerant in kinds.get(operand["kind"], {}).get("enumerants", []):
                for parameter in enumerant.get("parameters", []):
                    if is_id(parameter["kind"]):
                        ids += 1
                    else:
                        literals += 1
        tail = ROLE_LITERAL if literals > ids else ROLE_ID
        type_declaration = instruction.get("class") == "Type-Declaration"
        schema[opcode] = (int(type_declaration) | tail << 1, descriptors)
    return schema


def _code_spirv_schema(coder, schema):
    if coder.encoding:
        coder(SPIRV_SCHEMA, len(schema))
        for opcode in sorted(schema):
            flags, descriptors = schema[opcode]
            coder(SPIRV_SCHEMA, opcode)
            coder(SPIRV_SCHEMA, flags)
            coder(SPIRV_SCHEMA, len(descriptors))
            for descriptor in descriptors:
                coder(SPIRV_SCHEMA, descriptor)
        return schema
    schema = {}
    for _ in range(coder(SPIRV_SCHEMA)):
        opcode, flags = coder(SPIRV_SCHEMA), coder(SPIRV_SCHEMA)
        schema[opcode] = (flags, tuple(coder(SPIRV_SCHEMA) for _ in range(coder(SPIRV_SCHEMA))))
    return schema


def _spirv_roles(entry, count, operands):
    """Yields the role of each operand word; operands grows as words are coded."""
    flags, descriptors = entry
    k = 0
    for descriptor in descriptors:
        role, repeated = descriptor & 7, descriptor >> 3
        while k < count:
            if role == ROLE_STRING:
                while k < count:
                    yield ROLE_LITERAL
                    k += 1
                    if not operands[-1] >> 24:
                        break
            elif role in _PAIRS:
                for part in _PAIRS[role]:
                    if k < count:
                        yield part
                        k += 1
            else:
                yield role
                k += 1
            if not repeated:
                break
    tail = flags >> 1
    while k < count:
        yield tail
        k += 1


def _code_spirv(coder, schema, size, code=None):
    words = struct.unpack(f"<{size // 4}I", code) if code is not None else None
    count = size // 4
    out = [coder(SPIRV_HEADER, words[i] if words else None) for i in range(5)]
    ids, types, last = [], [], 0
    default = (ROLE_ID << 1, ())
    while len(out) < count:
        at = len(out)
        head = words[at] if words else 0
        if words and not head >> 16:
            raise ValueError("SPIR-V instruction without a word count")
        opcode = coder(SPIRV_OPCODE, head & 0xFFFF)
        length = coder(SPIRV_COUNT, (head >> 16) - 1)
        out.append((length + 1) << 16 | opcode)
        entry = schema.get(opcode, default)
        type_declaration = entry[0] & 1
        operands = []
        for role in _spirv_roles(entry, length, operands):
            word = words[at + 1 + len(operands)] if words else None
            if role == ROLE_LITERAL:
                word = coder(SPIRV_LITERAL, word)
            elif role == ROLE_RESULT:
                delta = coder(SPIRV_RESULT, _zig(word - last - 1) if words else None)
                word = last = last + 1 + _unzig(delta)
                _touch(ids, word)
                if type_declaration:
                    _touch(types, word)
            elif role == ROLE_TYPE:
                word = _move_to_front(coder, types, SPIRV_TYPE, word, last)
            else:
                word = _move_to_front(coder, ids, SPIRV_ID, word, last)
            operands.append(word)
        out += operands
    if len(out) != count:
        raise ValueError("SPIR-V instruction overruns the module")
    return struct.pack(f"<{count}I", *out)


# --------------------------------------------------------------------------
# DXIL

class _Scopes:
    """Block nesting and abbreviations of an LLVM bitstream."""

    def __init__(self):
        self.stack = [(-1, 2, [])]  # block id, abbreviation width, abbreviations
        self.blockinfo = {}
        self.blockinfo_target = None

    @property
    def block(self):
        return self.stack[-1][0]

    @property
    def width(self):
        return self.stack[-1][1]

    @property
    def abbrevs(self):
        return self.stack[-1][2]

    def apply(self, event):
        kind = event[0]
        if kind == END:
            self.stack.pop()
        elif kind == ENTER:
            self.stack.append((event[1], event[2], list(self.blockinfo.get(event[1], ()))))
        elif kind == DEFINE:
            if self.block == 0:
                self.blockinfo.setdefault(self.blockinfo_target, []).append(event[1])
            else:
                self.abbrevs.append(event[1])
        elif self.block == 0 and event[1] == 1 and event[3]:  # BLOCKINFO SETBID
            self.blockinfo_target = event[3][0]


class _BitReader:
    def __init__(self, data):
        self.data, self.byte, self.acc, self.bits = data, 0, 0, 0

    @property
    def position(self):
        return self.byte * 8 - self.bits

    def read(self, n):
        while self.bits < n:
            self.acc |= self.data[self.byte] << self.bits
            self.byte += 1
            self.bits += 8
        value = self.acc & ((1 << n) - 1)
        self.acc >>= n
        self.bits -= n
        return value

    def vbr(self, n):
        value = shift = 0
        high = 1 << (n - 1)
        while True:
            chunk = self.read(n)
            value |= (chunk & (high - 1)) << shift
            if not chunk & high:
                return value
            shift += n - 1

    def align(self):
        if self.read(-self.position & 31):
            raise ValueError("nonzero bitstream padding")


class _BitWriter:
    def __init__(self):
        self.out, self.acc, self.bits = bytearray(), 0, 0

    @property
    def position(self):
        return len(self.out) * 8 + self.bits

    def write(self, value, n):
        if value >> n:
            raise ValueError("bitstream field overflow")
        self.acc |= value << self.bits
        self.bits += n
        while self.bits >= 8:
            self.out.append(self.acc & 0xFF)
            self.acc >>= 8
            self.bits -= 8

    def vbr(self, value, n):
        high = 1 << (n - 1)
        while value >= high:
            self.write(value & (high - 1) | high, n)
            value >>= n - 1
        self.write(value, n)

    def align(self):
        self.write(0, -self.position & 31)


def _variable_count(spec):
    return any(encoding in (ARRAY, BLOB) for encoding, _ in spec)


def _read_scalar(reader, encoding, value):
    if encoding == FIXED:
        return reader.read(value)
    if encoding == VBR:
        return reader.vbr(value)
    if encoding == CHAR6:
        return _CHAR6[reader.read(6)]
    raise ValueError("invalid abbreviation operand")


def _write_scalar(writer, encoding, width, value):
    if encoding == FIXED:
        writer.write(value, width)
    elif encoding == VBR:
        writer.vbr(value, width)
    elif encoding == CHAR6:
        writer.write(_CHAR6_INDEX[value], 6)
    else:
        raise ValueError("invalid abbreviation operand")


def _parse_bitcode(stream):
    """Yields the events of an LLVM bitstream without its magic."""
    reader, scopes = _BitReader(stream), _Scopes()
    end = len(stream) * 8
    while not (len(scopes.stack) == 1 and reader.position >= end):
        aid = reader.read(scopes.width)
        if aid == 0:
            reader.align()
            event = (END,)
        elif aid == 1:
            block, width = reader.vbr(8), reader.vbr(4)
            reader.align()
            reader.read(32)
            event = (ENTER, block, width)
        elif aid == 2:
            spec = []
            for _ in range(reader.vbr(5)):
                if reader.read(1):
                    spec.append((LITERAL, reader.vbr(8)))
                else:
                    encoding = reader.read(3)
                    spec.append((encoding, reader.vbr(5) if encoding in (FIXED, VBR) else 0))
            event = (DEFINE, tuple(spec))
        elif aid == 3:
            code, count = reader.vbr(6), reader.vbr(6)
            event = (RECORD, code, aid, [reader.vbr(6) for _ in range(count)])
        else:
            spec = scopes.abbrevs[aid - 4]
            values = []
            for k, (encoding, value) in enumerate(spec):
                if encoding == LITERAL:
                    values.append(value)
                elif encoding == ARRAY:
                    element = spec[k + 1]
                    values += [_read_scalar(reader, *element) for _ in range(reader.vbr(6))]
                    break
                elif encoding == BLOB:
                    count = reader.vbr(6)
                    reader.align()
                    values += [reader.read(8) for _ in range(count)]
                    reader.align()
                    break
                else:
                    values.append(_read_scalar(reader, encoding, value))
            event = (RECORD, values[0], aid, values[1:])
        scopes.apply(event)
        yield event
    if reader.position != end:
        raise ValueError("bitcode has trailing data")


class _BitcodeEmitter:
    """Writes events back into an LLVM bitstream without its magic."""

    def __init__(self):
        self.writer, self.scopes, self.lengths = _BitWriter(), _Scopes(), []

    def emit(self, event):
        w, scopes = self.writer, self.scopes
        kind = event[0]
        if kind == END:
            w.write(0, scopes.width)
            w.align()
            start = self.lengths.pop()
            struct.pack_into("<I", w.out, start, (len(w.out) - start - 4) // 4)
        elif kind == ENTER:
            w.write(1, scopes.width)
            w.vbr(event[1], 8)
            w.vbr(event[2], 4)
            w.align()
            self.lengths.append(len(w.out))
            w.write(0, 32)
        elif kind == DEFINE:
            w.write(2, scopes.width)
            w.vbr(len(event[1]), 5)
            for encoding, value in event[1]:
                if encoding == LITERAL:
                    w.write(1, 1)
                    w.vbr(value, 8)
                else:
                    w.write(0, 1)
                    w.write(encoding, 3)
                    if encoding in (FIXED, VBR):
                        w.vbr(value, 5)
        else:
            _, code, aid, ops = event
            w.write(aid, scopes.width)
            if aid == 3:
                w.vbr(code, 6)
                w.vbr(len(ops), 6)
                for op in ops:
                    w.vbr(op, 6)
            else:
                self._abbreviated(scopes.abbrevs[aid - 4], [code] + ops)
        scopes.apply(event)

    def _abbreviated(self, spec, values):
        w = self.writer
        at = 0
        for k, (encoding, value) in enumerate(spec):
            if encoding == LITERAL:
                if values[at] != value:
                    raise ValueError("record contradicts a literal abbreviation operand")
                at += 1
            elif encoding == ARRAY:
                element = spec[k + 1]
                w.vbr(len(values) - at, 6)
                for v in values[at:]:
                    _write_scalar(w, element[0], element[1], v)
                return
            elif encoding == BLOB:
                w.vbr(len(values) - at, 6)
                w.align()
                for v in values[at:]:
                    w.write(v, 8)
                w.align()
                return
            else:
                _write_scalar(w, encoding, value, values[at])
                at += 1
        if at != len(values):
            raise ValueError("record does not fit its abbreviation")


class _Fields:
    """The operands of one record, coded in order."""

    def __init__(self, coder, count, source):
        self.coder, self.count, self.source, self.values = coder, count, source, []

    def more(self, n=1):
        return len(self.values) + n <= self.count

    def peek(self):
        return self.source[len(self.values)] if self.source is not None else None

    def literal(self, context):
        if self.more():
            self.values.append(self.coder(context, self.peek()))
            return self.values[-1]
        return None

    def rest(self, context):
        while self.more():
            self.literal(context)


class _BitcodeModel:
    """Codes bitstream events, numbering LLVM values like the bitcode writer."""

    def __init__(self, coder):
        self.coder = coder
        self.types = []         # (code, ops) of the type table
        self.bodies = []        # types of the functions with bodies, in order
        self.body = 0           # index of the next function body
        self.blocks = []
        self.globals = self.constants = 0
        self.valno = self.local = self.bb = 0
        self.ids = []

    def code(self, scopes, event=None):
        c = self.coder
        block = scopes.block
        symbol = None
        if c.encoding:
            symbol = 3 + event[1] if event[0] == RECORD else event[0]
        symbol = c(_context(DXIL_EVENT, block), symbol)
        if symbol == END:
            bid = self.blocks.pop()
            if bid == _CONSTANTS and self.blocks == [_MODULE]:
                self.constants = self.valno - self.globals
            return (END,)
        if symbol == ENTER:
            bid = c(DXIL_BLOCK, event[1] if event else None)
            width = c(DXIL_BLOCK, event[2] if event else None)
            self._enter(bid)
            return (ENTER, bid, width)
        if symbol == DEFINE:
            spec = event[1] if event else ()
            n = c(DXIL_DEFINE, len(spec) if event else None)
            ops = []
            for k in range(n):
                encoding, value = spec[k] if event else (0, 0)
                op = c(DXIL_DEFINE, value << 3 | encoding if event else None)
                ops.append((op & 7, op >> 3))
            return (DEFINE, tuple(ops))
        code = symbol - 3
        aid = 3 + c(_context(DXIL_ABBREV, block, code), event[2] - 3 if event else None)
        spec = scopes.abbrevs[aid - 4] if aid > 3 else None
        if spec is None or _variable_count(spec):
            count = c(_context(DXIL_COUNT, block, code), len(event[3]) if event else None)
        else:
            count = len(spec) - 1
        fields = _Fields(c, count, event[3] if event else None)
        if block == _FUNCTION:
            self._instruction(fields, code)
        fields.rest(_context(DXIL_LITERAL, block, code))
        ops = fields.values
        if block == _TYPES and code not in _TYPE_RECORDS_WITHOUT_TYPE:
            self.types.append((code, ops))
        elif block == _MODULE and code in _MODULE_VALUES:
            self.globals += 1
            if code == 8 and len(ops) > 2 and ops[2] == 0:
                self.bodies.append(ops[0])
        elif block == _CONSTANTS and code != _SETTYPE:
            self.valno += 1
        return (RECORD, code, aid, ops)

    def _enter(self, bid):
        self.blocks.append(bid)
        if bid == _FUNCTION:
            fn = self.bodies[self.body] if self.body < len(self.bodies) else None
            self.body += 1
            args = 0
            if fn is not None and fn < len(self.types) and self.types[fn][0] == _TYPE_FUNCTION:
                args = max(len(self.types[fn][1]) - 2, 0)
            self.local = self.globals + self.constants
            self.valno = self.local + args
            self.ids, self.bb = [], 0
        elif bid == _CONSTANTS and self.blocks == [_MODULE, _CONSTANTS]:
            self.valno = self.globals

    def _returns_void(self, fnty):
        if fnty is None or fnty >= len(self.types):
            return False
        code, ops = self.types[fnty]
        if code != _TYPE_FUNCTION or len(ops) < 2 or ops[1] >= len(self.types):
            return False
        return self.types[ops[1]][0] == _TYPE_VOID

    def _value(self, fields, signed=False):
        """Codes a relative value operand; returns its raw value or None."""
        if not fields.more():
            return None
        c, ids = self.coder, self.ids
        if c.encoding:
            raw = fields.peek()
            rel = ((raw >> 1) * (-1 if raw & 1 else 1)) if signed else raw
            absid = self.valno - rel
            known = 0 < rel <= self.valno
            if known and absid in ids:
                symbol = ids.index(absid) + 2
            else:
                symbol = 0 if known and absid < self.local else 1
            c(DXIL_VALUE, symbol)
            if symbol == 0:
                c(DXIL_GLOBAL, absid)
            elif symbol == 1:
                c(DXIL_LOCAL, raw)
        else:
            symbol = c(DXIL_VALUE)
            if symbol == 1:
                raw = c(DXIL_LOCAL)
                rel = ((raw >> 1) * (-1 if raw & 1 else 1)) if signed else raw
                absid = self.valno - rel
                known = 0 < rel <= self.valno
            else:
                absid = c(DXIL_GLOBAL) if symbol == 0 else ids[symbol - 2]
                rel = self.valno - absid
                raw = rel << 1 if signed else rel
                known = True
        if symbol >= 2:
            del ids[symbol - 2]
        if known:
            ids.insert(0, absid)
        fields.values.append(raw)
        return raw

    def _typed_value(self, fields):
        raw = self._value(fields)
        if raw is not None and (raw == 0 or raw >= 1 << 31):
            fields.literal(DXIL_FORWARD_TYPE)

    def _bb(self, fields):
        if fields.more():
            delta = self.coder(DXIL_BB, _zig(fields.peek() - self.bb) if self.coder.encoding else None)
            fields.values.append(self.bb + _unzig(delta))

    def _instruction(self, f, code):
        literal = _context(DXIL_LITERAL, _FUNCTION, code)
        produces = code not in _NO_VALUE
        if code == 34:                        # CALL [attrs, cc, fnty, callee, args...]
            f.literal(literal)
            cc = f.literal(literal)
            fnty = f.literal(DXIL_TYPE) if cc is not None and cc & 1 << 15 else None
            self._typed_value(f)
            while f.more():
                self._value(f)
            produces = fnty is None or not self._returns_void(fnty)
        elif code == 16:                      # PHI [ty, (value, bb)*]
            f.literal(DXIL_TYPE)
            while f.more(2):
                self._value(f, True)
                self._bb(f)
        elif code in (2, 28):                 # BINOP, CMP2 [a+ty, b, ...]
            self._typed_value(f)
            self._value(f)
        elif code in (3, 20):                 # CAST, LOAD [a+ty, ty, ...]
            self._typed_value(f)
            f.literal(DXIL_TYPE)
        elif code == 29:                      # VSELECT [a+ty, b, cond+ty]
            self._typed_value(f)
            self._value(f)
            self._typed_value(f)
        elif code == 6:                       # EXTRACTELT [a+ty, index+ty]
            self._typed_value(f)
            self._typed_value(f)
        elif code == 26:                      # EXTRACTVAL [a+ty, indices...]
            self._typed_value(f)
        elif code == 7:                       # INSERTELT [a+ty, b, index+ty]
            self._typed_value(f)
            self._value(f)
            self._typed_value(f)
        elif code in (27, 44):                # INSERTVAL, STORE [a+ty, b+ty, ...]
            self._typed_value(f)
            self._typed_value(f)
        elif code == 8:                       # SHUFFLEVEC [a+ty, b, mask]
            self._typed_value(f)
            self._value(f)
            self._value(f)
        elif code == 43:                      # GEP [inbounds, ty, (value+ty)*]
            f.literal(literal)
            f.literal(DXIL_TYPE)
            while f.more():
                self._typed_value(f)
        elif code == 11:                      # BR [bb, (bb, cond)]
            self._bb(f)
            if f.count == 3:
                self._bb(f)
                self._value(f)
        elif code == 10:                      # RET [(value+ty)*]
            while f.more():
                self._typed_value(f)
        elif code == 12:                      # SWITCH [ty, cond, ...]
            f.literal(DXIL_TYPE)
            self._value(f)
        if code in _TERMINATORS:
            self.bb += 1
        if produces:
            self.valno += 1


def _bitcode_range(container):
    """Returns the offset and size of the LLVM bitcode in a DXIL container."""
    if container[:4] != b"DXBC" or len(container) < 32:
        return 0, 0
    count = struct.unpack_from("<I", container, 28)[0]
    for offset in struct.unpack_from(f"<{count}I", container, 32):
        if container[offset:offset + 4] == b"DXIL":
            program = offset + 8
            bc_offset, bc_size = struct.unpack_from("<II", container, program + 16)
            start = program + 8 + bc_offset
            if container[start:start + 4] == BITCODE_MAGIC:
                # The magic stays with the container.
                return start + 4, bc_size - 4
    return 0, 0


def _code_dxil(coder, size, code=None):
    if coder.encoding:
        start, length = _bitcode_range(code)
        coder(DXIL_SPLIT, start)
        coder(DXIL_SPLIT, length)
        coder.bytes(DXIL_CONTAINER, code[:start] + code[start + length:])
        stream = code[start:start + length]
        model, emitter = _BitcodeModel(coder), _BitcodeEmitter()
        for event in _parse_bitcode(stream):
            emitter.emit(model.code(emitter.scopes, event))
        if bytes(emitter.writer.out) != stream:
            raise ValueError("bitcode does not re-emit exactly")
        return code
    start, length = coder(DXIL_SPLIT), coder(DXIL_SPLIT)
    container = coder.bytes(DXIL_CONTAINER, size - length)
    model, emitter = _BitcodeModel(coder), _BitcodeEmitter()
    end = length * 8
    while not (len(emitter.scopes.stack) == 1 and emitter.writer.position >= end):
        emitter.emit(model.code(emitter.scopes))
    return container[:start] + bytes(emitter.writer.out) + container[start:]


# --------------------------------------------------------------------------
# Packs

def _varint(out, value):
    while value >= 0x80:
        out.append(value & 0x7F | 0x80)
        value >>= 7
    out.append(value)


def _lzma_filter(dictionary, lc, lp, pb, **options):
    return [{"id": lzma.FILTER_LZMA1, "dict_size": dictionary,
             "lc": lc, "lp": lp, "pb": pb, **options}]


# Encoder settings tried for every pack. The optimal parse of LZMA reacts
# chaotically to small changes of its input, so no fixed setting stays the
# best one; each pack keeps the smallest encoding over these settings and the
# literal context, literal position and position bits lc 0-3, lp 0-1, pb 0-2.
_LZMA_SETTINGS = ({"preset": 9 | lzma.PRESET_EXTREME},
                  {"mode": lzma.MODE_NORMAL, "mf": lzma.MF_BT2, "nice_len": 273})


def _compress(payload, dictionary):
    """Returns the LZMA properties and the smallest raw LZMA1 encoding found."""
    best = None
    for lc, lp, pb, settings in itertools.product(range(4), range(2), range(3), _LZMA_SETTINGS):
        data = lzma.compress(payload, format=lzma.FORMAT_RAW,
                             filters=_lzma_filter(dictionary, lc, lp, pb, **settings))
        if best is None or len(data) < len(best[1]):
            best = lc + 9 * (lp + 5 * pb), data
    return best


def encode(format_, artifacts, spirv_grammar=None):
    """Packs the artifacts of one format; None marks a missing artifact."""
    codec = CODECS[format_]
    coder = _Encoder()
    if codec == SPIRV:
        opcodes = set()
        for code in artifacts:
            if code is not None:
                words = struct.unpack(f"<{len(code) // 4}I", code)
                at = 5
                while at < len(words):
                    opcodes.add(words[at] & 0xFFFF)
                    at += words[at] >> 16 or len(words)
        schema = _code_spirv_schema(coder, spirv_schema(spirv_grammar, opcodes))
    for code in artifacts:
        if code is None:
            continue
        if codec == SPIRV:
            _code_spirv(coder, schema, len(code), code)
        elif codec == DXIL:
            _code_dxil(coder, len(code), code)
        else:
            coder.bytes(0, code)
    payload = bytearray()
    _varint(payload, len(artifacts))
    for code in artifacts:
        _varint(payload, 0 if code is None else len(code))
    streams = sorted(coder.streams.items())
    _varint(payload, len(streams))
    for context, data in streams:
        _varint(payload, context)
        _varint(payload, len(data))
    for code in artifacts:
        if code is not None:
            payload += struct.pack("<I", xxh32(code))
    for _, data in streams:
        payload += data
    dictionary = 1 << max(len(payload) - 1, 4095).bit_length()
    properties, compressed = _compress(bytes(payload), dictionary)
    pack = MAGIC + struct.pack("<BBBBII", VERSION, codec, properties, 0,
                               dictionary, len(payload)) + compressed
    if decode(pack) != list(artifacts):
        raise ValueError(f"{format_} shader pack does not round-trip")
    return pack


def _unpack(pack):
    """Returns the codec and the decompressed payload of a pack."""
    if pack[:4] != MAGIC or len(pack) < 16:
        raise ValueError("not a shader pack")
    version, codec, properties, _, dictionary, size = struct.unpack_from("<BBBBII", pack, 4)
    lc, lp, pb = properties % 9, properties // 9 % 5, properties // 45
    if version != VERSION or codec > DXIL or properties >= 225 or lc + lp > 4:
        raise ValueError("unsupported shader pack")
    payload = lzma.decompress(pack[16:], format=lzma.FORMAT_RAW,
                              filters=_lzma_filter(dictionary, lc, lp, pb))
    if len(payload) != size:
        raise ValueError("shader pack size mismatch")
    return codec, payload


def sizes(pack):
    """Returns the artifact sizes of a pack, 0 for a missing artifact."""
    reader = _Decoder({0: _unpack(pack)[1]})
    return [reader(0) for _ in range(reader(0))]


def decode(pack):
    """Returns the artifacts of a pack; None marks a missing artifact."""
    codec, payload = _unpack(pack)
    reader = _Decoder({0: payload})
    sizes = [reader(0) for _ in range(reader(0))]
    layout = [(reader(0), reader(0)) for _ in range(reader(0))]
    at = reader.positions[0]
    checksums = []
    for shader_size in sizes:
        if shader_size:
            checksums.append(struct.unpack_from("<I", payload, at)[0])
            at += 4
    streams = {}
    for context, length in layout:
        streams[context] = payload[at:at + length]
        at += length
    if at != len(payload):
        raise ValueError("shader pack streams do not fill the payload")
    coder = _Decoder(streams)
    schema = _code_spirv_schema(coder, None) if codec == SPIRV else None
    artifacts = []
    for shader_size in sizes:
        if not shader_size:
            artifacts.append(None)
        elif codec == SPIRV:
            artifacts.append(_code_spirv(coder, schema, shader_size))
        elif codec == DXIL:
            artifacts.append(_code_dxil(coder, shader_size))
        else:
            artifacts.append(coder.bytes(0, shader_size))
    for code, checksum in zip((a for a in artifacts if a is not None), checksums):
        if len(code) == 0 or xxh32(code) != checksum:
            raise ValueError("shader pack checksum mismatch")
    if any(coder.positions[c] != len(streams[c]) for c in streams):
        raise ValueError("shader pack streams are not fully consumed")
    return artifacts
