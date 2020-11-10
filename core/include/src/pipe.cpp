#include <bitset>
#include <kangsw/enum_arithmetic.hxx>
#include <kangsw/misc.hxx>
#include <pipepp/pipe.hpp>

std::optional<bool> pipepp::impl__::pipe_base::input_slot_t::can_submit_input(fence_index_t fence) const
{
    auto active = active_input_fence();
    if (fence < active) {
        // 여러 가지 이유에 의해, 입력 fence가 격상된 경우입니다.
        // true를 반환 --> 호출자는 재시도하지 않으며, 이전 단계의 출력은 버려집니다.
        return {};
    }

    // fence == active : 입력 슬롯이 준비되었습니다.
    //       !=        : 아직 파이프가 처리중으로, 입력 슬롯이 준비되지 않았습니다.
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
    fence_index_.store(arg.fence_index);
    fence_object_ = std::move(arg.fence_obj);
    cached_input_ = std::move(arg.input);


}

bool pipepp::impl__::pipe_base::input_slot_t::submit_input(fence_index_t output_fence, size_t input_index, std::function<void(std::any&)> const& input_manip, std::shared_ptr<pipepp::base_fence_shared_object> const& fence_obj, bool abort_current)
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
