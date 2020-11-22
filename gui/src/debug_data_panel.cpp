#include "pipepp/gui/debug_data_panel.hpp"
#include "nana/gui/place.hpp"
#include "nana/gui/widgets/treebox.hpp"
#include "pipepp/pipeline.hpp"

using namespace nana;

struct pipepp::gui::debug_data_panel::data_type {
    explicit data_type(debug_data_panel* p)
        : self(*p)
    {}

public:
    debug_data_panel& self;
    pipeline_board* board_ref = {};

    std::weak_ptr<detail::pipeline_base> pipeline;
    pipe_id_t pipe = pipe_id_t::none;

    /********* widgets *********/
    place layout{self};
    treebox list{self};

    /********* data    *********/
};

pipepp::gui::debug_data_panel::debug_data_panel(window wd, bool visible, pipeline_board* board_ref)
    : super(wd, visible)
    , impl_(std::make_unique<data_type>(this))
    , m(*impl_)
{
    m.board_ref = board_ref;
    m.layout.div("<MAIN>");
    m.layout["MAIN"] << m.list;
}

pipepp::gui::debug_data_panel::~debug_data_panel() = default;

void pipepp::gui::debug_data_panel::reset_pipe(std::weak_ptr<detail::pipeline_base> pl, pipe_id_t id)
{
}

void pipepp::gui::debug_data_panel::update(std::shared_ptr<execution_context_data>)
{
}
