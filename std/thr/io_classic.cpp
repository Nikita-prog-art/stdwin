#include "io_classic.h"

#include "coro.h"
#include "pool.h"
#include "mutex.h"
#include "poll_fd.h"
#include "cond_var.h"
#include "io_reactor.h"
#include "reactor_poll.h"

#include <std/rng/mix.h>
#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>
#include <std/rng/split_mix_64.h>

#include <std/sys/plat.h>

#include <errno.h>

using namespace stl;

namespace {
    struct PollIoReactor: public IoReactor, public ThreadPoolHooks {
        Vector<ReactorIface*> reactors_;
        CoroExecutor* exec_;
        ThreadPool* offload_;

        PollIoReactor(ObjPool* pool, CoroExecutor* exec, size_t reactors);

        ReactorIface* reactor(int fd) noexcept {
            return reactors_[splitMix64(fd) % reactors_.length()];
        }

        ThreadPoolHooks* hooks() override {
            return this;
        }

        Mutex* createMutex(ObjPool* pool) override;
        CondVar* createCondVar(ObjPool* pool, size_t) override;

        PollerIface* createPoller(ObjPool* pool) override;

        int recv(int fd, size_t* nRead, void* buf, size_t len, u64 deadlineUs) override;
        int recvfrom(int fd, size_t* nRead, void* buf, size_t len, sockaddr* addr, u32* addrLen, u64 deadlineUs) override;
        int recvmsg(int fd, msghdr* msg, int flags, size_t* nRead, u64 deadlineUs) override;
        int recvmmsg(int fd, mmsghdr* msgs, unsigned vlen, int flags, unsigned* nMsgs, u64 deadlineUs) override;
        int send(int fd, size_t* nWritten, const void* buf, size_t len, u64 deadlineUs) override;
        int sendto(int fd, size_t* nWritten, const void* buf, size_t len, const sockaddr* addr, u32 addrLen, u64 deadlineUs) override;
        int sendmsg(int fd, const msghdr* msg, int flags, size_t* nWritten, u64 deadlineUs) override;
        int read(int fd, size_t* nRead, void* buf, size_t len, u64 deadlineUs) override;
        int write(int fd, size_t* nWritten, const void* buf, size_t len, u64 deadlineUs) override;
        int writev(int fd, size_t* nWritten, iovec* iov, size_t iovcnt, u64 deadlineUs) override;
        int accept(int fd, int* newFd, sockaddr* addr, u32* addrLen, u64 deadlineUs) override;
        int connect(int fd, const sockaddr* addr, u32 addrLen, u64 deadlineUs) override;

        int pread(int fd, size_t* nRead, void* buf, size_t len, off_t offset) override;
        int pwrite(int fd, size_t* nWritten, const void* buf, size_t len, off_t offset) override;
        int fsync(int fd) override;
        int fdatasync(int fd) override;

        u32 poll(PollFD pfd, u64 deadlineUs) override;
        void sleep(u64 deadlineUs) override;
    };
}

PollIoReactor::PollIoReactor(ObjPool* pool, CoroExecutor* exec, size_t reactors)
    : exec_(exec)
    , offload_(ThreadPool::simple(pool, reactors))
{
    for (size_t i = 0; i < reactors; ++i) {
        reactors_.pushBack(ReactorIface::create(exec, pool));
    }
}

int PollIoReactor::recv(int fd, size_t* nRead, void* buf, size_t len, u64 deadlineUs) {
    for (;;) {
        ssize_t n = stdRecv(fd, buf, len, 0);

        if (n >= 0) {
            *nRead = (size_t)n;
            return 0;
        }

        if (errno != EAGAIN) {
            return errno;
        }

        if (!reactor(fd)->poll({fd, PollFlag::In}, deadlineUs)) {
            return EAGAIN;
        }
    }
}

int PollIoReactor::recvfrom(int fd, size_t* nRead, void* buf, size_t len, sockaddr* addr, u32* addrLen, u64 deadlineUs) {
    for (;;) {
        socklen_t slen = addrLen ? (socklen_t)*addrLen : 0;
        ssize_t n = stdRecvFrom(fd, buf, len, 0, addr, addrLen ? &slen : nullptr);

        if (n >= 0) {
            *nRead = (size_t)n;

            if (addrLen) {
                *addrLen = (u32)slen;
            }

            return 0;
        }

        if (errno != EAGAIN) {
            return errno;
        }

        if (!reactor(fd)->poll({fd, PollFlag::In}, deadlineUs)) {
            return EAGAIN;
        }
    }
}

