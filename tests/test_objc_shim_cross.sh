#!/bin/bash
# Cross-compile and audit the complete AArch64 Objective-C shim.
set -euo pipefail

cd "$(dirname "$0")/.."

CC="${AARCH64_CC:-aarch64-linux-gnu-gcc}"
NM="${AARCH64_NM:-aarch64-linux-gnu-nm}"
OBJDUMP="${AARCH64_OBJDUMP:-aarch64-linux-gnu-objdump}"
QEMU="${QEMU_AARCH64:-qemu-aarch64}"

if ! command -v "$CC" >/dev/null 2>&1 ||
   ! command -v "$NM" >/dev/null 2>&1 ||
   ! command -v "$OBJDUMP" >/dev/null 2>&1; then
    echo "SKIP: AArch64 cross compiler/binutils unavailable"
    exit 0
fi

TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

python3 tools/generate_objc_shim.py \
    --manifest configs/sword3/binary-manifest.json \
    --output "$TMPDIR/sword3_objc_imports.c"

COMMON=(-std=c11 -Wall -Wextra -Werror -fPIC -I src/shim)
"$CC" "${COMMON[@]}" -c "$TMPDIR/sword3_objc_imports.c" \
    -o "$TMPDIR/imports.o"
"$CC" "${COMMON[@]}" -c src/shim/sword3_objc_shim.c \
    -o "$TMPDIR/runtime.o"
"$CC" -Wall -Wextra -Werror -fPIC -c src/shim/sword3_objc_msgsend.S \
    -o "$TMPDIR/msgsend.o"
"$CC" -shared -Wl,-z,defs \
    "$TMPDIR/imports.o" "$TMPDIR/runtime.o" "$TMPDIR/msgsend.o" \
    -o "$TMPDIR/libsword3_objc_shim.so"

python3 - "$NM" "$OBJDUMP" "$TMPDIR/libsword3_objc_shim.so" \
    "$TMPDIR/msgsend.o" <<'PY'
import json
import subprocess
import sys
from pathlib import Path

nm, objdump, library, assembly = sys.argv[1:]
manifest = json.loads(
    Path("configs/sword3/binary-manifest.json").read_text(encoding="utf-8")
)
expected = set()
for kind in ("lazy", "regular"):
    for entry in manifest["dyld_info_only"]["bind"][kind]["by_dylib"]:
        if entry["dylib"] == "/usr/lib/libobjc.A.dylib":
            expected.update(symbol["name"][1:] for symbol in entry["symbols"])

nm_output = subprocess.run(
    [nm, "-D", "--defined-only", library],
    check=True,
    capture_output=True,
    text=True,
).stdout
defined = {line.split()[-1] for line in nm_output.splitlines() if line.split()}
missing = sorted(expected - defined)
if missing:
    raise SystemExit("missing AArch64 ELF definitions: " + ", ".join(missing))

for extra_api in ("sel_registerName", "sel_getName"):
    if extra_api not in defined:
        raise SystemExit(f"missing required selector API: {extra_api}")

disassembly = subprocess.run(
    [objdump, "-dr", assembly],
    check=True,
    capture_output=True,
    text=True,
).stdout
if disassembly.count("R_AARCH64_CALL26\tsword3_objc_lookup_imp") != 2:
    raise SystemExit("both dispatch paths must call sword3_objc_lookup_imp")
if disassembly.count("br\tx16") != 2 and disassembly.count("br      x16") != 2:
    raise SystemExit("both dispatch paths must tail-branch through x16")
for register_pair in ("q0, q1", "q2, q3", "q4, q5", "q6, q7"):
    if register_pair not in disassembly:
        raise SystemExit(f"dispatch does not preserve {register_pair}")
PY

cat >"$TMPDIR/dispatch_fixture.c" <<'C'
#include "sword3_objc_shim.h"

#include <assert.h>
#include <stdint.h>

struct method_list {
    uint32_t entsize_and_flags;
    uint32_t count;
    struct sword3_objc_method entries[2];
};

struct test_object {
    sword3_objc_Class isa;
};

struct triple {
    uint64_t first;
    uint64_t second;
    uint64_t third;
};

struct pair {
    uint64_t first;
    uint64_t second;
};

struct four_doubles {
    double first;
    double second;
    double third;
    double fourth;
};

static struct test_object object;
static sword3_objc_sel expected_mixed_selector;

static uint64_t mixed_imp(
    sword3_objc_id receiver,
    sword3_objc_sel selector,
    uint64_t x2,
    uint64_t x3,
    uint64_t x4,
    uint64_t x5,
    uint64_t x6,
    uint64_t x7,
    uint64_t stack0,
    uint64_t stack1,
    double d0,
    double d1,
    double d2,
    double d3,
    double d4,
    double d5,
    double d6,
    double d7,
    double stack2
)
{
    assert(receiver == &object);
    assert(selector == expected_mixed_selector);
    assert(x2 == 2 && x3 == 3 && x4 == 4);
    assert(x5 == 5 && x6 == 6 && x7 == 7);
    assert(stack0 == 8 && stack1 == 9);
    assert(d0 == 0.5 && d1 == 1.5 && d2 == 2.5);
    assert(d3 == 3.5 && d4 == 4.5 && d5 == 5.5);
    assert(d6 == 6.5 && d7 == 7.5 && stack2 == 8.5);
    return UINT64_C(0x123456789abcdef0);
}

