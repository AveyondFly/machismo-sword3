# Build Apple-ABI libc++ against the Linaro 6.3.1 sysroot so GLIBC_ stays
# at 2.17. Linaro g++ 6.3 cannot compile this libc++ (needs C++20), so the
# compiler is Ubuntu's aarch64-linux-gnu GCC 11 while headers/libc/libgcc
# come from Linaro.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(LINARO_ROOT
    "/opt/toolchains/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu"
    CACHE PATH "Linaro 6.3.1 aarch64 toolchain root")

set(CMAKE_C_COMPILER "/usr/bin/aarch64-linux-gnu-gcc")
set(CMAKE_CXX_COMPILER "/usr/bin/aarch64-linux-gnu-g++")
set(CMAKE_SYSROOT "${LINARO_ROOT}/aarch64-linux-gnu/libc")

# Fortify helpers in newer gcc pull GLIBC_2.27+ *_chk symbols.
set(CMAKE_C_FLAGS_INIT "-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0")
set(CMAKE_CXX_FLAGS_INIT "-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-Wl,-rpath,\$ORIGIN")
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "\$ORIGIN")
set(CMAKE_BUILD_RPATH "\$ORIGIN")

link_directories("${LINARO_ROOT}/aarch64-linux-gnu/libc/lib")

set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
