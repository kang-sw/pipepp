#pragma once
#include "safe_queue.hxx"
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <thread>

namespace templates {
class timeout_exception : public std::exception {
public:
    timeout_exception() = default;

    explicit timeout_exception(char const* _Message)
        : exception(_Message)
    {
    }

    timeout_exception(char const* _Message, int i)
        : exception(_Message, i)
    {
    }

    explicit timeout_exception(exception const& _Other)
        : exception(_Other)
    {
    }
};

class thread_pool {
public:
    struct thread_instance {

        std::atomic_bool pending_dispose_;
        std::thread thr_;
    };

public:
    thread_pool(size_t task_queue_cap_ = 1024, size_t num_workers = std::thread::hardware_concurrency()) noexcept
        : tasks_(task_queue_cap_)
        , available_workers_(std::make_shared<std::atomic_size_t>(0))
    {
        resize_worker_pool(num_workers);
    }

    ~thread_pool()
    {
        join_all__();
    }

    void resize_worker_pool(size_t n);

    template <typename Fn_, typename... Args_>
    decltype(auto) launch_task(Fn_&& f, Args_... args);

private:
    void add_worker__();
    void pop_worker__();
    void join_all__();

public:
    std::chrono::milliseconds launch_timeout_ms{1000};

private:
    safe_queue<std::function<void()>> tasks_;
    std::vector<std::pair<std::shared_ptr<std::atomic_bool>, std::thread>> workers_;
    std::mutex worker_lock_;

    std::condition_variable event_wait_;
    std::mutex event_lock_;

    std::shared_ptr<std::atomic_size_t> available_workers_;
};

inline void thread_pool::resize_worker_pool(size_t n)
{
    std::lock_guard<std::mutex> lock(worker_lock_);
    const bool is_popping = n < workers_.size();
    while (n != workers_.size()) {
        if (n > workers_.size())
            add_worker__();
        else
            pop_worker__();
    }

    if (is_popping) {
        event_wait_.notify_all();
    }
}

inline void thread_pool::add_worker__()
{
    auto disposer = std::make_shared<std::atomic_bool>(false);
    auto worker = [this, disposer, active_jobs = available_workers_]() {
        std::function<void()> event;
        while (disposer->load() == false) {
            if (tasks_.try_pop(event)) {
                active_jobs->fetch_add(1);
                event();
                active_jobs->fetch_add(1);
            }
            else {
                std::unique_lock<std::mutex> lock(event_lock_);
                event_wait_.wait(lock);
            }
        }
    };

    workers_.emplace_back(disposer, std::thread(std::move(worker)));
}

inline void thread_pool::pop_worker__()
{
    auto& back = workers_.back();
    back.first->store(true);
    back.second.detach();
    workers_.pop_back();
}

inline void thread_pool::join_all__()
{
    std::lock_guard<std::mutex> lock(worker_lock_);
    for (auto& pair : workers_) { pair.first->store(true); }
    event_wait_.notify_all();
    for (auto& pair : workers_) { pair.second.join(); }
}

template <typename Fn_, typename... Args_>
decltype(auto) thread_pool::launch_task(Fn_&& f, Args_... args)
{
    using callable_return_type = std::invoke_result_t<Fn_, Args_...>;
    using return_type = std::future<callable_return_type>;

    auto promise = std::make_shared<std::promise<callable_return_type>>();
    auto value_tuple = std::make_tuple(promise, f, std::tuple<Args_...>(args...));

    struct executor {
        decltype(value_tuple) arg;
        void operator()()
        {
            auto& [promise, f, arg_pack] = arg;

            if constexpr (std::is_same_v<void, callable_return_type>) {
                std::apply(f, std::move(arg_pack));
                promise->set_value();
            }
            else {
                promise->set_value(std::apply(f, std::move(arg_pack)));
            }
        }
    };

    using std::chrono::system_clock;
    auto elapse_begin = system_clock::now();
    while (!tasks_.try_push(executor{std::move(value_tuple)})) {
        if (system_clock::now() - elapse_begin > launch_timeout_ms) {
            throw timeout_exception{};
        }

        std::this_thread::yield();
    }

    event_wait_.notify_one();
    return return_type(promise->get_future());
}
} // namespace templates
