#include "sys.h"
#include "out_fd.h"
#include "output.h"

#include <std/sys/fd.h>
#include <std/sys/plat.h>

#include <sys/stat.h>

using namespace stl;

namespace {
    template <typename T>
    struct Scoped: public FD, public T {
        Scoped(int fd) noexcept
            : FD(fd)
            , T(*(FD*)this)
        {
        }
    };

    static FDOutput* wrap(int fd) {
        struct stat st;

        if (fstat(fd, &st) == 0) {
            if (S_ISREG(st.st_mode) || S_ISBLK(st.st_mode)) {
                return new Scoped<FDRegular>(fd);
            }

            if (S_ISCHR(st.st_mode)) {
                return new Scoped<FDCharacter>(fd);
            }
        }

        return new Scoped<FDPipe>(fd);
    }

#if defined(_WIN32)
    static void enableVirtualTerminal(int fd) {
        auto handle = (HANDLE)_get_osfhandle(fd);

        if (handle == INVALID_HANDLE_VALUE) {
            return;
        }

        DWORD mode = 0;

        if (!GetConsoleMode(handle, &mode)) {
            return;
        }

        SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
}

Output& stl::stdoutStream() noexcept {
#if defined(_WIN32)
    enableVirtualTerminal(1);
#endif

    static auto fd = wrap(1);

    return *fd;
}

Output& stl::stderrStream() noexcept {
#if defined(_WIN32)
    enableVirtualTerminal(2);
#endif

    static auto fd = wrap(2);

    return *fd;
}
