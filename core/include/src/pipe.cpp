#include <bitset>
#include <kangsw/enum_arithmetic.hxx>
#include <kangsw/misc.hxx>
#include <pipepp/pipe.hpp>

std::optional<bool> pipepp::impl__::pipe_base::input_slot_t::can_submit_input(fence_index_t fence) const
{
    auto active = active_input_fence();
    if (fence < active) {
        // ���� ���� ������ ����, �Է� fence�� �ݻ�� ����Դϴ�.
        // non-opt ��ȯ --> ȣ���ڴ� ��õ����� ������, ���� �ܰ��� ����� �������ϴ�.
        return {};
    }

    // fence == active : �Է� ������ �غ�Ǿ����ϴ�.
    //       >         : ���� �������� ó��������, �Է� ������ �غ���� �ʾҽ��ϴ�.
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
    // ����� �õ�
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
        // ���� ��� �ݹ��� �����մϴ�.
        workers().timer(delay, &executor_slot::_output_link_callback, this, output_index, aborting);
    }
    else {
        // ����� ��� ����� ó���� ����, ���� ���� ������ idle ���·� �����ϴ�.
        fence_object_.reset();
        _swap_exec_context();

        // fence_index_�� ������ lock ������ �����ϹǷ�, ���� �������� �����մϴ�.
        fence_index_.store(fence_index_t::none, std::memory_order_seq_cst);
    }
}

bool pipepp::impl__::pipe_base::input_slot_t::submit_input(fence_index_t output_fence, pipepp::pipe_id_t input_pipe, std::function<void(std::any&)> const& input_manip, std::shared_ptr<pipepp::base_fence_shared_object> const& fence_obj, bool abort_current)
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