int PollIoReactor::recvmsg(int fd, msghdr* msg, int flags, size_t* nRead, u64 deadlineUs) {
#if defined(_WIN32)
    (void)fd; (void)msg; (void)flags; (void)nRead; (void)deadlineUs;
    return ENOTSUP;
#else
    for (;;) {
        ssize_t n = ::recvmsg(fd, msg, flags);

        if (n >= 0) {
            *nRead = (size_t)n;
            return 0;
        }

        if (errno != EAGAIN) {
            return errno;
        }

        if (!reactor(fd)->poll({fd, PollFlag::In}, deadlineUs)) {
            return EAGAIN;
        }
    }
#endif
}

int PollIoReactor::recvmmsg(int fd, mmsghdr* msgs, unsigned vlen, int flags, unsigned* nMsgs, u64 deadlineUs) {
#if defined(__linux__)
    for (;;) {
        int n = ::recvmmsg(fd, msgs, vlen, flags, nullptr);

        if (n >= 0) {
            *nMsgs = (unsigned)n;
            return 0;
        }

        if (errno != EAGAIN) {
            return errno;
        }

        if (!reactor(fd)->poll({fd, PollFlag::In}, deadlineUs)) {
            return EAGAIN;
        }
    }
#elif defined(_WIN32)
    (void)fd; (void)msgs; (void)vlen; (void)flags; (void)nMsgs; (void)deadlineUs;
    return ENOTSUP;
#else
    (void)fd; (void)msgs; (void)vlen; (void)flags; (void)nMsgs; (void)deadlineUs;
    return ENOSYS;
#endif
}

int PollIoReactor::send(int fd, size_t* nWritten, const void* buf, size_t len, u64 deadlineUs) {
    for (;;) {
        ssize_t n = stdSend(fd, buf, len, MSG_NOSIGNAL);

        if (n >= 0) {
            *nWritten = (size_t)n;
            return 0;
        }

        if (errno != EAGAIN) {
            return errno;
        }

        if (!reactor(fd)->poll({fd, PollFlag::Out}, deadlineUs)) {
            return EAGAIN;
        }
    }
}

int PollIoReactor::sendto(int fd, size_t* nWritten, const void* buf, size_t len, const sockaddr* addr, u32 addrLen, u64 deadlineUs) {
    for (;;) {
        ssize_t n = stdSendTo(fd, buf, len, MSG_NOSIGNAL, addr, addrLen);

        if (n >= 0) {
            *nWritten = (size_t)n;
            return 0;
        }

        if (errno != EAGAIN) {
            return errno;
        }

        if (!reactor(fd)->poll({fd, PollFlag::Out}, deadlineUs)) {
            return EAGAIN;
        }
    }
}

int PollIoReactor::sendmsg(int fd, const msghdr* msg, int flags, size_t* nWritten, u64 deadlineUs) {
#if defined(_WIN32)
    (void)fd; (void)msg; (void)flags; (void)nWritten; (void)deadlineUs;
    return ENOTSUP;
#else
    for (;;) {
        ssize_t n = ::sendmsg(fd, msg, flags | MSG_NOSIGNAL);

        if (n >= 0) {
            *nWritten = (size_t)n;
            return 0;
        }

        if (errno != EAGAIN) {
            return errno;
        }

        if (!reactor(fd)->poll({fd, PollFlag::Out}, deadlineUs)) {
            return EAGAIN;
        }
    }
#endif
}

int PollIoReactor::read(int fd, size_t* nRead, void* buf, size_t len, u64 deadlineUs) {
    for (;;) {
        ssize_t n = stdRead(fd, buf, len);

        if (n >= 0) {
            *nRead = (size_t)n;
            return 0;
        }

        if (errno != EAGAIN) {
            return errno;
        }

        if (!reactor(fd)->poll({fd, PollFlag::In}, deadlineUs)) {
            return EAGAIN;
        }
    }
}

