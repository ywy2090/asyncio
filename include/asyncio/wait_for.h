//
// Created by netcan on 2021/11/21.
//

#ifndef ASYNCIO_WAIT_FOR_H
#define ASYNCIO_WAIT_FOR_H
#include <asyncio/asyncio_ns.h>
#include <asyncio/concept/future.h>
#include <asyncio/concept/awaitable.h>
#include <asyncio/event_loop.h>
#include <asyncio/exception.h>
#include <asyncio/result.h>
#include <chrono>
ASYNCIO_NS_BEGIN
namespace detail {
template<typename R, typename Duration>
struct WaitForAwaiter {
    constexpr bool await_ready() noexcept { return false; }
    constexpr decltype(auto) await_resume() {
        return result_.result();
    }

    template<typename Promise>
    void await_suspend(std::coroutine_handle<Promise> caller) noexcept {
        continuation_ = &caller.promise();
        // set continuation_ to PENDING, don't schedule anymore, until it resume continuation_
        continuation_->set_state(PromiseState::PENDING);
    }

    template<concepts::Awaitable Fut>
    WaitForAwaiter(Fut&& fut, Duration timeout)
            : wait_for_task_(wait_for_task(no_wait_at_initial_suspend,
                                           std::forward<Fut>(fut)))
            , timeout_handle_(*this, timeout, fut.get_resumable())
            { }

private:
    template<concepts::Awaitable Fut>
    Task<> wait_for_task(NoWaitAtInitialSuspend, Fut&& fut) {
        try {
            if constexpr (std::is_void_v<R>) { co_await std::forward<Fut>(fut); }
            else { result_.set_value(co_await std::forward<Fut>(fut)); }
        } catch(...) {
            result_.unhandled_exception();
        }
        EventLoop& loop{get_event_loop()};
        loop.cancel_handle(timeout_handle_);
        loop.call_soon(*continuation_);
    }

private:
    Result<R> result_;
    Task<> wait_for_task_;
    Handle* continuation_{};

private:
    struct TimeoutHandle: Handle {
        TimeoutHandle(WaitForAwaiter& awaiter, Duration timeout, Handle& handle)
        : awaiter_(awaiter), current_task_(handle) {
            EventLoop& loop{get_event_loop()};
            loop.call_later(timeout, *this);
        }
        void run() override { // timeout!
            EventLoop& loop{get_event_loop()};
            loop.cancel_handle(current_task_);
            awaiter_.result_.set_exception(std::make_exception_ptr(TimeoutError{}));
            loop.call_soon(*awaiter_.continuation_);
        }
        const HandleFrameInfo& get_frame_info() override {
            static HandleFrameInfo frame_info;
            return frame_info;
        }

        WaitForAwaiter& awaiter_;
        Handle& current_task_;
    } timeout_handle_;
};

template<concepts::Awaitable Fut, typename Duration>
WaitForAwaiter(Fut&&, Duration) -> WaitForAwaiter<AwaitResult<Fut>, Duration>;

template<concepts::Awaitable Fut, typename Rep, typename Period>
auto wait_for(NoWaitAtInitialSuspend, Fut&& fut, std::chrono::duration<Rep, Period> timeout)
-> Task<AwaitResult<Fut>> {
    Fut future = std::forward<Fut>(fut); // lift fut lifetime
    co_return co_await WaitForAwaiter { std::forward<Fut>(future), timeout };
}
}

template<concepts::Awaitable Fut, typename Rep, typename Period>
[[nodiscard("discard wait_for doesn't make sense")]]
auto wait_for(Fut&& fut, std::chrono::duration<Rep, Period> timeout) {
    return detail::wait_for(no_wait_at_initial_suspend, std::forward<Fut>(fut), timeout);
}
ASYNCIO_NS_END
#endif // ASYNCIO_WAIT_FOR_H
