# Windows backend

Design notes for the native Win32 port (`_WIN32`, x86_64, UCRT): why the
platform layer looks the way it does, and which POSIX behaviours Windows cannot
provide. Build and test instructions live in [BUILD.md](BUILD.md).

This document replaces the earlier `PLAN-WINDOWS.md`. The plan itself — the
porting audit, the comparison with an MSYS2-runtime build, the phase estimates —
stays in git history; only the decisions that still describe the code are kept
here.

## Why native Win32

Two paths were considered: the native Win32 target (mingw-w64, UCRT) and MSYS2's
runtime (`msys-2.0.dll`, a Cygwin fork that gives POSIX semantics on top of
Windows).

The runtime path was rejected as a shipping target. A binary would depend on
`msys-2.0.dll`, which end users do not have; every I/O call would cross a
translation layer, `poll` there is emulated over `select`, and there is no
`epoll`. It also would not exercise anything this port has to solve — the
descriptor model, `WSAPoll`, `WSAStartup`, the UTF-16 boundary. The native port
costs more work in the platform layer and produces a binary that needs only the
system UCRT and `WS2_32.dll`.

The minimum target is Windows 10 x64 (`_WIN32_WINNT=0x0A00` in `build.py`):
`WSAPoll` and the UCRT require it. Windows ARM64 is not supported — the context
switch has a Win64 x86_64 branch only, and the build target is x86_64.

## Where the platform code lives

`std/sys/plat.h` + `std/sys/plat.cpp` are the only files that include Win32
headers, and they are included from `.cpp` files only. Public headers under
`std/*/*.h` stay platform-neutral; the remaining `#ifdef _WIN32` blocks are
local to `.cpp` files.

`plat.h` exposes a POSIX-shaped surface (`iovec`, `SHUT_*`, `MSG_NOSIGNAL`,
`stdOpen`/`stdRead`/`stdSend`, ...), so `net/`, `dns/` and `ios/` keep a single
implementation. The policy — `EAGAIN` loops, deadlines, buffering — is shared;
only the syscall underneath is platform-specific.

## Decisions

### D1. Descriptors: a tag in the `int`

On POSIX an `int` covers files, pipes and sockets. On Windows a CRT descriptor
and a `SOCKET` are separate namespaces with different close/read/write calls,
and the same small number can denote both at once. A table keyed by number is
not viable: numbers are reused by `_open` and by the kernel, and some closes
bypass the library, so the cache goes stale and lies.

The library descriptor therefore carries a bit: `socket | 0x40000000` (bit 30,
free because Windows handles are 32-bit indexes). `stdIsSocket()` is a bit test,
`nativeSocket()` strips the tag at the Win32 boundary, and every descriptor
created by the library goes through one factory. On POSIX both functions are
identity. Debug asserts pin the invariant down: a handle must not reuse bits
30/31, and `INVALID_SOCKET` maps to `-1`.

The cost is one `and`/`or` per syscall (never per byte) — below noise next to a
Winsock call.

### D2. eventfd and socketpair

`EventFD` and `JoinPipe` must be pollable. Windows pipes are not: `WSAPoll` and
`select` accept sockets only, and an anonymous pipe cannot be made
non-blocking (`ioctlsocket` is for sockets, `PIPE_NOWAIT` for named pipes).
`createSocketPairFD()` therefore builds a pair of connected datagram sockets on
loopback: `EventFD::signal()` sends one byte, `drain()` reads until
`EWOULDBLOCK`; `JoinPipe` uses the same pair. `stdSetNonBlocking()` on a
non-socket returns `ENOTSUP` instead of pretending to work.

A UDP pair is cheaper than a TCP pair (no listen/accept) and is pollable by
every backend.

### D3. Reactor backend: `WSAPoll`

`PollPoller` is the only backend on Windows; `WaitablePoller::create()` returns
nullptr, so `HybridPoller` falls back to it. The backend enforces what
`WSAPoll` requires:

- only sockets may be armed — a pipe or a file in the array fails the whole
  call with `WSAENOTSOCK`, so `arm()` asserts;
