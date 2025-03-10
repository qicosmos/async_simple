/*
 * Copyright (c) 2022, Alibaba Group Holding Limited;
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef FUTURE_SIMPLE_EXECUTOR_H
#define FUTURE_SIMPLE_EXECUTOR_H

#include <unistd.h>
#include <functional>

#include <async_simple/Executor.h>
#include <async_simple/executors/SimpleIOExecutor.h>
#include <async_simple/util/ThreadPool.h>

#include <thread>

namespace async_simple {

namespace executors {

// This is a simple executor. The intention of SimpleExecutor is to make the
// test available and show how user should implement their executors. People who
// want to have fun with async_simple could use SimpleExecutor for convenience,
// too. People who want to use async_simple in production level development
// should implement their own executor strategy and implement an Executor
// derived from async_simple::Executor as an interface.
//
// The actual strategy that SimpleExecutor used is implemented in
// async_simple/util/ThreadPool.h.
class SimpleExecutor : public Executor {
public:
    using Func = Executor::Func;
    using Context = Executor::Context;

    union ContextUnion {
        Context ctx;
        int64_t id;
    };

public:
    SimpleExecutor(size_t threadNum) : _pool(threadNum) {
        [[maybe_unused]] auto ret = _pool.start();
        assert(ret);
        _ioExecutor.init();
    }
    ~SimpleExecutor() { _ioExecutor.destroy(); }

public:
    bool schedule(Func func) override {
        return _pool.scheduleById(std::move(func)) ==
               util::ThreadPool::ERROR_NONE;
    }
    bool currentThreadInExecutor() const override {
        return _pool.getCurrentId() != -1;
    }
    ExecutorStat stat() const override { return ExecutorStat(); }

    size_t currentContextId() const override { return _pool.getCurrentId(); }

    Context checkout() override {
        ContextUnion u;
        // avoid u.id equal to NULLCTX
        u.id = _pool.getCurrentId() | 0x40000000;
        return u.ctx;
    }
    bool checkin(Func func, Context ctx, ScheduleOptions opts) override {
        ContextUnion u;
        u.ctx = ctx;
        // 0xBFFFFFFF == ~0x40000000
        auto prompt =
            _pool.getCurrentId() == (u.id & 0xBFFFFFFF) && opts.prompt;
        if (prompt) {
            func();
            return true;
        }
        return _pool.scheduleById(std::move(func), u.id & 0xBFFFFFFF) ==
               util::ThreadPool::ERROR_NONE;
    }

    IOExecutor* getIOExecutor() override { return &_ioExecutor; }

private:
    util::ThreadPool _pool;
    SimpleIOExecutor _ioExecutor;
};

}  // namespace executors

}  // namespace async_simple

#endif  // FUTURE_SIMPLE_EXECUTOR_H
