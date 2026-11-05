#include "context.h"

#include <std/mem/new.h>
#include <std/dbg/insist.h>
#include <std/sys/plat.h>
#include <std/thr/runable.h>
#include <std/alg/exchange.h>

using namespace stl;

namespace {
#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
    extern "C" {
        void __sanitizer_start_switch_fiber(void** fakeStackSave, const void* bottom, size_t size);
        void __sanitizer_finish_switch_fiber(void* fakeStackSave, const void** bottomOld, size_t* sizeOld);
    }

    struct AsanFiberState {
        void* fakeStack_ = nullptr;
        AsanFiberState* prev_ = nullptr;
        const void* stackBottom_ = nullptr;
        size_t stackSize_ = 0;

        void initStack(void* ptr, size_t size) noexcept {
            stackBottom_ = ptr;
            stackSize_ = size;
        }

        void beforeSwitch(AsanFiberState& target) noexcept {
            __sanitizer_start_switch_fiber(&fakeStack_, target.stackBottom_, target.stackSize_);
            target.prev_ = this;
        }

        void finishSwitch() noexcept {
            __sanitizer_finish_switch_fiber(fakeStack_, &stackBottom_, &stackSize_);
        }

        void afterSwitch() noexcept {
            prev_->finishSwitch();
        }
    };
#else
    struct AsanFiberState {
        void initStack(void*, size_t) noexcept {
        }

        void beforeSwitch(AsanFiberState&) noexcept {
        }

        void afterSwitch() noexcept {
        }
    };
#endif
}

namespace __cxxabiv1 {
    struct __cxa_eh_globals {
        void* caughtExceptions = nullptr;
        unsigned int uncaughtExceptions = 0;
    };

    extern "C" __cxa_eh_globals* __cxa_get_globals() noexcept;
}

namespace {
    struct alignas(max_align_t) ContextBase: public Context, public Newable {
        static void operator delete(void*) noexcept {
        }
    };
}

#if defined(_WIN32) && defined(__x86_64__)
namespace {
    struct ContextImpl: public ContextBase, public AsanFiberState {
        u64 rsp = 0;
        u64 stackBase_ = 0;
        u64 stackLimit_ = 0;
        __cxxabiv1::__cxa_eh_globals ehg_;
        Runable* entry_ = nullptr;

        ContextImpl() noexcept;
        ContextImpl(void* stackPtr, size_t stackSize, Runable& entry) noexcept;

        void switchTo(Context& target) noexcept override;

        __attribute__((naked, noinline)) static void swapContext(u64*, u64*);

        [[noreturn]]
        static void trampoline();
    };

    extern "C" [[noreturn]] void contextEntry(ContextImpl* self);

    // The Windows SEH dispatcher validates exception frames against the
    // current thread's TEB stack bounds, so a coroutine running on its own
    // stack must publish that stack while it is active. NT_TIB lives at
    // gs:0 (StackBase at +8, StackLimit at +16) on x64.
    u64 readTebStackBase() noexcept {
        u64 value;

        __asm__ volatile("movq %%gs:8, %0" : "=r"(value));

        return value;
    }

    u64 readTebStackLimit() noexcept {
        u64 value;

        __asm__ volatile("movq %%gs:16, %0" : "=r"(value));

        return value;
    }

    void writeTebStackBase(u64 value) noexcept {
        __asm__ volatile("movq %0, %%gs:8" ::"r"(value) : "memory");
    }

    void writeTebStackLimit(u64 value) noexcept {
        __asm__ volatile("movq %0, %%gs:16" ::"r"(value) : "memory");
    }
}

ContextImpl::ContextImpl() noexcept {
    stackBase_ = readTebStackBase();
    stackLimit_ = readTebStackLimit();
}

