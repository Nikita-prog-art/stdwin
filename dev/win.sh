#!/bin/sh

set -xue

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

JOBS="${JOBS:-$(nproc)}"
OPENSSL_DIR="${OPENSSL_DIR:-$ROOT/.build/deps/openssl-win}"
CPPFLAGS="${CPPFLAGS:-}"
LDFLAGS="${LDFLAGS:-}"

# TLS is optional: use the tree built by ./dev/openssl-win.sh when present
if [ -e "$OPENSSL_DIR/lib/libssl.a" ]; then
    CPPFLAGS="$CPPFLAGS -I$OPENSSL_DIR/include"
    LDFLAGS="$LDFLAGS -L$OPENSSL_DIR/lib -lssl -lcrypto -lcrypt32 -ladvapi32 -luser32"
fi

nix-shell -p 'pkgsCross.mingw-ucrt-x86_64-llvm.buildPackages.llvmPackages_23.clang' --run "
    CPPFLAGS='$CPPFLAGS' \
    LDFLAGS='$LDFLAGS' \
    CC=x86_64-w64-mingw32-clang \
    CXX=x86_64-w64-mingw32-clang++ \
    AR=x86_64-w64-mingw32-ar \
    ./build --target x86_64-w64-mingw32 -B .build-win -j ${JOBS}
"

# Run the tests natively when Windows interop is available (WSL); otherwise
# copy .build-win/tst/test.exe to a Windows machine.
if [ -n "${WSL_DISTRO_NAME:-}" ] || [ -e /proc/sys/fs/binfmt_misc/WSLInterop ]; then
    # WSL only forwards variables listed in WSLENV to Windows processes
    WSLENV="${WSLENV:+${WSLENV}:}STD_TEST_PORT_OFFSET" \
    STD_TEST_PORT_OFFSET="${STD_TEST_PORT_OFFSET:-1000}" \
    timeout 600 ./.build-win/tst/test.exe --top=20
else
    echo "built: run .build-win/tst/test.exe on Windows" >&2
fi
