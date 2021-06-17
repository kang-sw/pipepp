#include <bitset>
#include <cassert>
#include "fmt/format.h"
#include "kangsw/helpers/enum_arithmetic.hxx"
#include "kangsw/helpers/misc.hxx"
#include "kangsw/helpers/hash_index.hxx"
#include "kangsw/thread/thread_pool.hxx"
#include "pipepp/options.hpp"
#include "pipepp/pipepp.h"

std::optional<bool> pipepp::detail::pipe_base::input_slot_t::can_submit_input(fence_index_t output_fence) const
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

void pipepp::detail::pipe_base::input_slot_t::_prepare_next()
{
    using namespace kangsw::enum_arithmetic;
    for (auto& e : ready_conds_) { e = input_link_state::none; }
    active_input_fence_ = active_input_fence_.load() + 1;
    this->active_input_fence_object_.reset();
}

void pipepp::detail::pipe_base::input_slot_t::_propagate_fence_abortion(fence_index_t pending_fence, size_t output_link_index)
{
    using namespace std::chrono_literals;

    auto& output_link = *owner_.output_links_[output_link_index].pipe;
    auto& link_input = output_link.input_slot_;
    auto delay = 0us;

    if (auto query_result = link_input.can_submit_input(pending_fence); query_result.has_value()) {
        if (query_result.value() && link_input._submit_input(pending_fence, owner_.id(), {}, {}, true)) {
            ++output_link_index;
        } else {
            delay = 100us;
        }
    } else {
        ++output_link_index;
    }

    if (output_link_index < owner_.output_links_.size()) {
        owner_._thread_pool().add_timer(
          delay, &input_slot_t::_propagate_fence_abortion, this, pending_fence, output_link_index);
    } else {
        // 탈출 조건 ... 전파 완료함
        owner_.destruction_guard_.unlock();
    }
}

void pipepp::detail::pipe_base::executor_slot::_launch_async(launch_args_t arg)
{
    std::lock_guard destruction_guard{owner_.destruction_guard_};

    assert(!_is_executor_busy());

    fence_index_.store(arg.fence_index);
    fence_object_ = std::move(arg.fence_obj);
    cached_input_ = std::move(arg.input);

    owner_.destruction_guard_.lock();
    owner_._thread_pool().add_task(&executor_slot::_launch_callback, this);
}

kangsw::timer_thread_pool& pipepp::detail::pipe_base::executor_slot::workers()
{
    return owner_._thread_pool();
}

void pipepp::detail::pipe_base::executor_slot::_launch_callback()
{
    using namespace kangsw::literals;

    // 실행기 시동
    std::lock_guard destruction_guard{owner_.destruction_guard_};

    executor()->set_context_ref(&context_write());
    pipe_error exec_res;

    PIPEPP_REGISTER_CONTEXT(context_write());
    timer_scope_total_ = context_write().timer_scope("Total Execution Time");

    PIPEPP_ELAPSE_BLOCK("A. Executor Run Time")
    {
        latest_execution_result_.store(
          exec_res = executor()->invoke__(cached_input_, cached_output_),
          std::memory_order_relaxed);
    }

    // 출력 순서까지 대기
    using namespace std::literals;
    PIPEPP_ELAPSE_BLOCK("B. Await for output order")
    while (!_is_output_order()) { std::this_thread::sleep_for(50us); }

    // 먼저, 연결된 일반 핸들러를 모두 처리합니다.
    PIPEPP_ELAPSE_BLOCK("C. Output Handler Overhead")
    {
        auto fence_obj = fence_object_.get();
        for (auto& fn : owner_.output_handlers_) {
            fn(exec_res, *fence_obj, context_write(), cached_output_);
        }
    }

    timer_scope_link_ = context_write().timer_scope("D. Linker Overhead");
    if (owner_.output_links_.empty() == false) {
        // workers().add_task(&executor_slot::_output_link_callback, this, 0, exec_res > pipe_error::warning);
        _perform_output_link(0, exec_res > pipe_error::warning);
    } else {
        _perform_post_output();
    }
}

