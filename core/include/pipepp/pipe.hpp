#pragma once
#include "thread_pool.hxx"
#include <any>
#include <atomic>
#include <concepts>
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

/**
 * Fence object�� pipeline�� �����͸� ó������ �Է��� �� �����Ǵ� ������Ʈ��, ������ pipe�� ���� ó���� �� �����Ǵ� ������Ʈ �����Դϴ�.
 */
enum class fence_index_type : size_t { none = 0 };

/** ������ Pipe�� ���� �� �ο��� ������ pipe id�� �����ϴ�. */
enum class pipe_id_type : size_t { none = -1 };

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
    static pipe_id_type generate() { return static_cast<pipe_id_type>(gen_++); }
};

/**
 * ������ �⺻ Ŭ����
 *
 *
 */
class pipe_base : public std::enable_shared_from_this<pipe_base> {
    friend class pipeline_base;

private:
    inline static templates::thread_pool pipe_workers_{1024, 32};

public:
    static void set_num_workers(size_t n) { pipe_workers_.resize_worker_pool(n); }

public:
    class input_slot {
        friend class pipe_base;

    public:
        explicit input_slot(pipe_base& owner)
            : owner_(owner)
        {
        }

        /** ready condition ���� ����. �ʱ�ȭ �Լ� */
        void num_ready_conditions__(size_t n);

    public:
        /**
         * �Է� �����͸� �����ϱ� ���� ��޴ϴ�.
         */
        auto seq1_lock_input() -> std::pair<std::any&, std::lock_guard<std::mutex>>;

        /**
         * �Է� ������ ���� �Ϸ� �� ȣ���մϴ�.
         * ready_conds_�� �ش� �ε����� Ȱ��ȭ�մϴ�.
         * ready_conds_�� ��� �ε����� has_value()�� �����ϸ� �Է� �������� ����˴ϴ�.
         *
         * ���������δ� owner_���� �Է� ������ ���� ��û
         * �Է��� qualified �Ǹ� owner_�� �Է� ���� fence index�� 1 �����մϴ�. ���ο� fence index�� Ȱ��ȭ�� executor_slot�� �Ҵ�˴ϴ�. �׷���, Ȱ��ȭ�� executor_slot�� ������ ���� ���̸� active_slot()�� ���� �� ������, ���� �Է� ���� disable ���°� �˴ϴ�.
         *
         * @param input_index ready_conds������ ������ ��Ÿ���ϴ�.
         * @param execute ���� condition list�� false ���� �� ���� fence index ����. ��, �ϳ��� false�� ������ ��� �Է� �Ұ��� ���·� ��ȯ�ϰ�, fence_index�� 1 ����
         */
        void seq2_submit_input(size_t input_index, bool execute);

        /**
         * ��� ������ �õ��մϴ�. �Է� ��ũ�� ���� ��쿡�� ����.(������ ���� ����)
         * �ܺο��� ���������ο� ���� �Է��� �����ϱ� �����Դϴ�.
         */
        void seq2o_launch_immediate();

    private:
        pipe_base& owner_;
        bool is_optional_ = false;
        std::pair<std::any, std::mutex> cached_input_;
        std::vector<std::optional<bool>> ready_conds_;
        fence_index_type active_input_fence_ = fence_index_type::none;
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

    public: // �ʱ�ȭ ���� �޼ҵ�
        /**
         * ���� �Ϸ� �� �񵿱������� ȣ��Ǵ� �ݹ��Դϴ�.
         * ����� ��� �ڵ鷯 ������ iterate�մϴ�.
         *
         *  ���� owner_���� ���� ���� �ν��Ͻ��� ����� ���ʰ� �´��� ���� �ʿ�.
         *      output_fence == input_fence: ��� �غ�
         *      output_fence <  input_fence: discard
         *      output_fence >  input_fence: 
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
         *      2) �ƴ϶��, sumit_input�� false ����.
         *
         *  ��� ����� ó���� �Ŀ�, ready_conditions_�� ���� �Է� ���� ���·� ��ȯ
         *
         */
        void on_execution_finished();

    private: // �ܰ躰�� ��ϵǴ� �ݹ� ���
    private:
        pipe_base& owner_;

        std::unique_ptr<executor_base> executor_;
        std::optional<std::future<pipe_error>> execution_;

        execution_context contexts_[2];
        bool context_front_ = false;

        std::shared_ptr<base_fence_shared_object> fence_object_;
        fence_index_type current_fence_index_ = fence_index_type::none;

        std::any cached_output_;
    };

public: // accessors
    executor_slot& active_exec_slot() { return slots_[slot_active()]; }
    executor_slot const& active_exec_slot() const { return slots_[slot_active()]; }
    size_t slot_active() const { return slot_active_.load() % slots_.size(); }

public:
    /** ������ �Է��� ��� ��尡 ���� �Է�(��, �ڽ�)�� ���� �����մϴ�. */
    bool input_can_be_optional() const;

    /** input_can_be_optional == false�� throw. */
    void set_optional_input(bool is_optional);

    /** this�Է�->to��� �������� �����մϴ�. */
    void connect_output_to(pipe_base* other);

private:
    /** on_execution_finished()���� ȣ��, �ش� ������ ����� �������� �˻��մϴ�. */
    bool is_valid_output_order__(executor_slot* ref);

    /** ���� �Է� ������ Ȱ��ȭ. */
    size_t rotate_slot__() { return slot_active_.fetch_add(1); }

public:
    /** ��� ������ �Է��� ó���մϴ�. */
    input_slot input{*this};

private:
    struct input_link_desc {
        pipe_base* pipe;
    };
    struct output_link_desc {
        std::function<void(std::any const&, std::any&)> handler;
        pipe_base* pipe;
    };

private:
    pipe_id_type const id_ = pipe_id::generate();

    //
    std::vector<executor_slot> slots_;
    std::atomic_size_t slot_active_;         // idle ���� ����(�ݵ�� ������)
    std::atomic_size_t slot_pending_output_; // ������ ��� ����

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
