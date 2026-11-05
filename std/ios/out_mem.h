#pragma once

#include "out_zc.h"

namespace stl {
    class MemoryOutput: public ZeroCopyOutput {
        void* end_ = nullptr;
        void* imbueImpl(size_t* avail) override;
        void commitImpl(size_t len) noexcept override;

    public:
        void* ptr;

        MemoryOutput(void* _ptr) noexcept
            : ptr(_ptr)
        {
        }

        // bounded variant: imbue() never offers more than cap bytes
        MemoryOutput(void* _ptr, size_t cap) noexcept
            : ptr(_ptr)
            , end_((u8*)_ptr + cap)
        {
        }
    };
}
