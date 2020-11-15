#include "pipepp/gui/pipeline_board.hpp"
#include "pipepp/gui/basic_utility.hpp"
#include "pipepp/gui/pipe_view.hpp"

struct pipepp::gui::pipeline_board_data {
    pipeline_board& self;
    std::weak_ptr<impl__::pipeline_base> pipe;

    double zoom;
    nana::point center;
};

pipepp::gui::pipeline_board::pipeline_board()
    : pipeline_board({}, {}, false)
{
}

pipepp::gui::pipeline_board::pipeline_board(const nana::window& wd, bool visible)
    : pipeline_board(wd, {}, visible)
{
}

pipepp::gui::pipeline_board::pipeline_board(const nana::window& wd, const nana::rectangle& r, bool visible)
    : panel<true>(wd, r, visible)
    , impl_(std::make_unique<pipeline_board_data>(*this))
{
}

void pipepp::gui::pipeline_board::build_menu(nana::menu&) const
{
    // TODO
}

void pipepp::gui::pipeline_board::reset_pipeline(std::weak_ptr<impl__::pipeline_base> pipeline)
{
    // TODO
    // 0. 이미 존재하는 pipe_view 개체를 모두 소거합니다.
    // 1. pipeline의
}
