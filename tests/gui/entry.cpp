#include "nana/gui.hpp"
#include "nana/gui/detail/general_events.hpp"
#include "nana/gui/timer.hpp"
#include "nana/key_type.hpp"
#include "pipepp/gui/pipeline_board.hpp"
#include "sample_pipeline.hpp"

int main(void)
{
    nana::form fm;
    fm.div("<main>");

    pipepp::gui::pipeline_board board(fm, {}, true);
    fm["main"] << board;
    // board.debug_data_subscriber = [](std::string_view a, auto& b) { printf("hell, world! %s", a.data()); return true ; };

    auto pipe
      = build_pipeline();
    pipe->launch();
    board.reset_pipeline(pipe);

    fm.collocate();
    fm.show();

    fm.events().key_press([&](nana::arg_keyboard const& key) {
    });

    nana::timer tm;
    using namespace std::literals;
    tm.interval(66ms);
    tm.elapse([&]() {
        board.update();
        if (pipe->can_suply()) {
            pipe->suply(1, [](auto) {});
        }
    });
    tm.start();

    nana::exec();

    return 0;
}
