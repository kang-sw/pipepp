#include "pipepp/gui/pipe_view.hpp"

pipepp::gui::pipe_view::pipe_view()
{
}

pipepp::gui::pipe_view::pipe_view(const nana::window& wd, bool visible)
    : panel<true>(wd, visible)
{
}

pipepp::gui::pipe_view::pipe_view(const nana::window& wd, const nana::rectangle& r, bool visible)
    : panel<true>(wd, r, visible)
{
}
