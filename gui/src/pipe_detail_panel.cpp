#include "pipepp/gui/pipe_detail_panel.hpp"

#include "pipepp/pipeline.hpp"

struct pipepp::gui::pipe_detail_panel::data_type {
    pipe_detail_panel& self;

    std::weak_ptr<impl__::pipeline_base> pipeline;
    pipe_id_t pipe;
};

pipepp::gui::pipe_detail_panel::pipe_detail_panel(nana::window owner, const nana::rectangle& rectangle, const nana::appearance& appearance)
    : form(owner, rectangle, appearance)
    , impl_(std::make_unique<data_type>(*this))
{
}

pipepp::gui::pipe_detail_panel::~pipe_detail_panel() = default;

void pipepp::gui::pipe_detail_panel::reset_pipe(std::weak_ptr<impl__::pipeline_base> pl, pipe_id_t id)
{
    auto& m = *impl_;
    m.pipeline = pl;
    m.pipe = id;

    if (auto pipeline = pl.lock()) {
        auto proxy = pipeline->get_pipe(id);

        caption(proxy.name());
    }
}

void pipepp::gui::pipe_detail_panel::update(std::shared_ptr<execution_context_data>)
{
}