// Windows x64 ABI: callee-saved state is rbx, rbp, rdi, rsi, r12-r15 plus
// xmm6-xmm15, MXCSR and the x87 control word; the first two arguments arrive
// in rcx/rdx. The frame laid out here is what swapContext restores:
//
//   xmm6..xmm15 (160) | mxcsr (4) | x87cw (2) | pad (10)
//   r15 r14 r13 r12 rdi rsi rbp rbx
//   return address -> trampoline (rbx holds this)
void ContextImpl::swapContext(u64*, u64*) {
    __asm__(
        "pushq %rbx\n\t"
        "pushq %rbp\n\t"
        "pushq %rsi\n\t"
        "pushq %rdi\n\t"
        "pushq %r12\n\t"
        "pushq %r13\n\t"
        "pushq %r14\n\t"
        "pushq %r15\n\t"
        "subq $176, %rsp\n\t"
        "movdqu %xmm6,   0(%rsp)\n\t"
        "movdqu %xmm7,  16(%rsp)\n\t"
        "movdqu %xmm8,  32(%rsp)\n\t"
        "movdqu %xmm9,  48(%rsp)\n\t"
        "movdqu %xmm10, 64(%rsp)\n\t"
        "movdqu %xmm11, 80(%rsp)\n\t"
        "movdqu %xmm12, 96(%rsp)\n\t"
        "movdqu %xmm13, 112(%rsp)\n\t"
        "movdqu %xmm14, 128(%rsp)\n\t"
        "movdqu %xmm15, 144(%rsp)\n\t"
        "stmxcsr 160(%rsp)\n\t"
        "fnstcw   164(%rsp)\n\t"
        "movq %rsp, (%rcx)\n\t"
        "movq (%rdx), %rsp\n\t"
        "movdqu   0(%rsp), %xmm6\n\t"
        "movdqu  16(%rsp), %xmm7\n\t"
        "movdqu  32(%rsp), %xmm8\n\t"
        "movdqu  48(%rsp), %xmm9\n\t"
        "movdqu  64(%rsp), %xmm10\n\t"
        "movdqu  80(%rsp), %xmm11\n\t"
        "movdqu  96(%rsp), %xmm12\n\t"
        "movdqu 112(%rsp), %xmm13\n\t"
        "movdqu 128(%rsp), %xmm14\n\t"
        "movdqu 144(%rsp), %xmm15\n\t"
        "ldmxcsr 160(%rsp)\n\t"
        "fldcw    164(%rsp)\n\t"
        "addq $176, %rsp\n\t"
        "popq %r15\n\t"
        "popq %r14\n\t"
        "popq %r13\n\t"
        "popq %r12\n\t"
        "popq %rdi\n\t"
        "popq %rsi\n\t"
        "popq %rbp\n\t"
        "popq %rbx\n\t"
        "retq\n\t");
}

// entered by ret from swapContext: rsp is 8-mod-16, exactly as after a call
__attribute__((naked, noinline)) void ContextImpl::trampoline() {
    __asm__(
        "movq %rbx, %rcx\n\t"
        "jmp contextEntry\n\t");
}

namespace {
    extern "C" [[noreturn]] void contextEntry(ContextImpl* self) {
        self->afterSwitch();
        self->entry_->run();

        STD_INSIST(false);
        __builtin_unreachable();
    }
}

ContextImpl::ContextImpl(void* stackPtr, size_t stackSize, Runable& entry) noexcept {
    entry_ = &entry;

    auto top = (u8*)(((uintptr_t)stackPtr + stackSize) & ~(uintptr_t)15);

    // frame: 176 bytes of xmm/mxcsr/cw, 8 general registers, then the
    // trampoline return address at a 16-aligned slot (minus 16 bytes slack)
    auto frame = top - 256;

    for (size_t i = 0; i < 256; ++i) {
        frame[i] = 0;
    }

    auto regs = (u64*)(frame + 176);

    regs[0] = 0;          // r15
    regs[1] = 0;          // r14
    regs[2] = 0;          // r13
    regs[3] = 0;          // r12
    regs[4] = 0;          // rdi
    regs[5] = 0;          // rsi
    regs[6] = 0;          // rbp
    regs[7] = (u64)this;  // rbx

    *(u64*)(frame + 240) = (u64)trampoline;

    // default MXCSR and x87 control word, as a fresh thread would have them
    *(u32*)(frame + 160) = 0x1f80;
    *(u16*)(frame + 164) = 0x027f;

    rsp = (u64)frame;
    stackBase_ = (u64)top;
    stackLimit_ = (u64)stackPtr;
    initStack(stackPtr, stackSize);
}

