#!/usr/bin/env python3
"""Deterministic, read-only audit of a 64-bit little-endian arm64 Mach-O."""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any


MH_MAGIC_64 = 0xFEEDFACF
CPU_TYPE_ARM64 = 0x0100000C

LC_SEGMENT_64 = 0x19
LC_SYMTAB = 0x2
LC_DYSYMTAB = 0xB
LC_UUID = 0x1B
LC_MAIN = 0x80000028
LC_ENCRYPTION_INFO_64 = 0x2C
LC_FUNCTION_STARTS = 0x26
LC_DYLD_INFO_ONLY = 0x80000022

DYLIB_COMMANDS = {
    0xC: "LC_LOAD_DYLIB",
    0xD: "LC_ID_DYLIB",
    0x80000018: "LC_LOAD_WEAK_DYLIB",
    0x8000001F: "LC_REEXPORT_DYLIB",
    0x20: "LC_LAZY_LOAD_DYLIB",
    0x80000023: "LC_LOAD_UPWARD_DYLIB",
}

LOAD_COMMAND_NAMES = {
    0x1: "LC_SEGMENT",
    0x2: "LC_SYMTAB",
    0x5: "LC_UNIXTHREAD",
    0xB: "LC_DYSYMTAB",
    0xC: "LC_LOAD_DYLIB",
    0xD: "LC_ID_DYLIB",
    0xE: "LC_LOAD_DYLINKER",
    0x19: "LC_SEGMENT_64",
    0x1B: "LC_UUID",
    0x1D: "LC_CODE_SIGNATURE",
    0x20: "LC_LAZY_LOAD_DYLIB",
    0x22: "LC_DYLD_INFO",
    0x25: "LC_VERSION_MIN_IPHONEOS",
    0x26: "LC_FUNCTION_STARTS",
    0x29: "LC_DATA_IN_CODE",
    0x2A: "LC_SOURCE_VERSION",
    0x2C: "LC_ENCRYPTION_INFO_64",
    0x32: "LC_BUILD_VERSION",
    0x80000018: "LC_LOAD_WEAK_DYLIB",
    0x8000001C: "LC_RPATH",
    0x8000001F: "LC_REEXPORT_DYLIB",
    0x80000022: "LC_DYLD_INFO_ONLY",
    0x80000023: "LC_LOAD_UPWARD_DYLIB",
    0x80000028: "LC_MAIN",
}

FILETYPE_NAMES = {
    1: "MH_OBJECT",
    2: "MH_EXECUTE",
    3: "MH_FVMLIB",
    4: "MH_CORE",
    5: "MH_PRELOAD",
    6: "MH_DYLIB",
    7: "MH_DYLINKER",
    8: "MH_BUNDLE",
    10: "MH_DSYM",
    11: "MH_KEXT_BUNDLE",
    12: "MH_FILESET",
}

HEADER_FLAGS = {
    0x1: "MH_NOUNDEFS",
    0x2: "MH_INCRLINK",
    0x4: "MH_DYLDLINK",
    0x8: "MH_BINDATLOAD",
    0x10: "MH_PREBOUND",
    0x20: "MH_SPLIT_SEGS",
    0x40: "MH_LAZY_INIT",
    0x80: "MH_TWOLEVEL",
    0x100: "MH_FORCE_FLAT",
    0x200: "MH_NOMULTIDEFS",
    0x400: "MH_NOFIXPREBINDING",
    0x800: "MH_PREBINDABLE",
    0x1000: "MH_ALLMODSBOUND",
    0x2000: "MH_SUBSECTIONS_VIA_SYMBOLS",
    0x4000: "MH_CANONICAL",
    0x8000: "MH_WEAK_DEFINES",
    0x10000: "MH_BINDS_TO_WEAK",
    0x20000: "MH_ALLOW_STACK_EXECUTION",
    0x40000: "MH_ROOT_SAFE",
    0x80000: "MH_SETUID_SAFE",
    0x100000: "MH_NO_REEXPORTED_DYLIBS",
    0x200000: "MH_PIE",
    0x400000: "MH_DEAD_STRIPPABLE_DYLIB",
    0x800000: "MH_HAS_TLV_DESCRIPTORS",
    0x1000000: "MH_NO_HEAP_EXECUTION",
    0x2000000: "MH_APP_EXTENSION_SAFE",
    0x4000000: "MH_NLIST_OUTOFSYNC_WITH_DYLDINFO",
    0x8000000: "MH_SIM_SUPPORT",
    0x80000000: "MH_DYLIB_IN_CACHE",
}

