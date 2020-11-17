#include "nana/gui.hpp"
#include "nana/gui/detail/general_events.hpp"
#include "nana/gui/timer.hpp"
#include "pipepp/gui/pipeline_board.hpp"
#include "sample_pipeline.hpp"

int main(void)
{
    nana::form fm;
    fm.div("<main>");

    pipepp::gui::pipeline_board board(fm, {}, true);
    fm["main"] << board;

    auto pipe = build_pipeline();
    pipe->launch();
    board.reset_pipeline(pipe);

    fm.collocate();
    fm.show();

    fm.events().key_press([&](nana::arg_keyboard const& key) {
        if (key.key == L' ') {
            pipe->suply(1, [](auto) {});
        }
    });

    nana::timer tm;
    using namespace std::literals;
    tm.interval(66ms);
    tm.elapse([&]() {
        board.update();
    });
    tm.start();

    nana::exec();

    return 0;
}
