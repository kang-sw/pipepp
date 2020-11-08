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
    thread_pool(size_t task_queue_cap_ = 1024, size_t num_workers = std::thread::hardware_concurrency(), size_t concrete_worker_count_limit = -1) noexcept;

    ~thread_pool();

public:
    void resize_worker_pool(size_t new_size);
    size_t num_workers() const;
    size_t num_pending_task() const { return tasks_.size(); }
    size_t task_queue_capacity() const { return tasks_.capacity(); }
    size_t num_available_workers() const { return num_workers() - num_working_workers_; }

    template <typename Fn_, typename... Args_>
    decltype(auto) launch_task(Fn_&& f, Args_... args);

private:
    bool try_add_worker__();
    void pop_workers__(size_t count);

public:
    std::chrono::milliseconds launch_timeout_ms{1000};
    std::chrono::milliseconds stall_check_period{5};
    std::chrono::milliseconds stall_wait_tolerance{5};

private:
    struct worker_desc {
        struct atomic_bool_wrap_t {
            std::atomic_bool value;

            atomic_bool_wrap_t() = default;
            atomic_bool_wrap_t(const atomic_bool_wrap_t& other) = delete;
            atomic_bool_wrap_t(atomic_bool_wrap_t&& other) noexcept
            {
                value = other.value.load();
            }
            atomic_bool_wrap_t& operator=(const atomic_bool_wrap_t& other) = delete;
            atomic_bool_wrap_t& operator=(atomic_bool_wrap_t&& other) noexcept
            {
                value = other.value.load();
                return *this;
            }
        };
        std::thread thread;
        atomic_bool_wrap_t disposer;
        std::chrono::system_clock::time_point last_active;
    };

private:
    safe_queue<std::function<void()>> tasks_;
    std::vector<worker_desc> workers_;
    mutable std::mutex worker_lock_;

    std::condition_variable event_wait_;
    mutable std::mutex event_lock_;

    std::atomic_size_t num_working_workers_;
    size_t const worker_limit_;
};

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

inline thread_pool::thread_pool(size_t task_queue_cap_, size_t num_workers, size_t worker_limit) noexcept
    : tasks_(task_queue_cap_)
    , worker_limit_(worker_limit)
{
    resize_worker_pool(num_workers);

    launch_task([this]() {
        while (!workers_.front().disposer.value) {
            if (num_available_workers() == 0) {
                std::lock_guard<std::mutex> lock{worker_lock_};
                bool should_spawn_worker;
                {
                    std::chrono::system_clock::time_point max_tp = {};
                    for (auto& worker : workers_) {
                        max_tp = std::max(max_tp, worker.last_active);
                    }

                    should_spawn_worker
                      = std::chrono::system_clock::now() - max_tp > stall_wait_tolerance;
                }

                if (should_spawn_worker) {
                    // Extend threads 2x
                    for (size_t count = workers_.size();
                         count-- && workers_.size() < worker_limit_;
                         (void)0) {
                        try_add_worker__();
                    }
                }
            }

            std::this_thread::sleep_for(stall_check_period);
        }
    });
}

inline thread_pool::~thread_pool()
{
    pop_workers__(workers_.size());
}

inline void thread_pool::resize_worker_pool(size_t new_size)
{
    if (new_size == 0) {
        throw std::invalid_argument{"Size 0 is not allowed"};
    }
    new_size += 1; // Reserve first

    std::lock_guard<std::mutex> lock(worker_lock_);
    if (new_size > workers_.size()) {
        new_size = std::min(worker_limit_, new_size);
        while (new_size != workers_.size()) {
            try_add_worker__();
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

inline bool thread_pool::try_add_worker__()
{
    if (workers_.size() >= worker_limit_) {
        return false;
    }

    auto worker = [this, index = workers_.size()]() {
        std::function<void()> event;
        auto& desc = workers_[index];

        while (desc.disposer.value == false) {
            if (tasks_.try_pop(event)) {
                desc.last_active = std::chrono::system_clock::now();

                num_working_workers_.fetch_add(1);
                event();
                num_working_workers_.fetch_sub(1);
            }
            else {
                std::unique_lock<std::mutex> lock(event_lock_);
                event_wait_.wait(lock);
            }
        }
    };

    auto& wd = workers_.emplace_back();
    wd.disposer.value = false;
    wd.thread = std::thread(std::move(worker));
    wd.last_active = {};

    return true;
}

inline void thread_pool::pop_workers__(size_t count)
{
    auto const begin = workers_.end() - count;
    auto const end = workers_.end();

    for (auto it = begin; it != end; ++it) {
        it->disposer.value.store(true);
    }
    event_wait_.notify_all();
    for (auto it = begin; it != end; ++it) {
        it->thread.join();
    }

    workers_.erase(begin, end);
}
} // namespace templates
