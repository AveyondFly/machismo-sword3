#!/usr/bin/env python3
"""Deterministic static-recovery report for the pinned Sword3 arm64 Mach-O."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import sys
from collections import deque
from pathlib import Path
from typing import Any, Iterator

try:
    import capstone
    from capstone import CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN, Cs
    from capstone.arm64_const import ARM64_OP_IMM, ARM64_OP_MEM, ARM64_OP_REG
except ImportError:
    capstone = None


MH_MAGIC_64 = 0xFEEDFACF
CPU_TYPE_ARM64 = 0x0100000C
LC_SEGMENT_64 = 0x19
LC_SYMTAB = 0x2
LC_DYSYMTAB = 0xB
LC_UUID = 0x1B
LC_FUNCTION_STARTS = 0x26
LC_DYLD_INFO_ONLY = 0x80000022
LC_MAIN = 0x80000028

S_SYMBOL_STUBS = 0x8
S_CSTRING_LITERALS = 0x2
INDIRECT_SYMBOL_LOCAL = 0x80000000
INDIRECT_SYMBOL_ABS = 0x40000000

PINNED_ENTRY = 0x1000F17CC
PINNED_CALLBACK = 0x10002C3B0
PINNED_FUNCTION_COUNT = 6293
UIAPPLICATION_MAIN = "_UIApplicationMain"

BRANCH_MNEMONICS = {
    "b",
    "bl",
    "cbz",
    "cbnz",
    "tbz",
    "tbnz",
    "b.eq",
    "b.ne",
    "b.hs",
    "b.lo",
    "b.mi",
    "b.pl",
    "b.vs",
    "b.vc",
    "b.hi",
    "b.ls",
    "b.ge",
    "b.lt",
    "b.gt",
    "b.le",
}
CONDITIONAL_BRANCHES = BRANCH_MNEMONICS - {"b", "bl"}
TERMINATORS = {"b", "br", "ret", "eret", "brk"}


class RecoveryError(ValueError):
    """A clear error for malformed input or a failed pinned-binary invariant."""


def checked(data: bytes, offset: int, size: int, label: str) -> bytes:
    if offset < 0 or size < 0 or offset + size > len(data):
        raise RecoveryError(
            f"{label} is outside file bounds: offset={offset}, size={size}"
        )
    return data[offset : offset + size]


def unpack(fmt: str, data: bytes, offset: int, label: str) -> tuple[Any, ...]:
    checked(data, offset, struct.calcsize(fmt), label)
    return struct.unpack_from(fmt, data, offset)


def fixed_string(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("utf-8", "replace")


def read_uleb(raw: bytes, cursor: int, label: str) -> tuple[int, int]:
    value = 0
    shift = 0
    start = cursor
    while cursor < len(raw):
        byte = raw[cursor]
        cursor += 1
        value |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return value, cursor
        shift += 7
        if shift >= 64:
            raise RecoveryError(f"{label} ULEB128 at {start} exceeds 64 bits")
    raise RecoveryError(f"{label} has truncated ULEB128 at {start}")


def read_sleb(raw: bytes, cursor: int, label: str) -> tuple[int, int]:
    value = 0
    shift = 0
    start = cursor
    while cursor < len(raw):
        byte = raw[cursor]
        cursor += 1
        value |= (byte & 0x7F) << shift
        shift += 7
        if not byte & 0x80:
            if shift < 64 and byte & 0x40:
                value |= -(1 << shift)
            return value, cursor
        if shift >= 64:
            raise RecoveryError(f"{label} SLEB128 at {start} exceeds 64 bits")
    raise RecoveryError(f"{label} has truncated SLEB128 at {start}")


def hx(value: int | None) -> str | None:
    return None if value is None else f"0x{value:x}"


class MachO:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.segments: list[dict[str, Any]] = []
        self.sections: list[dict[str, Any]] = []
        self.commands: list[tuple[int, int, int]] = []
        self.main: dict[str, int] | None = None
        self.function_starts_command: tuple[int, int] | None = None
        self.symtab_command: tuple[int, int, int, int] | None = None
        self.dysymtab: dict[str, int] | None = None
        self.dyld_info: dict[str, int] | None = None
        self.uuid: str | None = None
        self.symbols: list[dict[str, Any]] = []
        self.bind_events: list[dict[str, Any]] = []
        self.stubs: dict[int, dict[str, Any]] = {}
        self.strings: list[dict[str, Any]] = []
        self.function_starts: list[int] = []
        self._parse()

    def _parse(self) -> None:
        header = unpack("<IiiIIIII", self.data, 0, "mach_header_64")
        magic, cputype, _, filetype, ncmds, sizeofcmds, _, _ = header
        if magic != MH_MAGIC_64:
            raise RecoveryError(
                "only 64-bit little-endian Mach-O (MH_MAGIC_64) is supported"
            )
        if cputype != CPU_TYPE_ARM64:
            raise RecoveryError("only arm64 Mach-O input is supported")
        if filetype != 2:
            raise RecoveryError("input must be an MH_EXECUTE Mach-O")
        checked(self.data, 32, sizeofcmds, "load commands")

        offset = 32
        for index in range(ncmds):
            cmd, cmdsize = unpack("<II", self.data, offset, f"load command {index}")
            if cmdsize < 8 or offset + cmdsize > 32 + sizeofcmds:
                raise RecoveryError(f"invalid load command {index} size {cmdsize}")
            self.commands.append((cmd, offset, cmdsize))
            self._parse_command(cmd, offset, cmdsize)
            offset += cmdsize
        if offset != 32 + sizeofcmds:
            raise RecoveryError("load command sizes do not equal header sizeofcmds")

        self._parse_symbols()
        self._parse_function_starts()
        self._parse_bind_streams()
        self._parse_stubs()
        self._parse_strings()

    def _parse_command(self, cmd: int, offset: int, cmdsize: int) -> None:
        if cmd == LC_SEGMENT_64:
            values = unpack(
                "<II16sQQQQiiII", self.data, offset, "LC_SEGMENT_64"
            )
            _, _, raw_name, vmaddr, vmsize, fileoff, filesize, _, _, nsects, _ = (
                values
            )
            if 72 + 80 * nsects > cmdsize:
                raise RecoveryError("LC_SEGMENT_64 sections exceed command size")
            segment = {
                "index": len(self.segments),
                "name": fixed_string(raw_name),
                "vmaddr": vmaddr,
                "vmsize": vmsize,
                "fileoff": fileoff,
                "filesize": filesize,
            }
            self.segments.append(segment)
            section_offset = offset + 72
            for _ in range(nsects):
                section_values = unpack(
                    "<16s16sQQIIIIIIII",
                    self.data,
                    section_offset,
                    "section_64",
                )
                (
                    raw_section,
                    raw_segment,
                    address,
                    size,
                    file_offset,
                    _,
                    _,
                    _,
                    flags,
                    reserved1,
                    reserved2,
                    _,
                ) = section_values
                self.sections.append(
                    {
                        "name": fixed_string(raw_section),
                        "segment": fixed_string(raw_segment),
                        "address": address,
                        "size": size,
                        "offset": file_offset,
                        "type": flags & 0xFF,
                        "reserved1": reserved1,
                        "reserved2": reserved2,
                    }
                )
                section_offset += 80
        elif cmd == LC_MAIN:
            if cmdsize < 24:
                raise RecoveryError("LC_MAIN is too small")
            _, _, entryoff, stacksize = unpack("<IIQQ", self.data, offset, "LC_MAIN")
            self.main = {"entryoff": entryoff, "stacksize": stacksize}
        elif cmd == LC_FUNCTION_STARTS:
            if cmdsize < 16:
                raise RecoveryError("LC_FUNCTION_STARTS is too small")
            _, _, dataoff, datasize = unpack(
                "<IIII", self.data, offset, "LC_FUNCTION_STARTS"
            )
            self.function_starts_command = (dataoff, datasize)
        elif cmd == LC_SYMTAB:
            if cmdsize < 24:
                raise RecoveryError("LC_SYMTAB is too small")
            _, _, symoff, nsyms, stroff, strsize = unpack(
                "<IIIIII", self.data, offset, "LC_SYMTAB"
            )
            self.symtab_command = (symoff, nsyms, stroff, strsize)
        elif cmd == LC_DYSYMTAB:
            if cmdsize < 80:
                raise RecoveryError("LC_DYSYMTAB is too small")
            values = unpack("<20I", self.data, offset, "LC_DYSYMTAB")
            self.dysymtab = {
                "indirectsymoff": values[14],
                "nindirectsyms": values[15],
            }
        elif cmd == LC_DYLD_INFO_ONLY:
            if cmdsize < 48:
                raise RecoveryError("LC_DYLD_INFO_ONLY is too small")
            values = unpack("<12I", self.data, offset, "LC_DYLD_INFO_ONLY")
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
        elif cmd == LC_UUID:
            if cmdsize < 24:
                raise RecoveryError("LC_UUID is too small")
            text = checked(self.data, offset + 8, 16, "LC_UUID").hex()
            self.uuid = (
                f"{text[:8]}-{text[8:12]}-{text[12:16]}-"
                f"{text[16:20]}-{text[20:]}"
            ).upper()

    @property
    def text_segment(self) -> dict[str, Any]:
        try:
            return next(segment for segment in self.segments if segment["name"] == "__TEXT")
        except StopIteration as error:
            raise RecoveryError("Mach-O has no __TEXT segment") from error

    @property
    def text_section(self) -> dict[str, Any]:
        try:
            return next(
                section
                for section in self.sections
                if section["segment"] == "__TEXT" and section["name"] == "__text"
            )
        except StopIteration as error:
            raise RecoveryError("Mach-O has no __TEXT,__text section") from error

    def address_to_offset(self, address: int, size: int = 1) -> int:
        for segment in self.segments:
            start = segment["vmaddr"]
            file_end = start + segment["filesize"]
            if start <= address and address + size <= file_end:
                return segment["fileoff"] + address - start
        raise RecoveryError(f"address {hx(address)} is not file-backed")

    def bytes_at(self, address: int, size: int) -> bytes:
        return checked(
            self.data,
            self.address_to_offset(address, size),
            size,
            f"bytes at {hx(address)}",
        )

    def pointer_at(self, address: int) -> int | None:
        try:
            return struct.unpack("<Q", self.bytes_at(address, 8))[0]
        except RecoveryError:
            return None

    def _parse_symbols(self) -> None:
        if self.symtab_command is None:
            raise RecoveryError("Mach-O has no LC_SYMTAB")
        symoff, nsyms, stroff, strsize = self.symtab_command
        checked(self.data, symoff, nsyms * 16, "nlist_64 array")
        strings = checked(self.data, stroff, strsize, "symbol strings")
        for index in range(nsyms):
            strx, n_type, n_sect, n_desc, n_value = unpack(
                "<IBBHQ", self.data, symoff + index * 16, f"symbol {index}"
            )
            if strx >= len(strings):
                raise RecoveryError(f"symbol {index} string index is invalid")
            end = strings.find(b"\0", strx)
            if end < 0:
                raise RecoveryError(f"symbol {index} is not NUL terminated")
            self.symbols.append(
                {
                    "index": index,
                    "name": strings[strx:end].decode("utf-8", "replace"),
                    "type": n_type,
                    "section": n_sect,
                    "description": n_desc,
                    "value": n_value,
                }
            )

    def _parse_function_starts(self) -> None:
        if self.function_starts_command is None:
            raise RecoveryError("Mach-O has no LC_FUNCTION_STARTS")
        dataoff, datasize = self.function_starts_command
        raw = checked(self.data, dataoff, datasize, "LC_FUNCTION_STARTS data")
        cursor = 0
        relative = 0
        terminated = False
        while cursor < len(raw):
            delta, cursor = read_uleb(raw, cursor, "LC_FUNCTION_STARTS")
            if delta == 0:
                terminated = True
                break
            relative += delta
            self.function_starts.append(self.text_segment["vmaddr"] + relative)
        if not terminated:
            raise RecoveryError("LC_FUNCTION_STARTS is not terminated")
        if self.function_starts != sorted(set(self.function_starts)):
            raise RecoveryError("LC_FUNCTION_STARTS is not strictly increasing")

    def _parse_bind_streams(self) -> None:
        if self.dyld_info is None:
            raise RecoveryError("Mach-O has no LC_DYLD_INFO_ONLY")
        for kind in ("bind", "weak_bind", "lazy_bind"):
            offset = self.dyld_info[f"{kind}_off"]
            size = self.dyld_info[f"{kind}_size"]
            self.bind_events.extend(self._decode_bind(offset, size, kind))

    def _decode_bind(
        self, offset: int, size: int, kind: str
    ) -> list[dict[str, Any]]:
        raw = checked(self.data, offset, size, f"{kind} opcodes")
        cursor = 0
        segment_index = 0
        segment_offset = 0
        symbol = ""
        ordinal = -3 if kind == "weak_bind" else 0
        addend = 0
        address_mask = (1 << 64) - 1
        events: list[dict[str, Any]] = []

        def emit() -> None:
            nonlocal segment_offset
            if segment_index >= len(self.segments):
                raise RecoveryError(f"{kind} uses invalid segment {segment_index}")
            segment = self.segments[segment_index]
            if segment_offset > segment["vmsize"]:
                raise RecoveryError(
                    f"{kind} offset {segment_offset} exceeds "
                    f"segment {segment['name']} size {segment['vmsize']}"
                )
            events.append(
                {
                    "kind": kind,
                    "address": segment["vmaddr"] + segment_offset,
                    "symbol": symbol,
                    "dylib_ordinal": ordinal,
                    "addend": addend,
                }
            )
            segment_offset = (segment_offset + 8) & address_mask

        while cursor < len(raw):
            byte = raw[cursor]
            cursor += 1
            opcode, immediate = byte & 0xF0, byte & 0x0F
            if opcode == 0x00:
                continue
            if opcode == 0x10:
                ordinal = immediate
            elif opcode == 0x20:
                ordinal, cursor = read_uleb(raw, cursor, kind)
            elif opcode == 0x30:
                ordinal = immediate | (-16 if immediate & 8 else 0)
            elif opcode == 0x40:
                end = raw.find(b"\0", cursor)
                if end < 0:
                    raise RecoveryError(f"{kind} symbol is not NUL terminated")
                symbol = raw[cursor:end].decode("utf-8", "replace")
                cursor = end + 1
            elif opcode == 0x50:
                pass
            elif opcode == 0x60:
                addend, cursor = read_sleb(raw, cursor, kind)
            elif opcode == 0x70:
                segment_index = immediate
                segment_offset, cursor = read_uleb(raw, cursor, kind)
            elif opcode == 0x80:
                value, cursor = read_uleb(raw, cursor, kind)
                segment_offset = (segment_offset + value) & address_mask
            elif opcode == 0x90:
                emit()
            elif opcode == 0xA0:
                emit()
                value, cursor = read_uleb(raw, cursor, kind)
                segment_offset = (segment_offset + value) & address_mask
            elif opcode == 0xB0:
                emit()
                segment_offset = (segment_offset + immediate * 8) & address_mask
            elif opcode == 0xC0:
                count, cursor = read_uleb(raw, cursor, kind)
                skip, cursor = read_uleb(raw, cursor, kind)
                for _ in range(count):
                    emit()
                    segment_offset = (segment_offset + skip) & address_mask
            elif opcode == 0xD0:
                if immediate == 0:
                    _, cursor = read_uleb(raw, cursor, kind)
                else:
                    raise RecoveryError(f"unsupported threaded {kind} bind apply opcode")
            else:
                raise RecoveryError(f"unsupported {kind} bind opcode {opcode:#x}")
        return events

    def _parse_stubs(self) -> None:
        if self.dysymtab is None:
            raise RecoveryError("Mach-O has no LC_DYSYMTAB")
        indirect_offset = self.dysymtab["indirectsymoff"]
        indirect_count = self.dysymtab["nindirectsyms"]
        checked(self.data, indirect_offset, indirect_count * 4, "indirect symbols")
        indirect = struct.unpack_from(f"<{indirect_count}I", self.data, indirect_offset)
        for section in self.sections:
            if section["type"] != S_SYMBOL_STUBS:
                continue
            width = section["reserved2"]
            if not width or section["size"] % width:
                raise RecoveryError("symbol stubs have invalid width")
            for local_index in range(section["size"] // width):
                indirect_index = section["reserved1"] + local_index
                if indirect_index >= len(indirect):
                    raise RecoveryError("stub indirect symbol index is invalid")
                symbol_index = indirect[indirect_index]
                if symbol_index & (INDIRECT_SYMBOL_LOCAL | INDIRECT_SYMBOL_ABS):
                    continue
                if symbol_index >= len(self.symbols):
                    raise RecoveryError("stub symbol index is invalid")
                address = section["address"] + local_index * width
                self.stubs[address] = {
                    "symbol": self.symbols[symbol_index]["name"],
                    "indirect_index": indirect_index,
                    "symbol_index": symbol_index,
                    "width": width,
                }

    def _parse_strings(self) -> None:
        for section in self.sections:
            if section["type"] != S_CSTRING_LITERALS:
                continue
            raw = checked(
                self.data, section["offset"], section["size"], "C string section"
            )
            cursor = 0
            while cursor < len(raw):
                end = raw.find(b"\0", cursor)
                if end < 0:
                    end = len(raw)
                if end > cursor:
                    self.strings.append(
                        {
                            "address": section["address"] + cursor,
                            "value": raw[cursor:end].decode("utf-8", "replace"),
                            "section": f"{section['segment']},{section['name']}",
                        }
                    )
                cursor = end + 1

    def function_bounds(self, start: int) -> tuple[int, int]:
        try:
            index = self.function_starts.index(start)
        except ValueError as error:
            raise RecoveryError(f"{hx(start)} is not an LC_FUNCTION_STARTS entry") from error
        text_end = self.text_section["address"] + self.text_section["size"]
        end = (
            self.function_starts[index + 1]
            if index + 1 < len(self.function_starts)
            else text_end
        )
        return start, min(end, text_end)

    def containing_function(self, address: int) -> int | None:
        lo, hi = 0, len(self.function_starts)
        while lo < hi:
            mid = (lo + hi) // 2
            if self.function_starts[mid] <= address:
                lo = mid + 1
            else:
                hi = mid
        if not lo:
            return None
        start = self.function_starts[lo - 1]
        _, end = self.function_bounds(start)
        return start if address < end else None


class Analyzer:
    def __init__(self, macho: MachO) -> None:
        if capstone is None:
            raise RecoveryError(
                "Python package 'capstone' is required; install it with "
                "'python3 -m pip install capstone'"
            )
        self.macho = macho
        self.disassembler = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
        self.disassembler.detail = True
        self.string_by_address = {
            entry["address"]: entry for entry in self.macho.strings
        }

    def instructions(self, start: int, end: int) -> list[Any]:
        raw = self.macho.bytes_at(start, end - start)
        instructions = list(self.disassembler.disasm(raw, start))
        if sum(instruction.size for instruction in instructions) != len(raw):
            raise RecoveryError(f"Capstone did not decode all bytes in {hx(start)}")
        return instructions

    @staticmethod
    def immediate_target(instruction: Any) -> int | None:
        for operand in reversed(instruction.operands):
            if operand.type == ARM64_OP_IMM:
                return operand.imm & ((1 << 64) - 1)
        return None

    def imported_target(self, address: int) -> str | None:
        stub = self.macho.stubs.get(address)
        return stub["symbol"] if stub else None

    def _string_for_address(self, address: int) -> dict[str, Any] | None:
        return self.string_by_address.get(address)

    def string_references(self, instructions: list[Any]) -> list[dict[str, str]]:
        registers: dict[int, int] = {}
        references: dict[tuple[int, int], dict[str, str]] = {}

        def assign(register: int, value: int, site: int) -> None:
            registers[register] = value
            string = self._string_for_address(value)
            if string is not None:
                references[(site, value)] = {
                    "instruction": hx(site),
                    "address": hx(value),
                    "string": string["value"],
                    "section": string["section"],
                }

        for instruction in instructions:
            operands = instruction.operands
            destination = (
                operands[0].reg
                if operands and operands[0].type == ARM64_OP_REG
                else None
            )
            source_register = (
                operands[1].reg
                if len(operands) >= 2 and operands[1].type == ARM64_OP_REG
                else None
            )
            source_value = registers.get(source_register)
            if destination is not None:
                registers.pop(destination, None)
            if (
                instruction.mnemonic in {"adr", "adrp"}
                and len(operands) >= 2
                and operands[1].type == ARM64_OP_IMM
            ):
                assign(destination, operands[1].imm, instruction.address)
            elif (
                instruction.mnemonic == "add"
                and len(operands) >= 3
                and operands[1].type == ARM64_OP_REG
                and operands[2].type == ARM64_OP_IMM
                and source_value is not None
            ):
                shift = getattr(operands[2].shift, "value", 0)
                assign(
                    destination,
                    source_value + (operands[2].imm << shift),
                    instruction.address,
                )
            elif (
                instruction.mnemonic == "mov"
                and len(operands) >= 2
                and operands[1].type == ARM64_OP_REG
                and source_value is not None
            ):
                assign(destination, source_value, instruction.address)
            elif (
                instruction.mnemonic.startswith("ldr")
                and len(operands) >= 2
            ):
                source = operands[1]
                location: int | None = None
                if source.type == ARM64_OP_IMM:
                    location = source.imm
                elif source.type == ARM64_OP_MEM and source.mem.base in registers:
                    location = registers[source.mem.base] + source.mem.disp
                if location is not None:
                    pointer = self.macho.pointer_at(location)
                    if pointer is not None:
                        assign(destination, pointer, instruction.address)
        return [references[key] for key in sorted(references)]

    def normalized_fingerprint(self, instructions: list[Any]) -> dict[str, Any]:
        normalized: list[str] = []
        pc_relative_registers: set[str] = set()
        for instruction in instructions:
            mnemonic = instruction.mnemonic
            operands = instruction.op_str
            pieces = [piece.strip() for piece in operands.split(",")] if operands else []
            if mnemonic in {"adr", "adrp"} and len(pieces) >= 2:
                pieces[1] = "<pcrel>"
                pc_relative_registers.add(pieces[0])
            elif mnemonic == "add" and len(pieces) >= 3:
                if pieces[1] in pc_relative_registers and pieces[2].startswith("#"):
                    pieces[2] = "<pcrel-lo>"
                    pc_relative_registers.add(pieces[0])
                else:
                    pc_relative_registers.discard(pieces[0])
            elif mnemonic in BRANCH_MNEMONICS and pieces:
                pieces[-1] = "<branch>"
            elif (
                mnemonic.startswith("ldr")
                and len(pieces) >= 2
                and pieces[1].startswith("#")
            ):
                pieces[1] = "<pcrel>"
            elif pieces:
                pc_relative_registers.discard(pieces[0])
            normalized.append(
                mnemonic + ((" " + ", ".join(pieces)) if pieces else "")
            )
        payload = "\n".join(normalized).encode("utf-8")
        return {
            "algorithm": "sha256(normalized-arm64-v1)",
            "sha256": hashlib.sha256(payload).hexdigest(),
            "instruction_count": len(normalized),
            "normalized_instructions": normalized,
        }

    def control_flow_and_calls(
        self, start: int, end: int, instructions: list[Any]
    ) -> tuple[dict[str, int], list[dict[str, str]], list[int]]:
        block_starts = {start}
        cfg_edges: set[tuple[int, int]] = set()
        calls: list[dict[str, str]] = []
        neighboring_functions: set[int] = set()
        addresses = {instruction.address for instruction in instructions}

        for index, instruction in enumerate(instructions):
            mnemonic = instruction.mnemonic
            next_address = (
                instructions[index + 1].address
                if index + 1 < len(instructions)
                else end
            )
            target = (
                self.immediate_target(instruction)
                if mnemonic in BRANCH_MNEMONICS
                else None
            )
            if mnemonic == "bl" and target is not None:
                call: dict[str, str] = {
                    "site": hx(instruction.address),
                    "target": hx(target),
                    "kind": "direct",
                }
                imported = self.imported_target(target)
                if imported:
                    call["symbol"] = imported
                    call["kind"] = "import-stub"
                else:
                    function = self.macho.containing_function(target)
                    if function == target:
                        neighboring_functions.add(function)
                calls.append(call)
            elif mnemonic == "blr":
                calls.append(
                    {
                        "site": hx(instruction.address),
                        "target": "register",
                        "kind": "indirect",
                    }
                )

            if target is not None and mnemonic != "bl":
                if target in addresses:
                    block_starts.add(target)
                    cfg_edges.add((instruction.address, target))
                else:
                    function = self.macho.containing_function(target)
                    if function == target:
                        neighboring_functions.add(function)
                if mnemonic in CONDITIONAL_BRANCHES and next_address < end:
                    block_starts.add(next_address)
                    cfg_edges.add((instruction.address, next_address))
            elif mnemonic in CONDITIONAL_BRANCHES and next_address < end:
                block_starts.add(next_address)
            if mnemonic in TERMINATORS and next_address < end:
                block_starts.add(next_address)

        return (
            {
                "basic_block_count": len(block_starts),
                "edge_count": len(cfg_edges),
                "direct_call_count": sum(call["kind"] != "indirect" for call in calls),
                "indirect_call_count": sum(call["kind"] == "indirect" for call in calls),
            },
            calls,
            sorted(neighboring_functions),
        )

    def summarize_function(self, start: int) -> dict[str, Any]:
        start, end = self.macho.function_bounds(start)
        instructions = self.instructions(start, end)
        cfg, calls, _ = self.control_flow_and_calls(start, end, instructions)
        return {
            "address": hx(start),
            "end_address": hx(end),
            "size": end - start,
            "expected_hex": self.macho.bytes_at(start, min(16, end - start)).hex(),
            "fingerprint": self.normalized_fingerprint(instructions),
            "cfg_call_summary": cfg,
            "call_targets": calls,
            "string_references": self.string_references(instructions),
        }

    def find_uiapplicationmain_path(self, entry: int) -> dict[str, Any]:
        queue: deque[tuple[int, list[int]]] = deque([(entry, [entry])])
        visited: set[int] = set()
        searched = 0
        while queue and searched < 512:
            start, path = queue.popleft()
            if start in visited or len(path) > 12:
                continue
            visited.add(start)
            searched += 1
            begin, end = self.macho.function_bounds(start)
            instructions = self.instructions(begin, end)
            _, calls, neighbors = self.control_flow_and_calls(
                begin, end, instructions
            )
            for call in calls:
                if call.get("symbol") == UIAPPLICATION_MAIN:
                    stub_address = int(call["target"], 16)
                    return {
                        "reached": True,
                        "symbol": UIAPPLICATION_MAIN,
                        "control_flow_path": [hx(address) for address in path],
                        "call_site": call["site"],
                        "stub_address": hx(stub_address),
                        "searched_function_count": searched,
                    }
            for neighbor in neighbors:
                if neighbor not in visited:
                    queue.append((neighbor, path + [neighbor]))
        return {
            "reached": False,
            "symbol": UIAPPLICATION_MAIN,
            "searched_function_count": searched,
        }

    def stub_evidence(self, stub_address: int, symbol: str) -> dict[str, Any]:
        stub = self.macho.stubs.get(stub_address)
        if stub is None or stub["symbol"] != symbol:
            raise RecoveryError(f"{hx(stub_address)} is not the {symbol} stub")
        instructions = list(
            self.disassembler.disasm(
                self.macho.bytes_at(stub_address, stub["width"]), stub_address
            )
        )
        pointer_address: int | None = None
        page: int | None = None
        for instruction in instructions:
            if instruction.mnemonic == "adrp":
                page = self.immediate_target(instruction)
            elif (
                instruction.mnemonic.startswith("ldr")
                and len(instruction.operands) >= 2
            ):
                source = instruction.operands[1]
                if source.type == ARM64_OP_IMM:
                    pointer_address = source.imm
                elif page is not None and source.type == ARM64_OP_MEM:
                    pointer_address = page + source.mem.disp
        lazy = next(
            (
                event
                for event in self.macho.bind_events
                if event["kind"] == "lazy_bind"
                and event["symbol"] == symbol
                and (pointer_address is None or event["address"] == pointer_address)
            ),
            None,
        )
        if lazy is None:
            raise RecoveryError(f"no lazy bind event proves the {symbol} stub")
        if pointer_address != lazy["address"]:
            raise RecoveryError(
                f"{symbol} stub pointer {hx(pointer_address)} does not match "
                f"lazy bind slot {hx(lazy['address'])}; instructions: "
                + "; ".join(
                    f"{instruction.mnemonic} {instruction.op_str}"
                    for instruction in instructions
                )
            )
        return {
            "indirect_symbol": {
                "symbol": stub["symbol"],
                "symbol_index": stub["symbol_index"],
                "indirect_index": stub["indirect_index"],
            },
            "stub_instructions": [
                {
                    "address": hx(instruction.address),
                    "mnemonic": instruction.mnemonic,
                    "operands": instruction.op_str,
                }
                for instruction in instructions
            ],
            "lazy_pointer_address": hx(pointer_address),
            "lazy_bind": {
                "address": hx(lazy["address"]),
                "symbol": lazy["symbol"],
                "dylib_ordinal": lazy["dylib_ordinal"],
            },
            "evidence": [
                "indirect symbol table maps the stub to _UIApplicationMain",
                "stub literal/ADRP LDR resolves to the recorded lazy-bind pointer",
                "LC_DYLD_INFO_ONLY lazy-bind stream maps that pointer to _UIApplicationMain",
            ],
        }


def load_manifest(path: Path) -> dict[str, Any]:
    try:
        with path.open(encoding="utf-8") as stream:
            manifest = json.load(stream)
    except json.JSONDecodeError as error:
        raise RecoveryError(f"manifest is not valid JSON: {error}") from error
    if not isinstance(manifest, dict):
        raise RecoveryError("manifest root must be an object")
    return manifest


def validate_manifest(
    macho: MachO, binary_data: bytes, manifest: dict[str, Any]
) -> dict[str, Any]:
    binary_hash = hashlib.sha256(binary_data).hexdigest()
    expected_hash = manifest.get("file", {}).get("sha256")
    manifest_starts = [
        entry.get("address")
        for entry in manifest.get("function_starts", {}).get("entries", [])
    ]
    facts = {
        "sha256": binary_hash == expected_hash,
        "uuid": macho.uuid == manifest.get("uuid"),
        "lc_main_entryoff": (
            macho.main is not None
            and macho.main.get("entryoff") == manifest.get("lc_main", {}).get("entryoff")
        ),
        "function_starts": macho.function_starts == manifest_starts,
    }
    if not all(facts.values()):
        failed = ", ".join(name for name, passed in facts.items() if not passed)
        raise RecoveryError(f"binary does not match manifest: {failed}")
    return {
        "matched": True,
        "checks": facts,
        "manifest_sha256": hashlib.sha256(
            json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode("utf-8")
        ).hexdigest(),
    }


def anchored_candidates(
    analyzer: Analyzer, patterns: list[str]
) -> dict[str, Any]:
    if not patterns:
        return {"patterns": [], "matched_strings": [], "candidates": []}
    try:
        compiled = [re.compile(pattern) for pattern in patterns]
    except re.error as error:
        raise RecoveryError(f"invalid --anchor regular expression: {error}") from error
    matching = [
        string
        for string in analyzer.macho.strings
        if any(regex.search(string["value"]) for regex in compiled)
    ]
    matching_addresses = {entry["address"] for entry in matching}
    candidates: list[dict[str, Any]] = []
    for start in analyzer.macho.function_starts:
        begin, end = analyzer.macho.function_bounds(start)
        instructions = analyzer.instructions(begin, end)
        refs = [
            reference
            for reference in analyzer.string_references(instructions)
            if int(reference["address"], 16) in matching_addresses
        ]
        if not refs:
            continue
        summary = analyzer.summarize_function(start)
        summary["confidence"] = "high"
        summary["evidence"] = [
            {
                "type": "anchor-string-xref",
                "instruction": reference["instruction"],
                "string_address": reference["address"],
                "string": reference["string"],
            }
            for reference in refs
        ]
        candidates.append(summary)
    return {
        "patterns": patterns,
        "matched_strings": [
            {"address": hx(entry["address"]), "string": entry["value"]}
            for entry in matching
        ],
        "candidates": candidates,
    }


def build_report(
    binary: Path, manifest_path: Path, anchor_patterns: list[str]
) -> dict[str, Any]:
    data = binary.read_bytes()
    manifest = load_manifest(manifest_path)
    macho = MachO(data)
    analyzer = Analyzer(macho)
    manifest_validation = validate_manifest(macho, data, manifest)

    if macho.main is None:
        raise RecoveryError("Mach-O has no LC_MAIN")
    entry = macho.text_segment["vmaddr"] + macho.main["entryoff"]
    if entry != PINNED_ENTRY:
        raise RecoveryError(
            f"LC_MAIN resolved to {hx(entry)}, expected {hx(PINNED_ENTRY)}"
        )
    if len(macho.function_starts) != PINNED_FUNCTION_COUNT:
        raise RecoveryError(
            f"LC_FUNCTION_STARTS has {len(macho.function_starts)} entries, "
            f"expected {PINNED_FUNCTION_COUNT}"
        )
    if PINNED_CALLBACK not in macho.function_starts:
        raise RecoveryError(
            f"callback candidate {hx(PINNED_CALLBACK)} is not a function start"
        )

    entry_start, entry_end = macho.function_bounds(entry)
    entry_instructions = analyzer.instructions(entry_start, entry_end)
    if not entry_instructions:
        raise RecoveryError("LC_MAIN function is empty")
    first = entry_instructions[0]
    first_target = analyzer.immediate_target(first)
    first_register = (
        first.reg_name(first.operands[0].reg)
        if first.operands and first.operands[0].type == ARM64_OP_REG
        else None
    )
    if (
        first.mnemonic != "adr"
        or first_register != "x2"
        or first_target != PINNED_CALLBACK
    ):
        raise RecoveryError(
            "LC_MAIN first instruction is not the expected "
            f"ADR x2, {hx(PINNED_CALLBACK)}"
        )

    ui_path = analyzer.find_uiapplicationmain_path(entry)
    if not ui_path["reached"]:
        raise RecoveryError(
            "LC_MAIN control flow did not reach the _UIApplicationMain lazy stub"
        )
    ui_path["stub_proof"] = analyzer.stub_evidence(
        int(ui_path["stub_address"], 16), UIAPPLICATION_MAIN
    )

    callback = analyzer.summarize_function(PINNED_CALLBACK)
    callback["role"] = "entry-callback-candidate"
    callback["confidence"] = "high"
    callback["evidence"] = [
        f"LC_MAIN first instruction materializes {hx(PINNED_CALLBACK)} in x2",
        f"{hx(PINNED_CALLBACK)} is an LC_FUNCTION_STARTS entry",
    ]

    return {
        "schema_version": 1,
        "mode": "static-recovery",
        "inputs": {
            "binary": binary.name,
            "binary_size": len(data),
            "binary_sha256": hashlib.sha256(data).hexdigest(),
            "manifest": "configs/sword3/binary-manifest.json",
        },
        "manifest_validation": manifest_validation,
        "macho": {
            "format": "Mach-O 64-bit little-endian",
            "architecture": "arm64",
            "uuid": macho.uuid,
        },
        "function_index": {
            "source": "LC_FUNCTION_STARTS",
            "count": len(macho.function_starts),
            "text_address": hx(macho.text_section["address"]),
            "text_end_address": hx(
                macho.text_section["address"] + macho.text_section["size"]
            ),
        },
        "entry_validation": {
            "passed": True,
            "lc_main": {
                "entryoff": hx(macho.main["entryoff"]),
                "address": hx(entry),
            },
            "first_instruction": {
                "address": hx(first.address),
                "mnemonic": first.mnemonic,
                "operands": first.op_str,
                "destination_register": first_register,
                "resolved_address": hx(first_target),
                "passed": True,
            },
            "uiapplicationmain": ui_path,
        },
        "candidates": [callback],
        "anchor_search": anchored_candidates(analyzer, anchor_patterns),
        "recovery_method": {
            "function_boundaries": "LC_FUNCTION_STARTS ULEB128 deltas",
            "string_anchors": "S_CSTRING_LITERALS addresses plus ARM64 ADR/ADRP xrefs",
            "fingerprint": (
                "ARM64 instruction text with PC-relative immediates and branch "
                "targets masked before SHA-256"
            ),
            "cfg_calls": (
                "direct branch targets, basic-block starts, BL targets, and "
                "unresolved BLR counts"
            ),
            "naming_policy": (
                "No SDL function name is assigned without independent symbol "
                "or semantic evidence; results are candidates with confidence "
                "and explicit evidence."
            ),
        },
    }


def parser() -> argparse.ArgumentParser:
    root = Path(__file__).resolve().parents[1]
    result = argparse.ArgumentParser(
        description=(
            "Build a deterministic static-recovery report from a 64-bit arm64 "
            "Mach-O and its checked binary manifest."
        )
    )
    result.add_argument("binary", type=Path, help="arm64 Mach-O executable")
    result.add_argument(
        "--manifest",
        type=Path,
        default=root / "configs" / "sword3" / "binary-manifest.json",
        help="binary manifest (default: configs/sword3/binary-manifest.json)",
    )
    result.add_argument(
        "--anchor",
        action="append",
        default=[],
        metavar="REGEX",
        help="find C strings matching REGEX and report functions that reference them",
    )
    result.add_argument("-o", "--output", type=Path, help="write JSON report here")
    return result


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    if capstone is None:
        print(
            "recover_sdl: error: Python package 'capstone' is required; install "
            "it with 'python3 -m pip install capstone'",
            file=sys.stderr,
        )
        return 2
    try:
        report = build_report(args.binary, args.manifest, args.anchor)
        rendered = json.dumps(
            report, ensure_ascii=False, indent=2, sort_keys=True
        ) + "\n"
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(rendered, encoding="utf-8")
        else:
            sys.stdout.write(rendered)
        return 0
    except (RecoveryError, OSError) as error:
        print(f"recover_sdl: error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
