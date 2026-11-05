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

### Stack traces

| Header | Library | Used in |
|--------|---------|---------|
| `<execinfo.h>` | libc (glibc/macOS) | `std/tst/ctx.cpp` — `backtrace()` fallback |
| `<cpptrace/cpptrace.hpp>` | cpptrace | `tst/test.cpp` — pretty stack traces in test runner |
