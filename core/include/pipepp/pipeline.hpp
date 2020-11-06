#pragma once
#include <any>
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
 *          ����� �����ͷ� ����ü�� �˰��� ����
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

private:
    void loop__()
    {
        for (is_running_ = true; pending_dispose_ == false;) {
            // fetch next pipe data
            // pipeline class provides next valid fence index and pipe data based on current fence index.

            // wait until all inputs are gathered
            // if current fence is invalidated, do 'continue'

            // run pipe + handler
            exec_pipe__();
        }

        is_disposed_ = true;
        is_running_ = false;
    }

protected:
    void notify_submit_input__(size_t stage_id_) {}
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
    }

    // �ڼ� �Լ��� connect() �Լ����� �������� ���� ó�� ���� �� ȣ��
    void process_output_link__(std::shared_ptr<stage_type> to)
    {
        if (is_running_ || is_disposed_) {
            throw pipe_exception("Pipe connection cannot be changed during running");
        }

        // to�� optional�� ���, �Է� ���� �ϳ��� �����ؾ� �մϴ�.
        if (to->input_policy_ == stage_input_policy::only_if_possible && !to->input_stages_.empty()) {
            throw pipe_exception("Optional input pipe cannot hold more than one input source");
        }

        if (auto found = std::find_if(input_stages_.begin(), input_stages_.end(), stage_hash_predicate(to->id())); found != input_stages_.end()) {
            throw pipe_exception("Given pipeline is already in link list!");
        }

        // to�� ����� ��������� �˻���, this�� �Է����� ����Ǵ� ��ȯ ������ �����ϴ��� �˻��մϴ�.

        // to�� �Է�, this�� �ڽ��� ������ �Է��� ��������� �˻���, ���� ����� optional ���������� ������ �˻��մϴ�.


        // establish link
        auto& insertion = input_stages_.emplace_back();
        insertion.stage = to;
        insertion.cached_id = to->id();
        insertion.input_submit_fence = 0;
    }

private:
    virtual void exec_pipe__() = 0;

private:
    const size_t id_;
    const std::string name_;

    const stage_execution_policy execution_policy_;
    const stage_input_policy input_policy_;

    pipeline_type* owning_pipeline_;

    std::atomic_bool is_running_ = false;
    std::atomic_bool is_disposed_ = false;
    std::atomic_bool pending_dispose_ = false;

    std::thread owning_thread_;
    std::pair<std::condition_variable, std::mutex> event_wait_;

    struct input_link_desc {
        id_type cached_id;
        std::weak_ptr<stage_type> stage;
        fence_index_type input_submit_fence;
    };
    std::vector<input_link_desc> input_stages_;

    std::weak_ptr<shared_data_type> fetched_shared_data_;
    fence_index_type fence_index = 0;
};

} // namespace impl__

template <Pipe Pipe_, typename SharedData_>
class stage : public impl__::stage_base<SharedData_> {
public:
    stage(const std::string& name, stage_execution_policy execution_policy, stage_input_policy input_policy)
        : impl__::stage_base<SharedData_>(name, execution_policy, input_policy)
    {
    }

    explicit stage(const impl__::stage_base<SharedData_>& other)
        : impl__::stage_base<SharedData_>(other)
    {
    }

public:
    using pipe_type = Pipe_;
    using input_type = typename pipe_type::input_type;
    using ouput_type = typename pipe_type::output_type;

private:
    void exec_pipe__() override;
};
} // namespace pipepp