int PollIoReactor::write(int fd, size_t* nWritten, const void* buf, size_t len, u64 deadlineUs) {
    for (;;) {
        ssize_t n = stdWrite(fd, buf, len);

        if (n >= 0) {
            *nWritten = (size_t)n;
            return 0;
        }

        if (errno != EAGAIN) {
            return errno;
        }

        if (!reactor(fd)->poll({fd, PollFlag::Out}, deadlineUs)) {
            return EAGAIN;
        }
    }
}

int PollIoReactor::writev(int fd, size_t* nWritten, iovec* iov, size_t iovcnt, u64 deadlineUs) {
    for (;;) {
        ssize_t n = stdWriteV(fd, iov, iovcnt);

        if (n >= 0) {
            *nWritten = (size_t)n;
            return 0;
        }

        if (errno != EAGAIN) {
            return errno;
        }

        if (!reactor(fd)->poll({fd, PollFlag::Out}, deadlineUs)) {
            return EAGAIN;
        }
    }
}

int PollIoReactor::accept(int fd, int* newFd, sockaddr* addr, u32* addrLen, u64 deadlineUs) {
    for (;;) {
        socklen_t slen = addrLen ? (socklen_t)*addrLen : 0;

        int r = stdAccept(fd, addr, addrLen ? &slen : nullptr);

        if (r >= 0) {
            if (addrLen) {
                *addrLen = (u32)slen;
            }

            *newFd = r;

            return 0;
        }

        if (errno != EAGAIN) {
            return errno;
        }

        if (!reactor(fd)->poll({fd, PollFlag::In}, deadlineUs)) {
            return EAGAIN;
        }
    }
}

int PollIoReactor::connect(int fd, const sockaddr* addr, u32 addrLen, u64 deadlineUs) {
    if (int r = stdConnect(fd, addr, addrLen); r == 0) {
        return 0;
    } else if (errno != EINPROGRESS) {
        return errno;
    }

    reactor(fd)->poll({fd, PollFlag::Out}, deadlineUs);

    int err = 0;
    socklen_t len = sizeof(err);

    if (stdGetSockOpt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
        return errno;
    }

    return err;
}

int PollIoReactor::pread(int fd, size_t* nRead, void* buf, size_t len, off_t offset) {
    int result = 0;

    exec_->offload(offload_, [&] {
        ssize_t n = stdPread(fd, buf, len, offset);

        if (n < 0) {
            result = errno;
        } else {
            *nRead = (size_t)n;
        }
    });

    return result;
}

int PollIoReactor::pwrite(int fd, size_t* nWritten, const void* buf, size_t len, off_t offset) {
    int result = 0;

    exec_->offload(offload_, [&] {
        ssize_t n = stdPwrite(fd, buf, len, offset);

        if (n < 0) {
            result = errno;
        } else {
            *nWritten = (size_t)n;
        }
    });

    return result;
}

int PollIoReactor::fsync(int fd) {
    int result = 0;

    exec_->offload(offload_, [&] {
        if (stdFsync(fd) < 0) {
            result = errno;
        }
    });

    return result;
}

int PollIoReactor::fdatasync(int fd) {
    int result = 0;

    exec_->offload(offload_, [&] {
        if (stdFdatasync(fd) < 0) {
            result = errno;
        }
    });

    return result;
}

u32 PollIoReactor::poll(PollFD pfd, u64 deadlineUs) {
    return reactor(pfd.fd)->poll(pfd, deadlineUs);
}

void PollIoReactor::sleep(u64 deadlineUs) {
    poll({-1, 0}, deadlineUs);
}

PollerIface* PollIoReactor::createPoller(ObjPool* pool) {
    return reactors_[mix(&pool, pool) % reactors_.length()]->createPoller(pool);
}

Mutex* PollIoReactor::createMutex(ObjPool* pool) {
    return Mutex::create(pool);
}

CondVar* PollIoReactor::createCondVar(ObjPool* pool, size_t) {
    return CondVar::create(pool);
}

IoReactor* stl::createPollIoReactor(ObjPool* pool, CoroExecutor* exec, size_t reactors) {
    return pool->make<PollIoReactor>(pool, exec, reactors);
}
