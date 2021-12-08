//
// Created by netcan on 2021/09/07.
//
#include <chrono>
#include <optional>
#include <asyncio/event_loop.h>

namespace ranges = std::ranges;

ASYNCIO_NS_BEGIN
EventLoop& get_event_loop() {
    static EventLoop loop;
    return loop;
}

void EventLoop::run_forever() {
    while (! is_stop()) {
        run_once();
    }
}

void EventLoop::run_once() {
    std::optional<MSDuration> timeout;
    // Remove delayed calls that were cancelled from head of queue.
    while (! schedule_.empty()) {
        auto&& [when, handle_info] = schedule_[0];
        if (auto iter = cancelled_.find(handle_info.id); iter != cancelled_.end()) {
            ranges::pop_heap(schedule_,std::ranges::greater{}, &TimerHandle::first);
            schedule_.pop_back();
            cancelled_.erase(iter);
        } else {
            break;
        }
    }

    if (! ready_.empty()) {
        timeout.emplace(0);
    } else if (! schedule_.empty()) {
        auto&& [when, _] = schedule_[0];
        timeout = std::max(when - time(), MSDuration(0));
    }

    auto event_lists = selector_.select(timeout.has_value() ? timeout->count() : -1);
    for (auto&& event: event_lists) {
        Handle* continuation_ = reinterpret_cast<Handle*>(event.data);
        ready_.push({continuation_->get_handle_id(), continuation_});
    }

    auto end_time = time();
    while (! schedule_.empty()) {
        auto&& [when, handle_info] = schedule_[0];
        if (when >= end_time) break;
        ready_.push(handle_info);
        ranges::pop_heap(schedule_,std::ranges::greater{}, &TimerHandle::first);
        schedule_.pop_back();
    }

    for (size_t ntodo = ready_.size(), i = 0; i < ntodo ; ++i ) {
        auto [handle_id, handle] = ready_.front();
        ready_.pop();
        if (auto iter = cancelled_.find(handle_id); iter != cancelled_.end()) {
            cancelled_.erase(iter);
        } else {
            handle->run();
        }
    }
}

HandleId EventLoop::handle_alloc_id_ = 0;

Handle::Handle() noexcept {
    auto& loop = get_event_loop();
    handle_id_ = loop.allocate_handle_id();
}

const std::source_location& Handle::get_frame_info() const {
    static const std::source_location frame_info = std::source_location::current();
    return frame_info;
}
ASYNCIO_NS_END
