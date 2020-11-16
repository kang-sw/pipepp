#include "nana/gui.hpp"
#include "pipepp/gui/pipeline_board.hpp"
#include "sample_pipeline.hpp"

int main(void)
{
    nana::form fm;
    fm.div("<main>");

    pipepp::gui::pipeline_board board(fm, {}, true);
    fm["main"] << board;

    auto pipe = build_pipeline();
    board.reset_pipeline(pipe);

    fm.collocate();
    fm.show();
    nana::exec();

    return 0;
}