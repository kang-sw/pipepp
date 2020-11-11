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
/** ������ ���� ���� */
enum class pipe_error {
    ok,
    warning,
    error,
    fatal
};

/** ������ ���� ���� */
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
 * Fence object�� pipeline�� �����͸� ó������ �Է��� �� �����Ǵ� ������Ʈ��, ������ pipe�� ���� ó���� �� �����Ǵ� ������Ʈ �����Դϴ�.
 */
enum class fence_index_t : size_t { none = 0 };

/** ������ Pipe�� ���� �� �ο��� ������ pipe id�� �����ϴ�. */
enum class pipe_id_t : size_t { none = -1 };

/** fence shared data�� �⺻ ������Դϴ�. */
struct base_fence_shared_object {
    virtual ~base_fence_shared_object();
};

namespace impl__ {

/** �⺻ �����. */
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
 * ������ �⺻ Ŭ����
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

        /** ready condition ���� ����. �ʱ�ȭ �Լ� */
        void _supply_input_to_active_executor(bool is_initial_call = true);

    public:
        /**
         * �Է� �ε��� �����ϱ�
         */
        fence_index_t active_input_fence() const { return active_input_fence_; }

        /**
         * @return has_value() == false�̸� ���� �Է��� ������ �մϴ�.
         */
        std::optional<bool> can_submit_input(fence_index_t fence) const;
        /**
         * �Է� ������ ���� �Ϸ� �� ȣ���մϴ�.
         * ready_conds_�� �ش� �ε����� Ȱ��ȭ�մϴ�.
         * ready_conds_�� ��� �ε����� has_value()�� �����ϸ� �Է� �������� ����˴ϴ�.
         *
         * ���������δ� owner_���� �Է� ������ ���� ��û
         * �Է��� qualified �Ǹ� owner_�� �Է� ���� fence index�� 1 �����մϴ�. ���ο� fence index�� Ȱ��ȭ�� executor_slot�� �Ҵ�˴ϴ�. �׷���, Ȱ��ȭ�� executor_slot�� ������ ���� ���̸� active_slot()�� ���� �� ������, ���� �Է� ���� disable ���°� �˴ϴ�.
         *
         * @returns true ��ȯ�� ó�� �Ϸ�, false ��ȯ �� retry�� �ʿ��մϴ�.
         */
        bool _submit_input(
          fence_index_t output_fence,
          pipe_id_t input_index,
          std::function<void(std::any&)> const& input_manip,
          std::shared_ptr<base_fence_shared_object> const& fence_obj,
          bool abort_current = false);

        /**
         * �־��� �Է� �����ͷ� ��� �����մϴ�. �Է� ��ũ�� ���� ��쿡�� ����.(������ ���� ����)
         * �ܺο��� ���������ο� ���� �Է��� ������ �� �ֽ��ϴ�.
         *
         * ��, �̸� ���� ��� �ϳ��� ����Ⱑ ��� �־�� �մϴ�. �ƴϸ� false�� ��ȯ�մϴ�.
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

    public: // ���� ���� ����
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

    private: // �ܰ躰�� ��ϵǴ� �ݹ� ���
        /**
         * ���� �Ϸ� �� �񵿱������� ȣ��Ǵ� �ݹ��Դϴ�.
         * ����� ��� �ڵ鷯 ������ iterate�մϴ�.
         *
         *  ���� owner_���� ���� ���� �ν��Ͻ��� ����� ���ʰ� �´��� ���� �ʿ�.
         *      output_fence == input_fence: ��� �غ�
         *      output_fence <  input_fence: discard
         *      output_fence >  input_fence: ó�� ��. ��� �ʿ�
         *  �ƴϸ� ����մϴ�. ���� ��� ��ũ�� fence_index�� ��� fence_index���� ���ٸ�, �̴� �ش� fence�� invalidated �� ������ �ƹ��͵� ���� �ʽ��ϴ�.
         *
         *  1) ���� ��
         *      submit_input�� false ����. �ڵ����� ���� �ܰ��� �ٸ� ��µ� ���� ��ȿȭ
         *      
         *  2-A) ���� �� - �ʼ��� �Է�
         *      ��� �����͸� �Է� �����Ϳ� �����մϴ�(�ݹ�).
         *      ����� �Է� ������ ���� ����
         *          (this->output_fence_index == other->input_fence_index)
         *      �� �� �� ���� ���
         *      
         *  2-B) ���� �� - ������ �Է�
         *      ����� �Է� ������ ���� �������� �����մϴ�.
         *      1) ���� ���¶��, ��� �����͸� �Է� �����Ϳ� �����մϴ�
         *      2) �ƴ϶��, sumit_input�� false ����. ��� increment�մϴ�.
         *
         *  ��� ����� ó���� �Ŀ�, ready_conditions_�� ���� �Է� ���� ���·� ��ȯ
         *
         */
        void _launch_callback(); // �Ķ���ʹ� ���߿� �߰�
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

    // ����� ������ Ǯ ���۷��� ȹ��
    kangsw::timer_thread_pool& thread_pool() const { return *ref_workers_; }

public: // accessors
    executor_slot& _active_exec_slot() { return executor_slots_[_slot_active()]; }
    executor_slot const& _active_exec_slot() const { return executor_slots_[_slot_active()]; }
    size_t _slot_active() const { return active_exec_slot_.load() % executor_slots_.size(); }

public:
    /** �Է� ������ */
    template <typename Shared_, typename PrevOut_, typename NextIn_, typename Fn_>
    void connect_output_to(pipe_base& other, Fn_&&);

    /** ������������ �õ��մϴ�. */
    void launch(std::function<std::unique_ptr<executor_base>()>&& factory, size_t num_executors);

    /** �Է� ���� �õ� */
    bool try_submit(std::any&& input, std::shared_ptr<base_fence_shared_object> fence_object) { return input_slot_._submit_input_direct(std::move(input), std::move(fence_object)); }

private:
    /** this���->to�Է� �������� �����մϴ�. */
    void _connect_output_to_impl(pipe_base* other, output_link_adapter_t adapter);

    /** ����� �Ϸ�� ���Կ��� ȣ���մϴ�. ���� ������ �Է� Ȱ��ȭ */
    void _rotate_output_order(executor_slot* ref);

    /** ���� �Է� ������ Ȱ��ȭ. */
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

    /** ��� ������ �Է��� ó���մϴ�. */
    input_slot_t input_slot_{*this};

    /** ������� ������ ���������� �õ� ���� ������ �ʾƾ� �մϴ�. */
    std::span<executor_slot> executor_slots_;
    std::unique_ptr<executor_slot[]> executor_buffer_;
    std::atomic_size_t active_exec_slot_; // idle ���� ����(�ݵ�� ������)

    /** ��� ����� ��ũ�� ���������� �õ� ���� ������ �ʾƾ� �մϴ�. */
    std::vector<input_link_desc> input_links_;
    std::vector<output_link_desc> output_links_;

    // ���� �ֱٿ� ����� execution ����
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
 * ������ �˰��� ����� �ϳ��� �����մϴ�.
 * �������� �����ϴ� ��� ������ �� Ŭ������ ����ؾ� �մϴ�.
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
