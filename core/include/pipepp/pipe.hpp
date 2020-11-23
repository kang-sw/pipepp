#pragma once
#include <any>
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>

#include "kangsw/misc.hxx"
#include "kangsw/ptr_proxy.hxx"
#include "kangsw/thread_pool.hxx"
#include "kangsw/thread_utility.hxx"
#include "pipepp/execution_context.hpp"
#include "pipepp/options.hpp"

namespace pipepp {
namespace detail {
class pipeline_base;
}

/** 파이프 에러 형식 */
enum class pipe_error {
    ok,
    warning,
    abort,
    error,
    fatal
};

/** 파이프 예외 형식 */
class pipe_exception : public std::exception {
public:
    explicit pipe_exception(const char* msg)
        : exception(combine_stack_trace(msg).c_str())
    {
    }

private:
    std::string combine_stack_trace(const char* msg) { return msg; /* TODO */ }
};

class pipe_link_exception : public pipe_exception {
public:
    explicit pipe_link_exception(const char* msg)
        : pipe_exception(msg)
    {
    }
};

class pipe_input_exception : public pipe_exception {
public:
    explicit pipe_input_exception(const char* msg)
        : pipe_exception(msg)
    {
    }
};

/**
 * Fence object는 pipeline에 데이터를 처음으로 입력할 때 생성되는 오브젝트로, 각각의 pipe에 의해 처리될 때 공유되는 오브젝트 형식입니다.
 */
enum class fence_index_t : size_t { none = 0 };

/** 각각의 Pipe는 생성 시 부여된 고유한 pipe id를 갖습니다. */
enum class pipe_id_t : size_t { none = -1 };

/** fence shared data의 기본 상속형입니다. */
struct base_shared_context {
    friend class detail::pipeline_base;
    virtual ~base_shared_context() = default;
    auto& option() const noexcept { return global_options_; }
    operator detail::option_base const &() const { return *global_options_; }
    auto launch_time_point() const { return launched_; }

private:
    detail::option_base const* global_options_;
    std::chrono::system_clock::time_point launched_;
};

enum class executor_condition_t : uint8_t {
    idle,
    idle_aborted,
    idle_output,
    busy,
    busy_output
};

namespace detail {

/** 기본 실행기. */
class executor_base {
public:
    virtual ~executor_base() = default;
    virtual pipe_error invoke__(std::any& input, std::any& output) = 0;

public:
    void set_context_ref(execution_context* ref) { context_ = ref, context_->_clear_records(); }

    // 실제 실행기의 레퍼런스를 획득합니다.
    // 추후, json_option_interface를 상속하는 실행기 등에 사용
    template <typename Ty_> Ty_ const* actual_executor() const { return dynamic_cast<Ty_ const*>(_get_actual_executor()); }
    template <typename Ty_> Ty_* actual_executor() { return dynamic_cast<Ty_*>(const_cast<void*>(_get_actual_executor())); }

private:
    virtual void const* _get_actual_executor() const = 0;

protected:
    execution_context* context_ = nullptr;
};

struct pipe_id_gen {
    inline static size_t gen_ = 0;
    static pipe_id_t generate() { return static_cast<pipe_id_t>(gen_++); }
};

/**
 * 파이프 기본 클래스
 */
class pipe_base final : public std::enable_shared_from_this<pipe_base> {
public:
    using output_link_adapter_type = std::function<void(base_shared_context&, execution_context&, std::any const& output, std::any& input)>;
    using output_handler_type = std::function<void(pipe_error, base_shared_context&, execution_context&, std::any const&)>;
    using system_clock = std::chrono::system_clock;

    explicit pipe_base(std::string name, bool optional_pipe = false)
        : name_(name)
    {
        input_slot_.is_optional_ = optional_pipe;
    }

public:
    struct tweak_t {
        kangsw::ptr_proxy<bool> selective_input;
        kangsw::ptr_proxy<bool> selective_output;
        kangsw::ptr_proxy<bool> is_optional;
    };
    struct const_tweak_t {
        kangsw::ptr_proxy<const bool> selective_input;
        kangsw::ptr_proxy<const bool> selective_output;
        kangsw::ptr_proxy<const bool> is_optional;
    };

