#include <bitset>
#include <cassert>
#include <kangsw/enum_arithmetic.hxx>
#include <kangsw/misc.hxx>
#include <pipepp/pipe.hpp>

std::optional<bool> pipepp::impl__::pipe_base::input_slot_t::can_submit_input(fence_index_t output_fence) const
{
    auto active = active_input_fence();
    if (output_fence < active) {
        // 여러 가지 이유에 의해, 입력 fence가 격상된 경우입니다.
        // non-opt 반환 --> 호출자는 재시도하지 않으며, 이전 단계의 출력은 버려집니다.
        return {};
    }

    // 또한, 이용 가능한 입력 슬롯이 있어야 합니다.
    bool is_idle = owner_._active_exec_slot()._is_executor_busy() == false;

    // fence == active : 입력 슬롯이 준비되었습니다.
    //       >         : 아직 파이프가 처리중으로, 입력 슬롯이 준비되지 않았습니다.
    return is_idle && output_fence == active;
}

void pipepp::impl__::pipe_base::input_slot_t::_prepare_next()
{
    using namespace kangsw::enum_arithmetic;
    for (auto& e : ready_conds_) { e = input_link_state::none; }
    active_input_fence_ = active_input_fence_.load() + 1;
    this->active_input_fence_object_.reset();
}

void pipepp::impl__::pipe_base::input_slot_t::_propagate_fence_abortion(fence_index_t pending_fence, size_t output_link_index)
{
    using namespace std::chrono_literals;

    auto& output_link = *owner_.output_links_[output_link_index].pipe;
    auto& link_input = output_link.input_slot_;
    auto delay = 0us;

    if (auto query_result = link_input.can_submit_input(pending_fence); query_result.has_value()) {
        if (query_result.value() && link_input._submit_input(pending_fence, owner_.id(), {}, {}, true)) {
            ++output_link_index;
        }
        else {
            delay = 100us;
        }
    }
    else {
        ++output_link_index;
    }

    if (output_link_index < owner_.output_links_.size()) {
        owner_.thread_pool().add_timer(
          delay, &input_slot_t::_propagate_fence_abortion, this, pending_fence, output_link_index);
    }
    else {
        // 탈출 조건 ... 전파 완료함
        owner_.destruction_guard_.unlock();
    }
}

void pipepp::impl__::pipe_base::executor_slot::_launch_async(launch_args_t arg)
{
    std::lock_guard destruction_guard{owner_.destruction_guard_};

    assert(!_is_executor_busy());

    fence_index_.store(arg.fence_index);
    fence_object_ = std::move(arg.fence_obj);
    cached_input_ = std::move(arg.input);

    owner_.destruction_guard_.lock();
    owner_.thread_pool().add_task(&executor_slot::_launch_callback, this);
}

kangsw::timer_thread_pool& pipepp::impl__::pipe_base::executor_slot::workers()
{
    return owner_.thread_pool();
}

void pipepp::impl__::pipe_base::executor_slot::_launch_callback()
{
    // 실행기 시동
    std::lock_guard destruction_guard{owner_.destruction_guard_};

    executor()->set_context_ref(&context_write());
    auto exec_res = executor()->invoke__(cached_input_, cached_output_);
    latest_execution_result_ = exec_res;

    if (owner_.output_links_.empty() == false) {
        workers().add_task(&executor_slot::_output_link_callback, this, 0, exec_res > pipe_error::warning);
    }
    else {
        // owner_.destruction_guard_.unlock();
        _perform_post_output();
    }
}

void pipepp::impl__::pipe_base::executor_slot::_perform_post_output()
{
    auto constexpr RELAXED = std::memory_order_relaxed;
    // 연결된 모든 출력을 처리한 경우입니다.

    // 먼저, 연결된 일반 핸들러를 모두 처리합니다.
    auto fence_obj = fence_object_.get();
    auto exec_res = latest_execution_result_.load(RELAXED);
    for (auto& fn : owner_.output_handlers_) {
        fn(exec_res, *fence_obj, cached_output_);
    }

    // 실행기의 내부 상태를 정리합니다.
    fence_object_.reset();
    owner_._rotate_output_order(this); // 출력 순서 회전

    // 실행 문맥 버퍼를 전환합니다.
    _swap_exec_context();
    owner_.latest_exec_context_.store(&context_read(), RELAXED);
    owner_.latest_output_fence_.store(fence_index_.load(RELAXED), RELAXED);

    // fence_index_는 일종의 lock 역할을 수행하므로, 가장 마지막에 지정합니다.
    fence_index_.store(fence_index_t::none, std::memory_order_seq_cst);

    owner_.destruction_guard_.unlock();
}

