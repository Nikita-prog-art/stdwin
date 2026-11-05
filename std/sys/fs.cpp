#include "fs.h"

#include <std/alg/defer.h>
#include <std/sys/plat.h>
#include <std/sys/throw.h>
#include <std/lib/buffer.h>
#include <std/str/builder.h>

#if defined(_WIN32)
    #include <wchar.h>
#else
    #include <dirent.h>
    #include <sys/types.h>
#endif

using namespace stl;

#if defined(_WIN32)
void stl::listDirImpl(StringView path, VisitorFace&& vis) {
    wchar_t* wide = stdToWide((const char*)path.data(), path.length());

    if (!wide) {
        errno = EINVAL;
        Errno().raise(StringBuilder() << StringView(u8"opendir() failed for ") << path);
    }

    const size_t wideLen = wcslen(wide);
    auto pattern = (wchar_t*)malloc((wideLen + 3) * sizeof(wchar_t));

    if (!pattern) {
        stdFreeWide(wide);
        errno = ENOMEM;
        Errno().raise(StringBuilder() << StringView(u8"opendir() failed for ") << path);
    }

    wcscpy(pattern, wide);
    stdFreeWide(wide);

    if (wideLen > 0 && pattern[wideLen - 1] != L'\\' && pattern[wideLen - 1] != L'/') {
        wcscat(pattern, L"\\");
    }

    wcscat(pattern, L"*");

    WIN32_FIND_DATAW data;
    HANDLE handle = FindFirstFileW(pattern, &data);

    free(pattern);

    if (handle == INVALID_HANDLE_VALUE) {
        errno = stdMapWin32Error(GetLastError());
        Errno().raise(StringBuilder() << StringView(u8"opendir() failed for ") << path);
    }

    STD_DEFER {
        FindClose(handle);
    };

    do {
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) {
            continue;
        }

        char name[1024];
        const size_t nameLen = stdFromWide(data.cFileName, name, sizeof(name));

        TPathInfo pi = {
            .item = StringView((const u8*)name, nameLen),
            .isDir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0,
        };

        vis.visit(&pi);
    } while (FindNextFileW(handle, &data));
}
#else
void stl::listDirImpl(StringView path, VisitorFace&& vis) {
    DIR* dir = opendir(Buffer(path).cStr());

    if (!dir) {
        Errno().raise(StringBuilder() << StringView(u8"opendir() failed for ") << path);
    }

    STD_DEFER {
        if (closedir(dir) != 0) {
            Errno().raise(StringBuilder() << StringView(u8"closedir() failed for ") << path);
        }
    };

    while (struct dirent* entry = readdir(dir)) {
        StringView name((const char*)entry->d_name);

        if (name == StringView(u8".") || name == StringView(u8"..")) {
            continue;
        }

        TPathInfo pi = {
            .item = name,
            .isDir = entry->d_type == DT_DIR,
        };

        // name points into entry->d_name, which is valid until the next readdir call —
        // vis.visit() completes before that, so no dangling reference here.
        vis.visit(&pi);
    }
}
#endif