    /** pre launch tweak 획득 */
    tweak_t get_prelaunch_tweaks();
    const_tweak_t read_tweaks() const;

    class input_slot_t {
        friend class pipe_base;

    public:
        explicit input_slot_t(pipe_base& owner)
            : owner_(owner)
        {
        }

        /** ready condition 개수 설정. 초기화 함수 */
        void _supply_input_to_active_executor(bool is_initial_call = true);

    public:
        /**
         * 입력 인덱스 검증하기
         */
        fence_index_t active_input_fence() const { return active_input_fence_; }

        /**
         * @return has_value() == false이면 현재 입력을 버려야 합니다.
         */
        std::optional<bool> can_submit_input(fence_index_t output_fence) const;
        /**
         * 입력 데이터 공급 완료 후 호출합니다.
         * ready_conds_의 해당 인덱스를 활성화합니다.
         * ready_conds_의 모든 인덱스가 has_value()를 리턴하면 입력 시퀀스가 종료됩니다.
         *
         * 내부적으로는 owner_에게 입력 시퀀스 갱신 요청
         * 입력이 qualified 되면 owner_의 입력 가능 fence index가 1 증가합니다. 새로운 fence index는 활성화된 executor_slot에 할당됩니다. 그러나, 활성화된 executor_slot이 여전히 실행 중이면 active_slot()을 얻을 수 없으며, 따라서 입력 또한 disable 상태가 됩니다.
         *
         * @returns true 반환시 처리 완료, false 반환 시 retry가 필요합니다.
         */
        bool _submit_input(
          fence_index_t output_fence,
          pipe_id_t input_index,
          std::function<void(std::any&)> const& input_manip,
          std::shared_ptr<base_shared_context> const& fence_obj,
          bool abort_current = false);

        /**
         * 주어진 입력 데이터로 즉시 실행합니다. 입력 링크가 없는 경우에만 가능.(있으면 예외 던짐)
         * 외부에서 파이프라인에 직접 입력을 공급할 수 있습니다.
         *
         * 단, 이를 위해 적어도 하나의 실행기가 비어 있어야 합니다. 아니면 false를 반환합니다.
         */
        bool _submit_input_direct(std::any&& input, std::shared_ptr<base_shared_context> fence_object);
        bool _can_submit_input_direct() const;

    private:
        void _prepare_next();
        void _propagate_fence_abortion(fence_index_t pending_fence, size_t output_link_index);

    private:
        // clang-format off
        enum class input_link_state { none, valid, discarded };
        // clang-format on

    private:
        pipe_base& owner_;
        bool is_optional_ = false;
        std::pair<std::any, std::mutex> cached_input_;
        std::vector<input_link_state> ready_conds_;
        std::atomic<fence_index_t> active_input_fence_ = fence_index_t::none;
        std::shared_ptr<base_shared_context> active_input_fence_object_;
    };

    class alignas(64) executor_slot {
    public:
        explicit executor_slot(pipe_base& owner,
                               std::unique_ptr<executor_base>&& exec,
                               size_t index,
                               option_base* options)
            : owner_(owner)
            , executor_(std::move(exec))
            , index_(index)
        {
            context_._internal__set_option(options);
        }

    public: // 실행 문맥 관련
        executor_base* executor() const { return executor_.get(); }
        execution_context const& context_read() const { return context_; }
        execution_context& context_write() { return context_; }
        fence_index_t fence_index() const { return fence_index_; }
        bool _is_executor_busy() const { return fence_index_ != fence_index_t::none; }
        bool _is_output_order() const { return index_ == owner_._pending_output_slot_index(); }
        bool _is_busy() const { return timer_scope_total_.has_value(); }
        auto latest_exec_result() const { return latest_execution_result_.load(std::memory_order_relaxed); }

    public:
        struct launch_args_t {
            std::shared_ptr<base_shared_context> fence_obj;
            fence_index_t fence_index;
            std::any input;
        };
        void _launch_async(launch_args_t arg);
        kangsw::timer_thread_pool& workers();