void pipepp::impl__::pipe_base::executor_slot::_output_link_callback(size_t output_index, bool aborting)
{
    auto& link = owner_.output_links_[output_index];
    auto& slot = link.pipe->input_slot_;
    using namespace std::chrono;
    auto delay = 0us;

    if (!is_output_order_.load(std::memory_order_relaxed)) {
        // 만약 이 실행기의 출력 순서가 아직 오지 않았다면, 단순히 대기합니다.
        delay = 50us;
    }
    else if (auto check = slot.can_submit_input(fence_index_); check.has_value()) {
        auto input_manip = [this, &link](std::any& out) {
            link.handler(*fence_object_, cached_output_, out);
        };

        // 만약 optional인 경우, 입력이 준비되지 않았다면 abort에 true를 지정해,
        //현재 입력 fence를 즉시 취소합니다.
        bool const abort_optional = aborting || (slot.is_optional_ && !*check);
        bool const can_try_submit = *check || abort_optional;
        if (can_try_submit && slot._submit_input(fence_index_, owner_.id(), input_manip, fence_object_, abort_optional)) {
            // no need to retry.
            // go to next index.
            ++output_index;
        }
        else {
            // Retry after short delay.
            delay = 200us;
        }
    }
    else { // simply discards current output link
        ++output_index;
    }

    if (output_index < owner_.output_links_.size()) {
        // 다음 출력 콜백을 예약합니다.
        workers().add_timer(delay, &executor_slot::_output_link_callback, this, output_index, aborting);
    }
    else {
        _perform_post_output();
    }
}

void pipepp::impl__::pipe_base::_connect_output_to_impl(pipe_base* other, pipepp::impl__::pipe_base::output_link_adapter_t adapter)
{
    if (is_launched()) {
        throw pipe_link_exception("pipe already launched");
    }

    if (this == other) {
        throw pipe_link_exception("cannot link with itself");
    }

    // 출력을 대상의 입력에 연결합니다.
    // - 중복되지 않아야 합니다.
    // - 출력이 입력으로 순환하지 않아야 합니다.
    // - 출력은 자신을 포함한, 입력은 부모 중 가장 가까운 optional 노드를 공유해야 합니다.
    for (auto& out : output_links_) {
        if (out.pipe->id() == other->id())
            throw pipe_link_exception("pipe link duplication");
    }

    // 재귀적 탐색을 위한 predicate 함수
    //
    auto output_recurse = [](pipe_base* ty, auto emplacer) {
        for (auto& out : ty->output_links_) {
            emplacer(out.pipe);
        }
    };
    auto input_recurse = [](pipe_base* ty, auto emplacer) {
        for (auto& in : ty->input_links_) {
            emplacer(in.pipe);
        }
    };

    // 출력 파이프의 순환 여부 검사
    // other 노드의 출력 노드를 재귀적으로 탐색해, 입력 노드와 순환하는지 검색합니다.
    if (
      bool is_met = false;
      kangsw::recurse_for_each(
        other, output_recurse,
        [my_id = id(), &is_met](pipe_base* node) {
            if (node->id() == my_id) {
                is_met = true;
                return kangsw::recurse_return::do_break;
            }
            return kangsw::recurse_return::do_continue;
        }),
      is_met) {
        throw pipe_link_exception("cyclic link found");
    }

    // 각각 가장 가까운 optional node, 가장 먼 essential node를 공유해야 함 (항상 true로 가정)
    pipe_base *optional_to = nullptr, *optional_from = nullptr;
    size_t min_depth = -1;
    kangsw::recurse_for_each(
      other, input_recurse, // 입력 노드는 자기 자신을 포함하지 않고 계산
      [my_id = id(), &optional_to, &min_depth](pipe_base* node, size_t depth) {
          if (node->input_slot_.is_optional_ && depth < min_depth && depth > 0) {
              optional_to = node;
              min_depth = depth;
          }
      });
    min_depth = -1;
    kangsw::recurse_for_each(
      this, input_recurse, // 입력 노드는 자기 자신을 포함하지 않고 계산
      [my_id = id(), &optional_from, &min_depth](pipe_base* node, size_t depth) {
          if (node->input_slot_.is_optional_ && depth < min_depth) {
              optional_from = node;
              min_depth = depth;
          }
      });

    if (other->input_links_.empty() == false && optional_to != optional_from) {
        throw pipe_input_exception("nearlest optional node does not match");
    }

    output_links_.push_back({std::move(adapter), other});
    other->input_links_.push_back({this});
    other->input_slot_.ready_conds_.push_back(input_slot_t::input_link_state::none);

    // If not, somethin's wrong.
    assert(other->input_links_.size() == other->input_slot_.ready_conds_.size());
}

void pipepp::impl__::pipe_base::launch(size_t num_executors, std::function<std::unique_ptr<executor_base>()>&& factory)
{
    if (is_launched()) {
        throw pipe_exception("this pipe is already launched!");
    }

    if (num_executors == 0) {
        throw std::invalid_argument("invalid number of executors");
    }

    // 각 슬롯 인스턴스는 동일한 실행기를 가져야 하므로, 팩토리 함수를 받아와서 생성합니다.
    for (auto index : kangsw::counter_range(num_executors)) {
        executor_slots_.emplace_back(std::make_unique<executor_slot>(*this, factory(), index == 0, executor_options_.get()));
    }

    input_slot_.active_input_fence_.store((fence_index_t)1, std::memory_order_relaxed);
}

