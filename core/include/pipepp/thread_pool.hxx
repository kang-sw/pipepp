#pragma once
#include <condition_variable>
#include <mutex>
#include <thread>

namespace templates {
class thread_pool {
public:
public:
    struct thread_instance {

        std::atomic_bool pending_dispose_;
        std::thread thr_;
    };

public:
    void reserve_threads(size_t n);

private:
    size_t next_index();

private:

};
} // namespace templates
