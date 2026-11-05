#include "plat.h"

#include <std/dbg/insist.h>

#include <errno.h>
#include <string.h>

#if defined(_WIN32)
    #include <sys/stat.h>
#else
    #include <stdlib.h>
    #include <string.h>
#endif

using namespace stl;

namespace {
#if defined(_WIN32)
    constexpr int SocketTag = 0x40000000;
    constexpr int SocketMask = ~SocketTag;

    void wsaInitOnce() {
        static const bool ok = [] {
            WSADATA data;

            return WSAStartup(MAKEWORD(2, 2), &data) == 0;
        }();

        STD_INSIST(ok);
    }

    int mapWsa(int code) noexcept {
        switch (code) {
            case WSAEWOULDBLOCK: {
                return EAGAIN;
            }

            case WSAEINPROGRESS: {
                return EINPROGRESS;
            }

            case WSAEALREADY: {
                return EALREADY;
            }

            case WSAEINTR: {
                return EINTR;
            }

            case WSAEBADF: {
                return EBADF;
            }

            case WSAEACCES: {
                return EACCES;
            }

            case WSAEFAULT: {
                return EFAULT;
            }

            case WSAEINVAL: {
                return EINVAL;
            }

            case WSAEMFILE: {
                return EMFILE;
            }

            case WSAENOTSOCK: {
                return ENOTSOCK;
            }

            case WSAEADDRINUSE: {
                return EADDRINUSE;
            }

            case WSAEADDRNOTAVAIL: {
                return EADDRNOTAVAIL;
            }

            case WSAEAFNOSUPPORT: {
                return EAFNOSUPPORT;
            }

            case WSAECONNRESET: {
                return ECONNRESET;
            }

            case WSAECONNREFUSED: {
                return ECONNREFUSED;
            }

            case WSAECONNABORTED: {
                return ECONNABORTED;
            }

            case WSAENOTCONN: {
                return ENOTCONN;
            }

            case WSAEISCONN: {
                return EISCONN;
            }

            case WSAENETUNREACH: {
                return ENETUNREACH;
            }

            case WSAENETDOWN: {
                return ENETDOWN;
            }

            case WSAEHOSTUNREACH: {
                return EHOSTUNREACH;
            }

            case WSAETIMEDOUT: {
                return ETIMEDOUT;
            }

            case WSAEMSGSIZE: {
                return EMSGSIZE;
            }

            case WSAENOBUFS: {
                return ENOBUFS;
            }

            case WSAEPROTONOSUPPORT: {
                return EPROTONOSUPPORT;
            }

            case WSAESHUTDOWN: {
                return EPIPE;
            }

            default: {
                return EIO;
            }
        }
    }

    void setWsaErrno() noexcept {
        errno = mapWsa(WSAGetLastError());
    }

    SOCKET nativeSocket(int fd) noexcept {
        return (SOCKET)(fd & SocketMask);
    }

    int tagSocket(SOCKET s) noexcept {
        if (s == INVALID_SOCKET) {
            return -1;
        }

        auto value = (uintptr_t)s;

        STD_INSIST((value & (uintptr_t)SocketTag) == 0);
        STD_INSIST((value & 0x80000000u) == 0);

        return (int)(value | (uintptr_t)SocketTag);
    }

    bool makeNonBlocking(SOCKET s) noexcept {
        u_long one = 1;

        return ::ioctlsocket(s, FIONBIO, &one) == 0;
    }

