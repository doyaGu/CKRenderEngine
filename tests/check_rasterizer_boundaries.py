#!/usr/bin/env python3
"""Verify the four rasterizer modules and their dependency direction."""

import argparse
from collections import defaultdict
from pathlib import Path
import re
import sys


INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]', re.MULTILINE)
TOKEN = re.compile(r'"([^"\\]*(?:\\.[^"\\]*)*)"|([^\s]+)')
VARIABLE = re.compile(r'\$\{([^}]+)\}')

MODULE_TARGETS = {
    "CKRasterizerLib": {"CKRasterizerLib"},
    "CKFFPLib": {"CKFFPLib"},
    "CKSdlGpuRasterizer": {"CKSdlGpuRasterizer", "CKSdlGpuRasterizerStatic"},
    "CKBgfxRasterizer": {"CKBgfxRasterizer", "CKBgfxRasterizerStatic"},
}
FORBIDDEN_TARGETS = {
    "CKRenderSupport",
    "CKRenderProfiling",
    "CKRasterizerBackend",
    "CKSdlGpuBackend",
    "CKBgfxBackend",
}
FORBIDDEN_DIRECTORIES = {"CKRenderSupport", "CKRasterizerBackend", "tests"}
ADAPTER_BUILD_TOKENS = {
    "BGFX_DIR",
    "CKBgfxShaderArtifacts",
    "CKRE_SHADERC_COMMAND",
    "CKRE_SHADERC_DEPENDS",
    "CKSdlGpuShaderArtifacts",
    "compile_shaders.py",
}


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


def target_public_include_paths(cmake_text, target):
    paths = []
    for command, values, line in commands(cmake_text):
        if command != "target_include_directories" or not values or values[0] != target:
            continue
        visibility = None
        for value in values[1:]:
            if value in {"PUBLIC", "PRIVATE", "INTERFACE"}:
                visibility = value
            elif visibility in {"PUBLIC", "INTERFACE"}:
                paths.append((value.replace('\\', '/'), line))
    return paths


def cmake_model(root):
    sources, edges, declarations = defaultdict(set), defaultdict(set), set()
    inherited = {}
    parent = root / "src" / "CKRasterizer" / "CMakeLists.txt"
    for command, values, _ in commands(parent.read_text(encoding="utf-8-sig")):
        if command == "set" and values:
            inherited[values[0]] = expand(values[1:], inherited)

    for cmake in sorted((root / "src" / "CKRasterizer").rglob("CMakeLists.txt")):
        variables = dict(
            inherited,
            CMAKE_CURRENT_SOURCE_DIR=[cmake.parent.as_posix()],
            CKRE_INCLUDE_DIR=[(root / "include").as_posix()],
        )
        for command, values, _ in commands(cmake.read_text(encoding="utf-8-sig")):
            if not values:
                continue
            args = expand(values, variables)
            if command == "set":
                variables[values[0]] = args[1:]
            elif command == "list" and len(args) > 2 and args[0].upper() == "APPEND":
                variables.setdefault(args[1], []).extend(args[2:])
            elif command == "add_library":
                target = args[0]
                declarations.add(target)
                for value in args[1:]:
                    if Path(value).suffix.lower() in {".cpp", ".c", ".h"}:
                        sources[target].add((cmake.parent / value).resolve())
            elif command in {"target_link_libraries", "add_dependencies"}:
                for dependency in args[1:]:
                    if dependency not in {"PUBLIC", "PRIVATE", "INTERFACE"}:
                        edges[args[0]].add(dependency)
    return sources, edges, declarations


def module_for(path, module_roots, public_root):
    if path.is_relative_to(public_root) and path.name.startswith("CKRasterizer"):
        return "CKRasterizerLib"
    for name, directory in module_roots.items():
        if path.is_relative_to(directory):
            return name
    return None


