#pragma once
#include "nana/gui/widgets/panel.hpp"

namespace pipepp {
namespace detail {
class pipeline_base;
} // namespace detail

enum class pipe_id_t : unsigned long long;
struct execution_context_data;
} // namespace pipepp

namespace pipepp::gui {
class pipeline_board;

class debug_data_panel : public nana::panel<true> {
    using super = nana::panel<true>;

public:
    debug_data_panel(nana::window wd, bool visible);
    ~debug_data_panel();

    void _set_board_ref(pipeline_board* ref);

public:
    void scroll_width(size_t v);
    void elem_height(size_t v);

public:
    void _reset_pipe(std::weak_ptr<detail::pipeline_base> pl, pipe_id_t id);
    void _update(std::shared_ptr<execution_context_data>);

private:
    void _refresh_layout();

private:
    friend struct inline_widget;
    struct data_type;
    std::unique_ptr<data_type> impl_;
    data_type& m;
};
} // namespace pipepp::gui
