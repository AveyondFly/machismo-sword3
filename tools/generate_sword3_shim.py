#!/usr/bin/env python3
"""Generate auditable fail-fast ELF definitions for Sword3's iOS imports."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = ROOT / "configs" / "sword3" / "binary-manifest.json"
DEFAULT_OUTPUT = ROOT / "build" / "generated" / "sword3_ios_imports.c"

APPLE_FRAMEWORKS = frozenset(
    {
        "AudioToolbox",
        "AVFoundation",
        "AVKit",
        "CoreFoundation",
        "CoreGraphics",
        "CoreMotion",
        "Foundation",
        "GameController",
        "ImageIO",
        "MediaPlayer",
        "Metal",
        "MobileCoreServices",
        "QuartzCore",
        "UIKit",
    }
)

# OpenGLES is deliberately absent. Its imports are handled by the graphics
# compatibility layer and must never be folded into this fail-fast boundary.
EXCLUDED_FRAMEWORKS = frozenset({"OpenGLES"})
ASM_SYMBOL_RE = re.compile(r"^[A-Za-z0-9_.$]+$")
DATA_STORAGE_SIZE = 64
DATA_STORAGE_ALIGNMENT = 16
IMPLEMENTED_FUNCTIONS = frozenset(
    {
        "CFRunLoopGetCurrent",
        "CFRunLoopRunInMode",
        "CGRectGetHeight",
        "CGRectGetMaxX",
        "CGRectGetMaxY",
        "CGRectGetMidX",
        "CGRectGetMidY",
        "CGRectGetMinX",
        "CGRectGetMinY",
        "CGRectGetWidth",
        "NSLog",
        "NSSearchPathForDirectoriesInDomains",
    }
)
IMPLEMENTED_DATA = frozenset(
    {
        "OBJC_CLASS_$_NSBundle",
        "OBJC_METACLASS_$_NSBundle",
        "OBJC_CLASS_$_NSDictionary",
        "OBJC_CLASS_$_NSException",
        "OBJC_CLASS_$_NSFileManager",
        "OBJC_CLASS_$_NSLocale",
        "OBJC_CLASS_$_NSString",
        "OBJC_CLASS_$_NSArray",
        "OBJC_CLASS_$_NSMutableArray",
        "OBJC_CLASS_$_NSNumber",
        "OBJC_CLASS_$_NSData",
        "OBJC_CLASS_$_NSDate",
        "NSFileModificationDate",
        "OBJC_CLASS_$_NSDateFormatter",
        "OBJC_CLASS_$_NSCalendar",
        "OBJC_CLASS_$_UIDevice",
        "OBJC_CLASS_$_UIScreen",
        "OBJC_CLASS_$_UIColor",
        "OBJC_CLASS_$_UIImage",
        "OBJC_CLASS_$_UIImageView",
        "OBJC_CLASS_$_UIResponder",
        "OBJC_METACLASS_$_UIResponder",
        "OBJC_CLASS_$_UIView",
        "OBJC_METACLASS_$_UIView",
        "OBJC_CLASS_$_UIViewController",
        "OBJC_METACLASS_$_UIViewController",
        "OBJC_CLASS_$_UIWindow",
        "OBJC_METACLASS_$_UIWindow",
        "OBJC_CLASS_$_NSURL",
        "OBJC_CLASS_$_NSNotificationCenter",
        "OBJC_CLASS_$_AVPlayer",
        "OBJC_CLASS_$_AVPlayerLayer",
        "OBJC_CLASS_$_AVPlayerViewController",
        "OBJC_CLASS_$_AVAudioPlayer",
        "OBJC_CLASS_$_CADisplayLink",
        "OBJC_CLASS_$_NSRunLoop",
        "kCFRunLoopDefaultMode",
        "NSDefaultRunLoopMode",
    }
)


class GenerationError(ValueError):
    """Raised when the checked-in manifest violates a generator invariant."""


@dataclass(frozen=True)
class Import:
    macho_name: str
    elf_name: str
    frameworks: tuple[str, ...]


def _framework_name(dylib: Any) -> str | None:
    if not isinstance(dylib, str):
        raise GenerationError("bind dylib name is not a string")
    path = PurePosixPath(dylib)
    framework = path.name
    if path.parent.name != f"{framework}.framework":
        return None
    if framework in EXCLUDED_FRAMEWORKS:
        return None
    return framework if framework in APPLE_FRAMEWORKS else None


def _elf_name(macho_name: Any) -> str:
    if not isinstance(macho_name, str) or not macho_name.startswith("_"):
        raise GenerationError(f"invalid Mach-O import name: {macho_name!r}")
    elf_name = macho_name[1:]
    if not elf_name or not ASM_SYMBOL_RE.fullmatch(elf_name):
        raise GenerationError(
            f"Mach-O import cannot be represented as an ELF assembler name: "
            f"{macho_name!r}"
        )
    return elf_name


def _imports_for_kind(bind: dict[str, Any], kind: str) -> dict[str, set[str]]:
    try:
        dylibs = bind[kind]["by_dylib"]
    except (KeyError, TypeError) as error:
        raise GenerationError(f"manifest has no dyld {kind} bind list") from error
    if not isinstance(dylibs, list):
        raise GenerationError(f"manifest dyld {kind} bind list is not an array")

    imports: dict[str, set[str]] = {}
    for dylib_entry in dylibs:
        if not isinstance(dylib_entry, dict):
            raise GenerationError(f"manifest dyld {kind} dylib entry is not an object")
        framework = _framework_name(dylib_entry.get("dylib"))
        if framework is None:
            continue
        symbols = dylib_entry.get("symbols")
        if not isinstance(symbols, list):
            raise GenerationError(f"{framework} {kind} symbols are not an array")
        for symbol_entry in symbols:
            if not isinstance(symbol_entry, dict):
                raise GenerationError(
                    f"{framework} {kind} symbol entry is not an object"
                )
            macho_name = symbol_entry.get("name")
            elf_name = _elf_name(macho_name)
            imports.setdefault(elf_name, set()).add(framework)
    return imports


def read_imports(manifest: dict[str, Any]) -> tuple[list[Import], list[Import]]:
    """Return sorted function and data imports from dyld binding classes."""
    try:
        bind = manifest["dyld_info_only"]["bind"]
    except (KeyError, TypeError) as error:
        raise GenerationError("manifest has no dyld bind information") from error
    if not isinstance(bind, dict):
        raise GenerationError("manifest dyld bind information is not an object")

    # Mach-O lazy pointers are callable imports; regular binds are addresses of
    # data, constants, Objective-C classes, and Objective-C metaclasses.
    function_map = _imports_for_kind(bind, "lazy")
    data_map = _imports_for_kind(bind, "regular")
    overlap = sorted(function_map.keys() & data_map.keys())
    if overlap:
        raise GenerationError(
            "imports occur as both lazy functions and regular data: "
            + ", ".join(overlap)
        )

    def materialize(import_map: dict[str, set[str]]) -> list[Import]:
        return [
            Import(
                macho_name=f"_{elf_name}",
                elf_name=elf_name,
                frameworks=tuple(sorted(frameworks)),
            )
            for elf_name, frameworks in sorted(import_map.items())
        ]

    functions = [
        imported
        for imported in materialize(function_map)
        if imported.elf_name not in IMPLEMENTED_FUNCTIONS
    ]
    data = [
        imported
        for imported in materialize(data_map)
        if imported.elf_name not in IMPLEMENTED_DATA
    ]
    return functions, data


def _c_string(value: str) -> str:
    return (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\r", "\\r")
    )


def render(
    functions: list[Import], data: list[Import], manifest_sha256: str
) -> str:
    lines = [
        "/* Generated by tools/generate_sword3_shim.py; do not edit. */",
        f"/* Manifest SHA-256: {manifest_sha256} */",
        f"/* Imports: {len(functions)} functions, {len(data)} data symbols. */",
        "",
        '#include "sword3_ios_shim.h"',
        "",
        "#if !defined(__GNUC__) && !defined(__clang__)",
        '#error "exact ELF import names require GNU-compatible asm labels"',
        "#endif",
        "",
        f"#define SWORD3_IMPORT_DATA_SIZE {DATA_STORAGE_SIZE}",
        "",
    ]

    for index, imported in enumerate(functions):
        identifier = f"sword3_ios_import_function_{index:04d}"
        frameworks = ", ".join(imported.frameworks)
        symbol = _c_string(imported.elf_name)
        lines.extend(
            [
                f"/* function: {frameworks} :: {imported.macho_name} */",
                "SWORD3_EXPORT SWORD3_NORETURN",
                f"void {identifier}(void) __asm__(\"{symbol}\");",
                "SWORD3_EXPORT SWORD3_NORETURN",
                f"void {identifier}(void)",
                "{",
                f'    sword3_unsupported_symbol("{symbol}");',
                "}",
                "",
            ]
        )

    for index, imported in enumerate(data):
        identifier = f"sword3_ios_import_data_{index:04d}"
        frameworks = ", ".join(imported.frameworks)
        symbol = _c_string(imported.elf_name)
        lines.extend(
            [
                f"/* data: {frameworks} :: {imported.macho_name} */",
                "SWORD3_EXPORT "
                f"SWORD3_ALIGNED({DATA_STORAGE_ALIGNMENT})",
                f"unsigned char {identifier}[SWORD3_IMPORT_DATA_SIZE]",
                f'    __asm__("{symbol}") = {{0}};',
                "",
            ]
        )

    return "\n".join(lines)


def generate(manifest_path: Path, output_path: Path) -> None:
    try:
        manifest_bytes = manifest_path.read_bytes()
    except OSError as error:
        raise GenerationError(f"cannot read manifest {manifest_path}: {error}") from error
    try:
        manifest = json.loads(manifest_bytes)
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        raise GenerationError(f"invalid JSON manifest {manifest_path}: {error}") from error
    if not isinstance(manifest, dict):
        raise GenerationError("manifest root is not an object")

    functions, data = read_imports(manifest)
    if not functions or not data:
        raise GenerationError("selected Apple framework imports are unexpectedly empty")
    output = render(functions, data, hashlib.sha256(manifest_bytes).hexdigest())
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(output, encoding="utf-8", newline="\n")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--manifest",
        type=Path,
        default=DEFAULT_MANIFEST,
        help=f"input audit manifest (default: {DEFAULT_MANIFEST})",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT,
        help=f"generated C output (default: {DEFAULT_OUTPUT})",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        generate(args.manifest, args.output)
    except GenerationError as error:
        raise SystemExit(f"generate_sword3_shim.py: {error}") from error
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
