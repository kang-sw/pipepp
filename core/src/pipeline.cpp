#include <mutex>
#include "pipepp/pipeline.hpp"

void pipepp::impl__::pipeline_base::sync()
{
    for (bool is_busy = true; is_busy;) {
        using namespace std::literals;
        std::this_thread::sleep_for(5ms);
        is_busy = false;

        for (auto& pipe : pipes_) {
            if (pipe->is_async_operation_running()) {
                is_busy = true;
                break;
            }
        }

        is_busy = is_busy
                  || workers_.num_total_waitings() > 0;
    }
}

std::shared_ptr<pipepp::base_shared_context> pipepp::impl__::pipeline_base::_fetch_shared()
{
    std::lock_guard lock(fence_object_pool_lock_);

    for (auto& ptr : fence_objects_) {
        if (ptr.use_count() == 1) {
            // 만약 다른 레퍼런스가 모두 해제되었다면, 재사용합니다.
            return ptr;
        }
    }

    auto& gen = fence_objects_.emplace_back(_new_shared_object());
    return gen->global_options_ = &global_options_, gen;
}
