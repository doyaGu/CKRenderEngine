#!/usr/bin/env python3
"""Check source includes and declared CMake dependencies at rasterizer boundaries.

There are no grandfathered violations. Native implementation roots come from
their CMake target source lists; plugin entry points and FFP catalogs are checked
as separate compositions rather than excluded from a native source directory.
"""

import argparse
from collections import defaultdict
from pathlib import Path
import re
import sys


INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]', re.MULTILINE)
TOKEN = re.compile(r'"([^"\\]*(?:\\.[^"\\]*)*)"|([^\s]+)')
VARIABLE = re.compile(r'\$\{([^}]+)\}')
PROTECTED = {"CKRasterizerBackend", "CKBgfxBackend", "CKSdlGpuBackend", "CKRenderSupport"}
FFP_TARGETS = {"CKRasterizerLib", "CKBgfxRasterizer", "CKBgfxRasterizerStatic",
               "CKSdlGpuRasterizer", "CKSdlGpuRasterizerStatic", "CK2_3D", "CK2_3DStatic"}
FFP_SYMBOL = re.compile(r'\b(?:CKRasterizer|CKRasterizerDriver|CKRasterizerContext|'
                        r'CKRasterizerBackendDriver|CKRasterizerBackendLibrary|'
                        r'CKVertexBufferDesc|CKIndexBufferDesc|CKRSTVertexLayout|CKViewportData|'
                        r'CKMaterialData|CKLightData|CKRasterizerOptions|CKRasterizerCapsDesc|'
                        r'CKReadbackCallback|CKRenderStats|CKBackendShaderSet|CKBuiltinShader|'
                        r'CKFF[A-Za-z_0-9]*|CKTranslated[A-Za-z_0-9]*|CKRST_BLOCK_[A-Za-z_0-9]*)\b')
CPP_NON_CODE = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.DOTALL)


def code_only(text):
    """Retain positions/lines while ignoring comments and string literals."""
    return CPP_NON_CODE.sub(lambda match: ''.join('\n' if char == '\n' else ' ' for char in match[0]), text)


def commands(text):
    """Read balanced CMake commands, retaining line numbers for diagnostics."""
    text = re.sub(r'(?m)#.*$', '', text)
    position = 0
    pattern = re.compile(r'([A-Za-z_][A-Za-z_0-9]*)\s*\(')
    while match := pattern.search(text, position):
        start = match.end()
        depth, quoted, escaped, end = 1, False, False, start
        while end < len(text) and depth:
            char = text[end]
            if escaped:
                escaped = False
            elif char == '\\':
                escaped = True
            elif char == '"':
                quoted = not quoted
            elif not quoted:
                depth += (char == '(') - (char == ')')
            end += 1
        if depth:
            raise ValueError("unterminated CMake command")
        values = [quoted_token or bare for quoted_token, bare in TOKEN.findall(text[start:end - 1])]
        yield match.group(1).lower(), values, text.count('\n', 0, match.start()) + 1
        position = end


def expand(values, variables):
    expanded = []
    for value in values:
        match = VARIABLE.fullmatch(value)
        if match and match.group(1) in variables:
            expanded.extend(variables[match.group(1)])
        else:
            expanded.append(VARIABLE.sub(lambda item: ';'.join(variables.get(item.group(1), [item[0]])), value))
    return expanded


def cmake_model(root):
    sources, edges, declarations = defaultdict(set), defaultdict(set), set()
    links = []
    inherited = {}
    parent = root / "src" / "CKRasterizer" / "CMakeLists.txt"
    for command, values, _ in commands(parent.read_text(encoding="utf-8-sig")):
        if command == "set" and values:
            inherited[values[0]] = expand(values[1:], inherited)
    for cmake in sorted((root / "src").rglob("CMakeLists.txt")):
        variables = dict(inherited, CMAKE_CURRENT_SOURCE_DIR=[cmake.parent.as_posix()],
                         CKRE_INCLUDE_DIR=[(root / "include").as_posix()])
        for command, values, line in commands(cmake.read_text(encoding="utf-8-sig")):
            if not values:
                continue
            args = expand(values, variables)
            if command == "set":
                variables[values[0]] = args[1:]
            elif command == "list" and args[0] == "APPEND":
                variables.setdefault(args[1], []).extend(args[2:])
            elif command == "add_library":
                declarations.add(args[0])
                for value in args[1:]:
                    if Path(value).suffix in {".cpp", ".h", ".c"}:
                        sources[args[0]].add((cmake.parent / value).resolve())
            elif command in {"target_link_libraries", "add_dependencies"}:
                for dependency in args[1:]:
                    if dependency not in {"PUBLIC", "PRIVATE", "INTERFACE"}:
                        edges[args[0]].add(dependency)
                        links.append((cmake, line, args[0], dependency))
    return sources, edges, declarations, links


