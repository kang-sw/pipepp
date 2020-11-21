#include <filesystem>
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

nlohmann::json pipepp::impl__::pipeline_base::export_options()
{
    nlohmann::json opts;
    auto _lck = options().lock_read();
    opts["shared"] = options().value();
    auto& opts_pipe_section = opts["pipes"];

    for (auto& pipe : pipes_) {
        auto& opts_pipe = opts_pipe_section[pipe->name()];
        opts_pipe = pipe->options().value();
    }

    return opts;
}

void pipepp::impl__::pipeline_base::import_options(nlohmann::json const& in)
{
    // 재귀적으로 옵션을 대입합니다.
    auto _lck = options().lock_write();
    options().value().merge_patch(in["shared"]);
    auto& pipes_in = in["pipes"];

    using nlohmann::json;
    auto update = [](auto& recurse, json& l, json const& r) -> void {
        if (l.is_object() && r.is_object()) {
            for (auto& item : l.items()) {
                if (r.contains(item.key())) {
                    recurse(recurse, item.value(), r.at(item.key()));
                }
            }
        }
        else if (l.is_array() && r.is_array()) {
            int i;
            for (i = 0; i < std::min(l.size(), r.size()); ++i) {
                recurse(recurse, l[i], r[i]);
            }
            // 만약 r의 크기가 더 크다면, 일단 복사.
            while (i < r.size()) {
                l[i] = r[i];
            }
        }
        else if (strcmp(l.type_name(), r.type_name()) == 0) {
            l = r;
        }
    };

    for (auto& pipe : pipes_) {
        auto& opts = pipe->options().value();
        auto it_found = pipes_in.find(pipe->name());
        if (it_found == pipes_in.end()) { continue; }

        update(update, opts, it_found.value());
        pipe->mark_dirty();
    }
}

std::shared_ptr<pipepp::base_shared_context> pipepp::impl__::pipeline_base::_fetch_shared()
{
    std::lock_guard lock(fence_object_pool_lock_);

    for (auto& ptr : fence_objects_) {
        if (ptr.use_count() == 1) {
            // 만약 다른 레퍼런스가 모두 해제되었다면, 재사용합니다.
            ptr->launched_ = std::chrono::system_clock::now();
            return ptr;
        }
    }

    auto& gen = fence_objects_.emplace_back(_new_shared_object());
    gen->global_options_ = &global_options_;
    gen->launched_ = std::chrono::system_clock::now();

    return gen;
}

std::shared_ptr<pipepp::execution_context_data> pipepp::impl__::pipe_proxy_base::consume_execution_result()
{
    auto exec_result = pipe().latest_execution_context();
    return exec_result ? const_cast<execution_context*>(exec_result)->_consume_read_buffer()
                       : nullptr;
}

bool pipepp::impl__::pipe_proxy_base::execution_result_available() const
{
    auto exec_result = pipe().latest_execution_context();
    return exec_result && exec_result->can_consume_read_buffer();
}
