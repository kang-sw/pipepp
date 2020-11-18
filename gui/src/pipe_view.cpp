#include <chrono>
#include "nana/gui/drawing.hpp"
#include "nana/gui/place.hpp"
#include "nana/gui/widgets/button.hpp"
#include "nana/gui/widgets/label.hpp"
#include "nana/paint/graphics.hpp"
#include "pipepp/gui/pipe_detail_panel.hpp"
#include "pipepp/gui/pipe_view.hpp"

#include "nana/gui/programming_interface.hpp"
#include "pipepp/pipeline.hpp"

struct pipepp::gui::pipe_view::data_type {
    pipe_view& self;

    std::weak_ptr<impl__::pipeline_base> pipeline;
    pipe_id_t pipe;

    std::shared_ptr<execution_context_data> exec_data;
    std::shared_ptr<pipe_detail_panel> detail_view;

    nana::place layout{self};
    nana::button button{self};
    nana::panel<true> text{self};
};

pipepp::gui::pipe_view::pipe_view(const nana::window& wd, const nana::rectangle& r, bool visible)
    : super(wd, r, visible)
    , impl_(std::make_unique<data_type>(*this))
{
    auto& m = *impl_;

    m.layout.div("vert<MAIN weight=40%><TEXT>");
    m.layout["MAIN"] << m.button;
    m.layout["TEXT"] << m.text;
    //  m.text.text_align(nana::align::center, nana::align_v::center);

    nana::drawing(m.text).draw_diehard([&](nana::paint::graphics& gp) {
        gp.round_rectangle(
          nana::rectangle{{}, m.text.size()}, 5, 5,
          nana::colors::black, true, nana::colors::light_grey);
        auto extent = gp.text_extent_size(m.text.caption());
        nana::point str_draw_pos = {};
        str_draw_pos.y = m.text.size().height / 2 - extent.height / 2;
        str_draw_pos.x = m.text.size().width - 5 - extent.width;
        gp.string(str_draw_pos, m.text.caption());
    });

    m.button.events().click([&](auto) {
        if (details() == nullptr) {
            open_details(*this);
        }
        else {
            close_details();
        }
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
    m.button.caption(proxy.name());
    m.button.bgcolor(nana::colors::antique_white);
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

        if (auto detail_view = details()) {
            detail_view->update(m.exec_data);
        }
    }
}

std::shared_ptr<pipepp::gui::pipe_detail_panel> pipepp::gui::pipe_view::details() const
{
    return !impl_->detail_view || impl_->detail_view->empty() ? nullptr : impl_->detail_view;
}

void pipepp::gui::pipe_view::open_details(const nana::window& wd)
{
    auto& m = *impl_;
    if (m.detail_view == nullptr || m.detail_view->empty()) {
        nana::rectangle parent_rect;
        nana::API::get_window_rectangle(this->parent(), parent_rect);
        parent_rect.width = 480;
        parent_rect.height = 640;
        nana::appearance appear;
        appear.floating = false;
        appear.decoration = true;
        appear.maximize = false;
        appear.minimize = false;
        appear.sizable = false;
        appear.taskbar = true;

        auto p = m.detail_view
          = std::make_shared<pipe_detail_panel>(this->parent(), parent_rect, appear);

        p->reset_pipe(m.pipeline, m.pipe);
        if (m.exec_data) { p->update(m.exec_data); }

        p->show();
    }
}

void pipepp::gui::pipe_view::close_details()
{
    auto& m = *impl_;
    if (m.detail_view) {
        m.detail_view->close();
        m.detail_view.reset();
    }
}

void pipepp::gui::pipe_view::_m_caption(native_string_type&& f)
{
    impl_->button.caption(f);
    super::_m_caption(std::move(f));
}

void pipepp::gui::pipe_view::_m_bgcolor(const nana::color& c)
{
    impl_->button.bgcolor(c);
    super::_m_bgcolor(c);
}

void pipepp::gui::pipe_view::_m_typeface(const nana::paint::font& font)
{
    impl_->button.typeface(font);
    super::_m_typeface(font);
}