static struct triple triple_imp(
    sword3_objc_id receiver,
    sword3_objc_sel selector,
    uint64_t seed
)
{
    struct triple result = {seed, seed + 1, seed + 2};
    assert(receiver == &object);
    assert(selector != NULL);
    return result;
}

extern uint64_t send_mixed(
    sword3_objc_id,
    sword3_objc_sel,
    uint64_t,
    uint64_t,
    uint64_t,
    uint64_t,
    uint64_t,
    uint64_t,
    uint64_t,
    uint64_t,
    double,
    double,
    double,
    double,
    double,
    double,
    double,
    double,
    double
) __asm__("objc_msgSend");

extern uint64_t send_super_mixed(
    struct sword3_objc_super *,
    sword3_objc_sel,
    uint64_t,
    uint64_t,
    uint64_t,
    uint64_t,
    uint64_t,
    uint64_t,
    uint64_t,
    uint64_t,
    double,
    double,
    double,
    double,
    double,
    double,
    double,
    double,
    double
) __asm__("objc_msgSendSuper2");

extern struct triple send_triple(
    sword3_objc_id,
    sword3_objc_sel,
    uint64_t
) __asm__("objc_msgSend");
extern uint64_t send_nil_integer(
    sword3_objc_id,
    sword3_objc_sel
) __asm__("objc_msgSend");
extern double send_nil_double(
    sword3_objc_id,
    sword3_objc_sel
) __asm__("objc_msgSend");
extern struct pair send_nil_pair(
    sword3_objc_id,
    sword3_objc_sel
) __asm__("objc_msgSend");
extern struct four_doubles send_nil_hfa(
    sword3_objc_id,
    sword3_objc_sel
) __asm__("objc_msgSend");
extern struct pair send_super_nil_pair(
    struct sword3_objc_super *,
    sword3_objc_sel
) __asm__("objc_msgSendSuper2");

int main(void)
{
    sword3_objc_sel mixed_selector = sel_registerName("mixed");
    sword3_objc_sel triple_selector = sel_registerName("triple");
    struct method_list methods = {
        .entsize_and_flags = sizeof(struct sword3_objc_method),
        .count = 2,
        .entries = {
            {
                .name = mixed_selector,
                .types = "Q@:",
                .imp = (sword3_objc_imp)(uintptr_t)mixed_imp,
            },
            {
                .name = triple_selector,
                .types = "{triple=QQQ}@:Q",
                .imp = (sword3_objc_imp)(uintptr_t)triple_imp,
            },
        },
    };
    struct sword3_objc_class_ro base_ro = {
        .instance_size = sizeof(object),
        .name = "Base",
        .base_methods = (const struct sword3_objc_method_list *)&methods,
    };
    struct sword3_objc_class_ro child_ro = {
        .instance_size = sizeof(object),
        .name = "Child",
    };
    struct sword3_objc_class base = {
        .data_bits = (uintptr_t)&base_ro,
    };
    struct sword3_objc_class child = {
        .superclass = &base,
        .data_bits = (uintptr_t)&child_ro,
    };
    struct sword3_objc_super super = {
        .receiver = &object,
        .current_class = &child,
    };
    struct sword3_objc_super nil_super = {
        .receiver = NULL,
        .current_class = &child,
    };
    struct triple triple;
    struct pair pair;
    struct four_doubles hfa;

    object.isa = &child;
    expected_mixed_selector = mixed_selector;
    assert(send_mixed(
        &object, mixed_selector,
        2, 3, 4, 5, 6, 7, 8, 9,
        0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5
    ) == UINT64_C(0x123456789abcdef0));
    assert(send_super_mixed(
        &super, mixed_selector,
        2, 3, 4, 5, 6, 7, 8, 9,
        0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5
    ) == UINT64_C(0x123456789abcdef0));

    triple = send_triple(&object, triple_selector, 40);
    assert(triple.first == 40 && triple.second == 41 && triple.third == 42);
    assert(send_nil_integer(NULL, mixed_selector) == 0);
    assert(send_nil_double(NULL, mixed_selector) == 0.0);
    pair = send_nil_pair(NULL, mixed_selector);
    assert(pair.first == 0 && pair.second == 0);
    hfa = send_nil_hfa(NULL, mixed_selector);
    assert(
        hfa.first == 0.0 && hfa.second == 0.0 &&
        hfa.third == 0.0 && hfa.fourth == 0.0
    );
    pair = send_super_nil_pair(&nil_super, mixed_selector);
    assert(pair.first == 0 && pair.second == 0);
    return 0;
}
C

"$CC" "${COMMON[@]}" "$TMPDIR/dispatch_fixture.c" \
    src/shim/sword3_objc_shim.c src/shim/sword3_objc_msgsend.S \
    -static -o "$TMPDIR/dispatch_fixture"
if command -v "$QEMU" >/dev/null 2>&1; then
    "$QEMU" "$TMPDIR/dispatch_fixture"
fi

echo "PASS: AArch64 Objective-C shim compiles and exports every manifest import"
