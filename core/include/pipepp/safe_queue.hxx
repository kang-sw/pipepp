#pragma once
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <shared_mutex>

namespace templates {

namespace lock_free {
template <typename Ty_>
class safe_queue {
public:
    using difference_type = std::ptrdiff_t;
    using element_type = Ty_;

public:
    safe_queue(size_t capacity)
        : array_(std::make_unique<element_type[]>(capacity + 1))
        , capacity_(capacity + 1)
    {
    }

    template <typename RTy_>
    bool try_push(RTy_&& elem)
    {
        size_t tail = tail_write_.load();
        size_t next = (tail + 1) % capacity_;
        if (next == head_fence()) { return false; }

        // race condition으로 인해 실패했다면 즉시 재시도합니다.
        if (tail_write_.compare_exchange_weak(tail, next)) {
            array_[tail] = std::forward<RTy_>(elem);
            tail_fence_.fetch_add(1, std::memory_order_seq_cst);
            return true;
        }
        return false;
    }

    bool try_pop(Ty_& retval)
    {
        size_t head = head_read_.load();
        size_t next = (head + 1) % capacity_;
        if (head == tail_fence()) { return false; }

        if (head_read_.compare_exchange_weak(head, next)) {
            retval = std::move(array_[head]);
            head_fence_.fetch_add(1, std::memory_order_seq_cst);
            return true;
        }
        return false;
    }

    bool empty() const { return head_fence() == tail_fence(); }

    size_t capacity() const { return capacity_ - 1; }
    size_t head_fence() const { return head_fence_.load() % capacity_; }
    size_t head_read() const { return head_read_.load(); }
    size_t tail_write() const { return tail_write_.load(); }
    size_t tail_fence() const { return tail_fence_.load() % capacity_; }

    size_t size() const
    {
        size_t head = head_fence();
        size_t tail = tail_fence();

        return tail >= head
                 ? tail - head
                 : tail + (capacity_ - head);
    }

private:
    std::unique_ptr<element_type[]> array_;
    size_t capacity_;
    std::atomic_size_t head_fence_ = 0;
    std::atomic_size_t head_read_ = 0;
    std::atomic_size_t tail_write_ = 0;
    std::atomic_size_t tail_fence_ = 0;
};
} // namespace lock_free

inline namespace lock {
template <typename Ty_>
class safe_queue {
public:
    using difference_type = std::ptrdiff_t;
    using element_type = Ty_;
    using read_lock_type = std::shared_lock<std::shared_mutex>;
    using write_lock_type = std::lock_guard<std::shared_mutex>;

public:
    safe_queue(size_t capacity)
        : queue_()
    {
    }

    template <typename RTy_>
    bool try_push(RTy_&& elem)
    {
        write_lock_type lock(queue_lock_);
        queue_.emplace_front(std::forward<RTy_>(elem));
        return true;
    }

    bool
    try_pop(Ty_& retval)
    {
        write_lock_type lock(queue_lock_);
        if (queue_.empty()) {
            return false;
        }

        retval = std::move(queue_.back());
        queue_.pop_back();
        return true;
    }

    bool empty() const
    {
        read_lock_type lock(queue_lock_);
        return queue_.empty();
    }

    size_t capacity() const { return -1; }
    size_t head_fence() const { return 0; }
    size_t head_read() const { return 0; }
    size_t tail_write() const { return 0; }
    size_t tail_fence() const { return 0; }

    size_t size() const
    {
        read_lock_type lock(queue_lock_);
        return queue_.size();
    }

private:
    std::deque<element_type> queue_;
    mutable std::shared_mutex queue_lock_;
};
} // namespace lock
} // namespace templates