def check(root):
    errors = []
    rasterizer = root / "src" / "CKRasterizer"
    public_root = root / "include"
    module_roots = {name: rasterizer / name for name in MODULE_TARGETS}
    allowed_imports = {
        "CKRasterizerLib": {"CKRasterizerLib"},
        "CKFFPLib": {"CKRasterizerLib", "CKFFPLib"},
        "CKSdlGpuRasterizer": {"CKRasterizerLib", "CKFFPLib", "CKSdlGpuRasterizer"},
        "CKBgfxRasterizer": {"CKRasterizerLib", "CKFFPLib", "CKBgfxRasterizer"},
    }

    sources, edges, declarations = cmake_model(root)
    for target in sorted(FORBIDDEN_TARGETS & declarations):
        errors.append(f"CMake declares obsolete rasterizer target {target}")
    for directory in sorted(FORBIDDEN_DIRECTORIES):
        if (rasterizer / directory).exists():
            errors.append(f"non-module directory remains under src/CKRasterizer: {directory}")

    ffp_cmake = (module_roots["CKFFPLib"] / "CMakeLists.txt").read_text(encoding="utf-8-sig")
    for token in sorted(ADAPTER_BUILD_TOKENS):
        if token in ffp_cmake:
            errors.append(f"CMake: CKFFPLib references Adapter build input {token}")

    ffp_public_includes = target_public_include_paths(ffp_cmake, "CKFFPLib")
    if not ffp_public_includes:
        errors.append("CMake: CKFFPLib has no library Interface include seam")
    for include, line in ffp_public_includes:
        if not re.search(r'/Interface(?:>|$)', include):
            errors.append(
                f"src/CKRasterizer/CKFFPLib/CMakeLists.txt:{line}: "
                f"CKFFPLib exposes non-interface include path {include}"
            )

    for adapter in ("CKSdlGpuRasterizer", "CKBgfxRasterizer"):
        adapter_cmake_path = module_roots[adapter] / "CMakeLists.txt"
        adapter_cmake = adapter_cmake_path.read_text(encoding="utf-8-sig")
        for command, values, line in commands(adapter_cmake):
            if command != "target_include_directories":
                continue
            if values and (values[0].startswith("test_") or values[0] == "${_test}"):
                continue
            for value in values[1:]:
                normalized = value.replace('\\', '/')
                if re.search(r'CKFFPLib/(?:Backend|Context|FixedFunction)(?:/|$)', normalized):
                    errors.append(
                        f"{adapter_cmake_path.relative_to(root)}:{line}: "
                        f"{adapter} exposes CKFFPLib Implementation include path {value}"
                    )

    for module, targets in MODULE_TARGETS.items():
        for target in targets:
            if target not in declarations:
                errors.append(f"CMake: missing {target} target")
            if not sources[target]:
                errors.append(f"CMake: {target} has no explicit implementation sources")

        production_cpp = {
            path.resolve()
            for path in module_roots[module].rglob("*.cpp")
            if "tests" not in path.parts and "generated" not in path.parts
        }
        declared_cpp = {
            path for target in targets for path in sources[target] if path.suffix.lower() == ".cpp"
        }
        missing = production_cpp - declared_cpp
        foreign = {path for path in declared_cpp if not path.is_relative_to(module_roots[module])}
        for path in sorted(missing):
            errors.append(f"CMake: {module} does not own {path.relative_to(root)}")
        for path in sorted(foreign):
            errors.append(f"CMake: {module} compiles source outside its module: {path.relative_to(root)}")

        if module in {"CKSdlGpuRasterizer", "CKBgfxRasterizer"}:
            shared, static = sorted(targets)
            shared_cpp = {path for path in sources[shared] if path.suffix.lower() == ".cpp"}
            static_cpp = {path for path in sources[static] if path.suffix.lower() == ".cpp"}
            if shared_cpp != static_cpp:
                errors.append(f"CMake: {module} shared and static targets compile different implementation sources")

    internal_install_targets = {"CKFFPLib", "CKSdlGpuRasterizerStatic", "CKBgfxRasterizerStatic"}
    runtime_rasterizers = {"CKSdlGpuRasterizer", "CKBgfxRasterizer"}
    for cmake in sorted(rasterizer.rglob("CMakeLists.txt")):
        for command, values, line in commands(cmake.read_text(encoding="utf-8-sig")):
            if command != "install" or not values or values[0].upper() != "TARGETS":
                continue
            installed = set(values[1:])
            for target in sorted(installed & internal_install_targets):
                errors.append(f"{cmake.relative_to(root)}:{line}: installs internal target {target}")
            if installed & runtime_rasterizers and any(value.upper() == "EXPORT" for value in values):
                errors.append(f"{cmake.relative_to(root)}:{line}: exports a concrete rasterizer target")

    if "CKRasterizerLib" not in edges["CKFFPLib"]:
        errors.append("CMake: CKFFPLib must link CKRasterizerLib")
    for dependency in edges["CKRasterizerLib"]:
        if dependency in {"CKFFPLib", "CKSdlGpuRasterizer", "CKSdlGpuRasterizerStatic",
                          "CKBgfxRasterizer", "CKBgfxRasterizerStatic"}:
            errors.append(f"CMake: CKRasterizerLib depends on {dependency}")
    for dependency in edges["CKFFPLib"]:
        if dependency in {"CKSdlGpuRasterizer", "CKSdlGpuRasterizerStatic",
                          "CKBgfxRasterizer", "CKBgfxRasterizerStatic"}:
            errors.append(f"CMake: CKFFPLib depends on concrete rasterizer {dependency}")
    for adapter in ("CKSdlGpuRasterizer", "CKBgfxRasterizer"):
        cmake_text = (module_roots[adapter] / "CMakeLists.txt").read_text(encoding="utf-8-sig")
        if not re.search(r'\btarget_link_libraries\s*\([^)]*\bCKFFPLib\b', cmake_text, re.DOTALL):
            errors.append(f"CMake: {adapter} must link CKFFPLib")

    forbidden_ffp_lifecycle = {
        "CKRasterizerBackendLibrary",
        "CKRasterizerBackendDriver",
        "CKTranslatedRasterizerStart",
        "CKTranslatedRasterizerClose",
    }
    for source in module_roots["CKFFPLib"].rglob("*"):
        if source.suffix.lower() not in {".cpp", ".h"}:
            continue
        text = source.read_text(encoding="utf-8-sig", errors="replace")
        for token in sorted(forbidden_ffp_lifecycle):
            if token in text:
                errors.append(
                    f"{source.relative_to(root)}: CKFFPLib owns rasterizer lifecycle token {token}"
                )

    header_index = defaultdict(list)
    for folder in (public_root, rasterizer):
        for header in folder.rglob("*.h"):
            if "generated" not in header.parts:
                header_index[header.name].append(header.resolve())

    def resolve(source, include):
        local = (source.parent / include).resolve()
        if local.is_file():
            return local
        choices = header_index[Path(include).name]
        return choices[0] if len(choices) == 1 else None

    checked_files = 0
    source_files = []
    for module, directory in module_roots.items():
        source_files.extend((path.resolve(), module) for path in directory.rglob("*")
                            if path.suffix.lower() in {".cpp", ".h"} and "generated" not in path.parts)
    source_files.extend((path.resolve(), "CKRasterizerLib") for path in public_root.glob("CKRasterizer*.h"))

    seen = set()
    ffp_interface_root = module_roots["CKFFPLib"] / "Interface"
    for source, owner in source_files:
        checked_files += 1
        text = source.read_text(encoding="utf-8-sig", errors="replace")
        for match in INCLUDE.finditer(text):
            resolved = resolve(source, match.group(1))
            if not resolved:
                continue
            imported = module_for(resolved, module_roots, public_root)
            if imported == "CKFFPLib":
                adapter = owner in {"CKSdlGpuRasterizer", "CKBgfxRasterizer"}
                interface_header = resolved.is_relative_to(ffp_interface_root)
                if adapter and not interface_header:
                    line = text.count('\n', 0, match.start()) + 1
                    errors.append(
                        f"{source.relative_to(root)}:{line}: {owner} imports private "
                        f"CKFFPLib header {match.group(1)}"
                    )
                if source.is_relative_to(ffp_interface_root) and not interface_header:
                    line = text.count('\n', 0, match.start()) + 1
                    errors.append(
                        f"{source.relative_to(root)}:{line}: library Interface imports "
                        f"private CKFFPLib header {match.group(1)}"
                    )
            if imported and imported not in allowed_imports[owner]:
                line = text.count('\n', 0, match.start()) + 1
                key = (source, line, imported)
                if key not in seen:
                    seen.add(key)
                    errors.append(
                        f"{source.relative_to(root)}:{line}: {owner} imports {imported} header {match.group(1)}"
                    )

    null_files = list(module_roots["CKRasterizerLib"].glob("CKNullRasterizer.*"))
    for source in null_files:
        text = source.read_text(encoding="utf-8-sig", errors="replace")
        for match in INCLUDE.finditer(text):
            resolved = resolve(source.resolve(), match.group(1))
            imported = module_for(resolved, module_roots, public_root) if resolved else None
            if imported and imported != "CKRasterizerLib":
                line = text.count('\n', 0, match.start()) + 1
                errors.append(
                    f"{source.relative_to(root)}:{line}: direct NULL implementation imports {imported}"
                )

    internal_roots = {"CKFFPLib", "CKSdlGpuRasterizer", "CKBgfxRasterizer"}
    for source in (root / "src").rglob("*"):
        if source.suffix.lower() not in {".cpp", ".h"} or source.is_relative_to(rasterizer):
            continue
        text = source.read_text(encoding="utf-8-sig", errors="replace")
        for match in INCLUDE.finditer(text):
            resolved = resolve(source.resolve(), match.group(1))
            imported = module_for(resolved, module_roots, public_root) if resolved else None
            if imported in internal_roots:
                line = text.count('\n', 0, match.start()) + 1
                errors.append(
                    f"{source.relative_to(root)}:{line}: engine source imports internal rasterizer module {imported}"
                )

    return errors, checked_files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    errors, checked = check(args.source_root.resolve())
    if errors:
        print("Rasterizer module boundary violations:")
        for error in sorted(set(errors)):
            print(f"  {error}")
        return 1
    print(f"Rasterizer module boundaries passed: {checked} files checked.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
