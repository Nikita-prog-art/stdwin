#include "coro.h"
#include "mutex.h"
#include "cond_var.h"

#include <std/str/view.h>
#include <std/sys/throw.h>
#include <std/dbg/insist.h>
#include <std/str/builder.h>
#include <std/mem/obj_pool.h>

#include <std/sys/plat.h>

using namespace stl;

namespace {
#if defined(_WIN32)
    struct WinCondVarImpl: public CondVar {
        CONDITION_VARIABLE cond_ = CONDITION_VARIABLE_INIT;

        void wait(Mutex* mutex) noexcept override {
            STD_INSIST(SleepConditionVariableSRW(&cond_, (SRWLOCK*)mutex->nativeHandle(), INFINITE, 0));
        }

        void signal() noexcept override {
            WakeConditionVariable(&cond_);
        }

        void broadcast() noexcept override {
            WakeAllConditionVariable(&cond_);
        }
    };
#else
    // pthread_cond_t cannot be a base class: it is a union on glibc
    struct PosixCondVarImpl: public CondVar {
        pthread_cond_t cond;

        PosixCondVarImpl() {
            if (pthread_cond_init(&cond, nullptr) != 0) {
                Errno().raise(StringBuilder() << StringView(u8"pthread_cond_init failed"));
            }
        }

        ~PosixCondVarImpl() noexcept {
            STD_INSIST(pthread_cond_destroy(&cond) == 0);
        }

        void wait(Mutex* mutex) noexcept override {
            STD_INSIST(pthread_cond_wait(&cond, (pthread_mutex_t*)mutex->nativeHandle()) == 0);
        }

        void signal() noexcept override {
            STD_INSIST(pthread_cond_signal(&cond) == 0);
        }

        void broadcast() noexcept override {
            STD_INSIST(pthread_cond_broadcast(&cond) == 0);
        }
    };
#endif
}

CondVar* CondVar::create(ObjPool* pool) {
#if defined(_WIN32)
    return pool->make<WinCondVarImpl>();
#else
    return pool->make<PosixCondVarImpl>();
#endif
}

CondVar* CondVar::create(ObjPool* pool, CoroExecutor* exec) {
    return exec->createCondVar(pool);
}
