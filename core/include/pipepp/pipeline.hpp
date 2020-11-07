#pragma once
#include <map>
#include <pipepp/stage.hpp>
#include <pipepp/templates.hxx>
#include <shared_mutex>

namespace pipepp::impl__ {
class pipeline_base {
public:
    using shared_data_type = shared_data_base;
    using stage_type = stage_base<shared_data_type>;
    using stage_pointer = std::shared_ptr<stage_type>;
    using stage_weak_pointer = std::weak_ptr<stage_type>;

private:
    void setup_stage_callbacks(stage_type& stage)
    {
        stage.on_fence_data_expired_ = [this](fence_index_type index) {
            auto lock_wr = templates::lock_write(data_lock_);
            auto it = data_.find(index);

            if (it == data_.end()) {
                // 에러가 발생했을 때 해당 파이프 데이터를 미리 버렸을 수 있습니다.
                return;
            }

            if (it->second.expired() == false) {
                throw pipe_exception("Expired shared data is being used");
            }

            data_.erase(it);
        };

        stage.purge_fence_on_error_ = [this](fence_index_type index) {
            auto lock_wr = templates::lock_write(data_lock_);
            data_.erase(index); // 여러 번 이상 시도될 수 있음
        };
    }

private:
    std::vector<stage_pointer> stages_;
    std::map<fence_index_type, std::weak_ptr<shared_data_type>> data_;
    std::atomic<fence_index_type> fence_index_head_;
    std::shared_mutex data_lock_;
};

} // namespace pipepp::impl__
