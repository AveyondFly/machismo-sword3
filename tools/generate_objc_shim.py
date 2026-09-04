#!/usr/bin/env python3
"""Generate fail-fast wrappers for Sword3's exact libobjc import surface."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = ROOT / "configs" / "sword3" / "binary-manifest.json"
DEFAULT_OUTPUT = ROOT / "build" / "generated" / "sword3_objc_imports.c"
LIBOBJC = "/usr/lib/libobjc.A.dylib"
ASM_SYMBOL_RE = re.compile(r"^[A-Za-z0-9_.$]+$")

# These four regular-bind imports are objects, not callable entry points.  They
# are defined with their real layouts in sword3_objc_shim.c.
DATA_SYMBOLS = frozenset(
    {
        "OBJC_CLASS_$_NSObject",
        "OBJC_METACLASS_$_NSObject",
        "_objc_empty_cache",
        "_objc_empty_vtable",
    }
)

# Implemented by the hand-written C and AArch64 assembly.  Every other
# imported function gets its own generated fail-fast definition.
IMPLEMENTED_FUNCTIONS = frozenset(
    {
        "__objc_personality_v0",
        "class_getInstanceMethod",
        "class_getName",
        "class_getSuperclass",
        "class_isMetaClass",
        "objc_alloc",
        "objc_autorelease",
        "objc_autoreleasePoolPop",
        "objc_autoreleasePoolPush",
        "objc_autoreleaseReturnValue",
        "objc_constructInstance",
        "objc_destroyWeak",
        "objc_getClass",
        "objc_getMetaClass",
        "objc_getProperty",
        "objc_getRequiredClass",
        "objc_loadWeakRetained",
        "objc_lookUpClass",
        "objc_msgSend",
        "objc_msgSendSuper2",
        "objc_release",
        "objc_retain",
        "objc_retainAutorelease",
        "objc_retainAutoreleaseReturnValue",
        "objc_retainAutoreleasedReturnValue",
        "objc_setProperty_atomic",
        "objc_storeStrong",
        "objc_storeWeak",
        "object_getClass",
        "object_getIndexedIvars",
        "sel_getUid",
    }
)


class GenerationError(ValueError):
    """Raised when the manifest cannot define an auditable shim."""


@dataclass(frozen=True)
class ObjCImports:
    functions: tuple[str, ...]
    data: tuple[str, ...]

    @property
    def all_symbols(self) -> tuple[str, ...]:
        return tuple(sorted((*self.functions, *self.data)))

    @property
    def unsupported_functions(self) -> tuple[str, ...]:
        return tuple(symbol for symbol in self.functions if symbol not in IMPLEMENTED_FUNCTIONS)


def macho_to_elf(name: Any) -> str:
    if not isinstance(name, str) or not name.startswith("_"):
        raise GenerationError(f"invalid Mach-O symbol name: {name!r}")
    elf_name = name[1:]
    if not elf_name or ASM_SYMBOL_RE.fullmatch(elf_name) is None:
        raise GenerationError(f"symbol has no safe ELF spelling: {name!r}")
    return elf_name


def _symbols_for_bind_kind(bind: dict[str, Any], kind: str) -> set[str]:
    try:
        dylibs = bind[kind]["by_dylib"]
    except (KeyError, TypeError) as error:
        raise GenerationError(f"manifest has no dyld {kind} bind list") from error
    if not isinstance(dylibs, list):
        raise GenerationError(f"dyld {kind} bind list is not an array")

    symbols: set[str] = set()
    found = False
    for entry in dylibs:
        if not isinstance(entry, dict):
            raise GenerationError(f"dyld {kind} dylib entry is not an object")
        if entry.get("dylib") != LIBOBJC:
            continue
        found = True
        raw_symbols = entry.get("symbols")
        if not isinstance(raw_symbols, list):
            raise GenerationError(f"{LIBOBJC} {kind} symbols are not an array")
        for symbol in raw_symbols:
            if not isinstance(symbol, dict):
                raise GenerationError(f"{LIBOBJC} {kind} symbol is not an object")
            symbols.add(macho_to_elf(symbol.get("name")))
    if not found:
        raise GenerationError(f"manifest has no {kind} binds for {LIBOBJC}")
    return symbols


def read_imports(manifest: dict[str, Any]) -> ObjCImports:
    try:
        bind = manifest["dyld_info_only"]["bind"]
    except (KeyError, TypeError) as error:
        raise GenerationError("manifest has no dyld bind information") from error
    if not isinstance(bind, dict):
        raise GenerationError("manifest dyld bind information is not an object")

    lazy = _symbols_for_bind_kind(bind, "lazy")
    regular = _symbols_for_bind_kind(bind, "regular")
    unexpected_lazy_data = sorted(lazy & DATA_SYMBOLS)
    if unexpected_lazy_data:
        raise GenerationError(
            "data symbols unexpectedly use lazy binds: " + ", ".join(unexpected_lazy_data)
        )
    missing_data = sorted(DATA_SYMBOLS - regular)
    if missing_data:
        raise GenerationError(
            "required Objective-C data imports are missing: " + ", ".join(missing_data)
        )

    all_symbols = lazy | regular
    data = tuple(sorted(all_symbols & DATA_SYMBOLS))
    functions = tuple(sorted(all_symbols - DATA_SYMBOLS))
    return ObjCImports(functions=functions, data=data)


def _c_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def render(imports: ObjCImports, manifest_sha256: str) -> str:
    unsupported = imports.unsupported_functions
    lines = [
        "/* Generated by tools/generate_objc_shim.py; do not edit. */",
        f"/* Manifest SHA-256: {manifest_sha256} */",
        (
            f"/* libobjc imports: {len(imports.functions)} functions, "
            f"{len(imports.data)} data; {len(unsupported)} fail-fast wrappers. */"
        ),
        "",
        '#include "sword3_objc_shim.h"',
        "",
    ]
    for index, symbol in enumerate(unsupported):
        escaped = _c_string(symbol)
        identifier = f"sword3_objc_unsupported_{index:03d}"
        lines.extend(
            [
                f"/* unsupported libobjc import: {symbol} */",
                "SWORD3_OBJC_EXPORT SWORD3_OBJC_NORETURN",
                f'void {identifier}(void) __asm__("{escaped}");',
                "SWORD3_OBJC_EXPORT SWORD3_OBJC_NORETURN",
                f"void {identifier}(void)",
                "{",
                f'    sword3_objc_unsupported_symbol("{escaped}");',
                "}",
                "",
            ]
        )
    return "\n".join(lines)


def generate(manifest_path: Path, output_path: Path) -> ObjCImports:
    try:
        manifest_bytes = manifest_path.read_bytes()
        manifest = json.loads(manifest_bytes)
    except OSError as error:
        raise GenerationError(f"cannot read {manifest_path}: {error}") from error
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        raise GenerationError(f"invalid JSON manifest {manifest_path}: {error}") from error
    if not isinstance(manifest, dict):
        raise GenerationError("manifest root is not an object")

    imports = read_imports(manifest)
    output = render(imports, hashlib.sha256(manifest_bytes).hexdigest())
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(output, encoding="utf-8", newline="\n")
    return imports


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("-o", "--output", type=Path, default=DEFAULT_OUTPUT)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        generate(args.manifest, args.output)
    except GenerationError as error:
        raise SystemExit(f"generate_objc_shim.py: {error}") from error
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
