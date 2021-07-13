#pragma once
#include <memory>

#include "nana/gui/place.hpp"
#include "nana/gui/widgets/panel.hpp"
#include "pipepp/execution_context.hpp"
#include "pipepp/options.hpp"

namespace nana {
struct arg_listbox;
}

namespace pipepp {
struct execution_context_data;
enum class pipe_id_t : unsigned long long;
namespace detail {
class pipeline_base;
}
} // namespace pipepp

namespace pipepp::gui {
/**
 * 단일 파이프의 옵션을 및 디버그 데이터를 표시합니다.
 */
class pipe_detail_panel : public nana::panel<false> {
    using super = nana::panel;

public:
    pipe_detail_panel(nana::window owner);
    ~pipe_detail_panel() override;

public:
    void reset_pipe(std::weak_ptr<detail::pipeline_base> pl, pipe_id_t id);
    void update(std::shared_ptr<execution_context_data>);

    void div(std::string s) { _place.div(std::move(s)); }
    void collocate() { _place.collocate(); }
    auto& operator[](char const* s) { return _place[s]; }

public:
    size_t num_timer_text_view_horizontal_chars = 58;

private:
    struct data_type;
    std::unique_ptr<data_type> impl_;

    nana::place _place{*this};
};

} // namespace pipepp::gui
