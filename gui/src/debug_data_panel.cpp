#include "pipepp/gui/debug_data_panel.hpp"

struct pipepp::gui::debug_data_panel::implementation {
    debug_data_panel& self;
    pipeline_board* board_ref;
};

pipepp::gui::debug_data_panel::debug_data_panel(nana::window wd, bool visible, pipeline_board* board_ref)
    : panel<false>(wd, visible)
    , impl_(std::make_unique<implementation>(*this))
    , m(*impl_)
{
    impl_->board_ref = board_ref;
}

pipepp::gui::debug_data_panel::~debug_data_panel() = default;

void pipepp::gui::debug_data_panel::reset_pipe(std::weak_ptr<detail::pipeline_base> pl, pipe_id_t id)
{
}

void pipepp::gui::debug_data_panel::update(std::shared_ptr<execution_context_data>)
{
}
