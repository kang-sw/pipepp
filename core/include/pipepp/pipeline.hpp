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
 * 파이프라인 디자인 개요
 *
 * 각 파이프라인 스테이지 = 스레드 1개 할당
 * 실행 정책:
 *      Synchronous Execution: 출력 연결한 파이프라인 모두 is_ready()일 때만 실행 가능
 *      Parallel Execution: 출력 연결된 파이프라인과 동시에 실행 가능
 *
 * 시작 정책:
 *      Primary Stage: 반드시 is_ready() 상태일 때까지 대기, 올바른 fence index를 전달.
 *      Optional Stage: is_ready()일 때만 파이프라인 시동. 입력 assembling 불가
 *                       단, 이 경우 출력을 연결하는 노드는 반드시 가장 가까운 부모 옵셔널 스테이지를 공유해야 함.
 *                       e.g.
 *                       아래의 경우, Pri0의 출력을 Pri1에 연결 불가, 반대도 불가
 *                       Pri1은 Pri2에 연결 가능, 반대도 가능
 *                          Pri - Pri - Opt + Pri - Pri0
 *                                          |
 *                                          + Opt +------- Pri1
 *                                                |
 *                                                + Pri -- Pri2
 *
 * 스테이지 구조:
 *      입력 처리기:
 *          0) fence 값으로부터 shared_data_ fetch해오기
 *          1) 이전 파이프 입력 처리
 *          1.a) Primary Stage의 경우, 이전 링크의 모든 입력 처리까지 대기
 *          1.b) Optional Stage의 경우 입력 즉시 파이프라인 가동 시작
 *
 *      파이프:
 *          입출력 데이터로 구현체의 알고리즘 실행
 *
 *      출력 처리기:
 *          1) 출력에 연결된 각 스테이지 iterate
 *          1.a) 실행 정책 Synchronous인 경우 다음 stage는 반드시 is_ready() 상태
 *               파이프라인에 연결된 다음 파이프라인 하나씩 순서대로 처리
 *               Pri0 + Pri1
 *                    + Pri2
 *                    + ...
 *               일 때 Pri1에 입력 후 is_ready()까지 대기, Pri2에 입력 후 is_ready()까지 대기 ... PriN까지 반복.
 *               즉, 이 경우 Optional 시작 정책은 의미 x
 *          1.b) 실행 정책 Parallel인 경우
 *          1.b.a) Primary Stage 연결은 is_ready()까지 대기, 데이터 전달
 *          1.b.b) Optional Stage 연결은 is_ready() == false이면 생략
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

    // 입력 스테이지 재귀(깊이 우선)
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

    // 자손 함수의 connect() 함수에서 본격적인 연결 처리 수행 전 호출
    void process_output_link__(std::shared_ptr<stage_type> to)
    {
        if (is_running_ || is_disposed_) {
            throw pipe_exception("Pipe connection cannot be changed during running");
        }

        // to가 optional인 경우, 입력 노드는 하나만 존재해야 합니다.
        if (to->input_policy_ == stage_input_policy::only_if_possible && !to->input_stages_.empty()) {
            throw pipe_exception("Optional input pipe cannot hold more than one input source");
        }

        if (auto found = std::find_if(input_stages_.begin(), input_stages_.end(), stage_hash_predicate(to->id())); found != input_stages_.end()) {
            throw pipe_exception("Given pipeline is already in link list!");
        }

        // to의 출력을 재귀적으로 검사해, this의 입력으로 연결되는 순환 연결이 존재하는지 검사합니다.

        // to의 입력, this의 자신을 포함한 입력을 재귀적으로 검사해, 가장 가까운 optional 스테이지가 같은지 검사합니다.


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