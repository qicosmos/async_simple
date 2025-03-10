/*
 * This file is open source software, licensed to you under the terms
 * of the Apache License, Version 2.0 (the "License").  See the NOTICE file
 * distributed with this work for additional information regarding copyright
 * ownership.  You may not use this file except in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*
 * Copyright (C) 2015 Cloudius Systems, Ltd.
 */

#include <ucontext.h>
#include <algorithm>
#include <string>

#include <async_simple/Common.h>
#include <async_simple/uthread/internal/thread.h>

namespace async_simple {
namespace uthread {
namespace internal {

thread_local jmp_buf_link g_unthreaded_context;
thread_local jmp_buf_link* g_current_context = nullptr;

static const std::string uthread_stack_size = "UTHREAD_STACK_SIZE_KB";
size_t get_base_stack_size() {
  static size_t stack_size = 0;
  if (stack_size) {
    return stack_size;
  }
  auto env = std::getenv(uthread_stack_size.data());
  if (env) {
    auto kb = std::strtoll(env, nullptr, 10);
    if (kb > 0 && kb < std::numeric_limits<size_t>::max()) {
      stack_size = 1024 * kb;
      return stack_size;
    }
  }
  stack_size = default_base_stack_size;
  return stack_size;
}

inline void jmp_buf_link::switch_in() {
  link = std::exchange(g_current_context, this);
  if (FL_UNLIKELY(!link)) {
      link = &g_unthreaded_context;
  }
  fcontext = _fl_jump_fcontext(fcontext, thread).fctx;
}

inline void jmp_buf_link::switch_out() {
  g_current_context = link;
  auto from = _fl_jump_fcontext(link->fcontext, nullptr).fctx;
  link->fcontext = from;
}

inline void jmp_buf_link::initial_switch_in_completed() {}

thread_context::thread_context(std::function<void()> func)
  : stack_size_(get_base_stack_size())
  , func_(std::move(func)) {
  setup();
}

thread_context::~thread_context() {}

thread_context::stack_holder thread_context::make_stack() {
  auto stack = stack_holder(new char[stack_size_]);
  return stack;
}

void thread_context::stack_deleter::operator()(char* ptr) const noexcept {
  delete[] ptr;
}

void thread_context::setup() {
  context_.fcontext =
    _fl_make_fcontext(stack_.get() + stack_size_, stack_size_, thread_context::s_main);
  context_.thread = this;
  context_.switch_in();
}

void thread_context::switch_in() {
  context_.switch_in();
}

void thread_context::switch_out() {
  context_.switch_out();
}

void thread_context::s_main(transfer_t t) {
  auto q = reinterpret_cast<thread_context*>(t.data);
  q->context_.link->fcontext = t.fctx;
  q->main();
}

void thread_context::main() {
#ifdef __x86_64__
  // There is no caller of main() in this context. We need to annotate this frame like this so that
  // unwinders don't try to trace back past this frame.
  // See https://github.com/scylladb/scylla/issues/1909.
  asm(".cfi_undefined rip");
#elif defined(__PPC__)
  asm(".cfi_undefined lr");
#elif defined(__aarch64__)
  asm(".cfi_undefined x30");
#else
#warning "Backtracing from uthreads may be broken"
#endif
  context_.initial_switch_in_completed();
  try {
    func_();
    done_.setValue(true);
  } catch (...) {
    done_.setException(std::current_exception());
  }

  context_.switch_out();
}

namespace thread_impl {

void switch_in(thread_context* to) {
  to->switch_in();
}

void switch_out(thread_context* from) {
  from->switch_out();
}

bool can_switch_out() {
  return g_current_context && g_current_context->thread;
}

}  // namespace thread_impl

} // namespace internal
} // namespace uthread
} // namespace fl
