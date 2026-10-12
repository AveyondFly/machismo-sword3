# Apple-ABI libc++ for aarch64 Linux (tracked prebuilt)

`libc++.so.1` / `libc++abi.so.1` use Apple's `std::string` layout
(`_LIBCPP_ABI_ALTERNATE_STRING_LAYOUT`, plus the Darwin mutex/mbstate patches),
which is what the Mach-O iOS binaries the loader runs were compiled against. A
stock Linux libc++ binds fine and then corrupts every `std::string`.

They are tracked here because rebuilding them needs the `extern/llvm-project`
submodule (a full LLVM checkout, which CI does not fetch) *and* an old-glibc
sysroot. Without these bytes a clone cannot produce a bootable package at all.
The GLIBC floor gate in `scripts/package-portmaster.sh` still runs against them,
so a wrong build here fails loudly instead of shipping.

`libatomic.so.1` is a `DT_NEEDED` of `libc++.so.1`. EmuELEC 4.7 ships only a
32-bit build of it, so a 64-bit one has to travel with the port; it goes into
`libs.aarch64/`, never next to the firmware's own libraries.

| file | GLIBC floor | md5 |
| --- | --- | --- |
| `libc++.so.1` | 2.17 | `31d4cd98ab9820ce20ea635acb646f38` |
| `libc++abi.so.1` | 2.18 | `34196270b3517a5d88ea555ee9dc0833` |
| `libatomic.so.1` | 2.17 | `b145cea34360ce2618af77a26f3b8ffa` |

Provenance: GCC 12.5 build of the darwin-abi-compat libc++, taken from the
`shotgunking` port (same machismo iOS-loader family) and then re-verified here —
on an RF45H running EmuELEC 4.7 (glibc 2.36) the port root pair plus this
libatomic gave `929 binds resolved, 0 failed`, `driver=KMSDRM`, and the port
reached in-game story screens.

To build them yourself instead, run `./scripts/build-libcxx.sh`, drop the
resulting `libc++.so.1`, `libc++abi.so.1` and a matching 64-bit `libatomic.so.1`
into `build-libcxx/lib/`; the packaging scripts prefer that directory over this
one, and `LIBCXX_BUILD_DIR` overrides it.

Licenses: libc++ and libc++abi are Apache-2.0 with the LLVM exception. libatomic
comes from the GCC runtime libraries (GPLv3 with the GCC Runtime Library
Exception).
