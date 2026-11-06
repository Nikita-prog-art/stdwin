# Build system

## Build runner

Source and test discovery is fully automatic via `build.glob`:

- `std/*/*.cpp` — all sources. Files ending in `_ut.cpp` are separated out as unit tests, the rest go into `.build/libstd.a`.
- `tst/*.cpp` — test runner and additional test code.

Adding a new `.cpp` file to either location is enough; `build.py` does not contain
a hardcoded source or binary list.

Build everything with `./build`. Run the unit tests with
`./.build/tst/test`. An explicit `./build libstd` builds only the library and
publishes `./libstd` as a symlink into the content-addressed cache.

The build runner has no filesystem install target. Packagers copy
`.build/libstd.a` and the headers under `std/` into their output tree.

The helper script `./dev/run.sh` sets up the build environment via `ix` (package manager), invokes `./dev/build.sh`, and runs the tests.

## Windows (native Win32, MinGW-w64/UCRT)

The Windows target is a native Windows x64 build (no POSIX emulation layer). It is
cross-compiled from Linux with clang from nixpkgs; GCC is not used:

```sh
nix-shell -p 'pkgsCross.mingw-ucrt-x86_64-llvm.buildPackages.llvmPackages_23.clang' --run '
    CC=x86_64-w64-mingw32-clang \
    CXX=x86_64-w64-mingw32-clang++ \
    AR=x86_64-w64-mingw32-ar \
    ./build --target x86_64-w64-mingw32 -B .build-win -j "$(nproc)"
'

./.build-win/tst/test.exe --top=20
```

`./dev/win.sh` wraps the same command and runs the tests when Windows interop is
available (WSL). Produced binaries are linked statically against libc++/libunwind,
so they only need the system UCRT and `WS2_32.dll`; no MinGW runtime DLLs are
shipped.

Two optional pieces are driven by their own scripts:

- `./dev/openssl-win.sh` builds a static OpenSSL for TLS, which `./dev/win.sh`
  then picks up automatically; `./dev/tls_smoke.sh` checks HTTPS on loopback.
- `./dev/win-asan.sh` runs the suite under ASAN and UBSAN with llvm-mingw,
  because the nixpkgs clang for mingw has no sanitizer runtime.

Both test suites bind fixed loopback ports, and on a WSL host Windows and Linux
share the network stack. `dev/win.sh` therefore runs the Windows tests with
`STD_TEST_PORT_OFFSET=1000`, which shifts every fixed test port. On WSL the
variable must also be listed in `WSLENV` to reach the Windows process; `dev/win.sh`
takes care of both. Set the same variables when running `test.exe` manually next
to a Linux test run.

Platform notes:

- Windows-specific code lives in `std/sys/plat.h` / `std/sys/plat.cpp`; the
  `#ifdef` blocks in other `.cpp` files stay local. Public headers under
  `std/*/*.h` remain platform-neutral.
- Socket descriptors carry a tag (`int` on Windows is not a universal handle), so
  `FD`/`PollFD`/`IoReactor` still take a plain `int` while the platform layer
  dispatches to `recv`/`send`/`closesocket`.
- The Windows poller is `WSAPoll` and only accepts sockets; the server wakeup
  (`EventFD`) and coroutine `JoinPipe` use a connected loopback socket pair.
- `std/thr/context.cpp` has a Win64 ABI context switch that publishes the
  coroutine stack in the TEB so SEH unwinding works.
- `recvmsg`/`sendmsg`/`recvmmsg` and `SCM_RIGHTS`-style control messages return
  `ENOTSUP` on Windows.

The reasoning behind these decisions, the full list of platform limits and the
POSIX → Win32 mapping are in [WIN32.md](WIN32.md).

## Optional dependencies

The project uses `__has_include` to detect available libraries at compile time. No library is required — the code always compiles with built-in fallbacks.

### Hash (`std/str/hash.cpp`)

Backend for `shash32`/`shash64`, selected by priority:

| Priority | Header | Library | Notes |
|----------|--------|---------|-------|
| 1 | `<rapidhash.h>` | rapidhash | Header-only, fastest |
| 2 | `<xxhash.h>` | xxhash | `XXH3_64bits` |
| 3 | *(none)* | *(built-in)* | FNV-1a + SplitMix64 |

### TLS (`std/net/ssl_socket.cpp`)

Up to three TLS backends can be available simultaneously. Selection at runtime via `USE_SSL_ENGINE` env var (`openssl`, `s2n`, `mbedtls`), otherwise the first available is used in priority order:

