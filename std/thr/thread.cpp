#include "thread.h"

#include "coro.h"
#include "runable.h"

#include <std/str/view.h>
#include <std/sys/plat.h>
#include <std/sys/throw.h>
#include <std/dbg/insist.h>
#include <std/str/builder.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
#if defined(_WIN32)
    struct WinThreadImpl: public Thread {
        HANDLE handle_ = nullptr;
        Runable* runable_ = nullptr;
        void* stack_ = nullptr;
        size_t stackSize_ = 0;

        static DWORD WINAPI entry(void* arg) {
            ((WinThreadImpl*)arg)->runable_->run();

            return 0;
        }

        static void runEntry(void* arg) {
            ((WinThreadImpl*)arg)->runable_->run();
        }

        static DWORD WINAPI stackEntry(void* arg) {
            auto self = (WinThreadImpl*)arg;

            stdRunOnStack((char*)self->stack_ + self->stackSize_, runEntry, self);

            return 0;
        }

        void start(Runable& runable) override {
            runable_ = &runable;
            handle_ = CreateThread(nullptr, 0, entry, this, 0, nullptr);

            if (!handle_) {
                Errno().raise(StringBuilder() << StringView(u8"CreateThread failed"));
            }
        }

        void startWithStack(Runable& runable, void* stack, size_t stackSize) {
            runable_ = &runable;
            stack_ = stack;
            stackSize_ = stackSize;
            handle_ = CreateThread(nullptr, 0, stackEntry, this, 0, nullptr);

            if (!handle_) {
                Errno().raise(StringBuilder() << StringView(u8"CreateThread failed"));
            }
        }

        void join() noexcept override {
            if (!handle_) {
                return;
            }

            WaitForSingleObject(handle_, INFINITE);
            CloseHandle(handle_);
            handle_ = nullptr;
        }

        u64 threadId() const noexcept override {
            return (u64)GetThreadId(handle_);
        }
    };
#else
    struct PosixThreadImpl: public Thread {
        pthread_t thread_;

        static void* threadFunc(void* arg) {
            return (((Runable*)arg)->run(), nullptr);
        }

        void start(Runable& runable) override;
        void startWithStack(Runable& runable, void* stack, size_t stackSize);

        void join() noexcept override {
            STD_INSIST(pthread_join(thread_, nullptr) == 0);
        }

        u64 threadId() const noexcept override {
            static_assert(sizeof(pthread_t) <= sizeof(u64));
            return (u64)thread_;
        }
    };
#endif
}

#if !defined(_WIN32)
void PosixThreadImpl::start(Runable& runable) {
    if (pthread_create(&thread_, nullptr, threadFunc, &runable)) {
        Errno().raise(StringBuilder() << StringView(u8"pthread_create failed"));
    }
}

void PosixThreadImpl::startWithStack(Runable& runable, void* stack, size_t stackSize) {
    pthread_attr_t attr;

    if (int rc = pthread_attr_init(&attr); rc) {
        Errno(rc).raise(StringBuilder() << StringView(u8"pthread_attr_init failed"));
    }

    if (int rc = pthread_attr_setstack(&attr, stack, stackSize); rc) {
        pthread_attr_destroy(&attr);
        Errno(rc).raise(StringBuilder() << StringView(u8"pthread_attr_setstack failed"));
    }

    int rc = pthread_create(&thread_, &attr, threadFunc, &runable);

    pthread_attr_destroy(&attr);

    if (rc) {
        Errno(rc).raise(StringBuilder() << StringView(u8"pthread_create failed"));
    }
}
#endif

u64 Thread::currentThreadId() noexcept {
#if defined(_WIN32)
    return (u64)GetCurrentThreadId();
#else
    return (u64)pthread_self();
#endif
}

Thread* Thread::create(ObjPool* pool, Runable& runable) {
#if defined(_WIN32)
    auto t = pool->make<WinThreadImpl>();
#else
    auto t = pool->make<PosixThreadImpl>();
#endif

    t->start(runable);

    return t;
}

Thread* Thread::create(ObjPool* pool, CoroExecutor* exec, Runable& runable) {
    auto t = exec->createThread(pool);

    t->start(runable);

    return t;
}

Thread* Thread::create(ObjPool* pool, Runable& runable, void* stack, size_t stackSize) {
#if defined(_WIN32)
    auto t = pool->make<WinThreadImpl>();
#else
    auto t = pool->make<PosixThreadImpl>();
#endif

    t->startWithStack(runable, stack, stackSize);

    return t;
}
