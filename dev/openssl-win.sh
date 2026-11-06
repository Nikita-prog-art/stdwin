#!/bin/sh

# Builds a static OpenSSL for the Windows cross target into
# .build/deps/openssl-win. The nixpkgs package cannot be used as is: it also
# builds openssl.exe, whose resource script needs winver.h, and only the
# libraries are required here.
#
# ./dev/win.sh picks the result up automatically.

set -xue

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="${OPENSSL_DIR:-$ROOT/.build/deps/openssl-win}"
WORK="${OPENSSL_WORK:-$ROOT/.build/deps/openssl-src}"
JOBS="${JOBS:-$(nproc)}"

if [ -e "$OUT/lib/libssl.a" ]; then
    echo "openssl: $OUT is ready" >&2
    exit 0
fi

SRC="${OPENSSL_SRC:-}"
if [ -z "$SRC" ]; then
    SRC=$(nix-build --no-out-link -E 'with import <nixpkgs> {}; openssl.src')
fi

rm -rf "$WORK"
mkdir -p "$WORK" "$OUT"
tar -xf "$SRC" -C "$WORK" --strip-components=1

nix-shell -p 'pkgsCross.mingw-ucrt-x86_64-llvm.buildPackages.llvmPackages_23.clang' \
          'perl' 'gnumake' --run "
    export CC=x86_64-w64-mingw32-clang
    export CXX=x86_64-w64-mingw32-clang++
    export AR=x86_64-w64-mingw32-ar
    export RANLIB=x86_64-w64-mingw32-ranlib
    cd '$WORK' &&
    perl ./Configure mingw64 no-shared no-tests no-docs no-apps --prefix='$OUT' --openssldir='$OUT/ssl' &&
    make -j ${JOBS} build_libs
"

mkdir -p "$OUT/include" "$OUT/lib"
cp -R "$WORK/include/openssl" "$OUT/include/"
cp "$WORK/libssl.a" "$WORK/libcrypto.a" "$OUT/lib/"

echo "openssl: built $OUT" >&2