void pipepp::impl__::pipe_base::_rotate_output_order(executor_slot* ref)
{
    auto constexpr RELAX = std::memory_order_relaxed;
    for (auto index : kangsw::counter_range(executor_slots_.size())) {
        auto& slot = executor_slots_[index];

        if (slot->_is_output_order().exchange(false)) {
            assert(slot.get() == ref);
            auto next_index = (index + 1) % executor_slots_.size();
            auto& next = executor_slots_[next_index];
            next->_is_output_order().store(true);
            break;
        }
    }

    // ref->_is_output_order().store(false, RELAX);
    // next->_is_output_order().store(true, RELAX);
}

void pipepp::impl__::pipe_base::input_slot_t::_supply_input_to_active_executor(bool is_initial_call)
{
    if (is_initial_call) {
        owner_.destruction_guard_.lock();
    }

    if (owner_._active_exec_slot()._is_executor_busy()) {
        // 차례가 된 실행 슬롯이 여전히 바쁩니다.
        // 재시도를 요청합니다.
        using namespace std::chrono_literals;
        owner_.thread_pool().add_timer(200us, &input_slot_t::_supply_input_to_active_executor, this, false);
        return;
    }

    // 입력이 모두 준비되었으므로, 실행기에 입력을 넘깁니다.
    auto& exec = owner_._active_exec_slot();
    exec._launch_async({
      std::move(active_input_fence_object_),
      active_input_fence(),
      std::move(cached_input_.first),
    });

    // 다음 입력 받을 준비 완료
    owner_._rotate_slot(); // 입력을 받을 실행기 슬롯 회전
    _prepare_next();       // 입력 슬롯 클리어

    owner_.destruction_guard_.unlock();
}

bool pipepp::impl__::pipe_base::input_slot_t::_submit_input(fence_index_t output_fence, pipepp::pipe_id_t input_pipe, std::function<void(std::any&)> const& input_manip, std::shared_ptr<pipepp::base_fence_shared_object> const& fence_obj, bool abort_current)
{
    std::lock_guard lock{cached_input_.second};
    std::lock_guard destruction_guard{owner_.destruction_guard_};

    auto active_fence = active_input_fence();
    if (output_fence < active_fence) {
        // 여러 가지 이유에 의해, 입력 fence가 격상된 경우입니다.
        // true를 반환 --> 호출자는 재시도하지 않으며, 이전 단계의 출력은 버려집니다.
        return true;
    }

    if (active_fence < output_fence) {
        // 실행 슬롯이 준비중입니다.
        // false를 반환해 재시도를 요청합니다.
        return false;
    }

    size_t input_index = 0;
    for (auto& d : owner_.input_links_) {
        if (d.pipe->id() == input_pipe) {
            break;
        }
        ++input_index;
    }

    if (input_index == owner_.input_links_.size()) {
        throw pipe_input_exception("given pipe is not valid input link!");
    }

    auto& input_flag = ready_conds_[input_index];
    if (input_flag != input_link_state::none) {
        throw pipe_input_exception("duplicated input submit request ... something's wrong!");
    }

    if (abort_current) {
        // 현재 입력 슬롯의 펜스를 invalidate합니다.

        // 연결된 각각의 출력 링크에 새로운 인덱스를 전파합니다.
        // 기본 개념은, 현재 인덱스를 기다리고 있는 출력 링크의 입력 인덱스를 넘기는 것이므로,
        //현재 입력 펜스 인덱스를 캡쳐해 전달합니다.
        if (owner_.output_links_.empty() == false) {
            owner_.destruction_guard_.lock();
            owner_.thread_pool().add_task(&input_slot_t::_propagate_fence_abortion, this, active_input_fence(), 0);
        }

        // 다음 인덱스로 넘어갑니다.
        _prepare_next();
        return true;
    }

    // fence object가 비어 있다면, 채웁니다.
    if (active_input_fence_object_ == nullptr) {
        active_input_fence_object_ = fence_obj;
    }

    // 입력 슬롯을 채웁니다.
    input_manip(cached_input_.first);
    ready_conds_[input_index] = input_link_state::valid;

    bool const is_all_input_link_ready
      = std::count(ready_conds_.begin(), ready_conds_.end(),
                   input_link_state::valid)
        == ready_conds_.size();

    if (is_all_input_link_ready) {
        _supply_input_to_active_executor();
    }
    return true;
}

bool pipepp::impl__::pipe_base::input_slot_t::_submit_input_direct(std::any&& input, std::shared_ptr<pipepp::base_fence_shared_object> fence_object)
{
    std::lock_guard destruction_guard{owner_.destruction_guard_};

    if (owner_.input_links_.empty() == false) {
        throw pipe_exception("input cannot directly fed when there's any input link existing");
    }

    if (owner_._active_exec_slot()._is_executor_busy()) {
        return false;
    }

    active_input_fence_object_ = std::move(fence_object);
    cached_input_.first = std::move(input);

    _supply_input_to_active_executor();

    return true;
}

bool pipepp::impl__::pipe_base::input_slot_t::_can_submit_input_direct() const
{
    return owner_._active_exec_slot()._is_executor_busy() == false;
}
