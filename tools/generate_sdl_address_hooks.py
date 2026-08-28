#!/usr/bin/env python3
"""Generate fail-closed address hooks from reviewed SDL string and call-graph anchors."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path
from typing import Any

from recover_sdl import Analyzer, MachO, RecoveryError, anchored_candidates, hx


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BINARY = Path("/tmp/sword3-ipa-extract/Payload/SWD3.app/SWD3")
DEFAULT_ANCHORS = ROOT / "configs/sword3/sdl-symbol-anchors.json"
DEFAULT_OUTPUT = ROOT / "configs/sword3/address-hooks.conf"
DEFAULT_REPORT = ROOT / "configs/sword3/sdl-hook-report.json"


def _require_symbol(entry: dict[str, Any]) -> str:
    symbol = entry.get("symbol")
    if not isinstance(symbol, str) or not symbol:
        raise RecoveryError("every SDL anchor requires a symbol")
    return symbol


def _collect_scan_strings(symbols: list[Any]) -> list[str]:
    values: list[str] = []
    for entry in symbols:
        if not isinstance(entry, dict):
            raise RecoveryError("every SDL anchor must be an object")
        for key in ("string",):
            value = entry.get(key)
            if value is not None:
                if not isinstance(value, str) or not value:
                    raise RecoveryError(f"{_require_symbol(entry)} has an empty {key}")
                values.append(value)
        exclude = entry.get("exclude_strings", [])
        if exclude:
            if not isinstance(exclude, list) or any(
                not isinstance(item, str) or not item for item in exclude
            ):
                raise RecoveryError(
                    f"{_require_symbol(entry)} exclude_strings must be non-empty strings"
                )
            values.extend(exclude)
        via = entry.get("via")
        if via:
            parent = via.get("parent_string") if isinstance(via, dict) else None
            if not isinstance(parent, str) or not parent:
                raise RecoveryError(
                    f"{_require_symbol(entry)} via.parent_string is required"
                )
            values.append(parent)
        nth = entry.get("nth_after")
        if nth:
            after_string = nth.get("after_string") if isinstance(nth, dict) else None
            if isinstance(after_string, str) and after_string:
                values.append(after_string)
    if not values:
        raise RecoveryError("anchor file contains no recoverable strings")
    return values


def _unique_function_for_string(
    search: dict[str, Any],
    anchor: str,
    exclude_strings: list[str],
    symbol: str,
) -> dict[str, Any]:
    matches = []
    for candidate in search["candidates"]:
        evidence_strings = {item["string"] for item in candidate["evidence"]}
        if anchor not in evidence_strings:
            continue
        if evidence_strings.intersection(exclude_strings):
            continue
        matches.append(candidate)
    if len(matches) != 1:
        raise RecoveryError(
            f"{symbol}: expected one function for anchor {anchor!r} "
            f"(exclude={exclude_strings!r}), found {len(matches)}"
        )
    return matches[0]


def _summary_at(analyzer: Analyzer, address: int) -> dict[str, Any]:
    start = analyzer.macho.containing_function(address)
    if start != address:
        raise RecoveryError(
            f"{hx(address)} is not an LC_FUNCTION_STARTS entry "
            f"(contained in {hx(start)})"
        )
    return analyzer.summarize_function(address)


def _direct_call_targets(summary: dict[str, Any]) -> list[int]:
    targets: list[int] = []
    for call in summary["call_targets"]:
        if call["kind"] != "direct":
            continue
        targets.append(int(call["target"], 16))
    return targets


def _next_function_starts(analyzer: Analyzer, start: int) -> list[int]:
    starts = analyzer.macho.function_starts
    try:
        index = starts.index(start)
    except ValueError as error:
        raise RecoveryError(f"{hx(start)} is not an LC_FUNCTION_STARTS entry") from error
    return starts[index + 1 :]


def _emit(
    *,
    symbol: str,
    host_symbol: str,
    library: str,
    group: str,
    method: str,
    candidate: dict[str, Any],
    comment: str,
    seen_addresses: set[str],
    resolved: dict[str, dict[str, Any]],
    lines: list[str],
    report_entries: list[dict[str, Any]],
    expected_address: str | None,
) -> None:
    address = candidate["address"]
    if expected_address and address != expected_address:
        raise RecoveryError(
            f"{symbol}: recovered {address}, expected reviewed address "
            f"{expected_address}"
        )
    if address in seen_addresses:
        raise RecoveryError(f"multiple SDL symbols resolve to {address}")
    seen_addresses.add(address)
    resolved[symbol] = candidate
    expected = candidate["expected_hex"]
    comment = " ".join(comment.split())
    lines.append(f"{address} {library} {host_symbol} {expected} # {comment}")
    report_entries.append(
        {
            "symbol": symbol,
            "host_symbol": host_symbol,
            "library": library,
            "group": group,
            "method": method,
            "anchor": comment,
            "address": address,
            "end_address": candidate["end_address"],
            "expected_hex": expected,
            "fingerprint": candidate["fingerprint"]["sha256"],
            "evidence": candidate.get("evidence", []),
        }
    )


def generate(binary_path: Path, anchor_path: Path) -> tuple[str, dict]:
    data = binary_path.read_bytes()
    binary_sha256 = hashlib.sha256(data).hexdigest()
    anchors = json.loads(anchor_path.read_text(encoding="utf-8"))
    if binary_sha256 != anchors.get("binary_sha256"):
        raise RecoveryError(
            f"binary hash {binary_sha256} does not match reviewed anchor set"
        )

    default_library = anchors.get("library")
    symbols = anchors.get("symbols")
    if (
        not isinstance(default_library, str)
        or not default_library
        or not isinstance(symbols, list)
    ):
        raise RecoveryError("anchor file requires library and symbols")

    analyzer = Analyzer(MachO(data))
    scan_strings = _collect_scan_strings(symbols)
    search = anchored_candidates(
        analyzer, [f"^{re.escape(anchor)}$" for anchor in scan_strings]
    )
    string_to_function: dict[str, dict[str, Any]] = {}

    lines = [
        "# Generated by tools/generate_sdl_address_hooks.py; do not edit.",
        f"# Sword3 Mach-O SHA-256: {binary_sha256}",
        "# Coverage is intentionally partial: only reviewed unique anchors are emitted.",
    ]
    report_entries: list[dict[str, Any]] = []
    seen_addresses: set[str] = set()
    resolved: dict[str, dict[str, Any]] = {}

    def host_name(entry: dict[str, Any], symbol: str) -> str:
        value = entry.get("host_symbol")
        if value is None:
            return f"sword3_{symbol}"
        if not isinstance(value, str) or not value:
            raise RecoveryError(f"{symbol} host_symbol must be a non-empty string")
        return value

    def library_name(entry: dict[str, Any]) -> str:
        value = entry.get("library", default_library)
        if not isinstance(value, str) or not value:
            raise RecoveryError("hook library must be a non-empty string")
        return value

    # Pass 1: unique string xrefs. One scan covers every reviewed C string.
    for entry in symbols:
        symbol = _require_symbol(entry)
        anchor = entry.get("string")
        if not isinstance(anchor, str):
            continue
        exclude = list(entry.get("exclude_strings", []))
        candidate = _unique_function_for_string(search, anchor, exclude, symbol)
        string_to_function[anchor] = candidate
        _emit(
            symbol=symbol,
            host_symbol=host_name(entry, symbol),
            library=library_name(entry),
            group=str(entry.get("group", "sdl")),
            method="unique exact cstring xref + LC_FUNCTION_STARTS",
            candidate=candidate,
            comment=anchor,
            seen_addresses=seen_addresses,
            resolved=resolved,
            lines=lines,
            report_entries=report_entries,
            expected_address=entry.get("expected_address"),
        )

    # Pass 2: deterministic BL index from a uniquely identified parent function.
    for entry in symbols:
        via = entry.get("via")
        if not via:
            continue
        symbol = _require_symbol(entry)
        parent_string = via["parent_string"]
        bl_index = via.get("bl_index")
        if not isinstance(bl_index, int):
            raise RecoveryError(f"{symbol} via.bl_index must be an integer")
        parent = string_to_function.get(parent_string)
        if parent is None:
            parent = _unique_function_for_string(search, parent_string, [], symbol)
            string_to_function[parent_string] = parent
        parent_summary = _summary_at(analyzer, int(parent["address"], 16))
        targets = _direct_call_targets(parent_summary)
        if bl_index < 0 or bl_index >= len(targets):
            raise RecoveryError(
                f"{symbol}: parent {parent_string!r} has {len(targets)} direct "
                f"calls, bl_index {bl_index} is out of range"
            )
        candidate = _summary_at(analyzer, targets[bl_index])
        direct_calls = [
            call
            for call in parent_summary["call_targets"]
            if call["kind"] == "direct"
        ]
        candidate["evidence"] = [
            {
                "type": "parent-bl-index",
                "parent_string": parent_string,
                "parent_address": parent["address"],
                "bl_index": bl_index,
                "call_site": direct_calls[bl_index]["site"],
            }
        ]
        _emit(
            symbol=symbol,
            host_symbol=host_name(entry, symbol),
            library=library_name(entry),
            group=str(entry.get("group", "sdl")),
            method="unique parent string + ordered direct BL index",
            candidate=candidate,
            comment=f"via {parent_string} bl[{bl_index}]",
            seen_addresses=seen_addresses,
            resolved=resolved,
            lines=lines,
            report_entries=report_entries,
            expected_address=entry.get("expected_address"),
        )

    # Pass 3: Nth LC_FUNCTION_STARTS entry after an already resolved symbol.
    for entry in symbols:
        nth = entry.get("nth_after")
        if not nth:
            continue
        symbol = _require_symbol(entry)
        after_symbol = nth.get("after_symbol")
        after_string = nth.get("after_string")
        n = nth.get("n")
        if not isinstance(n, int) or n < 0:
            raise RecoveryError(f"{symbol} nth_after.n must be a non-negative integer")
        if after_symbol:
            if after_symbol not in resolved:
                raise RecoveryError(
                    f"{symbol} nth_after.after_symbol {after_symbol} is not resolved"
                )
            after_address = int(resolved[after_symbol]["address"], 16)
            origin = after_symbol
        elif isinstance(after_string, str) and after_string:
            parent = string_to_function.get(after_string)
            if parent is None:
                parent = _unique_function_for_string(
                    search, after_string, [], symbol
                )
                string_to_function[after_string] = parent
            after_address = int(parent["address"], 16)
            origin = after_string
        else:
            raise RecoveryError(
                f"{symbol} nth_after requires after_symbol or after_string"
            )
        following = _next_function_starts(analyzer, after_address)
        if n >= len(following):
            raise RecoveryError(
                f"{symbol}: only {len(following)} functions follow {origin}"
            )
        candidate = _summary_at(analyzer, following[n])
        candidate["evidence"] = [
            {
                "type": "nth-function-after",
                "origin": origin,
                "origin_address": hx(after_address),
                "n": n,
            }
        ]
        _emit(
            symbol=symbol,
            host_symbol=host_name(entry, symbol),
            library=library_name(entry),
            group=str(entry.get("group", "sdl")),
            method="nth LC_FUNCTION_STARTS after resolved origin",
            candidate=candidate,
            comment=f"nth_after {origin} n={n}",
            seen_addresses=seen_addresses,
            resolved=resolved,
            lines=lines,
            report_entries=report_entries,
            expected_address=entry.get("expected_address"),
        )

    if len(report_entries) != len(symbols):
        raise RecoveryError(
            f"resolved {len(report_entries)} hooks, expected {len(symbols)}"
        )

    report = {
        "schema_version": 2,
        "binary_sha256": binary_sha256,
        "method": (
            "unique exact cstring xref, ordered parent BL index, and nth "
            "LC_FUNCTION_STARTS after a resolved origin"
        ),
        "coverage": "partial",
        "hook_count": len(report_entries),
        "hooks": report_entries,
    }
    return "\n".join(lines) + "\n", report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path, nargs="?", default=DEFAULT_BINARY)
    parser.add_argument("--anchors", type=Path, default=DEFAULT_ANCHORS)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    args = parser.parse_args()
    try:
        output, report = generate(args.binary, args.anchors)
    except (OSError, json.JSONDecodeError, RecoveryError) as error:
        raise SystemExit(f"generate_sdl_address_hooks.py: {error}") from error
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(output, encoding="utf-8", newline="\n")
    args.report.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
