import os
import build


target_is_windows = "windows" in build.target or "mingw" in build.target
exe_suffix = ".exe" if target_is_windows else ""

build.cxxflags += [
    "-W",
    "-Wall",
    "-std=c++26",
    "-O2",
    "-g",
    "-fno-omit-frame-pointer",
    "-mno-omit-leaf-frame-pointer",
]
if build.target.startswith("x86_64"):
    build.cxxflags += ["-mcx16"]
if target_is_windows:
    build.cxxflags += [
        "-D_WIN32_WINNT=0x0A00",
        "-DWIN32_LEAN_AND_MEAN",
        "-DNOMINMAX",
        "-D_CRT_SECURE_NO_WARNINGS",
    ]
    # static libc++/libunwind: the produced binaries must run on Windows
    # without shipping MinGW DLLs
    build.ldflags += [
        "-static",
        "-lws2_32",
        "-liphlpapi",
        "-ldbghelp",
    ]
std_sources = sorted(build.glob("$(S)/std/*/*.cpp"))
unit_sources = [source for source in std_sources if source.endswith("_ut.cpp")]
library_sources = [source for source in std_sources if not source.endswith("_ut.cpp")]

external_monotonic_clock = "-DSTL_EXTERNAL_MONOTONIC_NOW_US=1" in build.cppflags
libstd_name = "libstd_external_clock" if external_monotonic_clock else "libstd"

libstd = library(
    name=libstd_name,
    srcs=library_sources,
    output=f"$(B)/{libstd_name}.a",
)

# POSIX pipe/uring stress tools have no Windows equivalent
windows_excluded_tools = {"coro_pipe", "io_uring_msg"}

test_sources = sorted(build.glob("$(S)/tst/*.cpp"))
test_main = "$(S)/tst/test.cpp"
test = program(
    srcs=[test_main, *unit_sources],
    output=f"$(B)/tst/test{exe_suffix}",
    deps=[libstd],
)

test_binaries = []
for source in test_sources:
    if source == test_main:
        continue
    name = os.path.basename(source).removesuffix(".cpp")
    if target_is_windows and name in windows_excluded_tools:
        continue
    test_binaries.append(program(
        name=name,
        srcs=[source],
        output=f"$(B)/tst/{name}{exe_suffix}",
        deps=[libstd],
    ))


install(libstd, test, *test_binaries)
