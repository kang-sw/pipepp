#pragma once
#include <any>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <pipepp/pipe.hpp>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace pipepp {
/**
 * ���������� ������ ����
 *
 * �� ���������� �������� = ������ 1�� �Ҵ�
 * ���� ��å:
 *      Synchronous Execution: ��� ������ ���������� ��� is_ready()�� ���� ���� ����
 *      Parallel Execution: ��� ����� ���������ΰ� ���ÿ� ���� ����
 *
 * ���� ��å:
 *      Primary Stage: �ݵ�� is_ready() ������ ������ ���, �ùٸ� fence index�� ����.
 *      Optional Stage: is_ready()�� ���� ���������� �õ�. �Է� assembling �Ұ�
 *                       ��, �� ��� ����� �����ϴ� ���� �ݵ�� ���� ����� �θ� �ɼų� ���������� �����ؾ� ��.
 *                       e.g.
 *                       �Ʒ��� ���, Pri0�� ����� Pri1�� ���� �Ұ�, �ݴ뵵 �Ұ�
 *                       Pri1�� Pri2�� ���� ����, �ݴ뵵 ����
 *                          Pri - Pri - Opt + Pri - Pri0
 *                                          |
 *                                          + Opt +------- Pri1
 *                                                |
 *                                                + Pri -- Pri2
 *
 * �������� ����:
 *      �Է� ó����:
 *          0) fence �����κ��� shared_data_ fetch�ؿ���
 *          1) ���� ������ �Է� ó��
 *          1.a) Primary Stage�� ���, ���� ��ũ�� ��� �Է� ó������ ���
 *          1.b) Optional Stage�� ��� �Է� ��� ���������� ���� ����
 *
 *      ������:
 *          ����� �����ͷ� ����ü�� �˰����� ����
 *
 *      ��� ó����:
 *          1) ��¿� ����� �� �������� iterate
 *          1.a) ���� ��å Synchronous�� ��� ���� stage�� �ݵ�� is_ready() ����
 *               ���������ο� ����� ���� ���������� �ϳ��� ������� ó��
 *               Pri0 + Pri1
 *                    + Pri2
 *                    + ...
 *               �� �� Pri1�� �Է� �� is_ready()���� ���, Pri2�� �Է� �� is_ready()���� ��� ... PriN���� �ݺ�.
 *               ��, �� ��� Optional ���� ��å�� �ǹ� x
 *          1.b) ���� ��å Parallel�� ���
 *          1.b.a) Primary Stage ������ is_ready()���� ���, ������ ����
 *          1.b.b) Optional Stage ������ is_ready() == false�̸� ����
 *
 */
enum stage_execution_policy {
    sync,
    async
};

enum stage_input_policy {
    await_always,
    only_if_possible
};

class stage_id_generator {
    inline static size_t id_ = 0;

public:
    size_t operator()() const { return id_++; }
};

class pipeline_link_exception : public pipe_exception {
public:
    explicit pipeline_link_exception(const char* msg)
        : pipe_exception(msg)
    {}
};

namespace impl__ {
template <typename SharedData_>
class stage_base : public std::enable_shared_from_this<stage_base<SharedData_>> {
    template <SharedData_>
    friend class pipeline_base;

public:
    using shared_data_type = SharedData_;
    using pipeline_type = pipeline_base<shared_data_type>;
    using stage_type = stage_base<shared_data_type>;

    using id_type = size_t;
    using fence_index_type = size_t;

public:
    explicit stage_base(std::string name, stage_execution_policy execution_policy, stage_input_policy input_policy)
        : id_(stage_id_generator{}())
        , name_(std::move(name))
        , execution_policy_(execution_policy)
        , input_policy_(input_policy)
        , owning_pipeline_(nullptr)
    {
    }
    virtual ~stage_base() = default;

