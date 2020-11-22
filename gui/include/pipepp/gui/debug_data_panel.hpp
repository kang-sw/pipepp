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

class debug_data_panel : nana::panel<false> {
    using super = nana::panel<false>;

public:
    debug_data_panel(nana::window wd, bool visible, pipeline_board* board_ref);
    ~debug_data_panel();

public:
    void reset_pipe(std::weak_ptr<detail::pipeline_base> pl, pipe_id_t id);
    void update(std::shared_ptr<execution_context_data>);

private:
    struct implementation;
    std::unique_ptr<implementation> impl_;
    implementation& m;
};
} // namespace pipepp::gui
