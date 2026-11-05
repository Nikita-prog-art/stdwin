#pragma once

// Internal platform surface. Included from .cpp files only: the public headers
// under std/*/*.h stay free of platform includes.

#include <std/sys/types.h>

#include <errno.h>

#if defined(_WIN32)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #include <io.h>
    #include <fcntl.h>
    #include <malloc.h>
    #include <direct.h>
    #include <stdio.h>
    #include <stdlib.h>

    #ifndef O_RDONLY
        #define O_RDONLY _O_RDONLY
    #endif

    #ifndef O_WRONLY
        #define O_WRONLY _O_WRONLY
    #endif

    #ifndef O_RDWR
        #define O_RDWR _O_RDWR
    #endif

    #ifndef O_CREAT
        #define O_CREAT _O_CREAT
    #endif

    #ifndef O_TRUNC
        #define O_TRUNC _O_TRUNC
    #endif

    #ifndef O_APPEND
        #define O_APPEND _O_APPEND
    #endif

    #ifndef MSG_NOSIGNAL
        #define MSG_NOSIGNAL 0
    #endif

    #ifndef SHUT_RD
        #define SHUT_RD SD_RECEIVE
        #define SHUT_WR SD_SEND
        #define SHUT_RDWR SD_BOTH
    #endif

    struct iovec {
        void* iov_base;
        size_t iov_len;
    };
#else
    #include <fcntl.h>
    #include <poll.h>
    #include <sched.h>
    #include <pthread.h>
    #include <alloca.h>
    #include <unistd.h>
    #include <sys/uio.h>
    #include <sys/stat.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <arpa/inet.h>
    #include <netdb.h>
#endif

namespace stl {
    // poll() shim: WSAPoll on Windows, poll() elsewhere
    struct StdPollFD {
        int fd;
        short events;
        short revents;
    };

#if defined(_WIN32)
    constexpr short stdPollIn = POLLIN;
    constexpr short stdPollOut = POLLOUT;
#else
    constexpr short stdPollIn = POLLIN;
    constexpr short stdPollOut = POLLOUT;
#endif

    int stdPoll(StdPollFD* fds, unsigned count, int timeoutMs);

    // memory / string
    const void* stdMemMem(const void* hay, size_t hayLen, const void* needle, size_t needleLen) noexcept;

    // process / time
    u32 stdPageSize() noexcept;
    void stdYield() noexcept;

    // file descriptors. On Windows a socket descriptor carries a tag; the
    // functions below dispatch on it, so callers keep passing plain ints.
    bool stdIsSocket(int fd) noexcept;

    int stdOpen(const char* path, int flags, int mode = 0644);
    int stdUnlink(const char* path);
    int stdMkdir(const char* path);
    int stdRmdir(const char* path);
    int stdPipe(int fds[2]);
    off_t stdSeek(int fd, off_t off, int whence);

    ssize_t stdRead(int fd, void* buf, size_t len);
    ssize_t stdWrite(int fd, const void* buf, size_t len);
    ssize_t stdReadV(int fd, iovec* iov, size_t count);
    ssize_t stdWriteV(int fd, iovec* iov, size_t count);
    int stdClose(int fd) noexcept;
    int stdFsync(int fd) noexcept;
    int stdFdatasync(int fd) noexcept;
    int stdSetNonBlocking(int fd);
    int stdSetBlocking(int fd);
    ssize_t stdPread(int fd, void* buf, size_t len, off_t off);
    ssize_t stdPwrite(int fd, const void* buf, size_t len, off_t off);

    // sockets. Returned descriptors are non-blocking, close-on-exec, and (on
    // Windows) tagged as sockets.
    int stdSocket(int domain, int type, int protocol);
    int stdAccept(int fd, sockaddr* addr, socklen_t* addrLen);
    int stdBind(int fd, const sockaddr* addr, socklen_t addrLen);
    int stdListen(int fd, int backlog);
    int stdConnect(int fd, const sockaddr* addr, socklen_t addrLen);
    int stdShutdown(int fd, int how);
    int stdSetSockOpt(int fd, int level, int name, const void* val, socklen_t len);
    int stdGetSockOpt(int fd, int level, int name, void* val, socklen_t* len);
    ssize_t stdRecv(int fd, void* buf, size_t len, int flags);
    ssize_t stdSend(int fd, const void* buf, size_t len, int flags);
    ssize_t stdRecvFrom(int fd, void* buf, size_t len, int flags, sockaddr* addr, socklen_t* addrLen);
    ssize_t stdSendTo(int fd, const void* buf, size_t len, int flags, const sockaddr* addr, socklen_t addrLen);

    void stdWsaInit() noexcept;

    // native socket value <-> library descriptor. Identity outside Windows;
    // on Windows the library descriptor carries the socket tag.
    int stdFromNativeSocket(u64 native) noexcept;
    u64 stdToNativeSocket(int fd) noexcept;

#if defined(_WIN32)
    SOCKET stdNativeSocket(int fd) noexcept;
    int stdMapWin32Error(unsigned long code) noexcept;

    // UTF-8 (the library's internal encoding) -> UTF-16 at the Win32 boundary
    wchar_t* stdToWide(const char* src, size_t len);
    size_t stdFromWide(const wchar_t* src, char* dst, size_t cap) noexcept;
    void stdFreeWide(wchar_t* ptr) noexcept;

    // switches the current thread to [stackLimit, stackTop) and runs fn(arg)
    // there; the stack bounds are published in the TEB so SEH unwinding works.
    // The thread is exited afterwards, the function never returns.
    using StdThreadFn = void (*)(void*);
    void stdRunOnStack(void* stackTop, void* stackLimit, StdThreadFn fn, void* arg);

    #ifndef setenv
        inline int setenv(const char* name, const char* value, int overwrite) {
            (void)overwrite;

            return _putenv_s(name, value);
        }

        inline int unsetenv(const char* name) {
            return _putenv_s(name, "");
        }
    #endif
#endif
}
