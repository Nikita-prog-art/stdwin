#include "fs.h"

#include <std/tst/ut.h>
#include <std/sys/fd.h>
#include <std/sys/plat.h>
#include <std/alg/defer.h>
#include <std/lib/buffer.h>
#include <std/lib/vector.h>

using namespace stl;

STD_TEST_SUITE(ListDir) {
    STD_TEST(CurrentDirectoryBasic) {
        Vector<TPathInfo> items;

        listDir(StringView(u8"."), [&items](const TPathInfo& pi) {
            items.pushBack(pi);
        });

        u64 dirCount = 0;
        u64 fileCount = 0;

        for (u64 i = 0; i < items.length(); ++i) {
            if (items[i].isDir) {
                dirCount++;
            } else {
                fileCount++;
            }
        }

        STD_INSIST(dirCount > 2);
        STD_INSIST(fileCount > 2);
    }

    STD_TEST(CurrentDirectoryContainsExpectedItems) {
        bool foundTstDir = false;
        bool foundStdDir = false;
        bool foundBuildFile = false;

        listDir(StringView(u8"."), [&](const TPathInfo& pi) {
            if (pi.item == StringView(u8"tst") && pi.isDir) {
                foundTstDir = true;
            }
            if (pi.item == StringView(u8"std") && pi.isDir) {
                foundStdDir = true;
            }
            if (pi.item == StringView(u8"build.py") && !pi.isDir) {
                foundBuildFile = true;
            }
        });

        STD_INSIST(foundTstDir);
        STD_INSIST(foundStdDir);
        STD_INSIST(foundBuildFile);
    }

    STD_TEST(NonAsciiFileName) {
        StringView name = StringView(u8"std-test-тест-文件.txt");
        Buffer path(name);
        bool created = false;

        STD_DEFER {
            if (created) {
                stdUnlink(path.cStr());
            }
        };

        int fd = stdOpen(path.cStr(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        STD_INSIST(fd >= 0);

        ScopedFD scoped(fd);
        created = true;

        const char data[] = "unicode";
        const size_t len = sizeof(data) - 1;

        STD_INSIST(scoped.write(data, len) == len);
        scoped.close();

        bool found = false;

        listDir(StringView(u8"."), [&](const TPathInfo& pi) {
            if (pi.item == name) {
                found = true;
            }
        });

        STD_INSIST(found);
    }
}
