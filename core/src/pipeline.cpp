#include "pipepp/pipeline.hpp"
#include <mutex>

std::shared_ptr<pipepp::base_shared_context> pipepp::impl__::pipeline_base::_fetch_shared()
{
    std::lock_guard lock(fence_object_pool_lock_);

    for (auto& ptr : fence_objects_) {
        if (ptr.use_count() == 1) {
            // ���� �ٸ� ���۷����� ��� �����Ǿ��ٸ�, �����մϴ�.
            return ptr;
        }
    }

    return fence_objects_.emplace_back(std::make_shared<base_shared_context>());
}
