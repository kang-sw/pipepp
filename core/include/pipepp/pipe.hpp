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
/**
 * ���� ���� Ŭ����.
 * ����� �� ����͸��� ���� Ŭ������,
 *
 * 1) ����� �÷��� ����
 * 2) ����� ������ ����
 * 3) ������ ������ ���� �ð� ����
 * 4) �ɼǿ� ���� ���� ����(�ɼ� �ν��Ͻ��� pipe�� ����, ���� ������ �Ķ���͸� ������)
 *
 * ���� ����� �����մϴ�.
 */
class execution_context {
    // TODO
};

/** �⺻ �����. */
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
 * ������ �⺻ Ŭ����
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

        /** ready condition ���� ����. �ʱ�ȭ �Լ� */
        void num_ready_conditions__(size_t n);

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
        bool submit_input(
          fence_index_t fence,
          size_t input_index,
          std::function<void(std::any&)> const& input_manip,
          std::shared_ptr<base_fence_shared_object> fence_obj,
          bool abort_current = false);

        /**
         * �־��� �Է� �����ͷ� ��� �����մϴ�. �Է� ��ũ�� ���� ��쿡�� ����.(������ ���� ����)
         * �ܺο��� ���������ο� ���� �Է��� ������ �� �ֽ��ϴ�.
         *
         * ��, �̸� ���� ��� �ϳ��� ����Ⱑ ��� �־�� �մϴ�. �ƴϸ� false�� ��ȯ�մϴ�.
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

        // ���� ���� ����
        executor_base* executor() const { return executor_.get(); }
        execution_context const& context_read() const { return contexts_[context_front_]; }
        execution_context& context_write() { return contexts_[!context_front_]; }
        void swap_exec_context() { context_front_ = !context_front_; }

    public: // �ܰ躰�� ��ϵǴ� �ݹ� ���
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
        void on_execution_finished(); // �Ķ���ʹ� ���߿� �߰�
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
    /** ������ �Է��� ��� ��尡 ���� �Է�(��, �ڽ�)�� ���� �����մϴ�. */
    bool input_can_be_optional() const;

    /** input_can_be_optional == false�� throw. */
    void set_optional_input(bool is_optional);

    /** this���->to�Է� �������� �����մϴ�. */
    void connect_output_to(pipe_base* other);

    /** �Է��� ������ �����մϴ�. */
    // TODO
private:
    /** on_execution_finished()���� ȣ��, �ش� ������ ����� �������� �˻��մϴ�. */
    bool is_valid_output_order__(executor_slot* ref);

    /** ���� �Է� ������ Ȱ��ȭ. */
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

    /** ��� ������ �Է��� ó���մϴ�. */
    input_slot_t input_slot_{*this};

    std::vector<executor_slot> exec_slots_;
    std::atomic_size_t active_exec_slot_;         // idle ���� ����(�ݵ�� ������)
    std::atomic_size_t pending_output_exec_slot_; // ������ ��� ����

    std::vector<input_link_desc> input_links_;
    std::vector<output_link_desc> output_links_;

    std::vector<std::function<void(pipe_error, std::any const&)>> output_handlers_;
}; // namespace pipepp

} // namespace impl__
/**
 * ������ �˰��� ����� �ϳ��� �����մϴ�.
 * �������� �����ϴ� ��� ������ �� Ŭ������ ����ؾ� �մϴ�.
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
