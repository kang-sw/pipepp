#include <mutex>

#include "kangsw/hash_index.hxx"
#include "pipepp/execution_context.hpp"

kangsw::safe_string_table& pipepp::string_pool()
{
    static kangsw::safe_string_table stable_;
    return stable_;
}

pipepp::execution_context::timer_scope_indicator::~timer_scope_indicator()
{
    if (owning_) {
        auto& timer = self_->_wr()->timers;
        timer[index_].elapsed = clock::now() - issue_;
        self_->category_level_--;
    }
}

pipepp::execution_context::execution_context()
{
    for (auto& pt : context_data_) { pt = std::make_shared<execution_context_data>(); }
}

std::shared_ptr<pipepp::execution_context_data> pipepp::execution_context::_consume_read_buffer()
{
    std::lock_guard _0{swap_lock_};

    if (!rd_buf_valid_.test(std::memory_order_acquire)) {
        return {};
    }

    // 이전 데이터 크기를 미리 복사해, 벡터 확장에 따른 반복적인 메모리 재할당을 방지합니다.
    auto rd = _rd();
    auto& data = *(_rd() = std::make_shared<execution_context_data>());
    data.debug_data.reserve(rd->debug_data.capacity());
    data.timers.reserve(rd->timers.capacity());

    rd_buf_valid_.clear();
    return std::move(rd);
}

pipepp::execution_context::timer_scope_indicator pipepp::execution_context::timer_scope(kangsw::hash_pack name)
{
    timer_scope_indicator s;
    s.self_ = this;
    s.index_ = _wr()->timers.size();
    s.issue_ = clock::now();

    auto& elem = _wr()->timers.emplace_back();
    elem.category_level = category_level_;
    elem.name = string_pool()(name).second;

    category_level_++;
    return s;
}

void pipepp::execution_context::_clear_records()
{
    _wr()->debug_data.clear();
    _wr()->timers.clear();
}

void pipepp::execution_context::_swap_data_buff()
{
    std::lock_guard _0(swap_lock_);
    rd_buf_valid_.test_and_set(std::memory_order_release);
    front_data_buffer_ = !front_data_buffer_;
}
