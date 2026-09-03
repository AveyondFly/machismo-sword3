# ArkOS / old-glibc toolchain. Matches sword3/linaro_build.md:
# compiler from Linaro 6.3.1, link prefix from linaro-extra, libc from the
# Linaro sysroot so the binary's GLIBC_ demand stays at 2.17.
#
# Do not put a newer distro sysroot on the global -L path. Its libc/libm
# are GNU ld scripts with /usr/lib/... absolute names and will break the
# Linaro linker.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(LINARO_ROOT
    "/opt/toolchains/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu"
    CACHE PATH "Linaro 6.3.1 aarch64 toolchain root")
set(LINARO_EXTRA
    "/home/ubuntu/linaro-extra"
    CACHE PATH "Linaro-built SDL2/GLES/png/z prefix")
set(LINARO_HOST_SYSROOT
    ""
    CACHE PATH "Optional sysroot providing mixer/image/ttf/ffmpeg/EGL sonames")

set(CMAKE_C_COMPILER "${LINARO_ROOT}/bin/aarch64-linux-gnu-gcc")
set(CMAKE_CXX_COMPILER "${LINARO_ROOT}/bin/aarch64-linux-gnu-g++")
set(CMAKE_SYSROOT "${LINARO_ROOT}/aarch64-linux-gnu/libc")

# Newer _FORTIFY_SOURCE helpers pull GLIBC_2.27+ memcpy_chk.
set(CMAKE_C_FLAGS_INIT "-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0")
set(CMAKE_CXX_FLAGS_INIT "-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0")
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-Wl,-rpath-link,${LINARO_EXTRA}/lib -Wl,-rpath,\$ORIGIN")
set(CMAKE_SHARED_LINKER_FLAGS_INIT
    "-Wl,-rpath-link,${LINARO_EXTRA}/lib -Wl,-rpath,\$ORIGIN")
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "\$ORIGIN")
set(CMAKE_BUILD_RPATH "\$ORIGIN")

list(INSERT CMAKE_PREFIX_PATH 0 "${LINARO_EXTRA}")
list(APPEND CMAKE_FIND_ROOT_PATH "${LINARO_EXTRA}")
include_directories("${LINARO_EXTRA}/include")
link_directories("${LINARO_EXTRA}/lib")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

set(ENV{PKG_CONFIG_SYSROOT_DIR} "")
set(ENV{PKG_CONFIG_LIBDIR} "${LINARO_EXTRA}/lib/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "${LINARO_EXTRA}/lib/pkgconfig")