- entries with `fd < 0` (the "just sleep" case) are filtered before the call;
- after a writable `connect` the code still reads `SO_ERROR`: `WSAPoll` does
  not report a failed non-blocking connect reliably;
- `WSAPoll` is O(n) per wait, which caps a server at hundreds to thousands of
  connections. IOCP would lift the cap and is not implemented.

### D4. Context switch

Windows x64 is not the SysV ABI: callee-saved state is `rbx`, `rbp`, `rdi`,
`rsi`, `r12`–`r15` plus `xmm6`–`xmm15`, `MXCSR` and the x87 control word, and
the first two arguments arrive in `rcx`/`rdx`. `std/thr/context.cpp` has a
separate Win64 branch that saves exactly that frame and a naked trampoline.

Two Windows-specific details matter:

- there is no red zone, so the asm does not rely on one;
- the coroutine stack bounds are published in the TEB, otherwise the SEH
  dispatcher rejects frames while unwinding. A thrown exception inside a
  coroutine is what `context_ut` checks.

ASAN fiber hooks are used when the build is instrumented; the llvm-mingw
runtime provides them (see [BUILD.md](BUILD.md)).

### D5. File I/O

Files stay on the CRT: `_read`/`_write`/`_close`, `_commit` for `fsync`. The CRT
has no positional reads, so `pread`/`pwrite` are emulated with `_lseeki64` plus
`_read`/`_write` under a process-wide `SRWLOCK`.

Every open and pipe goes through `stdOpen`/`stdPipe`, which always add
`_O_BINARY` (otherwise the CRT translates CRLF and swallows `0x1A`) and
`_O_NOINHERIT`. This is a single choke point on purpose: one forgotten flag
would silently corrupt binary data.

### D6. Paths

Strings inside the library are UTF-8. At the Win32 boundary they are converted
to UTF-16 (`MultiByteToWideChar(CP_UTF8)`) and passed to `_wopen`,
`FindFirstFileW`, `CreateFileW`. The ANSI variants break on non-ASCII paths,
which is a correctness issue, not a nicety.

### D7. `errno` vs Winsock codes

Winsock reports errors through `WSAGetLastError()`. The wrappers in `plat.cpp`
map them (`WSAEWOULDBLOCK` → `EAGAIN`, `WSAEINPROGRESS` → `EINPROGRESS`,
`WSAECONNRESET` → `ECONNRESET`, unknown → `EIO`), set `errno` and return `-1`.
The shared code (`if (errno != EAGAIN) return errno;`) then works unchanged.

### D8. `iovec`

Windows has no `struct iovec`, so `plat.h` defines it and `stdReadV`/
`stdWriteV` convert to `WSABUF` (note the reversed field order), 16 buffers per
`WSARecv`/`WSASend` call.

### D9. `WSAStartup`

One lazy call from `stdWsaInit()` through a function-local static.
`WSACleanup` is not called: process exit releases the resources.

### D10. Public API

No signature changes. The tag lives in the value, `nativeSocket()` is applied
only inside `plat.cpp`, and nothing under `std/*/*.h` mentions Win32.

## Limits

Properties of the platform, documented in the API rather than emulated:

- `recvmsg`/`sendmsg`/`recvmmsg` and control messages return `ENOTSUP`. Windows
  does have `WSARecvMsg`/`WSASendMsg`, but there is no POSIX `msghdr`, no
  `SCM_RIGHTS`, and no UDP GRO; the public API takes POSIX `msghdr`, so a shim
  would be a different API with no callers.
- Anonymous pipes are neither pollable nor non-blocking; only sockets are.
- No IOCP: the poller is O(n) and limits large connection counts.
- No ARM64 build.
- TLS is server-side only (`SslCtx::create` builds a `TLS_server_method`
  context), so checking it needs an external client. On Windows it is built by
  `dev/openssl-win.sh`: nixpkgs' `openssl` package also builds `openssl.exe`,
  whose resource script fails in `windres`. s2n-tls has no Windows backend;
  mbedTLS would work only with its pthread layer off.