void ContextImpl::switchTo(Context& target) noexcept {
    auto& t = (ContextImpl&)target;
    ehg_ = exchange(*__cxxabiv1::__cxa_get_globals(), t.ehg_);
    beforeSwitch(t);

    writeTebStackBase(t.stackBase_);
    writeTebStackLimit(t.stackLimit_);

    swapContext(&rsp, &t.rsp);

    writeTebStackBase(stackBase_);
    writeTebStackLimit(stackLimit_);

    afterSwitch();
}
#elif defined(__x86_64__)
namespace {
    struct ContextImpl: public ContextBase, public AsanFiberState {
        u64 rsp = 0;
        __cxxabiv1::__cxa_eh_globals ehg_;
        Runable* entry_ = nullptr;

        ContextImpl() = default;
        ContextImpl(void* stackPtr, size_t stackSize, Runable& entry) noexcept;

        void switchTo(Context& target) noexcept override;

        __attribute__((naked, noinline)) static void swapContext(u64*, u64*);

        [[noreturn]]
        static void trampoline();
    };
}

void ContextImpl::swapContext(u64*, u64*) {
    __asm__(
        "pushq %rbx\n\t"
        "pushq %rbp\n\t"
        "pushq %r12\n\t"
        "pushq %r13\n\t"
        "pushq %r14\n\t"
        "pushq %r15\n\t"
        "movq %rsp, (%rdi)\n\t"
        "movq (%rsi), %rsp\n\t"
        "popq %r15\n\t"
        "popq %r14\n\t"
        "popq %r13\n\t"
        "popq %r12\n\t"
        "popq %rbp\n\t"
        "popq %rbx\n\t"
        "retq\n\t");
}

__attribute__((no_sanitize("address"))) void ContextImpl::trampoline() {
    ContextImpl* self;

    __asm__ volatile("movq %%rbx, %0" : "=r"(self));

    self->afterSwitch();
    self->entry_->run();

    STD_INSIST(false);
    __builtin_unreachable();
}

ContextImpl::ContextImpl(void* stackPtr, size_t stackSize, Runable& entry) noexcept {
    entry_ = &entry;

    auto top = (u64*)(((uintptr_t)stackPtr + stackSize) & ~(uintptr_t)15);

    // after swapContext pops 6 regs and does ret (7 * 8 = 56 bytes),
    // rsp = top - 8 which is 8-mod-16, matching the ABI for function entry
    *--top = 0;
    *--top = (u64)trampoline;
    *--top = (u64)this; // rbx
    *--top = 0;         // rbp
    *--top = 0;         // r12
    *--top = 0;         // r13
    *--top = 0;         // r14
    *--top = 0;         // r15

    rsp = (u64)top;
    initStack(stackPtr, stackSize);
}

void ContextImpl::switchTo(Context& target) noexcept {
    auto& t = (ContextImpl&)target;
    ehg_ = exchange(*__cxxabiv1::__cxa_get_globals(), t.ehg_);
    beforeSwitch(t);
    swapContext(&rsp, &t.rsp);
    afterSwitch();
}
#elif defined(__aarch64__)
namespace {
    struct ContextImpl: public ContextBase, public AsanFiberState {
        u64 sp_ = 0;
        __cxxabiv1::__cxa_eh_globals ehg_;
        Runable* entry_ = nullptr;

        ContextImpl() = default;
        ContextImpl(void* stackPtr, size_t stackSize, Runable& entry) noexcept;

        void switchTo(Context& target) noexcept override;

        __attribute__((naked, noinline)) static void swapContext(u64*, u64*);

        [[noreturn]]
        static void trampoline();
    };
}

void ContextImpl::swapContext(u64*, u64*) {
    __asm__(
        "stp d8,  d9,  [sp, #-16]!\n\t"
        "stp d10, d11, [sp, #-16]!\n\t"
        "stp d12, d13, [sp, #-16]!\n\t"
        "stp d14, d15, [sp, #-16]!\n\t"
        "stp x19, x20, [sp, #-16]!\n\t"
        "stp x21, x22, [sp, #-16]!\n\t"
        "stp x23, x24, [sp, #-16]!\n\t"
        "stp x25, x26, [sp, #-16]!\n\t"
        "stp x27, x28, [sp, #-16]!\n\t"
        "stp x29, x30, [sp, #-16]!\n\t"
        "mov x2, sp\n\t"
        "str x2, [x0]\n\t"
        "ldr x2, [x1]\n\t"
        "mov sp, x2\n\t"
        "ldp x29, x30, [sp], #16\n\t"
        "ldp x27, x28, [sp], #16\n\t"
        "ldp x25, x26, [sp], #16\n\t"
        "ldp x23, x24, [sp], #16\n\t"
        "ldp x21, x22, [sp], #16\n\t"
        "ldp x19, x20, [sp], #16\n\t"
        "ldp d14, d15, [sp], #16\n\t"
        "ldp d12, d13, [sp], #16\n\t"
        "ldp d10, d11, [sp], #16\n\t"
        "ldp d8,  d9,  [sp], #16\n\t"
        "ret\n\t");
}