| Priority | Header | Library |
|----------|--------|---------|
| 1 | `<openssl/ssl.h>` | OpenSSL / LibreSSL / AWS-LC |
| 2 | `<s2n.h>` | s2n-tls |
| 3 | `<mbedtls/ssl.h>` | Mbed TLS |

### DNS (`std/dns/ares.cpp`)

| Header | Library | Notes |
|--------|---------|-------|
| `<ares.h>` | c-ares | Async DNS via event loop; without it falls back to `getaddrinfo` offloaded to a thread pool |

### Optional dependencies in the Windows build

c-ares and xxhash build and work for the Windows target, but the build runner
does not inherit `NIX_CFLAGS_COMPILE`/`NIX_LDFLAGS`, so pass their paths
explicitly. `-l:lib*.dll.a` is required because `-static` makes plain `-l` skip
DLL import libraries:

```sh
CARES=$(nix-build --no-out-link -E 'let p = (import <nixpkgs> {}).pkgsCross.mingw-ucrt-x86_64-llvm; in p.c-ares')
CARES_DEV=$(nix-build --no-out-link -E 'let p = (import <nixpkgs> {}).pkgsCross.mingw-ucrt-x86_64-llvm; in p.c-ares.dev')
XHASH=$(nix-build --no-out-link -E 'let p = (import <nixpkgs> {}).pkgsCross.mingw-ucrt-x86_64-llvm; in p.xxhash')

nix-shell -p 'pkgsCross.mingw-ucrt-x86_64-llvm.buildPackages.llvmPackages_23.clang' \
          'pkgsCross.mingw-ucrt-x86_64-llvm.c-ares' \
          'pkgsCross.mingw-ucrt-x86_64-llvm.xxhash' --run "
    CPPFLAGS='-isystem $CARES_DEV/include -isystem $XHASH/include' \
    LDFLAGS='-L$CARES/lib -L$XHASH/lib -l:libcares.dll.a -l:libxxhash.dll.a' \
    CC=x86_64-w64-mingw32-clang CXX=x86_64-w64-mingw32-clang++ \
    AR=x86_64-w64-mingw32-ar \
    ./build --target x86_64-w64-mingw32 -B .build-win-deps -j \$(nproc)
"
```

Copy `libcares.dll` and `libxxhash.dll` from the package `bin/` directories next
to `test.exe` before running. The default build (without these packages) uses the
`getaddrinfo` resolver and the built-in hash and needs no DLLs.

### TLS for the Windows build

The nixpkgs `openssl` package cannot be cross-built as is: it also builds
`openssl.exe`, whose resource script fails in `windres` (`winver.h`). Only the
libraries are needed, so `./dev/openssl-win.sh` builds them from the same
nixpkgs source into `.build/deps/openssl-win`:

```sh
./dev/openssl-win.sh
./dev/win.sh
./dev/tls_smoke.sh
```

`./dev/win.sh` links the static `libssl.a`/`libcrypto.a` automatically when that
directory exists (set `OPENSSL_DIR` to use another tree), so the binaries still
need no MinGW DLLs. `./dev/tls_smoke.sh` starts `http_serve_ok.exe` and requests
`https://127.0.0.1:18443/` with curl.

mbedTLS is not used for the Windows target: nixpkgs enables
`MBEDTLS_THREADING_PTHREAD` (the upstream config has threading off), and s2n-tls
has no Windows backend at all.

### Sanitizers (ASAN/UBSAN) for the Windows build

The nixpkgs clang for mingw ships no `libclang_rt.asan`, so a sanitizer build
needs [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (same LLVM major):
its UCRT x86_64 tree provides the ASAN and UBSAN runtimes. `./dev/win-asan.sh`
downloads it into `.build/deps/llvm-mingw`, builds `.build-win-san` with
`-fsanitize=address,undefined -fno-sanitize-recover=undefined`, copies
`libclang_rt.asan_dynamic-x86_64.dll`, `libc++.dll` and `libunwind.dll` next to
`test.exe` and runs the suite:

```sh
./dev/win-asan.sh
```

The shipping build stays on nixpkgs clang. ASAN on Windows only exists as a
dynamic runtime and has no LeakSanitizer; ThreadSanitizer is not supported on
Windows at all, so races stay covered by the Linux run.

### Stack traces

| Header | Library | Used in |
|--------|---------|---------|
| `<execinfo.h>` | libc (glibc/macOS) | `std/tst/ctx.cpp` — `backtrace()` fallback |
| `<cpptrace/cpptrace.hpp>` | cpptrace | `tst/test.cpp` — pretty stack traces in test runner |
