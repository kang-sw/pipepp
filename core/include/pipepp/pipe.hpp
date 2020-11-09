#pragma once
#include <any>
#include <atomic>
#include <concepts>
#include <condition_variable>
#include <kangsw/thread_pool.hxx>
#include <map>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

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
/**
 * 실행 문맥 클래스.
 * 디버깅 및 모니터링을 위한 클래스로,
 *
 * 1) 디버그 플래그 제어
 * 2) 디버그 데이터 저장
 * 3) 구간별 계층적 실행 시간 계측
 * 4) 옵션에 대한 접근 제어(옵션 인스턴스는 pipe에 존재, 실행 문맥은 파라미터만 빌려옴)
 *
 * 등의 기능을 제공합니다.
 */
class execution_context {
    // TODO
};

/** 기본 실행기. */
class executor_base {
public:
    virtual ~executor_base() = default;
    virtual pipe_error invoke__(std::any const& input, std::any& output) = 0;

private:
    execution_context* context_ = nullptr;
};

struct pipe_id {
    inline static size_t gen_ = 0;
    static pipe_id_t generate() { return static_cast<pipe_id_t>(gen_++); }
};

/**
 * 파이프 기본 클래스
 *
 *
 */
class pipe_base : public std::enable_shared_from_this<pipe_base> {
    friend class pipeline_base;

private:
    inline static kangsw::thread_pool pipe_workers_{1024, 32, 1024};

public:
    static void set_num_workers(size_t n) { pipe_workers_.resize_worker_pool(n); }

public:
    class input_slot_t {
        friend class pipe_base;

    public:
        explicit input_slot_t(pipe_base& owner)
            : owner_(owner)
        {
        }

        /** ready condition 개수 설정. 초기화 함수 */
        void num_ready_conditions__(size_t n);

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
        bool submit_input(
          fence_index_t fence,
          size_t input_index,
          std::function<void(std::any&)> const& input_manip,
          std::shared_ptr<base_fence_shared_object> fence_obj,
          bool abort_current = false);

        /**
         * 주어진 입력 데이터로 즉시 실행합니다. 입력 링크가 없는 경우에만 가능.(있으면 예외 던짐)
         * 외부에서 파이프라인에 직접 입력을 공급할 수 있습니다.
         *
         * 단, 이를 위해 적어도 하나의 실행기가 비어 있어야 합니다. 아니면 false를 반환합니다.
         */
        bool submit_input_direct__(std::any&& input);

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

    class executor_slot {
    public:
        std::shared_ptr<base_fence_shared_object> fence_object;

    public:
        explicit executor_slot(pipe_base& owner)
            : owner_(owner)
        {
        }

        // 실행 문맥 관련
        executor_base* executor() const { return executor_.get(); }
        execution_context const& context_read() const { return contexts_[context_front_]; }
        execution_context& context_write() { return contexts_[!context_front_]; }
        void swap_exec_context() { context_front_ = !context_front_; }

    public: // 단계별로 등록되는 콜백 목록
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
        void on_execution_finished(); // 파라미터는 나중에 추가
        void wait_target_input_slot();

    private:
        pipe_base& owner_;

        std::unique_ptr<executor_base> executor_;
        std::optional<std::future<pipe_error>> execution_;

        execution_context contexts_[2] = {};
        bool context_front_ = false;

        std::shared_ptr<base_fence_shared_object> fence_object_;
        fence_index_t current_fence_index_ = fence_index_t::none;

        std::any cached_output_;
    };

public: // accessors
    executor_slot& active_exec_slot() { return exec_slots_[slot_active()]; }
    executor_slot const& active_exec_slot() const { return exec_slots_[slot_active()]; }
    size_t slot_active() const { return active_exec_slot_.load() % exec_slots_.size(); }

public:
    /** 선택적 입력은 출력 노드가 단일 입력(즉, 자신)일 때만 가능합니다. */
    bool input_can_be_optional() const;

    /** input_can_be_optional == false면 throw. */
    void set_optional_input(bool is_optional);

    /** this출력->to입력 방향으로 연결합니다. */
    void connect_output_to(pipe_base* other);

    /** 입력을 강제로 공급합니다. */
    // TODO
private:
    /** on_execution_finished()에서 호출, 해당 슬롯이 출력할 차례인지 검사합니다. */
    bool is_valid_output_order__(executor_slot* ref);

    /** 다음 입력 슬롯을 활성화. */
    size_t rotate_slot__() { return active_exec_slot_.fetch_add(1); }

private:
    struct input_link_desc {
        pipe_base* pipe;
    };
    struct output_link_desc {
        std::function<void(std::any const&, std::any&)> handler;
        pipe_base* pipe;
    };

private:
    pipe_id_t const id_ = pipe_id::generate();

    /** 모든 파이프 입력을 처리합니다. */
    input_slot_t input_slot_{*this};

    std::vector<executor_slot> exec_slots_;
    std::atomic_size_t active_exec_slot_;         // idle 슬롯 선택(반드시 순차적)
    std::atomic_size_t pending_output_exec_slot_; // 순차적 출력 보장

    std::vector<input_link_desc> input_links_;
    std::vector<output_link_desc> output_links_;

    std::vector<std::function<void(pipe_error, std::any const&)>> output_handlers_;
}; // namespace pipepp

} // namespace impl__
/**
 * 독립된 알고리즘 실행기 하나를 정의합니다.
 * 파이프에 공급하는 모든 실행기는 이 클래스를 상속해야 합니다.
 */
template <typename Input_, typename Output_>
class base_executor : impl__::executor_base {
public:
    using input_type = Input_;
    using output_type = Output_;

public:
    pipe_error invoke__(std::any const& input, std::any& output) final
    {
        return invoke(
          std::any_cast<input_type const&>(input),
          std::any_cast<output_type&>(output));
    }

    virtual pipe_error invoke(input_type const& i, output_type& o) = 0;
};

} // namespace pipepp