- The sanitizer run uses llvm-mingw (`dev/win-asan.sh`): the nixpkgs clang for
  mingw ships no `libclang_rt.asan`. LeakSanitizer and ThreadSanitizer are
  unavailable on Windows, so leaks and races stay covered by the Linux run.
- Stack traces have no Windows backend (`std/tst/ctx.cpp` uses `execinfo.h`
  when it exists).

## POSIX → Win32

| POSIX | Win32 / MinGW | Comment |
|---|---|---|
| `open/read/write/close` | `_wopen/_read/_write/_close` | `_O_BINARY` + `_O_NOINHERIT`; UTF-16 on entry |
| `pread/pwrite` | `_lseeki64` + `_read/_write` | process-wide SRWLOCK, no positional CRT call |
| `fsync/fdatasync` | `_commit` | |
| `pipe()` | `_pipe(..., 64 KiB, _O_BINARY)` | not pollable, not non-blocking |
| `socket/closesocket` | `socket`/`closesocket` | `SOCKET` ≠ CRT fd, hence the tag |
| `fcntl(F_SETFL, O_NONBLOCK)` | `ioctlsocket(FIONBIO)` | sockets only |
| `poll()` | `WSAPoll` | sockets only |
| `epoll`/`kqueue` | — | `PollPoller`; IOCP not implemented |
| `accept4` | `accept` | |
| `connect` + `EINPROGRESS` | `connect` + `WSAEWOULDBLOCK`, then `SO_ERROR` | |
| `shutdown` | `SD_RECEIVE/SD_SEND/SD_BOTH` | |
| `MSG_NOSIGNAL` | `0` | Windows has no `SIGPIPE` |
| `readv/writev` | `WSARecv/WSASend` (`WSABUF`, 16 at a time) | data path |
| `recvmsg/sendmsg/recvmmsg` | `ENOTSUP` | see Limits |
| `getaddrinfo` | `getaddrinfo` + lazy `WSAStartup` | |
| `opendir/readdir` | `FindFirstFileW`/`FindNextFileW` | UTF-16 |
| `memfd_create` | `CreateFileW(FILE_FLAG_DELETE_ON_CLOSE)` | |
| `eventfd` | connected UDP pair on loopback | must be pollable |
| `pthread_*` | `CreateThread`, `SRWLOCK`, `CONDITION_VARIABLE`, `CreateSemaphore` | no winpthreads |
| `sched_yield` | `SwitchToThread` | |
| `clock_gettime` | `QueryPerformanceCounter` | |
| `sysconf(_SC_PAGESIZE)` | `GetSystemInfo().dwPageSize` | |
| `sysconf(_SC_NPROCESSORS_ONLN)` | `GetActiveProcessorCount` | |
| `alloca` | `<malloc.h>` | |
| `memmem` | `stdMemMem` | shared implementation |
| `setenv/unsetenv` | `_putenv_s` | tests only |

## Keeping the platform layer contained

1. `plat.h` is the only header with Win32 includes; it never reaches a public
   header.
2. `plat.h` substitutes a POSIX surface, not the Windows API. That is what
   keeps `net/`, `dns/` and `ios/` free of branches.
3. Shared policy, platform primitive. `EAGAIN` loops, deadlines and buffering
   have one implementation; only the syscall differs.
4. No platform decisions in the contract. "The poller takes sockets only" is a
   property of the Windows backend; `ENOTSUP` is a property of the platform.
   Neither changes `PollerIface` or the meaning of an error.
5. Prefer positive changes over branches: `memmem` and page size moved into
   shared helpers instead of growing more `#ifdef`.
6. Guard against `#ifdef` creep. The check worth adding to `style.py` is a ban
   on platform macros in the portable directories (`std/alg`, `std/lib`,
   `std/str`, `std/sym`, `std/map`, `std/ptr`, `std/rng`, `std/typ`) plus a
   budget on guarded lines. It is not implemented yet.
7. The real risk is the Linux path breaking silently on a change to `plat.cpp`
   or `fd.cpp`. Runs are local today (no CI job); a platform matrix is the
   cheapest protection if that changes.