    private:
        void _swap_exec_context() { context_._swap_data_buff(); }

    private: // 단계별로 등록되는 콜백 목록
        /**
         * 실행 완료 후 비동기적으로 호출되는 콜백입니다.
         * 연결된 출력 핸들러 각각을 iterate합니다.
         *
         *  먼저 owner_에게 현재 슬롯 인스턴스가 출력할 차례가 맞는지 질의 필요.
         *      output_fence == input_fence: 출력 준비
         *      output_fence <  input_fence: discard
         *      output_fence >  input_fence: 처리 중. 대기 필요
         *  아니면 대기합니다. 만약 대상 링크의 fence_index가 출력 fence_index보다 높다면, 이는 해당 fence가 invalidated 된 것으로 아무것도 하지 않습니다.
         *
         *  1) 실패 시
         *      submit_input에 false 전달. 자동으로 같은 단계의 다른 출력도 전부 무효화
         *      
         *  2-A) 성공 시 - 필수적 입력
         *      출력 데이터를 입력 데이터에 연결합니다(콜백).
         *      연결된 입력 슬롯이 가능 상태
         *          (this->output_fence_index == other->input_fence_index)
         *      가 될 때 까지 대기
         *      
         *  2-B) 성공 시 - 선택적 입력
         *      연결된 입력 슬롯이 가능 상태인지 점검합니다.
         *      1) 가능 상태라면, 출력 데이터를 입력 데이터에 연결합니다
         *      2) 아니라면, sumit_input에 false 전달. 즉시 increment합니다.
         *
         *  모든 출력을 처리한 후엔, ready_conditions_를 비우고 입력 가능 상태로 전환
         *
         */
        void _launch_callback(); // 파라미터는 나중에 추가
        void _perform_post_output();
        void _perform_output_link(size_t output_index, bool aborting);

    private:
        pipe_base& owner_;

        std::unique_ptr<executor_base> executor_;
        std::atomic<pipe_error> latest_execution_result_;

        execution_context context_;

        std::shared_ptr<base_shared_context> fence_object_;
        std::atomic<fence_index_t> fence_index_ = fence_index_t::none;

        std::any cached_input_;
        std::any cached_output_;

        std::optional<execution_context::timer_scope_indicator> timer_scope_total_;
        std::optional<execution_context::timer_scope_indicator> timer_scope_link_;

        size_t index_;
    };

public:
    struct input_link_desc {
        pipe_base* pipe;
    };
    struct output_link_desc {
        output_link_adapter_type handler;
        pipe_base* pipe;
    };

public:
    pipe_id_t id() const { return id_; }
    auto& name() const { return name_; }
    /** launch()의 호출 여부 반환 */
    bool is_launched() const { return input_slot_.active_input_fence_.load(std::memory_order_relaxed) > fence_index_t::none; }

    /** 입출력 노드 반환 */
    auto& input_links() const { return input_links_; }
    auto& output_links() const { return output_links_; }

    /** 파이프 옵션 반환 */
    auto& options() { return executor_options_; }
    auto& options() const { return executor_options_; }

    /** 입력 가능 상태인지 확인 */
    bool can_submit_input_direct() const { return !_active_exec_slot()._is_executor_busy(); }
    bool is_paused() const { return paused_.load(std::memory_order_relaxed); }
    void pause() { paused_.store(true, std::memory_order_relaxed); }
    void unpause() { paused_.store(false, std::memory_order_relaxed); }
    bool recently_aborted() const { return recently_input_aborted_.load(std::memory_order::relaxed); }

    /** 상태 점검 */
    bool is_optional_input() const { return input_slot_.is_optional_; }
    size_t num_executors() const { return executor_slots_.size(); }
    void executor_conditions(std::vector<executor_condition_t>& conds) const;

    /** 출력 인터벌 반환 */
    auto output_interval() const
    {
        return latest_interval_.load(std::memory_order_relaxed);
    }
    auto output_latency() const { return latest_output_latency_.load(std::memory_order_relaxed); }

