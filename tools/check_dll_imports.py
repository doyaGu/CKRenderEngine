#!/usr/bin/env python3
"""Check whether a PE DLL's imports are satisfied by a set of provider DLLs.

Used to answer the phase-0 oracle question: can the original Virtools
CK2_3D.dll / CKDX8Rasterizer.dll bind against the Ballanced CK2.dll and
VxMath.dll? Pure Python PE parsing, works for any architecture.

Usage:
    check_dll_imports.py <dll-to-check> <provider.dll> [<provider.dll> ...]

Prints every imported module, and for modules matched by a provider (by file
name, case-insensitive) the imported names that the provider does not export.
Exit code 0 when everything resolves, 1 otherwise.
"""

import os
import struct
import sys


class PE:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.data = f.read()
        d = self.data
        if d[:2] != b"MZ":
            raise ValueError(f"{path}: not an MZ executable")
        pe_off = struct.unpack_from("<I", d, 0x3C)[0]
        if d[pe_off:pe_off + 4] != b"PE\0\0":
            raise ValueError(f"{path}: missing PE signature")
        coff = pe_off + 4
        self.machine, nsections, _, _, _, opt_size, _ = struct.unpack_from("<HHIIIHH", d, coff)
        opt = coff + 20
        magic = struct.unpack_from("<H", d, opt)[0]
        self.is64 = magic == 0x20B
        if self.is64:
            self.image_base = struct.unpack_from("<Q", d, opt + 24)[0]
            dd_off = opt + 112
        else:
            self.image_base = struct.unpack_from("<I", d, opt + 28)[0]
            dd_off = opt + 96
        ndirs = struct.unpack_from("<I", d, dd_off - 4)[0]
        self.dirs = []
        for i in range(ndirs):
            self.dirs.append(struct.unpack_from("<II", d, dd_off + i * 8))
        sec_off = opt + opt_size
        self.sections = []
        for i in range(nsections):
            name, vsize, va, rawsize, rawptr = struct.unpack_from("<8sIIII", d, sec_off + i * 40)
            self.sections.append((va, max(vsize, rawsize), rawptr))

    def rva(self, rva):
        for va, size, raw in self.sections:
            if va <= rva < va + size:
                return raw + (rva - va)
        raise ValueError(f"{self.path}: rva 0x{rva:x} not in any section")

    def cstr(self, rva):
        off = self.rva(rva)
        end = self.data.index(b"\0", off)
        return self.data[off:end].decode("latin-1")

    def imports(self):
        """Returns {module_name_lower: [names or 'ordinal#N']}."""
        result = {}
        if len(self.dirs) < 2 or self.dirs[1][0] == 0:
            return result
        off = self.rva(self.dirs[1][0])
        while True:
            ilt, _, _, name_rva, iat = struct.unpack_from("<IIIII", self.data, off)
            if ilt == 0 and name_rva == 0 and iat == 0:
                break
            module = self.cstr(name_rva)
            thunks = ilt if ilt else iat
            toff = self.rva(thunks)
            names = []
            while True:
                if self.is64:
                    entry = struct.unpack_from("<Q", self.data, toff)[0]
                    toff += 8
                    ordinal_flag = 1 << 63
                else:
                    entry = struct.unpack_from("<I", self.data, toff)[0]
                    toff += 4
                    ordinal_flag = 1 << 31
                if entry == 0:
                    break
                if entry & ordinal_flag:
                    names.append(f"ordinal#{entry & 0xFFFF}")
                else:
                    names.append(self.cstr((entry & 0x7FFFFFFF) + 2))
            result.setdefault(module.lower(), []).extend(names)
            off += 20
        return result

    def exports(self):
        """Returns (set of names, set of ordinals)."""
        names, ordinals = set(), set()
        if not self.dirs or self.dirs[0][0] == 0:
            return names, ordinals
        off = self.rva(self.dirs[0][0])
        (_, _, _, _, _, base, nfuncs, nnames, funcs_rva, names_rva, ords_rva) = struct.unpack_from(
            "<IIHHIIIIIII", self.data, off)
        for i in range(nnames):
            name_rva = struct.unpack_from("<I", self.data, self.rva(names_rva) + i * 4)[0]
            names.add(self.cstr(name_rva))
        for i in range(nfuncs):
            ordinals.add(base + i)
        return names, ordinals


def main(argv):
    if len(argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    target = PE(argv[1])
    providers = {}
    for p in argv[2:]:
        providers[os.path.basename(p).lower()] = PE(p)

    print(f"{argv[1]}: machine=0x{target.machine:04x} pe32{'+' if target.is64 else ''}")
    ok = True
    for module, names in sorted(target.imports().items()):
        provider = providers.get(module)
        if not provider:
            print(f"  {module}: {len(names)} imports (no provider given, skipped)")
            continue
        exp_names, exp_ords = provider.exports()
        missing = []
        for n in names:
            if n.startswith("ordinal#"):
                if int(n[8:]) not in exp_ords:
                    missing.append(n)
            elif n not in exp_names:
                missing.append(n)
        status = "OK" if not missing else f"{len(missing)} MISSING"
        print(f"  {module}: {len(names)} imports, {status}")
        for n in missing:
            print(f"    - {n}")
        if missing:
            ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