def check(root):
    errors = []
    rasterizer = root / "src" / "CKRasterizer"
    ffp = rasterizer / "CKRasterizerLib"
    generic = rasterizer / "CKRasterizerBackend"
    support = rasterizer / "CKRenderSupport"
    sources, edges, declarations, links = cmake_model(root)
    for target in PROTECTED:
        if target not in declarations:
            errors.append(f"CMake: missing independent {target} target")
        if not sources[target]:
            errors.append(f"CMake: {target} has no explicit implementation sources to check")
    for cmake, line, target, dependency in links:
        if target in PROTECTED and dependency in FFP_TARGETS:
            errors.append(f"{cmake.relative_to(root)}:{line}: {target} links upward to {dependency}")
    for target in PROTECTED:
        pending, visited = list(edges[target]), set()
        while pending:
            dependency = pending.pop()
            if dependency in visited:
                continue
            visited.add(dependency)
            if dependency in FFP_TARGETS:
                errors.append(f"CMake: {target} has a transitive dependency on {dependency}")
            pending.extend(edges[dependency])

    index = defaultdict(list)
    for folder in (root / "include", root / "src"):
        for header in folder.rglob("*.h"):
            if "generated" not in header.parts:
                index[header.name].append(header.resolve())

    def resolve(source, include):
        local = source.parent / include
        if local.is_file():
            return local.resolve()
        choices = index[Path(include).name]
        return choices[0] if len(choices) == 1 else None

    def forbidden(path, layer):
        if path.is_relative_to(ffp):
            return True
        if layer == "CK_3D":
            return path.is_relative_to(generic) or path in native_files
        if path == root / "include" / "CKRasterizerTypes.h":
            return True
        # Native backends and neutral support must not reach CK_3D implementation.
        return path.parent == root / "include" and path.name.startswith("RCK")

    native_files = set().union(*(sources[target] for target in PROTECTED if target != "CKRenderSupport"))
    roots = [(path, target) for target in PROTECTED for path in sources[target]]
    roots.extend((path.resolve(), "CK_3D") for folder in (root / "include", root / "src")
                 for path in folder.rglob("*")
                 if path.suffix in {".cpp", ".h"} and not path.is_relative_to(rasterizer))
    seen_violations = set()
    for source, layer in roots:
        if not source.is_file():
            errors.append(f"{layer}: missing declared source {source.relative_to(root)}")
            continue
        if layer in PROTECTED and source.is_relative_to(ffp):
            errors.append(f"CMake: {layer} compiles an FFP implementation file {source.relative_to(root)}")
            continue
        pending, visited = [(source, [])], set()
        while pending:
            current, chain = pending.pop()
            if current in visited:
                continue
            visited.add(current)
            text = current.read_text(encoding="utf-8-sig", errors="replace")
            # Fundamental SDK headers expose many engine types. Check actual
            # backend/helper code for use, rather than banning their definitions
            # in CKTypes/VxMath or accepting transitive FFP type leakage.
            if layer in PROTECTED and current.is_relative_to(rasterizer):
                for match in FFP_SYMBOL.finditer(code_only(text)):
                    line = text.count('\n', 0, match.start()) + 1
                    key = (layer, current, line, match[0])
                    if key not in seen_violations:
                        seen_violations.add(key)
                        errors.append(f"{current.relative_to(root)}:{line}: {layer} uses FFP symbol {match[0]}")
            for match in INCLUDE.finditer(text):
                include = match.group(1)
                resolved = resolve(current, include)
                if not resolved:
                    continue
                line = text.count('\n', 0, match.start()) + 1
                if forbidden(resolved, layer):
                    key = (layer, current, line, resolved)
                    if key not in seen_violations:
                        seen_violations.add(key)
                        route = " -> ".join([p.name for p in chain] + [current.name])
                        errors.append(f"{current.relative_to(root)}:{line}: {layer} includes {include} (via {route})")
                elif resolved.is_relative_to(root) and "generated" not in resolved.parts:
                    pending.append((resolved, chain + [current]))
    return errors, len(roots)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    errors, checked = check(args.source_root.resolve())
    if errors:
        print("Rasterizer boundary violations:")
        for error in sorted(set(errors)):
            print(f"  {error}")
        return 1
    print(f"Rasterizer boundaries passed: {checked} source roots; no upward source or CMake dependencies.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