    stage_base(const stage_base& other) = delete;
    stage_base(stage_base&& other) noexcept = default;
    stage_base& operator=(const stage_base& other) = delete;
    stage_base& operator=(stage_base&& other) noexcept = default;

public:
    void dispose();
    id_type id() const { return id_; }
    std::string_view name() const { return name_; }
    bool can_receive_input() const { return can_receive_input_; }
    fence_index_type fence_index() const { return fence_index_; }
    std::weak_ptr<shared_data_type> fence_shared_data() const { return fetched_shared_data_; }

private:
    void loop__()
    {
        using namespace std::chrono_literals;

        for (is_running_ = true; pending_dispose_ == false;) {
            // TODO: fetch next pipe data
            // pipeline class provides next valid fence index and pipe data based on current fence index.

            // wait until all inputs are gathered
            // if current fence is invalidated, do 'continue'
            bool is_fence_valid = true;
            bool is_ready = false;
            can_receive_input_ = true;
            for (; pending_dispose_ == false && !is_ready;) {
                auto lock = std::unique_lock<std::mutex>{event_wait_.second};
                event_wait_.first.wait_for(lock, 100ms);

                // TODO: check if fence is still valid

                // check if all inputs were gathered
                is_ready = true;
                for (auto& input_stage : input_stages_) {
                    if (input_stage.input_submit_fence != fence_index_) {
                        is_ready = false;
                        break;
                    }
                }
            }

            if (!is_fence_valid) { continue; }
            if (pending_dispose_) { break; }

            // run pipe + handler
            // before then, input must be disabled.
            can_receive_input_ = false;
            exec_pipe__();
        }

        is_disposed_ = true;
        is_running_ = false;
    }

protected:
    void notify_submit_input__(id_type stage_id_)
    {
        
    }

    static decltype(auto) stage_hash_predicate(id_type id)
    {
        return [id](auto elem) { elem.cached_id == id; };
    }

    // �Է� �������� ���(���� �켱)
    template <typename Fn_>
    void recurse_input_stages(Fn_ const& opr)
    {
        struct {
            std::set<id_type> visit_list;
            bool do_break = false;
            void exec(std::weak_ptr<stage_type> const& wptr, Fn_ const& callback)
            {
                std::shared_ptr<stage_type> self = wptr.lock();

                for (input_link_desc const& ref : self->input_stages_) {
                    if (do_break) {
                        break;
                    }

                    bool do_continue = callback(ref.stage);
                    if (do_continue == false) {
                        do_break = true;
                    }
                    else {
                        exec(ref.stage, callback);
                    }
                }
            }
        } impl;

        impl.exec(this->weak_from_this(), opr);
    }

    template <typename Fn_>
    void recurse_output_stages(Fn_ const& opr)
    {
        struct {
            std::set<id_type> visit_list;
            bool do_break = false;
            void exec(std::weak_ptr<stage_type> const& wptr, Fn_ const& callback)
            {
                std::shared_ptr<stage_type> self = wptr.lock();

                for (output_link_desc const& ref : self->output_stages_) {
                    if (do_break) {
                        break;
                    }

                    bool do_continue = callback(ref.stage);
                    if (do_continue == false) {
                        do_break = true;
                    }
                    else {
                        exec(ref.stage, callback);
                    }
                }
            }
        } impl;

        impl.exec(this->weak_from_this(), opr);
    }

    // �ڼ� �Լ��� connect() �Լ����� �������� ���� ó�� ���� �� ȣ��
    void process_output_link__(std::shared_ptr<stage_type> to)
    {
        if (is_running_ || is_disposed_) {
            throw pipeline_link_exception("Pipe connection cannot be changed during running");
        }

        bool const is_optional = to->input_policy_ == stage_input_policy::only_if_possible;
        bool const is_synchronous = to->execution_policy_ == stage_execution_policy::sync;
        if ((is_optional || is_synchronous) && !to->input_stages_.empty()) {
            // to�� optional�̰ų� sync�� ���, �Է� ���� �ϳ��� �����ؾ� �մϴ�.
            throw pipeline_link_exception("Optional input pipe cannot hold more than one input source");
        }

        // to�� ����� ��������� �˻���, this�� �Է����� ����Ǵ� ��ȯ ������ �����ϴ��� �˻��մϴ�.
        bool is_circular = false;
        to->recurse_output_stages([&is_circular, self = this->weak_from_this()](std::weak_ptr<stage_type> const& r) {
            if (r == self) {
                is_circular = true;
                return false;
            }
            return true;
        });

        if (is_circular) {
            throw pipeline_link_exception("Circular link detected!");
        }

        // to�� �Է�, this�� �ڽ��� ������ �Է��� ��������� �˻���, ���� ����� optional ���������� ������ �˻��մϴ�.
        std::weak_ptr<stage_type> first_optional_this = {};
        std::weak_ptr<stage_type> first_optional_to = {};
        this->recurse_input_stages([&first_optional_this](std::weak_ptr<stage_type> const& r) {
            auto ref = r.lock();
            if (ref->input_policy_ == stage_input_policy::only_if_possible) {
                first_optional_this = r;
                return false;
            }
            return true;
        });
        to->recurse_input_stages([&first_optional_to, &to](std::weak_ptr<stage_type> const& r) {
            auto ref = r.lock();
            if (to != r && ref->input_policy_ == stage_input_policy::only_if_possible) {
                first_optional_to = r;
                return false;
            }
            return true;
        });

        if (to->input_stages_.empty() || first_optional_this != first_optional_to) {
            throw pipeline_link_exception("Two pipes must share nearlest input optional stage");
        }

        // typical duplication check
        if (auto found = std::find_if(input_stages_.begin(), input_stages_.end(), stage_hash_predicate(to->id())); found != input_stages_.end()) {
            throw pipeline_link_exception("Given pipeline is already in link list!");
        }

        // establish link
        to->input_stages_.push_back(
          input_link_desc{
            .stage = this->weak_from_this(),
            .cached_id = this->id(),
            .input_submit_fence = 0});

        this->output_stages_.push_back(
          output_link_desc{
            .stage = to->weak_from_this(),
            .cached_id = to->id()});
    }

private:
    virtual void exec_pipe__() = 0;

private:
    const size_t id_;
    const std::string name_;