    /** 옵션 변경 후 호출, mark dirty */
    void mark_dirty();

public:
    /** 입력 연결자 */
    template <typename Shared_, typename PrevOut_, typename NextIn_, typename Fn_>
    void connect_output_to(pipe_base& other, Fn_&&);

    /** 파이프라인을 시동합니다. */
    void launch(size_t num_executors, std::function<std::unique_ptr<executor_base>()>&& factory);

    /** launch의 편의성 래퍼입니다. */
    template <typename Fn_, typename... Args_>
    void launch_by(size_t num_executors, Fn_&& factory, Args_&&... args);

    /** 입력 공급 시도 */
    bool try_submit(std::any&& input, std::shared_ptr<base_shared_context> fence_object) { return input_slot_._submit_input_direct(std::move(input), std::move(fence_object)); }

    /** 가동 중인 파이프 있는지 확인 */
    bool is_async_operation_running() const { return destruction_guard_.is_locked(); }

    /** context 읽어들이기 */
    execution_context const* latest_execution_context() const { return latest_exec_context_.load(std::memory_order_relaxed); }

    /** 출력 핸들러 추가 */
    void add_output_handler(output_handler_type handler) { output_handlers_.emplace_back(std::move(handler)); };

private:
    /** this출력->to입력 방향으로 연결합니다. */
    void _connect_output_to_impl(pipe_base* other, output_link_adapter_type adapter);

    /** 출력이 완료된 슬롯에서 호출합니다. 다음 슬롯을 입력 활성화 */
    void _rotate_output_order(executor_slot* ref);

    /** 다음 입력 슬롯을 활성화. */
    size_t _rotate_slot() { return active_exec_slot_.fetch_add(1); }

    /** 출력할 차례가 된 실행 슬롯 반환 */
    size_t _pending_output_slot_index() const { return output_exec_slot_.load(std::memory_order_relaxed) % executor_slots_.size(); }

public:
    void _set_thread_pool_reference(kangsw::timer_thread_pool* ref) { ref_workers_ = ref; }
    executor_slot const& _active_exec_slot() const { return *executor_slots_[_slot_active()]; }
    size_t _slot_active() const { return active_exec_slot_.load() % executor_slots_.size(); }
    void _refresh_interval_timer();
    void _update_latest_latency(system_clock::time_point launched);
    bool _is_selective_input() const { return mode_selectie_input_; }
    bool _is_selective_output() const { return mode_selective_output_; }
    void _update_abort_received(bool abort) { recently_input_aborted_.store(abort, std::memory_order::relaxed); }

private:
    kangsw::timer_thread_pool& _thread_pool() const { return *ref_workers_; }
    executor_slot& _active_exec_slot() { return *executor_slots_[_slot_active()]; }

private:
    pipe_id_t const id_ = pipe_id_gen::generate();
    std::string name_;

    /** 모든 파이프 입력을 처리합니다. */
    input_slot_t input_slot_{*this};

    /** 실행기의 개수는 파이프라인 시동 이후 변하지 않아야 합니다. */
    std::vector<std::unique_ptr<executor_slot>> executor_slots_;
    std::atomic_size_t active_exec_slot_; // idle 슬롯 선택(반드시 순차적)
    std::atomic_size_t output_exec_slot_; // 출력 슬롯 선택

    /** 모든 입출력 링크는 파이프라인 시동 이후 변하지 않아야 합니다. */
    std::vector<input_link_desc> input_links_;
    std::vector<output_link_desc> output_links_;

    /** 가장 최근에 실행된 execution 정보 */
    std::atomic<execution_context const*> latest_exec_context_;
    std::atomic<fence_index_t> latest_output_fence_;
    std::atomic<system_clock::duration> latest_interval_;
    std::atomic<system_clock::time_point> latest_output_tp_ = system_clock::now();
    std::atomic<system_clock::duration> latest_output_latency_;

    std::vector<output_handler_type> output_handlers_;

    kangsw::timer_thread_pool* ref_workers_ = nullptr;
    option_base executor_options_;

