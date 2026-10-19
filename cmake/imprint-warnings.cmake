# X-8: the project warning baseline.
#
# One place builds the per-compiler warning set; the top level applies it
# through `add_compile_options`, which is directory-scoped -- every
# in-tree target (libraries, shells, tools, apps, tests) gets it, while
# a consumer of the installed package or of add_subdirectory(imprint)
# never inherits it.
#
# The audit finding was that the tree compiled with no warnings enabled
# at all (`MSVC /utf-8` was the only compile option): a narrowing or a
# shadowed name could only be found by reading. The set below is chosen
# so the two families police the same bug classes (the numbers in the
# MSVC branch are the equivalents of the GCC/Clang names beside them).
#
#   IMPRINT_WARNINGS  the warning set itself (default ON)
#   IMPRINT_WERROR    promote the set to errors. Default ON in a
#                     top-level in-tree build, OFF as a subproject: a
#                     consumer's newer compiler must not fail their build
#                     on a diagnostic we have not seen yet.
#
# Vendored code does not ride the set: third_party/stb compiles only
# through the wrapper TUs, whose include directory is marked SYSTEM
# (imcore/CMakeLists.txt), so vendored-header diagnostics stay out of
# the project's output.
#
# The set is per-compiler and per-language: C++-only flags go through
# $<COMPILE_LANGUAGE:CXX> so a C TU (host_compat.c, test_zbapi.c) never
# sees "valid for C++ but not for C" warnings.
#
# Keep it clean on every entry of the build matrix (GCC and Clang on
# Linux, AppleClang on macOS, MSVC on Windows, arm-none-eabi-gcc on the
# NDS, emscripten on wasm). IMPRINT_WERROR=OFF is the escape hatch for a
# compiler that ships a new diagnostic before the tree is cleaned.

set(IMPRINT_WARNING_FLAGS_C)
set(IMPRINT_WARNING_FLAGS_CXX)
set(IMPRINT_WERROR_FLAG)

if (MSVC)
    # /W4 is MSVC's reasonable-strictness level; /Wall is unusable on
    # real code (it flags the CRT's own headers). /permissive- rejects
    # the non-conforming spellings GCC/Clang already reject.
    list(APPEND IMPRINT_WARNING_FLAGS_C /W4 /permissive-)
    list(APPEND IMPRINT_WARNING_FLAGS_CXX /W4 /permissive-)
    set(IMPRINT_WERROR_FLAG /WX)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang|Emscripten")
    # the common set: -Wall -Wextra are the baseline both families
    # implement; the rest are the high-signal subset that both accept.
    set(_imprint_common
        -Wall
        -Wextra
        -Wpedantic
        -Wformat=2
        -Wundef
        -Wshadow
        -Wcast-qual
        -Wdouble-promotion
        -Wimplicit-fallthrough
    )
    set(_imprint_cxx
        -Wnon-virtual-dtor
        -Woverloaded-virtual
        -Wold-style-cast
        -Wzero-as-null-pointer-constant
        -Wcast-align
        -Wnull-dereference
    )
    list(APPEND IMPRINT_WARNING_FLAGS_C ${_imprint_common})
    list(APPEND IMPRINT_WARNING_FLAGS_CXX ${_imprint_common} ${_imprint_cxx})
    set(IMPRINT_WERROR_FLAG -Werror)
else()
    # an unknown compiler: warn the operator, do not guess flags
    message(WARNING "IMPRINT_WARNINGS: no warning set known for ${CMAKE_CXX_COMPILER_ID}; the build compiles without the project baseline")
endif()

if (NOT IMPRINT_WARNINGS)
    set(IMPRINT_WARNING_FLAGS_C)
    set(IMPRINT_WARNING_FLAGS_CXX)
    set(IMPRINT_WERROR_FLAG)
elseif (NOT IMPRINT_WERROR)
    set(IMPRINT_WERROR_FLAG)
endif()
