#include "mem_fd.h"

#include <std/str/view.h>
#include <std/sys/plat.h>
#include <std/sys/throw.h>
#include <std/str/builder.h>

#if !defined(_WIN32)
    #if defined(__linux__)
        #include <sys/mman.h>
    #else
        #include <unistd.h>
        #include <stdlib.h>
    #endif
#endif

using namespace stl;

namespace {
#if defined(_WIN32)
    int memFDWindows() {
        wchar_t tempDir[MAX_PATH + 1];

        if (GetTempPathW(MAX_PATH + 1, tempDir) == 0) {
            errno = stdMapWin32Error(GetLastError());
            Errno().raise(StringBuilder() << StringView(u8"GetTempPathW() failed"));
        }

        wchar_t tempName[MAX_PATH + 1];

        if (GetTempFileNameW(tempDir, L"stl", 0, tempName) == 0) {
            errno = stdMapWin32Error(GetLastError());
            Errno().raise(StringBuilder() << StringView(u8"GetTempFileNameW() failed"));
        }

        HANDLE handle = CreateFileW(
            tempName,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
            nullptr);

        if (handle == INVALID_HANDLE_VALUE) {
            errno = stdMapWin32Error(GetLastError());
            Errno().raise(StringBuilder() << StringView(u8"CreateFileW() failed"));
        }

        int fd = ::_open_osfhandle((intptr_t)handle, _O_RDWR | _O_BINARY);

        if (fd < 0) {
            CloseHandle(handle);
            Errno().raise(StringBuilder() << StringView(u8"_open_osfhandle() failed"));
        }

        return fd;
    }
#elif !defined(__linux__)
    int memFDFallback(const char* name) {
        (void)name;

        const char* dir = getenv("TMPDIR");

        if (!dir) {
            dir = "/tmp";
        }

        StringBuilder sb;
        sb << StringView(dir) << StringView(u8"/memfd.XXXXXX");

        int fd = mkstemp(sb.cStr());

        if (fd < 0) {
            Errno().raise(StringBuilder() << StringView(u8"mkstemp() failed"));
        }

        unlink(sb.cStr());

        return fd;
    }
#endif
}

int stl::memFD(const char* name) {
#if defined(_WIN32)
    (void)name;

    return memFDWindows();
#elif defined(__linux__)
    int fd = memfd_create(name, 0);

    if (fd < 0) {
        Errno().raise(StringBuilder() << StringView(u8"memfd_create() failed"));
    }

    return fd;
#else
    return memFDFallback(name);
#endif
}
