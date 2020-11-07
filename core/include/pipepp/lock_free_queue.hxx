#pragma once
#include <atomic>
#include <memory>
#include <mutex>

namespace templates {
template <typename Ty_>
class lock_free_queue {
public:
    using difference_type = std::ptrdiff_t;
    using element_type = Ty_;

public:
    lock_free_queue(size_t capacity)
        : array_(std::make_unique<element_type[]>(capacity + 1))
        , capacity_(capacity + 1)
    {
    }

    template <typename RTy_>
    bool try_push(RTy_&& elem)
    {
        for (;;) {
            size_t tail = tail_;
            auto next = (tail + 1) % capacity_;
            if (next == head_fence()) { break; }

            // race condition으로 인해 실패했다면 즉시 재시도합니다.
            if (tail_.compare_exchange_weak(tail, next)) {
                array_[tail] = std::forward<RTy_>(elem);
                return true;
            }
        }
        return false;
    }

    bool try_pop(Ty_& retval)
    {
        for (;;) {
            size_t read = head_read_;
            size_t read_next = (read + 1) % capacity_;
            if (read == tail_) { break; }

            if (head_read_.compare_exchange_weak(read, read_next)) {
                retval = std::move(array_[read]);
                ++head_fence_;
                return true;
            }
        }
        return false;
    }

    bool empty() const { return head_fence() == tail_; }

    size_t capacity() const { return capacity_ - 1; }
    size_t head_fence() const { return head_fence_ % capacity_; }
    size_t head_read() const { return head_read_; }
    size_t tail() const { return tail_; }

    size_t size() const
    {
        size_t head = head_fence();
        size_t tail = tail_;

        return tail >= head
                 ? tail - head
                 : tail + (capacity_ - head);
    }

private:
    std::unique_ptr<element_type[]> array_;
    size_t capacity_;
    std::atomic_size_t head_fence_ = 0;
    std::atomic_size_t head_read_ = 0;
    std::atomic_size_t tail_ = 0;
};
} // namespace templates