SECTION_TYPES = {
    0x0: "S_REGULAR",
    0x1: "S_ZEROFILL",
    0x2: "S_CSTRING_LITERALS",
    0x3: "S_4BYTE_LITERALS",
    0x4: "S_8BYTE_LITERALS",
    0x5: "S_LITERAL_POINTERS",
    0x6: "S_NON_LAZY_SYMBOL_POINTERS",
    0x7: "S_LAZY_SYMBOL_POINTERS",
    0x8: "S_SYMBOL_STUBS",
    0x9: "S_MOD_INIT_FUNC_POINTERS",
    0xA: "S_MOD_TERM_FUNC_POINTERS",
    0xB: "S_COALESCED",
    0xC: "S_GB_ZEROFILL",
    0xD: "S_INTERPOSING",
    0xE: "S_16BYTE_LITERALS",
    0xF: "S_DTRACE_DOF",
    0x10: "S_LAZY_DYLIB_SYMBOL_POINTERS",
    0x11: "S_THREAD_LOCAL_REGULAR",
    0x12: "S_THREAD_LOCAL_ZEROFILL",
    0x13: "S_THREAD_LOCAL_VARIABLES",
    0x14: "S_THREAD_LOCAL_VARIABLE_POINTERS",
    0x15: "S_THREAD_LOCAL_INIT_FUNCTION_POINTERS",
}

REBASE_OPCODES = {
    0x00: "DONE",
    0x10: "SET_TYPE_IMM",
    0x20: "SET_SEGMENT_AND_OFFSET_ULEB",
    0x30: "ADD_ADDR_ULEB",
    0x40: "ADD_ADDR_IMM_SCALED",
    0x50: "DO_REBASE_IMM_TIMES",
    0x60: "DO_REBASE_ULEB_TIMES",
    0x70: "DO_REBASE_ADD_ADDR_ULEB",
    0x80: "DO_REBASE_ULEB_TIMES_SKIPPING_ULEB",
}

BIND_OPCODES = {
    0x00: "DONE",
    0x10: "SET_DYLIB_ORDINAL_IMM",
    0x20: "SET_DYLIB_ORDINAL_ULEB",
    0x30: "SET_DYLIB_SPECIAL_IMM",
    0x40: "SET_SYMBOL_TRAILING_FLAGS_IMM",
    0x50: "SET_TYPE_IMM",
    0x60: "SET_ADDEND_SLEB",
    0x70: "SET_SEGMENT_AND_OFFSET_ULEB",
    0x80: "ADD_ADDR_ULEB",
    0x90: "DO_BIND",
    0xA0: "DO_BIND_ADD_ADDR_ULEB",
    0xB0: "DO_BIND_ADD_ADDR_IMM_SCALED",
    0xC0: "DO_BIND_ULEB_TIMES_SKIPPING_ULEB",
    0xD0: "THREADED",
}


class AuditError(ValueError):
    """A deterministic error for malformed or unsupported input."""


def checked_slice(data: bytes, offset: int, size: int, label: str) -> bytes:
    if offset < 0 or size < 0 or offset + size > len(data):
        raise AuditError(
            f"{label} is outside file bounds: offset={offset}, size={size}"
        )
    return data[offset : offset + size]


def unpack_from(fmt: str, data: bytes, offset: int, label: str) -> tuple[Any, ...]:
    size = struct.calcsize(fmt)
    checked_slice(data, offset, size, label)
    return struct.unpack_from(fmt, data, offset)


def fixed_string(value: bytes) -> str:
    return value.split(b"\0", 1)[0].decode("utf-8", "replace")


def c_string(data: bytes, offset: int, end: int, label: str) -> tuple[str, int]:
    if offset < 0 or offset >= end or end > len(data):
        raise AuditError(f"{label} string starts outside bounds")
    nul = data.find(b"\0", offset, end)
    if nul < 0:
        raise AuditError(f"{label} string is not NUL terminated")
    return data[offset:nul].decode("utf-8", "replace"), nul + 1


def read_uleb(data: bytes, offset: int, end: int, label: str) -> tuple[int, int]:
    value = 0
    shift = 0
    start = offset
    while offset < end:
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return value, offset
        shift += 7
        if shift >= 64:
            raise AuditError(f"{label} ULEB128 at {start} exceeds 64 bits")
    raise AuditError(f"{label} has truncated ULEB128 at {start}")


def read_sleb(data: bytes, offset: int, end: int, label: str) -> tuple[int, int]:
    value = 0
    shift = 0
    start = offset
    while offset < end:
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7F) << shift
        shift += 7
        if not byte & 0x80:
            if shift < 64 and byte & 0x40:
                value |= -(1 << shift)
            return value, offset
        if shift >= 64:
            raise AuditError(f"{label} SLEB128 at {start} exceeds 64 bits")
    raise AuditError(f"{label} has truncated SLEB128 at {start}")


def version_string(value: int) -> str:
    return f"{value >> 16}.{(value >> 8) & 0xff}.{value & 0xff}"


def section_identity(section: dict[str, Any]) -> str:
    return f"{section['segment']},{section['name']}"


