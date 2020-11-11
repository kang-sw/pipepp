#pragma once
#include <any>
#include <atomic>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <vector>
#include "kangsw/misc.hxx"
#include "kangsw/thread_pool.hxx"
#include "kangsw/thread_utility.hxx"
#include "pipepp/internal/execution_context.hpp"

namespace pipepp {
/** 파이프 에러 형식 */
enum class pipe_error {
    ok,
    warning,
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
struct base_fence_shared_object {
    virtual ~base_fence_shared_object();
};

namespace impl__ {

/** 기본 실행기. */
class executor_base {
public:
    virtual ~executor_base() = default;
    virtual pipe_error invoke__(std::any const& input, std::any& output) = 0;

public:
    void set_context_ref(execution_context* ref) { context_ = ref, context_->clear_records(); }

private:
    execution_context* context_ = nullptr;
};

struct pipe_id_gen {
    inline static size_t gen_ = 0;
    static pipe_id_t generate() { return static_cast<pipe_id_t>(gen_++); }
};

/**
 * 파이프 기본 클래스
 */
class pipe_base : public std::enable_shared_from_this<pipe_base> {
    friend class pipeline_base;

public:
    using output_link_adapter_t = std::function<void(base_fence_shared_object&, std::any const& output, std::any& input)>;

    explicit pipe_base(bool optional_pipe = false)
    {
        input_slot_.is_optional_ = optional_pipe;
    }

public:
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
        std::optional<bool> can_submit_input(fence_index_t fence) const;
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
          std::shared_ptr<base_fence_shared_object> const& fence_obj,
          bool abort_current = false);

        /**
         * 주어진 입력 데이터로 즉시 실행합니다. 입력 링크가 없는 경우에만 가능.(있으면 예외 던짐)
         * 외부에서 파이프라인에 직접 입력을 공급할 수 있습니다.
         *
         * 단, 이를 위해 적어도 하나의 실행기가 비어 있어야 합니다. 아니면 false를 반환합니다.
         */
        bool _submit_input_direct(std::any&& input, std::shared_ptr<base_fence_shared_object> fence_object);
        bool _can_submit_input_direct() const;

    private:
        void _prepare_next();

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
        std::shared_ptr<base_fence_shared_object> active_input_fence_object_;
    };

    class alignas(64) executor_slot {
    public:
        explicit executor_slot(pipe_base& owner,
                               std::unique_ptr<executor_base>&& exec,
                               bool initial_output_order)
            : owner_(owner)
            , executor_(std::move(exec))
            , is_output_order_(initial_output_order)
        {
        }

    public: // 실행 문맥 관련
        executor_base* executor() const { return executor_.get(); }
        execution_context const& context_read() const { return contexts_[context_front_]; }
        execution_context& context_write() { return contexts_[!context_front_]; }
        fence_index_t fence_index() const { return fence_index_; }
        bool is_busy() const { return fence_index_ != fence_index_t::none; }

        std::atomic_bool& _is_output_order() { return is_output_order_; }

    public:
        struct launch_args_t {
            std::shared_ptr<base_fence_shared_object> fence_obj;
            fence_index_t fence_index;
            std::any input;
        };
        void _launch_async(launch_args_t arg);
        kangsw::timer_thread_pool& workers();

    private:
        void _swap_exec_context() { context_front_ = !context_front_; }

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
        void _output_link_callback(size_t output_index, bool aborting);

    private:
        pipe_base& owner_;

        std::unique_ptr<executor_base> executor_;
        std::atomic<pipe_error> latest_execution_result_;

        execution_context contexts_[2] = {};
        bool context_front_ = false;

        std::shared_ptr<base_fence_shared_object> fence_object_;
        std::atomic<fence_index_t> fence_index_ = fence_index_t::none;

        std::any cached_input_;
        std::any cached_output_;

        std::atomic_bool is_output_order_ = false;
    };

public:
    pipe_id_t id() const { return id_; }
    bool is_launched() const { return launched_; }

    // 저장된 스레드 풀 레퍼런스 획득
    kangsw::timer_thread_pool& thread_pool() const { return *ref_workers_; }

public: // accessors
    executor_slot& _active_exec_slot() { return executor_slots_[_slot_active()]; }
    executor_slot const& _active_exec_slot() const { return executor_slots_[_slot_active()]; }
    size_t _slot_active() const { return active_exec_slot_.load() % executor_slots_.size(); }

public:
    /** 입력 연결자 */
    template <typename Shared_, typename PrevOut_, typename NextIn_, typename Fn_>
    void connect_output_to(pipe_base& other, Fn_&&);

