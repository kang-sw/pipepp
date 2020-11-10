#include <bitset>
#include <kangsw/enum_arithmetic.hxx>
#include <kangsw/misc.hxx>
#include <pipepp/pipe.hpp>

std::optional<bool> pipepp::impl__::pipe_base::input_slot_t::can_submit_input(fence_index_t fence) const
{
    auto active = active_input_fence();
    if (fence < active) {
        // ���� ���� ������ ����, �Է� fence�� �ݻ�� ����Դϴ�.
        // true�� ��ȯ --> ȣ���ڴ� ��õ����� ������, ���� �ܰ��� ����� �������ϴ�.
        return {};
    }

    // fence == active : �Է� ������ �غ�Ǿ����ϴ�.
    //       !=        : ���� �������� ó��������, �Է� ������ �غ���� �ʾҽ��ϴ�.
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
        // ���� ���� ������ ����, �Է� fence�� �ݻ�� ����Դϴ�.
        // true�� ��ȯ --> ȣ���ڴ� ��õ����� ������, ���� �ܰ��� ����� �������ϴ�.
        return true;
    }

    if (active_fence < output_fence) {
        // �Է� ������ ���� �غ���� �ʾҽ��ϴ�.
        // false�� ��ȯ�� ��õ��� ��û�մϴ�.
        return false;
    }

    if (owner_.active_exec_slot().is_busy()) {
        // ���ʰ� �� ���� ������ �ٻ޴ϴ�.
        // ��õ��� ��û�մϴ�.
        return false;
    }

    auto& input_flag = ready_conds_[input_index];
    if (input_flag != input_link_state::none) {
        throw pipe_input_exception("duplicated input submit request ... something's wrong!");
    }

    if (abort_current) {

        // ���� �Է� ������ �潺�� invalidate�մϴ�.
        // �潺�� ���� �ε����� �Ѿ
        _prepare_next();
        return true;
    }

    // fence object�� ��� �ִٸ�, ä��ϴ�.
    if (active_input_fence_object_ == nullptr) {
        active_input_fence_object_ = fence_obj;
    }

    // �Է� ������ ä��ϴ�.
    input_manip(cached_input_.first);
    ready_conds_[input_index] = input_link_state::valid;

    bool const is_all_input_link_ready
      = std::count(ready_conds_.begin(), ready_conds_.end(),
                   input_link_state::valid)
        == ready_conds_.size();

    if (is_all_input_link_ready) {
        // �Է��� ��� �غ�Ǿ����Ƿ�, ����⿡ �Է��� �ѱ�ϴ�.
        auto& exec = owner_.active_exec_slot();
        exec._launch_async({
          std::move(active_input_fence_object_),
          active_fence,
          std::move(cached_input_.first),
        });

        // ���� �Է� ���� �غ� �Ϸ�
        _prepare_next();
    }
    return true;
}