    const stage_execution_policy execution_policy_;
    const stage_input_policy input_policy_;

    pipeline_type* owning_pipeline_;

    std::atomic_bool can_receive_input_ = false;
    std::atomic_bool is_running_ = false;
    std::atomic_bool is_disposed_ = false;
    std::atomic_bool pending_dispose_ = false;

    std::thread owning_thread_;
    std::pair<std::condition_variable, std::mutex> event_wait_;

    struct input_link_desc {
        id_type cached_id;
        std::weak_ptr<stage_type> stage;
        std::atomic<fence_index_type> input_submit_fence;
    };
    struct output_link_desc {
        id_type cached_id;
        std::weak_ptr<stage_type> stage;
    };

    std::vector<input_link_desc> input_stages_;
    std::vector<output_link_desc> output_stages_;

    std::weak_ptr<shared_data_type> fetched_shared_data_;
    fence_index_type fence_index_ = 0;
};

template <Pipe Pipe_, typename SharedData_>
class stage : public impl__::stage_base<SharedData_> {
public:
    stage(const std::string& name, stage_execution_policy execution_policy, stage_input_policy input_policy)
        : impl__::stage_base<SharedData_>(name, execution_policy, input_policy)
    {
    }

    stage(const stage& other) = delete;
    stage(stage&& other) noexcept = default;
    stage& operator=(const stage& other) = delete;
    stage& operator=(stage&& other) noexcept = default;

public:
    using pipe_type = Pipe_;
    using input_type = typename pipe_type::input_type;
    using output_type = typename pipe_type::output_type;
    using shared_data_type = SharedData_;
    using pipe_output_handler_type = std::function<bool(pipe_error, output_type const&)>;

    template <Pipe Other_>
    using stage_type = stage<Other_, shared_data_type>;

public:
    template <Pipe Other_, typename Fn_>
    void connect_output_to(std::shared_ptr<stage_type<Other_>> other, Fn_&& handler)
    {
        using other_input_type = typename Other_::input_type;
        static_assert(std::is_invocable_v<Fn_, pipe_error, shared_data_type&, output_type const&, other_input_type&>);

        this->process_output_link__(other);

        // ��ũ �ڵ鷯
        output_handlers_.emplace_back(
          [this, other, handler](pipe_error e, output_type const& o) -> bool {
              if (!other->can_receive_input()) { return false; }

              auto [input_ptr, lock] = other->input_front__();
              auto fence_data = other->fence_shared_data().lock();

              bool const is_fence_invalidated = fence_data == nullptr;
              if (is_fence_invalidated) { return false; }

              handler(e, *fence_data, o, *input_ptr);
              other->notify_submit_input__(this->id());
              return true;
          });
    }

private:
    void exec_pipe__() override;
    std::pair<input_type*, std::lock_guard<std::mutex>> input_front__() { return std::make_pair(&inputs[front_input_], std::lock_guard<std::mutex>(front_input_lock_)); }
    void swap_input__() { front_input_ = !front_input_; }

private:
    std::optional<pipe_type> pipe_instance_;
    std::vector<pipe_output_handler_type> output_handlers_;

    input_type inputs[2];
    bool front_input_ = false;
    std::mutex front_input_lock_;
};

} // namespace impl__
} // namespace pipepp