    /** 파이프라인을 시동합니다. */
    void launch(std::function<std::unique_ptr<executor_base>()>&& factory, size_t num_executors);

    /** 입력 공급 시도 */
    bool try_submit(std::any&& input, std::shared_ptr<base_fence_shared_object> fence_object) { return input_slot_._submit_input_direct(std::move(input), std::move(fence_object)); }

private:
    /** this출력->to입력 방향으로 연결합니다. */
    void _connect_output_to_impl(pipe_base* other, output_link_adapter_t adapter);

    /** 출력이 완료된 슬롯에서 호출합니다. 다음 슬롯을 입력 활성화 */
    void _rotate_output_order(executor_slot* ref);

    /** 다음 입력 슬롯을 활성화. */
    size_t _rotate_slot() { return active_exec_slot_.fetch_add(1); }

public:
    void set_thread_pool_reference(kangsw::timer_thread_pool* ref) { ref_workers_ = ref; }

private:
    struct input_link_desc {
        pipe_base* pipe;
    };
    struct output_link_desc {
        output_link_adapter_t handler;
        pipe_base* pipe;
    };

private:
    pipe_id_t const id_ = pipe_id_gen::generate();
    std::atomic_bool launched_ = false;

    /** 모든 파이프 입력을 처리합니다. */
    input_slot_t input_slot_{*this};

    /** 실행기의 개수는 파이프라인 시동 이후 변하지 않아야 합니다. */
    std::span<executor_slot> executor_slots_;
    std::unique_ptr<executor_slot[]> executor_buffer_;
    std::atomic_size_t active_exec_slot_; // idle 슬롯 선택(반드시 순차적)

    /** 모든 입출력 링크는 파이프라인 시동 이후 변하지 않아야 합니다. */
    std::vector<input_link_desc> input_links_;
    std::vector<output_link_desc> output_links_;

    // 가장 최근에 실행된 execution 정보
    std::atomic<execution_context const*> latest_exec_context_;
    std::atomic<fence_index_t> latest_output_fence_;

    std::vector<std::function<void(pipe_error, base_fence_shared_object const&, std::any const&)>> output_handlers_;

    kangsw::timer_thread_pool* ref_workers_;

    //---GUARD--//
    kangsw::destruction_guard destruction_guard_;
}; // namespace pipepp

template <typename Shared_, typename PrevOut_, typename NextIn_, typename Fn_>
void pipe_base::connect_output_to(pipe_base& other, Fn_&& fn)
{
    auto wrapper = [fn_ = std::move(fn)](Shared_& shared, std::any const& prev_out, std::any& next_in) {
        if (next_in.type() != typeid(NextIn_)) {
            next_in.emplace<NextIn_>();
        }
        if (prev_out.type() != typeid(PrevOut_)) {
            throw pipe_input_exception("argument type does not match");
        }
        fn_(shared, std::any_cast<PrevOut_ const&>(prev_out), std::any_cast<NextIn_&>(next_in));
    };

    _connect_output_to_impl(&other, wrapper);
}

} // namespace impl__
/**
 * 독립된 알고리즘 실행기 하나를 정의합니다.
 * 파이프에 공급하는 모든 실행기는 이 클래스를 상속해야 합니다.
 */
template <typename Exec_>
class base_executor : impl__::executor_base {
public:
    using executor_type = Exec_;
    using input_type = typename executor_type::input_type;
    using output_type = typename executor_type::output_type;

    static_assert(std::is_default_constructible_v<output_type>);

public:
    pipe_error invoke__(std::any const& input, std::any& output) final
    {
        if (input.type() != typeid(input_type)) {
            throw pipe_input_exception("input type not match");
        }
        if (output.type() != typeid(output_type)) {
            output.emplace<output_type>();
        }

        return std::invoke(
          &executor_type::invoke, this,
          std::any_cast<input_type const&>(input), std::any_cast<output_type&>(output));
    }

    // Non-virtual to be overriden by base class
    pipe_error invoke(execution_context& context, input_type const& i, output_type& o) { return pipe_error::ok; }
};

} // namespace pipepp
