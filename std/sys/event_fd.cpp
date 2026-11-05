#include "event_fd.h"

#include <std/sys/fd.h>
#include <std/str/view.h>
#include <std/sys/plat.h>
#include <std/sys/throw.h>
#include <std/str/builder.h>

#if defined(__linux__)
    #include <sys/eventfd.h>
#endif

using namespace stl;

struct EventFD::Impl {
#if defined(__linux__)
    ScopedFD efd;

    Impl() {
        int r = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

        if (r < 0) {
            Errno().raise(StringBuilder() << StringView(u8"eventfd() failed"));
        }

        ScopedFD(r).xchg(efd);
    }

    int fd() const noexcept {
        return efd.get();
    }

    void signal() {
        u64 val = 1;
        [[maybe_unused]] const auto ignored = ::write(efd.get(), &val, sizeof(val));
    }

    void drain() {
        u64 val;
        [[maybe_unused]] const auto ignored = ::read(efd.get(), &val, sizeof(val));
    }
#elif defined(_WIN32)
    // a Windows pipe cannot be polled, so the wakeup pair is a connected
    // datagram pair on loopback: signal() sends one byte, drain() reads until
    // the socket would block
    ScopedFD rfd;
    ScopedFD wfd;

    Impl() {
        createSocketPairFD(rfd, wfd);
        rfd.setNonBlocking();
        wfd.setNonBlocking();
    }

    int fd() const noexcept {
        return rfd.get();
    }

    void signal() {
        char val = 1;
        [[maybe_unused]] const auto ignored = stdWrite(wfd.get(), &val, sizeof(val));
    }

    void drain() {
        char buf[256];

        while (stdRead(rfd.get(), buf, sizeof(buf)) > 0) {
        }
    }
#else
    ScopedFD rfd;
    ScopedFD wfd;

    Impl() {
        createPipeFD(rfd, wfd);
        rfd.setNonBlocking();
        wfd.setNonBlocking();
    }

    int fd() const noexcept {
        return rfd.get();
    }

    void signal() {
        char val = 1;
        (void)::write(wfd.get(), &val, sizeof(val));
    }

    void drain() {
        char buf[256];

        while (::read(rfd.get(), buf, sizeof(buf)) > 0) {
        }
    }
#endif
};

EventFD::EventFD()
    : impl(new Impl())
{
}

EventFD::~EventFD() noexcept {
    delete impl;
}

int EventFD::fd() const noexcept {
    return impl->fd();
}

void EventFD::signal() {
    impl->signal();
}

void EventFD::drain() {
    impl->drain();
}
