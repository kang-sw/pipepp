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
    explicit stage_base(pipeline_type* owner, std::string name, stage_execution_policy execution_policy, stage_input_policy input_policy)
        : id_(stage_id_generator{}())
        , name_(std::move(name))
        , execution_policy_(execution_policy)
        , input_policy_(input_policy)
        , owning_pipeline_(owner)
    {
        using namespace std::chrono_literals;
        default_event_wait_duration = 100ms;
    }
    virtual ~stage_base() = default;

    stage_base(const stage_base& other) = delete;
    stage_base(stage_base&& other) = delete;
    stage_base& operator=(const stage_base& other) = delete;
    stage_base& operator=(stage_base&& other) = delete;

public: // getters
    id_type id() const { return id_; }
    std::string_view name() const { return name_; }
    bool can_receive_input() const { return can_receive_input_; }
    fence_index_type fence_index() const { return fence_index_; }
    std::weak_ptr<shared_data_type> fence_shared_data() const { return fetched_shared_data_; }
    bool is_pipe_running() const { return is_pipe_running_; }
    bool pending_dispose() const
    {
        auto ptr = pending_dispose_.lock();
        return !ptr || *ptr;
    }

public:
    virtual void dispose()
    {
        if (!worker_.joinable()) {
            throw pipe_exception("Worker is not in joinable state!");
        }

        // TODO: Dispose thread, etc ...
        pending_dispose_.lock()->store(true);

        output_event_wait_.first.notify_all();
        input_event_wait_.first.notify_all();

        worker_.join();
    }

private:
    void launch__(std::shared_ptr<bool> dispose_ptr)
    {
        // TODO: Launch thread, etc ...
        pending_dispose_ = dispose_ptr;
        worker_ = std::thread(&loop__, this);
    }

    bool is_input_ready() const
    {
        auto it = std::find_if(
          input_stages_.begin(), input_stages_.end(),
          [this](input_link_desc const& d) {
              return d.input_submit_fence != fence_index_;
          });
        return it == input_stages_.end();
    }

    void loop__()
    {
        for (is_running_ = true; pending_dispose() == false;) {
            // TODO: fetch next pipe data
            // pipeline class provides next valid fence index and pipe data based on current fence index.

            bool is_fence_valid = true;
            can_receive_input_ = true;
            output_event_wait_.first.notify_all();

            for (; pending_dispose() == false && !is_input_ready();) {
                // wait until all the inputs are gathered
                {
                    auto lock = std::unique_lock<std::mutex>{input_event_wait_.second};
                    input_event_wait_.first.wait_for(lock, default_event_wait_duration);
                }

                // if current fence is invalidated, do 'continue'
                // TODO: check if fence is still valid
                if (0) {
                    is_fence_valid = false;
                    break;
                }
            }

            if (pending_dispose()) { break; }
            if (!is_fence_valid) { continue; }

            // run pipe + handler
            // before then, input must be disabled.
            can_receive_input_ = false;
            is_pipe_running_ = true;
            exec_pipe__();
            is_pipe_running_ = false;
            output_event_wait_.first.notify_all();
        }

        is_disposed_ = true;
        is_running_ = false;
    }

