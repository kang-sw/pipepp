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
    thread_pool(size_t task_queue_cap_ = 1024, size_t num_workers = std::thread::hardware_concurrency()) noexcept;

    ~thread_pool();

public:
    void resize_worker_pool(size_t new_size);
    size_t num_workers() const;
    size_t num_pending_task() const { return tasks_.size(); }
    size_t task_queue_capacity() const { return tasks_.capacity(); }

    template <typename Fn_, typename... Args_>
    decltype(auto) launch_task(Fn_&& f, Args_... args);

private:
    void add_worker__();
    void pop_workers__(size_t count);

public:
    std::chrono::milliseconds launch_timeout_ms{1000};

private:
    safe_queue<std::function<void()>> tasks_;
    std::vector<std::pair<std::shared_ptr<std::atomic_bool>, std::thread>> workers_;
    mutable std::mutex worker_lock_;

    std::condition_variable event_wait_;
    mutable std::mutex event_lock_;

    std::shared_ptr<std::atomic_size_t> available_workers_;
};

inline thread_pool::thread_pool(size_t task_queue_cap_, size_t num_workers) noexcept
    : tasks_(task_queue_cap_)
    , available_workers_(std::make_shared<std::atomic_size_t>(0))
{
    resize_worker_pool(num_workers);
}

inline thread_pool::~thread_pool()
{
    pop_workers__(workers_.size());
}

inline void thread_pool::resize_worker_pool(size_t new_size)
{
    std::lock_guard<std::mutex> lock(worker_lock_);
    const bool is_popping = new_size < workers_.size();
    if (new_size > workers_.size()) {
        while (new_size != workers_.size()) {
            add_worker__();
        }
    }
    else if (new_size < workers_.size()) {
        pop_workers__(workers_.size() - new_size);
    }
}

inline size_t thread_pool::num_workers() const
{
    std::lock_guard<std::mutex> lock(worker_lock_);
    return workers_.size();
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

inline void thread_pool::pop_workers__(size_t count)
{
    auto const begin = workers_.end() - count;
    auto const end = workers_.end();

    for (auto it = begin; it != end; ++it) {
        it->first->store(true);
    }
    event_wait_.notify_all();
    for (auto it = begin; it != end; ++it) {
        it->second.join();
    }

    workers_.erase(begin, end);
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
