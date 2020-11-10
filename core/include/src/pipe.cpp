#include <bitset>
#include <kangsw/enum_arithmetic.hxx>
#include <kangsw/misc.hxx>
#include <pipepp/pipe.hpp>

std::optional<bool> pipepp::impl__::pipe_base::input_slot_t::can_submit_input(fence_index_t fence) const
{
    auto active = active_input_fence();
    if (fence < active) {
        // 여러 가지 이유에 의해, 입력 fence가 격상된 경우입니다.
        // non-opt 반환 --> 호출자는 재시도하지 않으며, 이전 단계의 출력은 버려집니다.
        return {};
    }

    // fence == active : 입력 슬롯이 준비되었습니다.
    //       >         : 아직 파이프가 처리중으로, 입력 슬롯이 준비되지 않았습니다.
    return fence == active;
}

void pipepp::impl__::pipe_base::input_slot_t::_prepare_next()
{
    using namespace kangsw::enum_arithmetic;
    for (auto& e : ready_conds_) { e = input_link_state::none; }
    active_input_fence_ = active_input_fence_.load() + 1;
    this->active_input_fence_object_.reset();
}

void pipepp::impl__::pipe_base::executor_slot::_launch_async(launch_args_t arg)
{
    if (is_busy()) {
        throw pipe_exception("executor is busy!");
    }

    fence_index_.store(arg.fence_index);
    fence_object_ = std::move(arg.fence_obj);
    cached_input_ = std::move(arg.input);

    owner_.thread_pool().task(&executor_slot::_launch_callback, this);
}

kangsw::timer_thread_pool& pipepp::impl__::pipe_base::executor_slot::workers()
{
    return owner_.thread_pool();
}

void pipepp::impl__::pipe_base::executor_slot::_launch_callback()
{
    // 실행기 시동
    executor()->set_context_ref(&context_write());
    auto exec_res = executor()->invoke__(cached_input_, cached_output_);
    latest_execution_result_ = exec_res;

    auto fence_obj = fence_object_.get();
    for (auto& fn : owner_.output_handlers_) {
        fn(exec_res, *fence_obj, cached_output_);
    }

    if (owner_.output_links_.empty() == false) {
        workers().task(&executor_slot::_output_link_callback, this, 0, exec_res > pipe_error::warning);
    }
}

void pipepp::impl__::pipe_base::executor_slot::_output_link_callback(size_t output_index, bool aborting)
{
    auto& link = owner_.output_links_[output_index];
    auto& slot = link.pipe->input_slot_;
    using namespace std::chrono;
    auto delay = 0us;

    if (auto check = slot.can_submit_input(fence_index_); check.has_value()) {
        auto input_manip = [this, &link](std::any& out) {
            link.handler(*fence_object_, cached_output_, out);
        };

        if (*check && slot.submit_input(fence_index_, owner_.id(), input_manip, fence_object_, aborting)) {
            // no need to retry.
            // go to next index.
            ++output_index;
        }
        else {
            // Retry after short delay.
            delay = 100us;
        }
    }
    else { // simply discards current output link
        ++output_index;
    }

    if (output_index < owner_.output_links_.size()) {
        // 다음 출력 콜백을 예약합니다.
        workers().timer(delay, &executor_slot::_output_link_callback, this, output_index, aborting);
    }
    else {
        // 연결된 모든 출력을 처리한 경우로, 현재 실행 슬롯을 idle 상태로 돌립니다.
        fence_object_.reset();
        _swap_exec_context();

        // fence_index_는 일종의 lock 역할을 수행하므로, 가장 마지막에 지정합니다.
        fence_index_.store(fence_index_t::none, std::memory_order_seq_cst);
    }
}

bool pipepp::impl__::pipe_base::input_slot_t::submit_input(fence_index_t output_fence, pipepp::pipe_id_t input_pipe, std::function<void(std::any&)> const& input_manip, std::shared_ptr<pipepp::base_fence_shared_object> const& fence_obj, bool abort_current)
{
    std::lock_guard<std::mutex> lock{cached_input_.second};
    auto active_fence = active_input_fence();
    if (output_fence < active_fence) {
        // 여러 가지 이유에 의해, 입력 fence가 격상된 경우입니다.
        // true를 반환 --> 호출자는 재시도하지 않으며, 이전 단계의 출력은 버려집니다.
        return true;
    }

    if (active_fence < output_fence) {
        // 입력 슬롯이 아직 준비되지 않았습니다.
        // false를 반환해 재시도를 요청합니다.
        return false;
    }

    if (owner_.active_exec_slot().is_busy()) {
        // 차례가 된 실행 슬롯이 바쁩니다.
        // 재시도를 요청합니다.
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
        // 펜스는 다음 인덱스로 넘어감
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
        // 입력이 모두 준비되었으므로, 실행기에 입력을 넘깁니다.
        auto& exec = owner_.active_exec_slot();
        exec._launch_async({
          std::move(active_input_fence_object_),
          active_fence,
          std::move(cached_input_.first),
        });

        // 다음 입력 받을 준비 완료
        _prepare_next();
    }
    return true;
}