protected:
    void notify_submit_input__(id_type stage_id_)
    {
        if (
          auto it = std::find_if(
            input_stages_.begin(), input_stages_.end(),
            stage_hash_predicate(stage_id_));
          it != input_stages_.end()) {
            if (it->input_submit_fence == fence_index()) {
                throw pipe_exception("Multiple input notification for same input stage");
            }
            it->input_submit_fence.store(fence_index());

            if (is_input_ready()) {
                input_event_wait_.first.notify_one();
            }
        }
        else {
            throw pipe_exception("Stage ID is not valid input stage");
        }
    }

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

    // 자손 함수의 connect() 함수에서 본격적인 연결 처리 수행 전 호출
    void process_output_link__(std::shared_ptr<stage_type> to)
    {
        if (is_running_ || is_disposed_) {
            throw pipeline_link_exception("Pipe connection cannot be changed during running");
        }

        bool const is_optional = to->input_policy_ == stage_input_policy::only_if_possible;
        bool const is_synchronous = to->execution_policy_ == stage_execution_policy::sync;
        if ((is_optional || is_synchronous) && !to->input_stages_.empty()) {
            // to가 optional이거나 sync인 경우, 입력 노드는 하나만 존재해야 합니다.
            throw pipeline_link_exception("Optional input pipe cannot hold more than one input source");
        }

        // to의 출력을 재귀적으로 검사해, this의 입력으로 연결되는 순환 연결이 존재하는지 검사합니다.
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

        // to의 입력, this의 자신을 포함한 입력을 재귀적으로 검사해, 가장 가까운 optional 스테이지가 같은지 검사합니다.
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

    void wait_output(std::chrono::milliseconds duration)
    {
        std::unique_lock<std::mutex> lock{output_event_wait_.second};
        output_event_wait_.first.wait_for(lock, duration);
    }

private:
    virtual void exec_pipe__() = 0;

public:
    std::chrono::milliseconds default_event_wait_duration;

private:
    const size_t id_;
    const std::string name_;

    const stage_execution_policy execution_policy_;
    const stage_input_policy input_policy_;

    pipeline_type* owning_pipeline_;

    std::atomic_bool is_pipe_running_ = false;
    std::atomic_bool can_receive_input_ = false;
    std::atomic_bool is_running_ = false;
    std::atomic_bool is_disposed_ = false;
    std::weak_ptr<std::atomic_bool> pending_dispose_;

    std::thread worker_;
    std::pair<std::condition_variable, std::mutex> input_event_wait_;
    std::pair<std::condition_variable, std::mutex> output_event_wait_;

    struct input_link_desc {
        id_type cached_id;
        std::weak_ptr<stage_type> stage;
        std::atomic<fence_index_type> input_submit_fence = -1;
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
    template <typename... PipeArgs_>
    stage(const std::string& name, stage_execution_policy execution_policy, stage_input_policy input_policy, PipeArgs_&&... args)
        : impl__::stage_base<SharedData_>(name, execution_policy, input_policy)
    {
        pipe_instance_.emplace(std::forward<PipeArgs_>(args)...);
    }

    stage(const stage& other) = delete;
    stage(stage&& other) = delete;
    stage& operator=(const stage& other) = delete;
    stage& operator=(stage&& other) = delete;

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

        // 링크 핸들러 목록에 추가
        output_handlers_.emplace_back(
          [this, other, handler](pipe_error e, output_type const& o) -> void {
              while (!other->can_receive_input()) {
                  if (stage_input_policy::only_if_possible == other->input_policy_) {
                      return;
                  }

                  other->wait_output(this->default_event_wait_duration);

                  // 대기 중 dispose 이벤트 발생하면 즉시 탈출 ...
                  if (this->pending_dispose()) {
                      return;
                  }
              }

              auto [input_ptr, lock] = other->lock_front_input__();
              auto fence_data = other->fence_shared_data().lock();

              bool const is_fence_invalidated = fence_data == nullptr;
              if (is_fence_invalidated) { return; }

              handler(e, *fence_data, o, *input_ptr);
              other->notify_submit_input__(this->id());

              if (stage_execution_policy::sync == other->execution_policy_) {
                  // 동기 실행인 경우 대상 파이프의 실행 종료까지 대기 ...
                  while (other->is_pipe_running()) {
                      other->wait_output(this->default_event_wait_duration);
                  }
              }
          });
    }

private:
    void exec_pipe__() override
    {
        auto [input_ptr, lock] = lock_front_input__();
        pipe<pipe_type>* pipe_ptr = &pipe_instance_.value();

        if (pipe_ptr->is_busy()) {
            throw pipe_exception("Invalid pipe execution call detected ... Pipe is busy!");
        }

        auto perr = pipe_ptr->invoke__(*input_ptr, output_);

        for (auto& handler : output_handlers_) {
            handler(perr, output_);

            if (this->pending_dispose()) {
                break;
            }
        }
    }

    std::pair<input_type*, std::lock_guard<std::mutex>> lock_front_input__() { return std::make_pair(&inputs_[front_input_], std::lock_guard<std::mutex>(front_input_lock_)); }
    void swap_input__() { front_input_ = !front_input_; }

private:
    std::optional<pipe_type> pipe_instance_;
    std::vector<pipe_output_handler_type> output_handlers_;

    output_type output_;
    input_type inputs_[2];
    bool front_input_ = false;
    std::mutex front_input_lock_;
};

} // namespace impl__
} // namespace pipepp