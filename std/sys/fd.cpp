#include "fd.h"

#include <std/alg/xchg.h>
#include <std/str/view.h>
#include <std/sys/plat.h>
#include <std/sys/throw.h>
#include <std/alg/minmax.h>
#include <std/str/builder.h>
#include <std/alg/exchange.h>

using namespace stl;

size_t FD::read(void* data, size_t len) {
    if (auto res = stdRead(fd, data, min(len, (size_t)(0x7ffff000 - 1))); res >= 0) {
        return res;
    }

    if (errno == EFAULT && len > 1024) {
        return read(data, len / 2);
    }

    Errno().raise(StringBuilder() << StringView(u8"read() failed"));
}

size_t FD::write(const void* data, size_t len) {
    auto res = stdWrite(fd, data, len);

    if (res < 0) {
        Errno().raise(StringBuilder() << StringView(u8"write() failed"));
    }

    return res;
}

size_t FD::writeV(iovec* parts, size_t count) {
    auto res = stdWriteV(fd, parts, count);

    if (res < 0) {
        Errno().raise(StringBuilder() << StringView(u8"writev() failed"));
    }

    return res;
}

void FD::setNonBlocking() {
    if (stdSetNonBlocking(fd) < 0) {
        Errno().raise(StringBuilder() << StringView(u8"fcntl(O_NONBLOCK) failed"));
    }
}

void FD::fsync() {
    if (stdFsync(fd) < 0) {
        Errno().raise(StringBuilder() << StringView(u8"fsync() failed"));
    }
}

void FD::close() {
    if (fd < 0) {
        return;
    }

    if (stdClose(exchange(fd, -1)) < 0) {
        Errno().raise(StringBuilder() << StringView(u8"close() failed"));
    }
}

void FD::xchg(FD& other) noexcept {
    ::stl::xchg(fd, other.fd);
}

ScopedFD::~ScopedFD() {
    close();
}

FD ScopedFD::release() noexcept {
    FD tmp;
    xchg(tmp);
    return tmp;
}

void stl::createPipeFD(ScopedFD& in, ScopedFD& out) {
    int fd[2];

    if (auto res = stdPipe(fd); res < 0) {
        Errno().raise(StringBuilder() << StringView(u8"pipe() failed"));
    }

    ScopedFD(fd[0]).xchg(in);
    ScopedFD(fd[1]).xchg(out);
}

void stl::createSocketPairFD(ScopedFD& a, ScopedFD& b) {
#if defined(_WIN32)
    ScopedFD left(stdSocket(AF_INET, SOCK_DGRAM, 0));

    if (left.get() < 0) {
        Errno().raise(StringBuilder() << StringView(u8"createSocketPairFD(): socket() failed"));
    }

    sockaddr_in leftAddr {};
    leftAddr.sin_family = AF_INET;
    leftAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    leftAddr.sin_port = 0;

    if (stdBind(left.get(), (const sockaddr*)&leftAddr, sizeof(leftAddr)) < 0) {
        Errno().raise(StringBuilder() << StringView(u8"createSocketPairFD(): bind() failed"));
    }

    socklen_t leftLen = sizeof(leftAddr);

    if (getsockname(stdNativeSocket(left.get()), (sockaddr*)&leftAddr, (int*)&leftLen) == SOCKET_ERROR) {
        Errno().raise(StringBuilder() << StringView(u8"createSocketPairFD(): getsockname() failed"));
    }

    ScopedFD right(stdSocket(AF_INET, SOCK_DGRAM, 0));

    if (right.get() < 0) {
        Errno().raise(StringBuilder() << StringView(u8"createSocketPairFD(): socket() failed"));
    }

    sockaddr_in rightAddr {};
    rightAddr.sin_family = AF_INET;
    rightAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    rightAddr.sin_port = 0;

    if (stdBind(right.get(), (const sockaddr*)&rightAddr, sizeof(rightAddr)) < 0) {
        Errno().raise(StringBuilder() << StringView(u8"createSocketPairFD(): bind() failed"));
    }

    socklen_t rightLen = sizeof(rightAddr);

    if (getsockname(stdNativeSocket(right.get()), (sockaddr*)&rightAddr, (int*)&rightLen) == SOCKET_ERROR) {
        Errno().raise(StringBuilder() << StringView(u8"createSocketPairFD(): getsockname() failed"));
    }

    if (stdConnect(left.get(), (const sockaddr*)&rightAddr, sizeof(rightAddr)) < 0) {
        Errno().raise(StringBuilder() << StringView(u8"createSocketPairFD(): connect() failed"));
    }

    if (stdConnect(right.get(), (const sockaddr*)&leftAddr, sizeof(leftAddr)) < 0) {
        Errno().raise(StringBuilder() << StringView(u8"createSocketPairFD(): connect() failed"));
    }

    if (stdSetBlocking(left.get()) < 0 || stdSetBlocking(right.get()) < 0) {
        Errno().raise(StringBuilder() << StringView(u8"createSocketPairFD(): setBlocking failed"));
    }

    left.xchg(a);
    right.xchg(b);
#else
    int fds[2];

    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
        Errno().raise(StringBuilder() << StringView(u8"socketpair() failed"));
    }

    ScopedFD left(fds[0]);
    ScopedFD right(fds[1]);

    if (stdSetBlocking(left.get()) < 0 || stdSetBlocking(right.get()) < 0) {
        Errno().raise(StringBuilder() << StringView(u8"createSocketPairFD(): setBlocking failed"));
    }

    left.xchg(a);
    right.xchg(b);
#endif
}
