#include "pipepp/execution_context.hpp"
#include <mutex>

std::shared_ptr<pipepp::execution_context_data> pipepp::execution_context::consume_read_buffer()
{
    std::lock_guard _0{swap_lock_};

    if (!rd_buf_valid.test(std::memory_order_acquire)) {
        return {};
    }

    // ���� ������ ũ�⸦ �̸� ������, ���� Ȯ�忡 ���� �ݺ����� �޸� ���Ҵ��� �����մϴ�.
    auto rd = _rd();
    auto& data = *(_rd() = std::make_shared<execution_context_data>());
    data.debug_data.reserve(rd->debug_data.capacity());
    data.timers.reserve(rd->timers.capacity());
    rd->flags = flags;

    rd_buf_valid.clear();
    return std::move(rd);
}

void pipepp::execution_context::_clear_records()
{
}

void pipepp::execution_context::_swap_data_buff()
{
    std::lock_guard _0(swap_lock_);
    rd_buf_valid.test_and_set(std::memory_order_release);
    front_data_buffer_ = !front_data_buffer_;
}