    bool makeBlocking(SOCKET s) noexcept {
        u_long zero = 0;

        return ::ioctlsocket(s, FIONBIO, &zero) == 0;
    }
#else
    int setNonBlockingFlags(int fd) noexcept {
        int flags = ::fcntl(fd, F_GETFL);

        if (flags < 0) {
            return -1;
        }

        return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    int setBlockingFlags(int fd) noexcept {
        int flags = ::fcntl(fd, F_GETFL);

        if (flags < 0) {
            return -1;
        }

        return ::fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    }
#endif
}

#if defined(_WIN32)
SOCKET stl::stdNativeSocket(int fd) noexcept {
    return nativeSocket(fd);
}
#endif

bool stl::stdIsSocket(int fd) noexcept {
#if defined(_WIN32)
    return fd >= 0 && (fd & SocketTag) != 0;
#else
    (void)fd;

    return false;
#endif
}

void stl::stdWsaInit() noexcept {
#if defined(_WIN32)
    wsaInitOnce();
#endif
}

int stl::stdPoll(StdPollFD* fds, unsigned count, int timeoutMs) {
#if defined(_WIN32)
    WSAPOLLFD raw[64];
    unsigned n = 0;

    for (unsigned i = 0; i < count && n < 64; ++i) {
        fds[i].revents = 0;

        if (fds[i].fd < 0) {
            continue;
        }

        raw[n].fd = nativeSocket(fds[i].fd);
        raw[n].events = fds[i].events;
        raw[n].revents = 0;
        ++n;
    }

    if (n == 0) {
        return 0;
    }

    int r = ::WSAPoll(raw, n, timeoutMs);

    if (r == SOCKET_ERROR) {
        setWsaErrno();

        return -1;
    }

    unsigned j = 0;

    for (unsigned i = 0; i < count && j < n; ++i) {
        if (fds[i].fd < 0) {
            continue;
        }

        fds[i].revents = raw[j].revents;
        ++j;
    }

    return r;
#else
    struct pollfd raw[64];
    unsigned n = 0;

    for (unsigned i = 0; i < count && n < 64; ++i) {
        fds[i].revents = 0;

        if (fds[i].fd < 0) {
            continue;
        }

        raw[n].fd = fds[i].fd;
        raw[n].events = fds[i].events;
        raw[n].revents = 0;
        ++n;
    }

    if (n == 0) {
        return 0;
    }

    int r = ::poll(raw, n, timeoutMs);

    if (r < 0) {
        return -1;
    }

    unsigned j = 0;

    for (unsigned i = 0; i < count && j < n; ++i) {
        if (fds[i].fd < 0) {
            continue;
        }

        fds[i].revents = raw[j].revents;
        ++j;
    }

    return r;
#endif
}

const void* stl::stdMemMem(const void* hay, size_t hayLen, const void* needle, size_t needleLen) noexcept {
    if (needleLen == 0) {
        return hay;
    }

    if (needleLen > hayLen) {
        return nullptr;
    }

    auto h = (const u8*)hay;
    auto n = (const u8*)needle;
    const size_t last = hayLen - needleLen;

    for (size_t i = 0; i <= last; ++i) {
        if (h[i] == n[0] && memcmp(h + i, n, needleLen) == 0) {
            return h + i;
        }
    }

    return nullptr;
}

u32 stl::stdPageSize() noexcept {
#if defined(_WIN32)
    SYSTEM_INFO info;

    GetSystemInfo(&info);

    return (u32)info.dwPageSize;
#else
    long size = ::sysconf(_SC_PAGESIZE);

    return size > 0 ? (u32)size : 4096;
#endif
}

void stl::stdYield() noexcept {
#if defined(_WIN32)
    SwitchToThread();
#else
    ::sched_yield();
#endif
}

int stl::stdOpen(const char* path, int flags, int mode) {
#if defined(_WIN32)
    wchar_t* wide = stdToWide(path, strlen(path));

    if (!wide) {
        errno = EINVAL;

        return -1;
    }

    int fd = ::_wopen(wide, flags | _O_BINARY | _O_NOINHERIT, mode);

    stdFreeWide(wide);

    return fd;
#else
    return ::open(path, flags, mode);
#endif
}

int stl::stdPipe(int fds[2]) {
#if defined(_WIN32)
    return ::_pipe(fds, 65536, _O_BINARY | _O_NOINHERIT);
#else
    return ::pipe(fds);
#endif
}

off_t stl::stdSeek(int fd, off_t off, int whence) {
#if defined(_WIN32)
    return (off_t)::_lseeki64(fd, off, whence);
#else
    return ::lseek(fd, off, whence);
#endif
}

ssize_t stl::stdRead(int fd, void* buf, size_t len) {
#if defined(_WIN32)
    if (stdIsSocket(fd)) {
        return stdRecv(fd, buf, len, 0);
    }

    unsigned chunk = len > 0x7ffff000u ? 0x7ffff000u : (unsigned)len;

    return ::_read(fd, buf, chunk);
#else
    return ::read(fd, buf, len);
#endif
}

ssize_t stl::stdWrite(int fd, const void* buf, size_t len) {
#if defined(_WIN32)
    if (stdIsSocket(fd)) {
        return stdSend(fd, buf, len, 0);
    }

    unsigned chunk = len > 0x7ffff000u ? 0x7ffff000u : (unsigned)len;

    return ::_write(fd, buf, chunk);
#else
    return ::write(fd, buf, len);
#endif
}

ssize_t stl::stdReadV(int fd, iovec* iov, size_t count) {
#if defined(_WIN32)
    if (!stdIsSocket(fd)) {
        size_t total = 0;

        for (size_t i = 0; i < count; ++i) {
            ssize_t n = stdRead(fd, iov[i].iov_base, iov[i].iov_len);

            if (n < 0) {
                return total ? (ssize_t)total : -1;
            }

            total += (size_t)n;

            if ((size_t)n < iov[i].iov_len) {
                break;
            }
        }

        return (ssize_t)total;
    }

    SOCKET s = nativeSocket(fd);
    size_t total = 0;

    while (count > 0) {
        DWORD batch = count > 16 ? 16 : (DWORD)count;
        WSABUF bufs[16];

        for (DWORD i = 0; i < batch; ++i) {
            bufs[i].buf = (CHAR*)iov[i].iov_base;
            bufs[i].len = (ULONG)iov[i].iov_len;
        }

        DWORD received = 0;
        DWORD flags = 0;

        if (::WSARecv(s, bufs, batch, &received, &flags, nullptr, nullptr) == SOCKET_ERROR) {
            if (total) {
                return (ssize_t)total;
            }

            setWsaErrno();

            return -1;
        }

        total += received;

        size_t consumed = 0;

        for (DWORD i = 0; i < batch; ++i) {
            if ((size_t)bufs[i].len >= iov[i].iov_len) {
                consumed = i + 1;
            } else {
                break;
            }
        }

        if (consumed == 0) {
            return (ssize_t)total;
        }

        iov += consumed;
        count -= consumed;
    }

    return (ssize_t)total;
#else
    return ::readv(fd, iov, (int)count);
#endif
}

ssize_t stl::stdWriteV(int fd, iovec* iov, size_t count) {
#if defined(_WIN32)
    if (!stdIsSocket(fd)) {
        size_t total = 0;

        for (size_t i = 0; i < count; ++i) {
            ssize_t n = stdWrite(fd, iov[i].iov_base, iov[i].iov_len);

            if (n < 0) {
                return total ? (ssize_t)total : -1;
            }

            total += (size_t)n;

            if ((size_t)n < iov[i].iov_len) {
                break;
            }
        }

        return (ssize_t)total;
    }

    SOCKET s = nativeSocket(fd);
    size_t total = 0;

    while (count > 0) {
        DWORD batch = count > 16 ? 16 : (DWORD)count;
        WSABUF bufs[16];
        size_t batchLen = 0;

        for (DWORD i = 0; i < batch; ++i) {
            bufs[i].buf = (CHAR*)iov[i].iov_base;
            bufs[i].len = (ULONG)iov[i].iov_len;
            batchLen += iov[i].iov_len;
        }

        DWORD sent = 0;

        if (::WSASend(s, bufs, batch, &sent, 0, nullptr, nullptr) == SOCKET_ERROR) {
            if (total) {
                return (ssize_t)total;
            }

            setWsaErrno();

            return -1;
        }

        total += sent;

        if (sent < batchLen) {
            break;
        }

        iov += batch;
        count -= batch;
    }

    return (ssize_t)total;
#else
    return ::writev(fd, iov, (int)count);
#endif
}

int stl::stdClose(int fd) noexcept {
#if defined(_WIN32)
    if (fd < 0) {
        return 0;
    }

    if (stdIsSocket(fd)) {
        if (::closesocket(nativeSocket(fd)) == SOCKET_ERROR) {
            setWsaErrno();

            return -1;
        }

        return 0;
    }

    return ::_close(fd);
#else
    return ::close(fd);
#endif
}

int stl::stdFsync(int fd) noexcept {
#if defined(_WIN32)
    if (stdIsSocket(fd)) {
        errno = ENOTSUP;

        return -1;
    }

    return ::_commit(fd);
#else
    return ::fsync(fd);
#endif
}

int stl::stdFdatasync(int fd) noexcept {
#if defined(_WIN32)
    if (stdIsSocket(fd)) {
        errno = ENOTSUP;

        return -1;
    }

    return ::_commit(fd);
#elif defined(__APPLE__)
    return ::fcntl(fd, F_FULLFSYNC);
#else
    return ::fdatasync(fd);
#endif
}

int stl::stdSetNonBlocking(int fd) {
#if defined(_WIN32)
    if (!stdIsSocket(fd)) {
        errno = ENOTSUP;

        return -1;
    }

    if (!makeNonBlocking(nativeSocket(fd))) {
        setWsaErrno();

        return -1;
    }

    return 0;
#else
    return setNonBlockingFlags(fd);
#endif
}

int stl::stdSetBlocking(int fd) {
#if defined(_WIN32)
    if (!stdIsSocket(fd)) {
        errno = ENOTSUP;

        return -1;
    }

    if (!makeBlocking(nativeSocket(fd))) {
        setWsaErrno();

        return -1;
    }

    return 0;
#else
    return setBlockingFlags(fd);
#endif
}

ssize_t stl::stdPread(int fd, void* buf, size_t len, off_t off) {
#if defined(_WIN32)
    static SRWLOCK lock = SRWLOCK_INIT;

    AcquireSRWLockExclusive(&lock);

    __int64 saved = ::_lseeki64(fd, 0, SEEK_CUR);
    int result = -1;

    if (saved >= 0 && ::_lseeki64(fd, off, SEEK_SET) >= 0) {
        unsigned chunk = len > 0x7ffff000u ? 0x7ffff000u : (unsigned)len;

        result = ::_read(fd, buf, chunk);

        if (result < 0) {
            int err = errno;

            ::_lseeki64(fd, saved, SEEK_SET);
            ReleaseSRWLockExclusive(&lock);
            errno = err;

            return -1;
        }
    }

    ::_lseeki64(fd, saved, SEEK_SET);
    ReleaseSRWLockExclusive(&lock);

    return result;
#else
    return ::pread(fd, buf, len, off);
#endif
}

ssize_t stl::stdPwrite(int fd, const void* buf, size_t len, off_t off) {
#if defined(_WIN32)
    static SRWLOCK lock = SRWLOCK_INIT;

    AcquireSRWLockExclusive(&lock);

    __int64 saved = ::_lseeki64(fd, 0, SEEK_CUR);
    int result = -1;

    if (saved >= 0 && ::_lseeki64(fd, off, SEEK_SET) >= 0) {
        unsigned chunk = len > 0x7ffff000u ? 0x7ffff000u : (unsigned)len;

        result = ::_write(fd, buf, chunk);

        if (result < 0) {
            int err = errno;

            ::_lseeki64(fd, saved, SEEK_SET);
            ReleaseSRWLockExclusive(&lock);
            errno = err;

            return -1;
        }
    }

    ::_lseeki64(fd, saved, SEEK_SET);
    ReleaseSRWLockExclusive(&lock);

    return result;
#else
    return ::pwrite(fd, buf, len, off);
#endif
}

int stl::stdSocket(int domain, int type, int protocol) {
#if defined(_WIN32)
    wsaInitOnce();

    SOCKET s = ::socket(domain, type, protocol);

    if (s == INVALID_SOCKET) {
        setWsaErrno();

        return -1;
    }

    if (!makeNonBlocking(s)) {
        setWsaErrno();
        ::closesocket(s);

        return -1;
    }

    return tagSocket(s);
#else
    int fd = ::socket(domain, type, protocol);

    if (fd < 0) {
        return -1;
    }

    if (setNonBlockingFlags(fd) < 0) {
        int err = errno;

        ::close(fd);
        errno = err;

        return -1;
    }

    #if defined(FD_CLOEXEC)
    ::fcntl(fd, F_SETFD, ::fcntl(fd, F_GETFD) | FD_CLOEXEC);
    #endif

    return fd;
#endif
}

int stl::stdAccept(int fd, sockaddr* addr, socklen_t* addrLen) {
#if defined(_WIN32)
    SOCKET s = ::accept(nativeSocket(fd), addr, (int*)addrLen);

    if (s == INVALID_SOCKET) {
        setWsaErrno();

        return -1;
    }

    if (!makeNonBlocking(s)) {
        setWsaErrno();
        ::closesocket(s);

        return -1;
    }

    return tagSocket(s);
#elif defined(__linux__)
    return ::accept4(fd, addr, addrLen, SOCK_NONBLOCK | SOCK_CLOEXEC);
#else
    return ::accept(fd, addr, addrLen);
#endif
}

int stl::stdBind(int fd, const sockaddr* addr, socklen_t addrLen) {
#if defined(_WIN32)
    if (::bind(nativeSocket(fd), addr, (int)addrLen) == SOCKET_ERROR) {
        setWsaErrno();

        return -1;
    }

    return 0;
#else
    return ::bind(fd, addr, addrLen);
#endif
}

int stl::stdListen(int fd, int backlog) {
#if defined(_WIN32)
    if (::listen(nativeSocket(fd), backlog) == SOCKET_ERROR) {
        setWsaErrno();

        return -1;
    }

    return 0;
#else
    return ::listen(fd, backlog);
#endif
}

int stl::stdConnect(int fd, const sockaddr* addr, socklen_t addrLen) {
#if defined(_WIN32)
    if (::connect(nativeSocket(fd), addr, (int)addrLen) == 0) {
        return 0;
    }

    int code = WSAGetLastError();

    if (code == WSAEWOULDBLOCK || code == WSAEINPROGRESS || code == WSAEALREADY) {
        errno = EINPROGRESS;

        return -1;
    }

    errno = mapWsa(code);

    return -1;
#else
    return ::connect(fd, addr, addrLen);
#endif
}

int stl::stdShutdown(int fd, int how) {
#if defined(_WIN32)
    if (::shutdown(nativeSocket(fd), how) == SOCKET_ERROR) {
        setWsaErrno();

        return -1;
    }

    return 0;
#else
    return ::shutdown(fd, how);
#endif
}

int stl::stdSetSockOpt(int fd, int level, int name, const void* val, socklen_t len) {
#if defined(_WIN32)
    if (::setsockopt(nativeSocket(fd), level, name, (const char*)val, (int)len) == SOCKET_ERROR) {
        setWsaErrno();

        return -1;
    }

    return 0;
#else
    return ::setsockopt(fd, level, name, val, len);
#endif
}

int stl::stdGetSockOpt(int fd, int level, int name, void* val, socklen_t* len) {
#if defined(_WIN32)
    if (::getsockopt(nativeSocket(fd), level, name, (char*)val, (int*)len) == SOCKET_ERROR) {
        setWsaErrno();

        return -1;
    }

    return 0;
#else
    return ::getsockopt(fd, level, name, val, len);
#endif
}

ssize_t stl::stdRecv(int fd, void* buf, size_t len, int flags) {
#if defined(_WIN32)
    unsigned chunk = len > 0x7fffffffu ? 0x7fffffffu : (unsigned)len;

    int r = ::recv(nativeSocket(fd), (char*)buf, chunk, flags);

    if (r == SOCKET_ERROR) {
        setWsaErrno();

        return -1;
    }

    return r;
#else
    return ::recv(fd, buf, len, flags);
#endif
}

ssize_t stl::stdSend(int fd, const void* buf, size_t len, int flags) {
#if defined(_WIN32)
    unsigned chunk = len > 0x7fffffffu ? 0x7fffffffu : (unsigned)len;

    int r = ::send(nativeSocket(fd), (const char*)buf, chunk, flags);

    if (r == SOCKET_ERROR) {
        setWsaErrno();

        return -1;
    }

    return r;
#else
    return ::send(fd, buf, len, flags);
#endif
}

ssize_t stl::stdRecvFrom(int fd, void* buf, size_t len, int flags, sockaddr* addr, socklen_t* addrLen) {
#if defined(_WIN32)
    unsigned chunk = len > 0x7fffffffu ? 0x7fffffffu : (unsigned)len;

    int r = ::recvfrom(nativeSocket(fd), (char*)buf, chunk, flags, addr, (int*)addrLen);

    if (r == SOCKET_ERROR) {
        setWsaErrno();

        return -1;
    }

    return r;
#else
    return ::recvfrom(fd, buf, len, flags, addr, addrLen);
#endif
}

ssize_t stl::stdSendTo(int fd, const void* buf, size_t len, int flags, const sockaddr* addr, socklen_t addrLen) {
#if defined(_WIN32)
    unsigned chunk = len > 0x7fffffffu ? 0x7fffffffu : (unsigned)len;

    int r = ::sendto(nativeSocket(fd), (const char*)buf, chunk, flags, addr, (int)addrLen);

    if (r == SOCKET_ERROR) {
        setWsaErrno();

        return -1;
    }

    return r;
#else
    return ::sendto(fd, buf, len, flags, addr, addrLen);
#endif
}

#if defined(_WIN32)
namespace {
    constexpr unsigned long long win32ErrorToErrno(unsigned long code) noexcept {
        switch (code) {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND: {
                return ENOENT;
            }

            case ERROR_ACCESS_DENIED: {
                return EACCES;
            }

            case ERROR_SHARING_VIOLATION: {
                return EBUSY;
            }

            case ERROR_INVALID_HANDLE: {
                return EBADF;
            }

            case ERROR_NOT_ENOUGH_MEMORY:
            case ERROR_OUTOFMEMORY: {
                return ENOMEM;
            }

            case ERROR_DISK_FULL: {
                return ENOSPC;
            }

            case ERROR_DIRECTORY: {
                return ENOTDIR;
            }

            default: {
                return EIO;
            }
        }
    }
}

int stl::stdMapWin32Error(unsigned long code) noexcept {
    return (int)win32ErrorToErrno(code);
}

wchar_t* stl::stdToWide(const char* src, size_t len) {
    if (!src) {
        return nullptr;
    }

    int needed = MultiByteToWideChar(CP_UTF8, 0, src, (int)len, nullptr, 0);

    if (needed <= 0) {
        return nullptr;
    }

    auto out = (wchar_t*)malloc(((size_t)needed + 1) * sizeof(wchar_t));

    if (!out) {
        return nullptr;
    }

    if (MultiByteToWideChar(CP_UTF8, 0, src, (int)len, out, needed) != needed) {
        free(out);

        return nullptr;
    }

    out[needed] = 0;

    return out;
}

size_t stl::stdFromWide(const wchar_t* src, char* dst, size_t cap) noexcept {
    if (!src || cap == 0) {
        return 0;
    }

    int needed = WideCharToMultiByte(CP_UTF8, 0, src, -1, nullptr, 0, nullptr, nullptr);

    if (needed <= 1) {
        dst[0] = 0;

        return 0;
    }

    int written = WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, (int)cap, nullptr, nullptr);

    if (written <= 0) {
        dst[0] = 0;

        return 0;
    }

    return (size_t)(written - 1);
}

void stl::stdFreeWide(wchar_t* ptr) noexcept {
    free(ptr);
}

extern "C" [[noreturn]] void stdThreadStackDone() {
    ExitThread(0);
}

__attribute__((naked, noreturn)) void stl::stdRunOnStack(void*, StdThreadFn, void*) {
    __asm__(
        "movq %rcx, %rsp\n\t"
        "andq $-16, %rsp\n\t"
        "subq $32, %rsp\n\t"
        "movq %r8, %rcx\n\t"
        "call *%rdx\n\t"
        "call stdThreadStackDone\n\t"
        "ud2\n\t");
}
#endif
