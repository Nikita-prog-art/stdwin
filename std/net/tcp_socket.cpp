#include "tcp_socket.h"

#include <std/sys/fd.h>
#include <std/sys/crt.h>
#include <std/sys/plat.h>
#include <std/thr/coro.h>
#include <std/thr/poll_fd.h>
#include <std/mem/obj_pool.h>
#include <std/thr/io_reactor.h>

using namespace stl;

namespace {
    void setSockFlags([[maybe_unused]] int fd) {
#if !defined(_WIN32) && defined(SO_NOSIGPIPE)
        int one = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    }
}

TcpSocket::TcpSocket(int fd, CoroExecutor* exec) noexcept
    : fd(fd)
    , io(exec->io())
{
    setNoDelay(true);
}

int TcpSocket::socket(int* out, int domain, int type, int protocol) {
    int fd = stdSocket(domain, type, protocol);

    if (fd < 0) {
        return errno;
    }

    setSockFlags(fd);
    *out = fd;

    return 0;
}

void TcpSocket::close() {
    if (fd >= 0) {
        stdClose(fd);
        fd = -1;
    }
}

void TcpSocket::shutdown(int how) {
    stdShutdown(fd, how);
}

int TcpSocket::setReuseAddr(bool on) {
    int opt = on ? 1 : 0;

    if (stdSetSockOpt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        return errno;
    }

    return 0;
}

int TcpSocket::setNoDelay(bool on) {
    int opt = on ? 1 : 0;

    if (stdSetSockOpt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
        return errno;
    }

    return 0;
}

int TcpSocket::bind(const sockaddr* addr, u32 addrLen) {
    if (int r = stdBind(fd, addr, addrLen); r < 0) {
        return errno;
    }

    return 0;
}

int TcpSocket::listen(int backlog) {
    if (int r = stdListen(fd, backlog); r < 0) {
        return errno;
    }

    return 0;
}

int TcpSocket::connect(int* out, CoroExecutor* exec, const sockaddr* addr, u32 addrLen, u64 deadlineUs) {
    int fd = stdSocket(addr->sa_family, SOCK_STREAM, 0);

    if (fd < 0) {
        return errno;
    }

    setSockFlags(fd);

    if (int r = exec->io()->connect(fd, addr, addrLen, deadlineUs)) {
        stdClose(fd);
        return r;
    }

    *out = fd;

    return 0;
}

int TcpSocket::connectTout(int* out, CoroExecutor* exec, const sockaddr* addr, u32 addrLen, u64 timeoutUs) {
    return connect(out, exec, addr, addrLen, monotonicNowUs() + timeoutUs);
}

int TcpSocket::connectInf(int* out, CoroExecutor* exec, const sockaddr* addr, u32 addrLen) {
    return connect(out, exec, addr, addrLen, UINT64_MAX);
}

int TcpSocket::accept(ScopedFD& out, sockaddr* addr, u32* addrLen, u64 deadlineUs) {
    int newFd;

    if (int r = io->accept(fd, &newFd, addr, addrLen, deadlineUs)) {
        return r;
    }

    setSockFlags(newFd);

    ScopedFD(newFd).xchg(out);

    return 0;
}

int TcpSocket::acceptTout(ScopedFD& out, sockaddr* addr, u32* addrLen, u64 timeoutUs) {
    return accept(out, addr, addrLen, monotonicNowUs() + timeoutUs);
}

int TcpSocket::acceptInf(ScopedFD& out, sockaddr* addr, u32* addrLen) {
    return accept(out, addr, addrLen, UINT64_MAX);
}

int TcpSocket::read(size_t* nRead, void* buf, size_t len, u64 deadlineUs) {
    return io->recv(fd, nRead, buf, len, deadlineUs);
}

int TcpSocket::readTout(size_t* nRead, void* buf, size_t len, u64 timeoutUs) {
    return read(nRead, buf, len, monotonicNowUs() + timeoutUs);
}

int TcpSocket::readInf(size_t* nRead, void* buf, size_t len) {
    return read(nRead, buf, len, UINT64_MAX);
}

int TcpSocket::write(size_t* nWritten, const void* buf, size_t len, u64 deadlineUs) {
    return io->send(fd, nWritten, buf, len, deadlineUs);
}

int TcpSocket::writeTout(size_t* nWritten, const void* buf, size_t len, u64 timeoutUs) {
    return write(nWritten, buf, len, monotonicNowUs() + timeoutUs);
}

int TcpSocket::writeInf(size_t* nWritten, const void* buf, size_t len) {
    return write(nWritten, buf, len, UINT64_MAX);
}

int TcpSocket::writev(size_t* nWritten, iovec* iov, size_t iovcnt, u64 deadlineUs) {
    return io->writev(fd, nWritten, iov, iovcnt, deadlineUs);
}

int TcpSocket::writevInf(size_t* nWritten, iovec* iov, size_t iovcnt) {
    return writev(nWritten, iov, iovcnt, UINT64_MAX);
}

bool TcpSocket::peek(u8& out) {
    if (!io->poll({fd, PollFlag::In}, UINT64_MAX)) {
        return false;
    }

    return stdRecv(fd, &out, 1, MSG_PEEK) == 1;
}

TcpSocket* TcpSocket::create(ObjPool* pool, int fd, CoroExecutor* exec) {
    struct ScopedTcpSocket: public TcpSocket {
        using TcpSocket::TcpSocket;

        ~ScopedTcpSocket() noexcept {
            close();
        }
    };

    return pool->make<ScopedTcpSocket>(fd, exec);
}
