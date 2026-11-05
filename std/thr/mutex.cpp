#include "mutex.h"
#include "coro.h"

#include <std/str/view.h>
#include <std/mem/obj_pool.h>
#include <std/sys/throw.h>
#include <std/dbg/insist.h>
#include <std/str/builder.h>

#include <std/sys/plat.h>

using namespace stl;

namespace {
    struct alignas(64) SpinMutexImpl: public Mutex {
        CoroExecutor* exec_;
        char flag_ = 0;

        SpinMutexImpl(CoroExecutor* exec) noexcept
            : exec_(exec)
        {
        }

        void spin() noexcept {
            if (exec_ && exec_->currentCoroId()) {
                exec_->yield();
            } else {
                stdYield();
            }
        }

        void* nativeHandle() noexcept override {
            return exec_;
        }

        void wait() noexcept override {
            for (;;) {
                if (!__atomic_test_and_set(&flag_, __ATOMIC_ACQUIRE)) {
                    return;
                }

                while (__atomic_load_n(&flag_, __ATOMIC_RELAXED)) {
                    spin();
                }
            }
        }

        void post() noexcept override {
            __atomic_clear(&flag_, __ATOMIC_RELEASE);
        }

        bool tryWait() noexcept override {
            return !__atomic_test_and_set(&flag_, __ATOMIC_ACQUIRE);
        }
    };

#if defined(_WIN32)
    struct WinMutexImpl: public Mutex {
        SRWLOCK lock_ = SRWLOCK_INIT;

        void wait() noexcept override {
            AcquireSRWLockExclusive(&lock_);
        }

        void post() noexcept override {
            ReleaseSRWLockExclusive(&lock_);
        }

        bool tryWait() noexcept override {
            return TryAcquireSRWLockExclusive(&lock_);
        }

        void* nativeHandle() noexcept override {
            return &lock_;
        }
    };
#else
    // pthread_mutex_t cannot be a base class: it is a union on glibc
    struct PosixMutexImpl: public Mutex {
        pthread_mutex_t mutex;

        PosixMutexImpl() {
            if (pthread_mutex_init(&mutex, nullptr) != 0) {
                Errno().raise(StringBuilder() << StringView(u8"pthread_mutex_init failed"));
            }
        }

        ~PosixMutexImpl() noexcept {
            STD_INSIST(pthread_mutex_destroy(&mutex) == 0);
        }

        void wait() noexcept override {
            STD_INSIST(pthread_mutex_lock(&mutex) == 0);
        }

        void post() noexcept override {
            STD_INSIST(pthread_mutex_unlock(&mutex) == 0);
        }

        bool tryWait() noexcept override {
            return pthread_mutex_trylock(&mutex) == 0;
        }

        void* nativeHandle() noexcept override {
            return &mutex;
        }
    };
#endif
}

Mutex* Mutex::create(ObjPool* pool) {
#if defined(_WIN32)
    return pool->make<WinMutexImpl>();
#else
    return pool->make<PosixMutexImpl>();
#endif
}

Mutex* Mutex::create(ObjPool* pool, CoroExecutor* exec) {
    return exec->createMutex(pool);
}

Mutex* Mutex::createSpinLock(ObjPool* pool) {
    return pool->make<SpinMutexImpl>(nullptr);
}

Mutex* Mutex::createSpinLock(ObjPool* pool, CoroExecutor* exec) {
    return pool->make<SpinMutexImpl>(exec);
}
