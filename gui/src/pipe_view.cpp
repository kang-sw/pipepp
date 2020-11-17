#include "pipepp/gui/pipe_view.hpp"
#include <chrono>

#include "nana/gui/drawing.hpp"
#include "nana/gui/place.hpp"
#include "nana/gui/widgets/button.hpp"
#include "nana/gui/widgets/label.hpp"
#include "nana/paint/graphics.hpp"
#include "pipepp/pipeline.hpp"

struct pipepp::gui::pipe_view::data_type {
    pipe_view& self;

    std::weak_ptr<impl__::pipeline_base> pipeline;
    pipe_id_t pipe;

    std::shared_ptr<execution_context_data> exec_data;

    nana::place layout{self};
    nana::button body{self};
    nana::label text{self};
};

pipepp::gui::pipe_view::pipe_view(const nana::window& wd, const nana::rectangle& r, bool visible)
    : super(wd, r, visible)
    , impl_(std::make_unique<data_type>(*this))
{
    auto& m = *impl_;

    m.layout.div("vert<MAIN weight=40%><TEXT>");
    m.layout["MAIN"] << m.body;
    m.layout["TEXT"] << m.text;
    m.text.text_align(nana::align::center, nana::align_v::center);

    nana::drawing(m.text).draw_diehard([&](nana::paint::graphics& gp) {
        gp.round_rectangle(
          nana::rectangle{{}, m.text.size()}, 5, 5,
          nana::colors::black, false, nana::colors::light_grey);
    });
}

pipepp::gui::pipe_view::~pipe_view() = default;

void pipepp::gui::pipe_view::reset_view(std::weak_ptr<impl__::pipeline_base> pipeline, pipe_id_t pipe)
{
    auto& m = *impl_;
    m.pipeline = pipeline;
    m.pipe = pipe;

    auto pl = m.pipeline.lock();
    if (pl == nullptr) { return; }

    auto proxy = pl->get_pipe(m.pipe);
    m.body.caption(proxy.name());
    m.body.bgcolor(nana::colors::antique_white);
}

void pipepp::gui::pipe_view::update()
{
    auto& m = *impl_;
    auto pl = m.pipeline.lock();
    if (pl == nullptr) { return; }

    auto proxy = pl->get_pipe(m.pipe);
    if (proxy.execution_result_available()) {
        m.exec_data = proxy.consume_execution_result();

        auto sec = kangsw::format(
          "%10.4f ms ",
          std::chrono::duration<double>(m.exec_data->timers[0].elapsed).count() * 1000.0);
        m.text.caption(sec);
    }
}

void pipepp::gui::pipe_view::_m_caption(native_string_type&& f)
{
    impl_->body.caption(f);
    super::_m_caption(std::move(f));
}

void pipepp::gui::pipe_view::_m_bgcolor(const nana::color& c)
{
    impl_->body.bgcolor(c);
    super::_m_bgcolor(c);
}

void pipepp::gui::pipe_view::_m_typeface(const nana::paint::font& font)
{
    impl_->body.typeface(font);
    super::_m_typeface(font);
}
