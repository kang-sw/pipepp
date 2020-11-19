#pragma once
#include <memory>

#include "nana/gui/widgets/form.hpp"
#include "pipepp/execution_context.hpp"
#include "pipepp/options.hpp"

namespace nana {
struct arg_listbox;
}

namespace pipepp {
struct execution_context_data;
enum class pipe_id_t : unsigned long long;
namespace impl__ {
class pipeline_base;
}
} // namespace pipepp

namespace pipepp::gui {
/**
 * 단일 파이프의 옵션을 및 디버그 데이터를 표시합니다.
 */
class pipe_detail_panel : public nana::form {
    using super = nana::form;

public:
    pipe_detail_panel(nana::window owner, const nana::rectangle& rectangle, const nana::appearance& appearance);
    ~pipe_detail_panel();

public:
    void reset_pipe(std::weak_ptr<impl__::pipeline_base> pl, pipe_id_t id);
    void update(std::shared_ptr<execution_context_data>);

private:
    void _reload_options(pipepp::impl__::option_base const& opt);
    void _cb_option_arg_selected(nana::arg_listbox const&);

public:
    size_t num_timer_text_view_horizontal_chars = 60;

private:
    struct data_type;
    std::unique_ptr<data_type> impl_;
};

} // namespace pipepp::gui
