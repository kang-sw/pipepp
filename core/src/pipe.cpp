#include <bitset>
#include <cassert>
#include <kangsw/enum_arithmetic.hxx>
#include <kangsw/misc.hxx>
#include <pipepp/pipe.hpp>

std::optional<bool> pipepp::impl__::pipe_base::input_slot_t::can_submit_input(fence_index_t output_fence) const
{
    auto active = active_input_fence();
    if (output_fence < active) {
        // ���� ���� ������ ����, �Է� fence�� �ݻ�� ����Դϴ�.
        // non-opt ��ȯ --> ȣ���ڴ� ��õ����� ������, ���� �ܰ��� ����� �������ϴ�.
        return {};
    }

    // ����, �̿� ������ �Է� ������ �־�� �մϴ�.
    bool is_idle = owner_._active_exec_slot()._is_executor_busy() == false;

    // fence == active : �Է� ������ �غ�Ǿ����ϴ�.
    //       >         : ���� �������� ó��������, �Է� ������ �غ���� �ʾҽ��ϴ�.
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
        // Ż�� ���� ... ���� �Ϸ���
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
    // ����� �õ�
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
    // ����� ��� ����� ó���� ����Դϴ�.

    // ����, ����� �Ϲ� �ڵ鷯�� ��� ó���մϴ�.
    auto fence_obj = fence_object_.get();
    auto exec_res = latest_execution_result_.load(RELAXED);
    for (auto& fn : owner_.output_handlers_) {
        fn(exec_res, *fence_obj, cached_output_);
    }

    // ������� ���� ���¸� �����մϴ�.
    fence_object_.reset();
    owner_._rotate_output_order(this); // ��� ���� ȸ��

    // ���� ���� ���۸� ��ȯ�մϴ�.
    _swap_exec_context();
    owner_.latest_exec_context_.store(&context_read(), RELAXED);
    owner_.latest_output_fence_.store(fence_index_.load(RELAXED), RELAXED);

    // fence_index_�� ������ lock ������ �����ϹǷ�, ���� �������� �����մϴ�.
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
        // ���� �� ������� ��� ������ ���� ���� �ʾҴٸ�, �ܼ��� ����մϴ�.
        delay = 50us;
    }
    else if (auto check = slot.can_submit_input(fence_index_); check.has_value()) {
        auto input_manip = [this, &link](std::any& out) {
            link.handler(*fence_object_, cached_output_, out);
        };

        // ���� optional�� ���, �Է��� �غ���� �ʾҴٸ� abort�� true�� ������,
        //���� �Է� fence�� ��� ����մϴ�.
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
        // ���� ��� �ݹ��� �����մϴ�.
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

    // ����� ����� �Է¿� �����մϴ�.
    // - �ߺ����� �ʾƾ� �մϴ�.
    // - ����� �Է����� ��ȯ���� �ʾƾ� �մϴ�.
    // - ����� �ڽ��� ������, �Է��� �θ� �� ���� ����� optional ��带 �����ؾ� �մϴ�.
    for (auto& out : output_links_) {
        if (out.pipe->id() == other->id())
            throw pipe_link_exception("pipe link duplication");
    }

    // ����� Ž���� ���� predicate �Լ�
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

    // ��� �������� ��ȯ ���� �˻�
    // other ����� ��� ��带 ��������� Ž����, �Է� ���� ��ȯ�ϴ��� �˻��մϴ�.
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

    // ���� ���� ����� optional node, ���� �� essential node�� �����ؾ� �� (�׻� true�� ����)
    pipe_base *optional_to = nullptr, *optional_from = nullptr;
    size_t min_depth = -1;
    kangsw::recurse_for_each(
      other, input_recurse, // �Է� ���� �ڱ� �ڽ��� �������� �ʰ� ���
      [my_id = id(), &optional_to, &min_depth](pipe_base* node, size_t depth) {
          if (node->input_slot_.is_optional_ && depth < min_depth && depth > 0) {
              optional_to = node;
              min_depth = depth;
          }
      });
    min_depth = -1;
    kangsw::recurse_for_each(
      this, input_recurse, // �Է� ���� �ڱ� �ڽ��� �������� �ʰ� ���
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

    // �� ���� �ν��Ͻ��� ������ ����⸦ ������ �ϹǷ�, ���丮 �Լ��� �޾ƿͼ� �����մϴ�.
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
        // ���ʰ� �� ���� ������ ������ �ٻ޴ϴ�.
        // ��õ��� ��û�մϴ�.
        using namespace std::chrono_literals;
        owner_.thread_pool().add_timer(200us, &input_slot_t::_supply_input_to_active_executor, this, false);
        return;
    }

    // �Է��� ��� �غ�Ǿ����Ƿ�, ����⿡ �Է��� �ѱ�ϴ�.
    auto& exec = owner_._active_exec_slot();
    exec._launch_async({
      std::move(active_input_fence_object_),
      active_input_fence(),
      std::move(cached_input_.first),
    });

    // ���� �Է� ���� �غ� �Ϸ�
    owner_._rotate_slot(); // �Է��� ���� ����� ���� ȸ��
    _prepare_next();       // �Է� ���� Ŭ����

    owner_.destruction_guard_.unlock();
}

bool pipepp::impl__::pipe_base::input_slot_t::_submit_input(fence_index_t output_fence, pipepp::pipe_id_t input_pipe, std::function<void(std::any&)> const& input_manip, std::shared_ptr<pipepp::base_fence_shared_object> const& fence_obj, bool abort_current)
{
    std::lock_guard lock{cached_input_.second};
    std::lock_guard destruction_guard{owner_.destruction_guard_};

    auto active_fence = active_input_fence();
    if (output_fence < active_fence) {
        // ���� ���� ������ ����, �Է� fence�� �ݻ�� ����Դϴ�.
        // true�� ��ȯ --> ȣ���ڴ� ��õ����� ������, ���� �ܰ��� ����� �������ϴ�.
        return true;
    }

    if (active_fence < output_fence) {
        // ���� ������ �غ����Դϴ�.
        // false�� ��ȯ�� ��õ��� ��û�մϴ�.
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

        // ����� ������ ��� ��ũ�� ���ο� �ε����� �����մϴ�.
        // �⺻ ������, ���� �ε����� ��ٸ��� �ִ� ��� ��ũ�� �Է� �ε����� �ѱ�� ���̹Ƿ�,
        //���� �Է� �潺 �ε����� ĸ���� �����մϴ�.
        if (owner_.output_links_.empty() == false) {
            owner_.destruction_guard_.lock();
            owner_.thread_pool().add_task(&input_slot_t::_propagate_fence_abortion, this, active_input_fence(), 0);
        }

        // ���� �ε����� �Ѿ�ϴ�.
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