__attribute__((no_sanitize("address"))) void ContextImpl::trampoline() {
    ContextImpl* self;

    __asm__ volatile("mov %0, x19" : "=r"(self));

    self->afterSwitch();
    self->entry_->run();

    STD_INSIST(false);
    __builtin_unreachable();
}

ContextImpl::ContextImpl(void* stackPtr, size_t stackSize, Runable& entry) noexcept {
    entry_ = &entry;

    auto top = (u64*)(((uintptr_t)stackPtr + stackSize) & ~(uintptr_t)15);

    // *--top fills high-to-low; ldp pops low-to-high
    // last pushed = lowest address = first popped
    // d8, d9 (popped last, filled first)
    *--top = 0;               // d9
    *--top = 0;               // d8
    *--top = 0;               // d11
    *--top = 0;               // d10
    *--top = 0;               // d13
    *--top = 0;               // d12
    *--top = 0;               // d15
    *--top = 0;               // d14
    *--top = 0;               // x20
    *--top = (u64)this;       // x19 — ContextImpl*
    *--top = 0;               // x22
    *--top = 0;               // x21
    *--top = 0;               // x24
    *--top = 0;               // x23
    *--top = 0;               // x26
    *--top = 0;               // x25
    *--top = 0;               // x28
    *--top = 0;               // x27
    *--top = (u64)trampoline; // x30 (lr)
    *--top = 0;               // x29 (fp)

    sp_ = (u64)top;
    initStack(stackPtr, stackSize);
}

void ContextImpl::switchTo(Context& target) noexcept {
    auto& t = (ContextImpl&)target;
    ehg_ = exchange(*__cxxabiv1::__cxa_get_globals(), t.ehg_);
    beforeSwitch(t);
    swapContext(&sp_, &t.sp_);
    afterSwitch();
}
#else
    #include <ucontext.h>

namespace {
    struct ContextImpl: public ContextBase {
        ucontext_t uctx;
        __cxxabiv1::__cxa_eh_globals ehg_;

        ContextImpl() noexcept;
        ContextImpl(void* stackPtr, size_t stackSize, Runable& entry) noexcept;

        void switchTo(Context& target) noexcept override;

        static void trampoline(u32 lo, u32 hi);
    };
}

ContextImpl::ContextImpl() noexcept {
}

ContextImpl::ContextImpl(void* stackPtr, size_t stackSize, Runable& entry) noexcept {
    getcontext(&uctx);

    uctx.uc_stack.ss_sp = stackPtr;
    uctx.uc_stack.ss_size = stackSize;
    uctx.uc_link = nullptr;

    auto p = (uintptr_t)&entry;

    makecontext(&uctx, (void (*)())trampoline, 2, (u32)p, (u32)(p >> 32));
}

void ContextImpl::trampoline(u32 lo, u32 hi) {
    ((Runable*)(uintptr_t)(((uintptr_t)hi << 32) | lo))->run();
}

void ContextImpl::switchTo(Context& target) noexcept {
    auto& t = (ContextImpl&)target;
    ehg_ = exchange(*__cxxabiv1::__cxa_get_globals(), t.ehg_);
    swapcontext(&uctx, &t.uctx);
}
#endif

size_t Context::implSize() noexcept {
    static_assert(sizeof(ContextImpl) % alignof(max_align_t) == 0);

    return sizeof(ContextImpl);
}

Context* Context::create(void* buf) noexcept {
    return new (buf) ContextImpl();
}

Context* Context::create(void* buf, void* stackPtr, size_t stackSize, Runable& entry) noexcept {
    return new (buf) ContextImpl(stackPtr, stackSize, entry);
}

Context::~Context() {
}
