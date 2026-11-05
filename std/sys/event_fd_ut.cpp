#include "event_fd.h"

#include <std/tst/ut.h>
#include <std/dbg/insist.h>
#include <std/sys/plat.h>

using namespace stl;

static bool isReadable(int fd) {
    StdPollFD pfd {};
    pfd.fd = fd;
    pfd.events = stdPollIn;

    return stdPoll(&pfd, 1, 0) > 0;
}

STD_TEST_SUITE(EventFD) {
    STD_TEST(FdIsValid) {
        EventFD ev;
        STD_INSIST(ev.fd() >= 0);
    }

    STD_TEST(SignalMakesReadable) {
        EventFD ev;
        STD_INSIST(!isReadable(ev.fd()));
        ev.signal();
        STD_INSIST(isReadable(ev.fd()));
    }

    STD_TEST(DrainClearsSignal) {
        EventFD ev;
        ev.signal();
        ev.drain();
        STD_INSIST(!isReadable(ev.fd()));
    }

    STD_TEST(MultipleSignalsDrained) {
        EventFD ev;
        ev.signal();
        ev.signal();
        ev.signal();
        ev.drain();
        STD_INSIST(!isReadable(ev.fd()));
    }
}
