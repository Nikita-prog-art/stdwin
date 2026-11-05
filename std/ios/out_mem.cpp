#include "out_mem.h"

#include <std/alg/advance.h>

using namespace stl;

void* MemoryOutput::imbueImpl(size_t* len) {
    if (end_) {
        auto cur = (u8*)ptr;

        *len = cur < end_ ? (size_t)((u8*)end_ - cur) : 0;
    } else {
        *len = (size_t)-1;
    }

    return ptr;
}

void MemoryOutput::commitImpl(size_t len) noexcept {
    ptr = advancePtr(ptr, len);
}