class MachOAudit:
    def __init__(self, data: bytes, filename: str) -> None:
        self.data = data
        self.filename = filename
        self.segments: list[dict[str, Any]] = []
        self.sections: list[dict[str, Any]] = []
        self.dylibs: list[dict[str, Any]] = []
        self.commands: list[tuple[int, int, int]] = []
        self.command_counts: Counter[str] = Counter()
        self.uuid: str | None = None
        self.lc_main: dict[str, int] | None = None
        self.encryption: dict[str, int] | None = None
        self.symtab_command: tuple[int, int, int, int] | None = None
        self.dysymtab: dict[str, int] | None = None
        self.function_starts_command: tuple[int, int] | None = None
        self.dyld_info: dict[str, int] | None = None
        self.bind_events: list[dict[str, Any]] = []
        self.unparsed_notes: list[str] = []

    def parse(self) -> dict[str, Any]:
        header = unpack_from("<IiiIIIII", self.data, 0, "mach_header_64")
        magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved = (
            header
        )
        if magic != MH_MAGIC_64:
            raise AuditError(
                "only 64-bit little-endian Mach-O (MH_MAGIC_64) is supported"
            )
        if cputype != CPU_TYPE_ARM64:
            raise AuditError(
                f"only arm64 is supported (found CPU type 0x{cputype & 0xffffffff:08x})"
            )
        checked_slice(self.data, 32, sizeofcmds, "load command region")
        command_offset = 32
        for index in range(ncmds):
            cmd, cmdsize = unpack_from(
                "<II", self.data, command_offset, f"load command {index}"
            )
            if cmdsize < 8 or command_offset + cmdsize > 32 + sizeofcmds:
                raise AuditError(f"invalid load command {index} size {cmdsize}")
            self.commands.append((cmd, command_offset, cmdsize))
            command_name = LOAD_COMMAND_NAMES.get(cmd, f"UNKNOWN_0x{cmd:08x}")
            self.command_counts[command_name] += 1
            self._parse_command(cmd, command_offset, cmdsize, index)
            command_offset += cmdsize
        if command_offset != 32 + sizeofcmds:
            raise AuditError("load command sizes do not equal header sizeofcmds")

        dependencies = [d for d in self.dylibs if d["command"] != "LC_ID_DYLIB"]
        symbol_table = self._parse_symbols(dependencies)
        function_starts = self._parse_function_starts()
        dyld = self._parse_dyld_info(dependencies)
        mod_init = self._parse_mod_init()
        objc = self._parse_objc()
        unwind = self._parse_unwind()

        subtype_value = cpusubtype & 0x00FFFFFF
        subtype_names = {0: "arm64_all", 1: "arm64_v8", 2: "arm64e"}
        header_result = {
            "magic": "0xfeedfacf",
            "bits": 64,
            "endianness": "little",
            "cpu": {
                "type": cputype,
                "name": "arm64",
                "subtype": subtype_value,
                "subtype_name": subtype_names.get(
                    subtype_value, f"unknown_{subtype_value}"
                ),
                "capabilities": (cpusubtype >> 24) & 0xFF,
            },
            "filetype": {
                "value": filetype,
                "name": FILETYPE_NAMES.get(filetype, f"UNKNOWN_{filetype}"),
            },
            "flags": {
                "value": flags,
                "names": [
                    name for bit, name in HEADER_FLAGS.items() if flags & bit
                ],
            },
            "ncmds": ncmds,
            "sizeofcmds": sizeofcmds,
            "reserved": reserved,
        }
        return {
            "schema_version": 1,
            "mode": "audit-only",
            "file": {
                "name": self.filename,
                "size": len(self.data),
                "sha256": hashlib.sha256(self.data).hexdigest(),
            },
            "macho": header_result,
            "uuid": self.uuid,
            "load_commands": {
                "count": ncmds,
                "by_type": dict(sorted(self.command_counts.items())),
            },
            "segments": self.segments,
            "lc_main": self.lc_main,
            "encryption_info_64": self.encryption,
            "dylibs": dependencies,
            "symtab": symbol_table,
            "function_starts": function_starts,
            "dyld_info_only": dyld,
            "mod_init_func": mod_init,
            "objc": objc,
            "unwind": unwind,
            "unparsed": sorted(set(self.unparsed_notes)),
        }

    def _parse_command(
        self, cmd: int, offset: int, cmdsize: int, index: int
    ) -> None:
        if cmd == LC_SEGMENT_64:
            self._parse_segment(offset, cmdsize, index)
        elif cmd in DYLIB_COMMANDS:
            self._parse_dylib(cmd, offset, cmdsize)
        elif cmd == LC_UUID:
            if cmdsize < 24:
                raise AuditError("LC_UUID is too small")
            raw = checked_slice(self.data, offset + 8, 16, "LC_UUID value")
            text = raw.hex()
            self.uuid = (
                f"{text[:8]}-{text[8:12]}-{text[12:16]}-"
                f"{text[16:20]}-{text[20:]}"
            ).upper()
        elif cmd == LC_MAIN:
            if cmdsize < 24:
                raise AuditError("LC_MAIN is too small")
            _, _, entryoff, stacksize = unpack_from(
                "<IIQQ", self.data, offset, "LC_MAIN"
            )
            self.lc_main = {"entryoff": entryoff, "stacksize": stacksize}
        elif cmd == LC_ENCRYPTION_INFO_64:
            if cmdsize < 24:
                raise AuditError("LC_ENCRYPTION_INFO_64 is too small")
            _, _, cryptoff, cryptsize, cryptid, pad = unpack_from(
                "<IIIIII", self.data, offset, "LC_ENCRYPTION_INFO_64"
            )
            self.encryption = {
                "cryptoff": cryptoff,
                "cryptsize": cryptsize,
                "cryptid": cryptid,
                "pad": pad,
            }
        elif cmd == LC_SYMTAB:
            if cmdsize < 24:
                raise AuditError("LC_SYMTAB is too small")
            _, _, symoff, nsyms, stroff, strsize = unpack_from(
                "<IIIIII", self.data, offset, "LC_SYMTAB"
            )
            self.symtab_command = (symoff, nsyms, stroff, strsize)
        elif cmd == LC_DYSYMTAB:
            if cmdsize < 80:
                raise AuditError("LC_DYSYMTAB is too small")
            values = unpack_from("<20I", self.data, offset, "LC_DYSYMTAB")
            keys = (
                "cmd",
                "cmdsize",
                "ilocalsym",
                "nlocalsym",
                "iextdefsym",
                "nextdefsym",
                "iundefsym",
                "nundefsym",
                "tocoff",
                "ntoc",
                "modtaboff",
                "nmodtab",
                "extrefsymoff",
                "nextrefsyms",
                "indirectsymoff",
                "nindirectsyms",
                "extreloff",
                "nextrel",
                "locreloff",
                "nlocrel",
            )
            self.dysymtab = dict(zip(keys[2:], values[2:]))
        elif cmd == LC_FUNCTION_STARTS:
            if cmdsize < 16:
                raise AuditError("LC_FUNCTION_STARTS is too small")
            _, _, dataoff, datasize = unpack_from(
                "<IIII", self.data, offset, "LC_FUNCTION_STARTS"
            )
            self.function_starts_command = (dataoff, datasize)
        elif cmd == LC_DYLD_INFO_ONLY:
            if cmdsize < 48:
                raise AuditError("LC_DYLD_INFO_ONLY is too small")
            values = unpack_from("<12I", self.data, offset, "LC_DYLD_INFO_ONLY")
            keys = (
                "rebase_off",
                "rebase_size",
                "bind_off",
                "bind_size",
                "weak_bind_off",
                "weak_bind_size",
                "lazy_bind_off",
                "lazy_bind_size",
                "export_off",
                "export_size",
            )
            self.dyld_info = dict(zip(keys, values[2:]))

    def _parse_segment(self, offset: int, cmdsize: int, index: int) -> None:
        if cmdsize < 72:
            raise AuditError(f"LC_SEGMENT_64 {index} is too small")
        values = unpack_from(
            "<II16sQQQQiiII", self.data, offset, f"LC_SEGMENT_64 {index}"
        )
        (
            _,
            _,
            raw_name,
            vmaddr,
            vmsize,
            fileoff,
            filesize,
            maxprot,
            initprot,
            nsects,
            segflags,
        ) = values
        if 72 + nsects * 80 > cmdsize:
            raise AuditError(f"LC_SEGMENT_64 {index} sections exceed command size")
        segment = {
            "index": len(self.segments),
            "name": fixed_string(raw_name),
            "vmaddr": vmaddr,
            "vmsize": vmsize,
            "fileoff": fileoff,
            "filesize": filesize,
            "maxprot": maxprot,
            "initprot": initprot,
            "flags": segflags,
            "sections": [],
        }
        section_offset = offset + 72
        for _section_index in range(nsects):
            section_values = unpack_from(
                "<16s16sQQIIIIIIII",
                self.data,
                section_offset,
                "section_64",
            )
            (
                raw_sectname,
                raw_segname,
                addr,
                size,
                file_offset,
                align,
                reloff,
                nreloc,
                flags,
                reserved1,
                reserved2,
                reserved3,
            ) = section_values
            section = {
                "index": len(self.sections) + 1,
                "name": fixed_string(raw_sectname),
                "segment": fixed_string(raw_segname),
                "address": addr,
                "size": size,
                "offset": file_offset,
                "align": align,
                "reloff": reloff,
                "nreloc": nreloc,
                "flags": flags,
                "type": SECTION_TYPES.get(
                    flags & 0xFF, f"UNKNOWN_0x{flags & 0xff:02x}"
                ),
                "reserved1": reserved1,
                "reserved2": reserved2,
                "reserved3": reserved3,
            }
            self.sections.append(section)
            segment["sections"].append(section)
            section_offset += 80
        self.segments.append(segment)

    def _parse_dylib(self, cmd: int, offset: int, cmdsize: int) -> None:
        if cmdsize < 24:
            raise AuditError(f"{DYLIB_COMMANDS[cmd]} is too small")
        _, _, name_offset, timestamp, current, compatibility = unpack_from(
            "<IIIIII", self.data, offset, DYLIB_COMMANDS[cmd]
        )
        if name_offset < 24 or name_offset >= cmdsize:
            raise AuditError(f"{DYLIB_COMMANDS[cmd]} has invalid name offset")
        name, _ = c_string(
            self.data,
            offset + name_offset,
            offset + cmdsize,
            DYLIB_COMMANDS[cmd],
        )
        self.dylibs.append(
            {
                "command": DYLIB_COMMANDS[cmd],
                "name": name,
                "timestamp": timestamp,
                "current_version": version_string(current),
                "compatibility_version": version_string(compatibility),
            }
        )

    def _parse_symbols(
        self, dependencies: list[dict[str, Any]]
    ) -> dict[str, Any] | None:
        if self.symtab_command is None:
            return None
        symoff, nsyms, stroff, strsize = self.symtab_command
        checked_slice(self.data, symoff, nsyms * 16, "nlist_64 array")
        strings = checked_slice(self.data, stroff, strsize, "symbol string table")
        undefined: list[dict[str, Any]] = []
        defined: list[dict[str, Any]] = []
        debug: list[dict[str, Any]] = []
        type_counts: Counter[str] = Counter()
        type_names = {0x0: "N_UNDF", 0x2: "N_ABS", 0xA: "N_INDR", 0xE: "N_SECT"}
        for index in range(nsyms):
            strx, n_type, n_sect, n_desc, n_value = unpack_from(
                "<IBBHQ", self.data, symoff + index * 16, f"symbol {index}"
            )
            if strx >= strsize:
                raise AuditError(f"symbol {index} string index is outside string table")
            nul = strings.find(b"\0", strx)
            if nul < 0:
                raise AuditError(f"symbol {index} name is not NUL terminated")
            name = strings[strx:nul].decode("utf-8", "replace")
            masked_type = n_type & 0x0E
            if n_type & 0xE0:
                category = "debug"
                type_name = "N_STAB"
            elif masked_type == 0x0:
                category = "undefined"
                type_name = "N_UNDF"
            else:
                category = "defined"
                type_name = type_names.get(masked_type, f"UNKNOWN_0x{masked_type:02x}")
            type_counts[type_name] += 1
            entry: dict[str, Any] = {
                "index": index,
                "name": name,
                "type": type_name,
                "external": bool(n_type & 0x01),
                "private_external": bool(n_type & 0x10),
                "section_index": n_sect,
                "description": n_desc,
                "value": n_value,
            }
            if 0 < n_sect <= len(self.sections):
                entry["section"] = section_identity(self.sections[n_sect - 1])
            if category == "undefined":
                ordinal = (n_desc >> 8) & 0xFF
                signed_ordinal = ordinal - 256 if ordinal >= 0x80 else ordinal
                entry["dylib_ordinal"] = signed_ordinal
                entry["dylib"] = self._ordinal_name(signed_ordinal, dependencies)
                entry["weak_reference"] = bool(n_desc & 0x40)
            elif category == "defined":
                entry["weak_definition"] = bool(n_desc & 0x80)
            {"undefined": undefined, "defined": defined, "debug": debug}[category].append(
                entry
            )
        result: dict[str, Any] = {
            "symoff": symoff,
            "nsyms": nsyms,
            "stroff": stroff,
            "strsize": strsize,
            "undefined_count": len(undefined),
            "defined_count": len(defined),
            "debug_count": len(debug),
            "by_type": dict(sorted(type_counts.items())),
            "undefined_symbols": undefined,
            "defined_symbols": defined,
        }
        if debug:
            result["debug_symbols"] = debug
        if self.dysymtab is not None:
            result["dysymtab"] = self.dysymtab
        return result

    @staticmethod
    def _ordinal_name(
        ordinal: int, dependencies: list[dict[str, Any]]
    ) -> str:
        special = {
            0: "self",
            -1: "main-executable",
            -2: "flat-lookup",
            -3: "weak-lookup",
        }
        if ordinal in special:
            return special[ordinal]
        if 1 <= ordinal <= len(dependencies):
            return dependencies[ordinal - 1]["name"]
        return f"invalid-ordinal-{ordinal}"

    def _parse_function_starts(self) -> dict[str, Any] | None:
        if self.function_starts_command is None:
            return None
        dataoff, datasize = self.function_starts_command
        raw = checked_slice(self.data, dataoff, datasize, "LC_FUNCTION_STARTS data")
        text = next((s for s in self.segments if s["name"] == "__TEXT"), None)
        if text is None:
            raise AuditError("LC_FUNCTION_STARTS requires a __TEXT segment")
        cursor = 0
        relative = 0
        entries: list[dict[str, int]] = []
        terminated = False
        while cursor < len(raw):
            delta, cursor = read_uleb(raw, cursor, len(raw), "LC_FUNCTION_STARTS")
            if delta == 0:
                terminated = True
                break
            relative += delta
            entries.append(
                {
                    "offset_from_text": relative,
                    "address": text["vmaddr"] + relative,
                }
            )
        return {
            "dataoff": dataoff,
            "datasize": datasize,
            "count": len(entries),
            "terminated": terminated,
            "entries": entries,
        }

    def _parse_dyld_info(
        self, dependencies: list[dict[str, Any]]
    ) -> dict[str, Any] | None:
        if self.dyld_info is None:
            return None
        if self.dyld_info["export_size"]:
            self.unparsed_notes.append(
                "LC_DYLD_INFO_ONLY export trie is recorded as a range but not decoded"
            )
        ranges = {
            name.removesuffix("_size"): {
                "offset": self.dyld_info[f"{name.removesuffix('_size')}_off"],
                "size": value,
            }
            for name, value in self.dyld_info.items()
            if name.endswith("_size")
        }
        return {
            "ranges": ranges,
            "rebase": self._decode_rebase(
                self.dyld_info["rebase_off"], self.dyld_info["rebase_size"]
            ),
            "bind": {
                "regular": self._decode_bind(
                    self.dyld_info["bind_off"],
                    self.dyld_info["bind_size"],
                    "regular",
                    dependencies,
                ),
                "weak": self._decode_bind(
                    self.dyld_info["weak_bind_off"],
                    self.dyld_info["weak_bind_size"],
                    "weak",
                    dependencies,
                ),
                "lazy": self._decode_bind(
                    self.dyld_info["lazy_bind_off"],
                    self.dyld_info["lazy_bind_size"],
                    "lazy",
                    dependencies,
                ),
            },
        }

    def _segment_location(self, segment_index: int, offset: int) -> tuple[str, int]:
        if segment_index < 0 or segment_index >= len(self.segments):
            raise AuditError(f"dyld opcode uses invalid segment index {segment_index}")
        segment = self.segments[segment_index]
        if offset > segment["vmsize"]:
            raise AuditError(
                f"dyld opcode offset {offset} exceeds segment {segment['name']}"
            )
        return segment["name"], segment["vmaddr"] + offset

    def _decode_rebase(self, dataoff: int, datasize: int) -> dict[str, Any]:
        raw = checked_slice(self.data, dataoff, datasize, "rebase opcodes")
        cursor = 0
        segment_index = 0
        segment_offset = 0
        rebase_type = 0
        pointer_size = 8
        address_mask = (1 << 64) - 1
        opcodes: Counter[str] = Counter()
        events: list[dict[str, Any]] = []

        def emit() -> None:
            nonlocal segment_offset
            segment_name, address = self._segment_location(
                segment_index, segment_offset
            )
            events.append(
                {
                    "segment": segment_name,
                    "address": address,
                    "type": rebase_type,
                }
            )
            segment_offset = (segment_offset + pointer_size) & address_mask

        while cursor < len(raw):
            byte = raw[cursor]
            cursor += 1
            opcode, immediate = byte & 0xF0, byte & 0x0F
            name = REBASE_OPCODES.get(opcode, f"UNKNOWN_0x{opcode:02x}")
            opcodes[name] += 1
            if opcode == 0x00:
                continue
            if opcode == 0x10:
                rebase_type = immediate
            elif opcode == 0x20:
                segment_index = immediate
                segment_offset, cursor = read_uleb(raw, cursor, len(raw), "rebase")
            elif opcode == 0x30:
                value, cursor = read_uleb(raw, cursor, len(raw), "rebase")
                segment_offset = (segment_offset + value) & address_mask
            elif opcode == 0x40:
                segment_offset = (
                    segment_offset + immediate * pointer_size
                ) & address_mask
            elif opcode == 0x50:
                for _ in range(immediate):
                    emit()
            elif opcode == 0x60:
                count, cursor = read_uleb(raw, cursor, len(raw), "rebase")
                for _ in range(count):
                    emit()
            elif opcode == 0x70:
                emit()
                value, cursor = read_uleb(raw, cursor, len(raw), "rebase")
                segment_offset = (segment_offset + value) & address_mask
            elif opcode == 0x80:
                count, cursor = read_uleb(raw, cursor, len(raw), "rebase")
                skip, cursor = read_uleb(raw, cursor, len(raw), "rebase")
                for _ in range(count):
                    emit()
                    segment_offset = (segment_offset + skip) & address_mask
            else:
                raise AuditError(f"unsupported rebase opcode 0x{opcode:02x}")
        by_segment = Counter(event["segment"] for event in events)
        return {
            "opcode_count": sum(opcodes.values()),
            "opcodes": dict(sorted(opcodes.items())),
            "rebase_count": len(events),
            "by_segment": dict(sorted(by_segment.items())),
        }

    def _decode_bind(
        self,
        dataoff: int,
        datasize: int,
        kind: str,
        dependencies: list[dict[str, Any]],
    ) -> dict[str, Any]:
        raw = checked_slice(self.data, dataoff, datasize, f"{kind} bind opcodes")
        cursor = 0
        segment_index = 0
        segment_offset = 0
        ordinal = -3 if kind == "weak" else 0
        symbol = ""
        symbol_flags = 0
        bind_type = 1
        addend = 0
        pointer_size = 8
        address_mask = (1 << 64) - 1
        opcodes: Counter[str] = Counter()
        events: list[dict[str, Any]] = []

        def emit() -> None:
            nonlocal segment_offset
            segment_name, address = self._segment_location(
                segment_index, segment_offset
            )
            dylib = self._ordinal_name(ordinal, dependencies)
            events.append(
                {
                    "segment": segment_name,
                    "address": address,
                    "dylib_ordinal": ordinal,
                    "dylib": dylib,
                    "symbol": symbol,
                    "symbol_flags": symbol_flags,
                    "weak_import": bool(symbol_flags & 0x1),
                    "non_weak_definition": bool(symbol_flags & 0x8),
                    "type": bind_type,
                    "addend": addend,
                }
            )
            segment_offset = (segment_offset + pointer_size) & address_mask

        while cursor < len(raw):
            byte = raw[cursor]
            cursor += 1
            opcode, immediate = byte & 0xF0, byte & 0x0F
            name = BIND_OPCODES.get(opcode, f"UNKNOWN_0x{opcode:02x}")
            opcodes[name] += 1
            if opcode == 0x00:
                continue
            if opcode == 0x10:
                ordinal = immediate
            elif opcode == 0x20:
                ordinal, cursor = read_uleb(raw, cursor, len(raw), f"{kind} bind")
            elif opcode == 0x30:
                ordinal = immediate | (-16 if immediate & 0x8 else 0)
            elif opcode == 0x40:
                symbol_flags = immediate
                symbol, cursor = c_string(
                    raw, cursor, len(raw), f"{kind} bind symbol"
                )
            elif opcode == 0x50:
                bind_type = immediate
            elif opcode == 0x60:
                addend, cursor = read_sleb(raw, cursor, len(raw), f"{kind} bind")
            elif opcode == 0x70:
                segment_index = immediate
                segment_offset, cursor = read_uleb(
                    raw, cursor, len(raw), f"{kind} bind"
                )
            elif opcode == 0x80:
                value, cursor = read_uleb(raw, cursor, len(raw), f"{kind} bind")
                segment_offset = (segment_offset + value) & address_mask
            elif opcode == 0x90:
                emit()
            elif opcode == 0xA0:
                emit()
                value, cursor = read_uleb(raw, cursor, len(raw), f"{kind} bind")
                segment_offset = (segment_offset + value) & address_mask
            elif opcode == 0xB0:
                emit()
                segment_offset = (
                    segment_offset + immediate * pointer_size
                ) & address_mask
            elif opcode == 0xC0:
                count, cursor = read_uleb(raw, cursor, len(raw), f"{kind} bind")
                skip, cursor = read_uleb(raw, cursor, len(raw), f"{kind} bind")
                for _ in range(count):
                    emit()
                    segment_offset = (segment_offset + skip) & address_mask
            elif opcode == 0xD0:
                if immediate == 0:
                    _, cursor = read_uleb(raw, cursor, len(raw), f"{kind} bind")
                    self.unparsed_notes.append(
                        f"{kind} bind threaded ordinal table is counted but not expanded"
                    )
                else:
                    self.unparsed_notes.append(
                        f"{kind} bind threaded apply is counted but not expanded"
                    )
            else:
                raise AuditError(f"unsupported bind opcode 0x{opcode:02x}")

        by_dylib_data: dict[str, list[dict[str, Any]]] = defaultdict(list)
        by_symbol_data: dict[str, list[dict[str, Any]]] = defaultdict(list)
        for event in events:
            by_dylib_data[event["dylib"]].append(event)
            by_symbol_data[event["symbol"]].append(event)
        self.bind_events.extend(events)
        by_dylib = []
        for dylib, grouped in sorted(by_dylib_data.items()):
            symbols = Counter(event["symbol"] for event in grouped)
            by_dylib.append(
                {
                    "dylib": dylib,
                    "count": len(grouped),
                    "symbol_count": len(symbols),
                    "symbols": [
                        {"name": name, "count": count}
                        for name, count in sorted(symbols.items())
                    ],
                }
            )
        by_symbol = []
        for name, grouped in sorted(by_symbol_data.items()):
            dylib_counts = Counter(event["dylib"] for event in grouped)
            by_symbol.append(
                {
                    "symbol": name,
                    "count": len(grouped),
                    "dylibs": [
                        {"name": dylib, "count": count}
                        for dylib, count in sorted(dylib_counts.items())
                    ],
                }
            )
        return {
            "opcode_count": sum(opcodes.values()),
            "opcodes": dict(sorted(opcodes.items())),
            "bind_count": len(events),
            "by_dylib": by_dylib,
            "by_symbol": by_symbol,
        }

    def _section_data(self, section: dict[str, Any], label: str) -> bytes:
        if section["type"] in {"S_ZEROFILL", "S_GB_ZEROFILL", "S_THREAD_LOCAL_ZEROFILL"}:
            return b""
        return checked_slice(
            self.data, section["offset"], section["size"], label
        )

    def _parse_mod_init(self) -> dict[str, Any]:
        matching = [
            section
            for section in self.sections
            if section["name"] == "__mod_init_func"
            or section["type"] == "S_MOD_INIT_FUNC_POINTERS"
        ]
        entries = []
        for section in matching:
            raw = self._section_data(section, "__mod_init_func")
            if len(raw) % 8:
                raise AuditError("__mod_init_func size is not pointer aligned")
            pointers = [
                struct.unpack_from("<Q", raw, offset)[0]
                for offset in range(0, len(raw), 8)
            ]
            entries.append(
                {
                    "section": section_identity(section),
                    "count": len(pointers),
                    "pointers": pointers,
                }
            )
        return {
            "section_count": len(entries),
            "count": sum(entry["count"] for entry in entries),
            "sections": entries,
        }

    def _parse_objc(self) -> dict[str, Any]:
        objc_sections = [s for s in self.sections if s["name"].startswith("__objc_")]
        if objc_sections:
            self.unparsed_notes.append(
                "Objective-C metadata reports section-level counts; runtime object "
                "graphs are not traversed"
            )
        pointer_sections = {
            "__objc_classlist": 8,
            "__objc_nlclslist": 8,
            "__objc_catlist": 8,
            "__objc_nlcatlist": 8,
            "__objc_protolist": 8,
            "__objc_selrefs": 8,
            "__objc_classrefs": 8,
            "__objc_superrefs": 8,
            "__objc_msgrefs": 16,
        }
        entries = []
        metadata_counts: dict[str, int] = {}
        for section in objc_sections:
            entry: dict[str, Any] = {
                "section": section_identity(section),
                "size": section["size"],
            }
            width = pointer_sections.get(section["name"])
            if width:
                count = section["size"] // width
                entry["entry_size"] = width
                entry["count"] = count
                entry["trailing_bytes"] = section["size"] % width
                metadata_counts[section["name"]] = (
                    metadata_counts.get(section["name"], 0) + count
                )
            if section["name"] == "__objc_imageinfo" and section["size"] >= 8:
                raw = self._section_data(section, "__objc_imageinfo")
                version, flags = struct.unpack_from("<II", raw)
                entry["image_info"] = {"version": version, "flags": flags}
            entries.append(entry)
        return {
            "section_count": len(entries),
            "total_size": sum(entry["size"] for entry in entries),
            "metadata_counts": dict(sorted(metadata_counts.items())),
            "sections": entries,
        }

    def _parse_unwind(self) -> dict[str, Any]:
        compact_sections = [
            section for section in self.sections if section["name"] == "__compact_unwind"
        ]
        info_sections = [
            section for section in self.sections if section["name"] == "__unwind_info"
        ]
        if info_sections:
            self.unparsed_notes.append(
                "__unwind_info reports header and personality data; second-level "
                "pages and LSDA are not decoded"
            )
        result: dict[str, Any] = {
            "compact_unwind_sections": [],
            "unwind_info_sections": [],
        }
        for section in compact_sections:
            raw = self._section_data(section, "__compact_unwind")
            entry_size = 32
            count = len(raw) // entry_size
            encodings: Counter[str] = Counter()
            personalities: Counter[int] = Counter()
            for index in range(count):
                _, _, encoding, personality, _ = struct.unpack_from(
                    "<QIIQQ", raw, index * entry_size
                )
                encodings[f"0x{encoding:08x}"] += 1
                if personality:
                    personalities[personality] += 1
            result["compact_unwind_sections"].append(
                {
                    "section": section_identity(section),
                    "entry_size": entry_size,
                    "count": count,
                    "trailing_bytes": len(raw) % entry_size,
                    "encodings": dict(sorted(encodings.items())),
                    "personalities": [
                        {"value": value, "count": count}
                        for value, count in sorted(personalities.items())
                    ],
                }
            )
        text = next((s for s in self.segments if s["name"] == "__TEXT"), None)
        for section in info_sections:
            raw = self._section_data(section, "__unwind_info")
            if len(raw) < 28:
                self.unparsed_notes.append("__unwind_info is shorter than its header")
                continue
            (
                version,
                common_offset,
                common_count,
                personality_offset,
                personality_count,
                index_offset,
                index_count,
            ) = struct.unpack_from("<7I", raw)
            checked_slice(raw, common_offset, common_count * 4, "common encodings")
            personality_raw = checked_slice(
                raw, personality_offset, personality_count * 4, "personality array"
            )
            checked_slice(raw, index_offset, index_count * 12, "unwind index")
            personalities = list(
                struct.unpack(f"<{personality_count}I", personality_raw)
                if personality_count
                else ()
            )
            personality_entries = []
            for value in personalities:
                address = text["vmaddr"] + value if text else None
                bind = next(
                    (
                        event
                        for event in self.bind_events
                        if event["address"] == address
                    ),
                    None,
                )
                personality: dict[str, Any] = {
                    "offset": value,
                    "address": address,
                }
                if bind is not None:
                    personality["symbol"] = bind["symbol"]
                    personality["dylib"] = bind["dylib"]
                personality_entries.append(personality)
            result["unwind_info_sections"].append(
                {
                    "section": section_identity(section),
                    "version": version,
                    "common_encoding_count": common_count,
                    "personality_count": personality_count,
                    "personalities": personality_entries,
                    "index_count": index_count,
                }
            )
        result["compact_unwind_entry_count"] = sum(
            entry["count"] for entry in result["compact_unwind_sections"]
        )
        result["personality_count"] = sum(
            entry["personality_count"] for entry in result["unwind_info_sections"]
        )
        return result


def audit_file(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    return MachOAudit(data, path.name).parse()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description=(
            "Read-only audit of a 64-bit little-endian arm64 Mach-O. "
            "The tool never maps or executes guest code."
        )
    )
    parser.add_argument("binary", type=Path, help="Mach-O executable to inspect")
    parser.add_argument(
        "--audit-only",
        action="store_true",
        help="explicitly request read-only auditing (also the unconditional default)",
    )
    parser.add_argument("-o", "--output", type=Path, help="write JSON to this file")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        result = audit_file(args.binary)
        rendered = json.dumps(
            result, ensure_ascii=False, indent=2, sort_keys=True
        ) + "\n"
        if args.output:
            args.output.write_text(rendered, encoding="utf-8")
        else:
            sys.stdout.write(rendered)
        return 0
    except (AuditError, OSError) as error:
        print(f"sword3_audit: error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