    /** 일시 정지 처리 */
    std::atomic_bool paused_;

    /** 상태 플래그 */
    std::atomic_bool recently_input_aborted_;

    /** 설정 플래그 */
    bool mode_selective_output_ = false;
    bool mode_selectie_input_ = false;

    //---GUARD--//
    kangsw::destruction_guard destruction_guard_;
}; // namespace pipepp

template <typename Shared_, typename PrevOut_, typename NextIn_, typename Fn_>
void pipe_base::connect_output_to(pipe_base& other, Fn_&& fn)
{
    auto wrapper = [fn_ = std::move(fn)](base_shared_context& shared, execution_context& ec, std::any const& prev_out, std::any& next_in) {
        if (next_in.type() != typeid(NextIn_)) {
            next_in.emplace<NextIn_>();
        }
        if (prev_out.type() != typeid(PrevOut_)) {
            throw pipe_input_exception("argument type does not match");
        }

        using SD = Shared_&;
        using PO = PrevOut_ const&;
        using NI = NextIn_&;
        using EC = execution_context&;

        auto& sd = static_cast<Shared_&>(shared);
        auto& po = std::any_cast<PrevOut_ const&>(prev_out);
        auto& ni = std::any_cast<NextIn_&>(next_in);

        // clang-format off
        if constexpr (std::is_invocable_v<Fn_, SD, PO, NI>) { fn_(sd, po, ni); }
        else if constexpr (std::is_invocable_v<Fn_, SD, NI>) { fn_(sd, ni); }
        else if constexpr (std::is_invocable_v<Fn_, PO, NI>) { fn_(po, ni); }
        else if constexpr (std::is_invocable_v<Fn_, EC, PO, NI>) { fn_(ec, po, ni); }
        else if constexpr (std::is_invocable_v<Fn_, SD, EC, PO, NI>) { fn_(sd, ec, po, ni); }
        else { static_assert(false, "No available invocable method"); }
        // clang-format on
    };

    _connect_output_to_impl(&other, wrapper);
}

template <typename Fn_, typename... Args_>
void pipe_base::launch_by(size_t num_executors, Fn_&& factory, Args_&&... args)
{
    launch(
      num_executors,
      std::bind<std::unique_ptr<executor_base>>(
        std::forward<Fn_>(factory),
        args...)); // Intentionally not forwarded to prevent move assignment
}

} // namespace detail
/**
 * 독립된 알고리즘 실행기 하나를 정의합니다.
 * 파이프에 공급하는 모든 실행기는 이 클래스를 상속해야 합니다.
 */
template <typename Exec_>
class executor final : public detail::executor_base {
public:
    using executor_type = Exec_;
    using input_type = typename executor_type::input_type;
    using output_type = typename executor_type::output_type;

    static_assert(std::is_default_constructible_v<output_type>);

public:
    template <typename... Ty_>
    executor(Ty_&&... args)
        : exec_(std::forward<Ty_>(args)...)
    {
    }

    executor(executor_type&& ref)
        : exec_(std::move(ref))
    {
    }

public:
    pipe_error invoke__(std::any& input, std::any& output) override
    {
        if (input.type() != typeid(input_type)) {
            throw pipe_input_exception("input type not match");
        }
        if (output.type() != typeid(output_type)) {
            output.emplace<output_type>();
        }

        return std::invoke(
          &executor_type::invoke, &exec_,
          *context_,
          std::any_cast<input_type&>(input), std::any_cast<output_type&>(output));
    }

    // Non-virtual to be overriden by base class
    pipe_error invoke(execution_context& context, input_type const& i, output_type& o) { throw; }

private:
    void const* _get_actual_executor() const override { return &exec_; }

    template <typename ExecFn_, typename... Args_>
    friend decltype(auto) make_executor(Args_&&... args);

private:
    executor_type exec_;
};

template <typename Exec_, typename... Args_>
decltype(auto) make_executor(Args_&&... args)
{
    return std::make_unique<executor<Exec_>>(std::forward<Args_>(args)...);
}

} // namespace pipepp
