"""Global C++ compilation and link flags.

Copied from dds rather than loaded from it, and deliberately kept under the
same file and variable names so the two stay diffable. The select() keys are
bare //: labels, which resolve against whichever repository is doing the
loading -- sharing this file across the boundary would mean depending on how
that resolves, for no gain on a file that changes about twice a year.

These must stay in step with dds's copy: this repository's code is compiled
into the same binaries as the solver's, under the same -Werror.
"""

DDS_CPPOPTS = select({
    "//:build_macos": [
        "-O3",
        "-flto=thin",
        "-mtune=generic",
        "-fPIC",
        "-Wpedantic",
        "-Wall",
        "-Wno-character-conversion",
        "-Werror",
    ],
    "//:debug_build_macos": [
        "-g",
        "-mtune=generic",
        "-fPIC",
        "-Wpedantic",
        "-Wall",
        "-Wno-character-conversion",
        "-Werror",
    ],
    "//:build_linux": [
        "-O3",
        "-fPIC",
        "-Wpedantic",
        "-Wall",
        "-Wno-character-conversion",
        "-Werror",
    ],
    "//:debug_build_linux": [
        "-g",
        "-O2",
        "-fPIC",
        "-Wpedantic",
        "-Wall",
        "-Wno-character-conversion",
        "-Werror",
    ],
    # Optimization (/O2, /Od) and language standard (/std) come from Bazel's
    # compilation_mode and the patched MSVC default_cpp_std (/std:c++20).
    # Restating them here overrides the toolchain and triggers MSVC D9025.
    # /utf-8 stays in these arms rather than becoming a global --cxxopt: it is
    # an MSVC-only spelling, and a global would reach the non-Windows arms.
    # (dds gives a second reason -- its wasm transitions -- that does not
    # apply here, since nothing in this repository is built for wasm.)
    "//:build_windows": [
        # Without /utf-8, MSVC decodes BOM-less UTF-8 sources in the system ANSI
        # codepage; on hosts below CP1252 that raises C4819, which /WX turns into
        # error C2220.
        "/utf-8",
        "/W4",
        "/WX",
        "/permissive-",
    ],
    "//:debug_build_windows": [
        "/Zi",
        "/utf-8",
        "/W4",
        "/WX",
        "/permissive-",
    ],
    "//conditions:default": [
        "-std=c++20"
    ],
})

DDS_LOCAL_DEFINES = select({
    "//:build_macos": [],
    "//:debug_build_macos": [],
    "//:build_linux": [],
    "//:debug_build_linux": [],
    "//conditions:default": [],
}) + select({
    "//:debug_all": ["DDS_DEBUG_ALL"],
    "//conditions:default": [],
}) + select({
    "//:tt_reset_debug": ["DDS_DEBUG_TT_RESET"],
    "//conditions:default": [],
}) + select({
    "//:ab_stats": ["DDS_AB_STATS"],
    "//conditions:default": [],
})

DDS_LINKOPTS = select({
    "//:build_macos": ["-flto=thin"],
    "//:debug_build_macos": [],
    "//:build_linux": [],
    "//:debug_build_linux": [],
    "//conditions:default": [],
})

# Per-target define to enable scheduler timing when desired.
# Controlled with: --define=scheduler=true
# Usage in BUILD files: local_defines = DDS_LOCAL_DEFINES + DDS_SCHEDULER_DEFINE
DDS_SCHEDULER_DEFINE = select({
    "//:scheduler": ["DDS_SCHEDULER"],
    "//conditions:default": [],
})
