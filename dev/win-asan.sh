#!/bin/sh

# Runs the Windows test suite under ASAN and UBSAN. The nixpkgs clang for mingw
# ships no libclang_rt.asan, so the sanitizer build uses llvm-mingw; the
# shipping build stays on nixpkgs clang (see dev/win.sh).

set -xue

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

JOBS="${JOBS:-$(nproc)}"
LM_VERSION="${LLVM_MINGW_VERSION:-20261104}"
LM_DIR="${LLVM_MINGW_DIR:-$ROOT/.build/deps/llvm-mingw}"
BUILD_DIR=.build-win-san

if [ ! -x "$LM_DIR/bin/x86_64-w64-mingw32-clang" ]; then
    ARCHIVE="$ROOT/.build/deps/llvm-mingw.tar.xz"
    mkdir -p "$LM_DIR" "$ROOT/.build/deps"
    curl -fL -o "$ARCHIVE" \
        "https://github.com/mstorsjo/llvm-mingw/releases/download/${LM_VERSION}/llvm-mingw-${LM_VERSION}-ucrt-ubuntu-22.04-x86_64.tar.xz"
    tar -xJf "$ARCHIVE" -C "$LM_DIR" --strip-components=1
    rm -f "$ARCHIVE"
fi

CC="$LM_DIR/bin/x86_64-w64-mingw32-clang" \
CXX="$LM_DIR/bin/x86_64-w64-mingw32-clang++" \
AR="$LM_DIR/bin/x86_64-w64-mingw32-ar" \
CPPFLAGS='-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer' \
LDFLAGS='-fsanitize=address,undefined' \
    ./build --target x86_64-w64-mingw32 -B "$BUILD_DIR" -j "$JOBS"

# ASAN on Windows is a dynamic runtime; test.exe needs all three DLLs next to it
RUN="$BUILD_DIR/run"
mkdir -p "$RUN"
cp -f "$BUILD_DIR/tst/test.exe" "$RUN/test.exe"

for lib in libclang_rt.asan_dynamic-x86_64.dll libc++.dll libunwind.dll; do
    cp -f "$LM_DIR/x86_64-w64-mingw32/bin/$lib" "$RUN/"
done

# Run the tests natively when Windows interop is available (WSL); otherwise
# copy the run directory to a Windows machine.
if [ -n "${WSL_DISTRO_NAME:-}" ] || [ -e /proc/sys/fs/binfmt_misc/WSLInterop ]; then
    # WSL only forwards variables listed in WSLENV to Windows processes
    WSLENV="${WSLENV:+${WSLENV}:}STD_TEST_PORT_OFFSET:UBSAN_OPTIONS" \
    STD_TEST_PORT_OFFSET="${STD_TEST_PORT_OFFSET:-1000}" \
    UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1}" \
    timeout 900 "$RUN/test.exe" --top=20
else
    echo "built: copy $RUN to Windows and run test.exe" >&2
fi