void pipepp::detail::pipe_base::executor_slot::_perform_post_output()
{
    if (!_is_output_order()) {
        using namespace std::literals;
        owner_._thread_pool().add_timer(100us, &executor_slot::_perform_post_output, this);
        return;
    }

    auto constexpr RELAXED = std::memory_order_relaxed;
    // -- 연결된 모든 출력을 처리한 경우입니다.
    // 타이머 관련 로직 처리
    timer_scope_link_.reset();
    timer_scope_total_.reset();
    owner_._refresh_interval_timer();
    owner_._update_latest_latency(fence_object_->launch_time_point());

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

void pipepp::detail::pipe_base::executor_slot::_perform_output_link(size_t output_index, bool aborting)
{
    PIPEPP_REGISTER_CONTEXT(context_write());
    for (; output_index < owner_.output_links_.size();) {
        assert(_is_output_order());

        auto& link = owner_.output_links_[output_index];
        auto& slot = link.pipe->input_slot_;
        using namespace std::chrono;
        auto delay = 0us;

        PIPEPP_ELAPSE_SCOPE_DYNAMIC(fmt::format(":: [{}]", link.pipe->name()).c_str());
        if (link.pipe->is_launched() == false) {
            throw pipe_exception("linked pipe is not launched yet!");
        }

        if (auto check = slot.can_submit_input(fence_index_); check.has_value()) {
            auto input_manip = [this, &link](std::any& out) {
                return link.handler(*fence_object_, context_write(), cached_output_, out, link.pipe->options());
            };

            // 만약 optional인 경우, 입력이 준비되지 않았다면 abort에 true를 지정해,
            //현재 입력 fence를 즉시 취소합니다.
            bool const abort_optional = aborting || (slot.is_optional_ && !*check);
            bool const can_try_submit = *check || abort_optional;
            if (can_try_submit && slot._submit_input(fence_index_, owner_.id(), input_manip, fence_object_, abort_optional)) {
                // no need to retry.
                // go to next index.
                ++output_index;

                if (!link.pipe->is_paused() && owner_._is_selective_output() && !aborting && !slot.is_optional_) {
                    // Optional 출력이 아닌 출력 노드에 대해, 성공적으로 입력을 제출한 경우입니다.
                    // selective 출력이 활성화되었다면, 나머지 출력 링크를 버립니다.
                    aborting = true;
                }
            } else {
                // Retry after short delay.
                delay = 200us;
            }
        } else { // simply discards current output link
            ++output_index;
        }

        // 다음 출력 콜백을 예약합니다.
        // workers().add_timer(delay, &executor_slot::_output_link_callback, this, output_index, aborting);
        if (delay > 0us) { std::this_thread::sleep_for(delay); }
    }
    _perform_post_output();
}

pipepp::detail::pipe_base::pipe_base(std::string name, bool optional_pipe)
    : name_(name)
    , executor_options_(std::make_unique<option_base>())
{
    input_slot_.is_optional_ = optional_pipe;
}

pipepp::detail::pipe_base::~pipe_base()
{
}

pipepp::detail::pipe_base::tweak_t pipepp::detail::pipe_base::get_prelaunch_tweaks()
{
    if (is_launched()) { throw pipe_exception("tweak must be editted before launch!"); }
    return tweak_t{
      .selective_input = &mode_selectie_input_,
      .selective_output = &mode_selective_output_,
      .is_optional = &input_slot_.is_optional_,
    };
}

pipepp::detail::pipe_base::const_tweak_t pipepp::detail::pipe_base::read_tweaks() const
{
    return const_tweak_t{
      .selective_input = &mode_selectie_input_,
      .selective_output = &mode_selective_output_,
      .is_optional = &input_slot_.is_optional_,
    };
}

void pipepp::detail::pipe_base::_connect_output_to_impl(pipe_base* other, pipepp::detail::pipe_base::output_link_adapter_type adapter)
{
    if (is_launched() || other->is_launched()) {
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
      kangsw::recurse_for_each(other, [&](pipe_base* node, auto emplacer) {
          if (id() == node->id()) {
              is_met = true;
          } else {
              output_recurse(node, emplacer);
          }
      }),
      is_met) {
        throw pipe_link_exception("cyclic link found");
    }

    // 각각 가장 가까운 optional node, 가장 먼 essential node를 공유해야 함 (항상 true로 가정)
    pipe_base *optional_to = nullptr, *optional_from = nullptr;
    size_t min_depth = -1;
    kangsw::recurse_for_each(other, [&, my_id = id()](pipe_base* node, size_t depth, auto emplacer) {
        if (node->input_slot_.is_optional_ && depth < min_depth && depth > 0) {
            optional_to = node;
            min_depth = depth;
        }
        input_recurse(node, emplacer);
    });
    min_depth = -1;
    kangsw::recurse_for_each(
      this, [&, my_id = id()](pipe_base* node, size_t depth, auto emplacer) {
          if (node->input_slot_.is_optional_ && depth < min_depth) {
              optional_from = node;
              min_depth = depth;
          }
          input_recurse(node, emplacer);
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

void pipepp::detail::pipe_base::executor_conditions(std::vector<executor_condition_t>& conds) const
{
    conds.resize(num_executors());
    for (auto i : kangsw::iota(conds.size())) {
        auto& exec = *executor_slots_[i];

        if (exec._is_busy()) {
            conds[i] = _pending_output_slot_index() == i ? executor_condition_t::busy_output : executor_condition_t::busy;
        } else {
            conds[i] = _pending_output_slot_index() == i
                         ? recently_aborted()
                             ? executor_condition_t::idle_aborted
                             : executor_condition_t::idle_output
                         : exec.latest_exec_result() > pipe_error::warning
                             ? executor_condition_t::idle_aborted
                             : executor_condition_t::idle;
        }
    }
}

void pipepp::detail::pipe_base::mark_dirty()
{
    for (auto& exec_ptr : executor_slots_) {
        exec_ptr->context_write().mark_dirty();
    }
}

void pipepp::detail::pipe_base::launch(size_t num_executors, std::function<std::unique_ptr<executor_base>()>&& factory)
{
    if (is_launched()) {
        throw pipe_exception("this pipe is already launched!");
    }

    if (num_executors == 0) {
        throw std::invalid_argument("invalid number of executors");
    }

    // 각 슬롯 인스턴스는 동일한 실행기를 가져야 하므로, 팩토리 함수를 받아와서 생성합니다.
    for (auto index : kangsw::iota(num_executors)) {
        executor_slots_.emplace_back(std::make_unique<executor_slot>(*this, factory(), index, &options()));
    }

    input_slot_.active_input_fence_.store((fence_index_t)1, std::memory_order_relaxed);
}

void pipepp::detail::pipe_base::_rotate_output_order(executor_slot* ref)
{
    assert(ref == executor_slots_[_pending_output_slot_index()].get());
    output_exec_slot_.fetch_add(1);
}

void pipepp::detail::pipe_base::_refresh_interval_timer()
{
    constexpr auto RELAXED = std::memory_order_relaxed;
    auto tp = latest_output_tp_.load(RELAXED);
    latest_interval_.store(system_clock::now() - tp);
    latest_output_tp_.compare_exchange_strong(tp, system_clock::now());
}

void pipepp::detail::pipe_base::_update_latest_latency(system_clock::time_point launched)
{
    constexpr auto RELAXED = std::memory_order_relaxed;
    latest_output_latency_.store(system_clock::now() - launched, RELAXED);
}

void pipepp::detail::pipe_base::input_slot_t::_supply_input_to_active_executor(bool is_initial_call)
{
    if (is_initial_call) {
        owner_.destruction_guard_.lock();
    }

    if (owner_._active_exec_slot()._is_executor_busy()) {
        // 차례가 된 실행 슬롯이 여전히 바쁩니다.
        // 재시도를 요청합니다.
        using namespace std::chrono_literals;
        owner_._thread_pool().add_timer(200us, &input_slot_t::_supply_input_to_active_executor, this, false);
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

bool pipepp::detail::pipe_base::input_slot_t::_submit_input(fence_index_t output_fence, pipepp::pipe_id_t input_pipe, std::function<bool(std::any&)> const& input_manip, std::shared_ptr<pipepp::base_shared_context> const& fence_obj, bool abort_current)
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

    bool should_abort_input = abort_current || owner_.is_paused();

    // fence object가 비어 있다면, 채웁니다.
    if (active_input_fence_object_ == nullptr) {
        active_input_fence_object_ = fence_obj;
    }

    // 입력을 전달합니다. 만약 입력에 실패한다면, 즉시 입력을 취소하게 됩니다.
    if (!should_abort_input) { should_abort_input = !input_manip(cached_input_.first); }

    // 해당하는 입력 슬롯을 채우거나, 버립니다.
    ready_conds_[input_index] = should_abort_input ? input_link_state::discarded : input_link_state::valid;

    bool const is_all_input_link_ready
      = std::ranges::count(ready_conds_, input_link_state::valid) == ready_conds_.size();

    if ((!should_abort_input && owner_._is_selective_input()) || is_all_input_link_ready) {
        _supply_input_to_active_executor();
        owner_._update_abort_received(false);
        return true;
    }

    bool const all_filled = std::ranges::count(ready_conds_, input_link_state::none) == 0;
    bool const contains_abort = all_filled && !is_all_input_link_ready;
    if ((!owner_._is_selective_input() && should_abort_input)
        || contains_abort // 선택적 입력인 경우 모두 채워질 때까지 유예합니다.
    ) {
        // 선택적 입력이 아니라면 즉시 abort를 propagate하고, 선택적 입력이라면 전체가 결과를 반환할 때까지 대기합니다.
        if (owner_.output_links_.empty() == false) {
            owner_.destruction_guard_.lock();
            owner_._thread_pool().add_task(&input_slot_t::_propagate_fence_abortion, this, active_input_fence(), 0);
        }

        // 다음 인덱스로 넘어갑니다.
        owner_._update_abort_received(true);
        _prepare_next();
    }

    // 입력이 성공적으로 제출되었거나, abort가 처리되었습니다.
    return true;
}

bool pipepp::detail::pipe_base::input_slot_t::_submit_input_direct(std::any&& input, std::shared_ptr<pipepp::base_shared_context> fence_object)
{
    if (owner_.is_paused()) { return false; }

    std::lock_guard lock{cached_input_.second};
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

bool pipepp::detail::pipe_base::input_slot_t::_can_submit_input_direct() const
{
    return owner_._active_exec_slot()._is_executor_busy() == false;
}
