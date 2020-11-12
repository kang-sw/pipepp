#include "pipepp/pipeline.hpp"
#include <mutex>

std::shared_ptr<pipepp::base_fence_shared_data> pipepp::impl__::pipeline_base::_fetch_shared()
{
    std::lock_guard lock(fence_object_pool_lock_);

    for (auto& ptr : fence_objects_) {
        if (ptr.use_count() == 1) {
            // 만약 다른 레퍼런스가 모두 해제되었다면, 재사용합니다.
            return ptr;
        }
    }

    return fence_objects_.emplace_back(std::make_shared<base_fence_shared_data>());
}
