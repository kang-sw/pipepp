#include <chrono>
#include "nana/gui/drawing.hpp"
#include "nana/gui/place.hpp"
#include "nana/gui/widgets/button.hpp"
#include "nana/gui/widgets/label.hpp"
#include "nana/paint/graphics.hpp"
#include "pipepp/gui/pipe_detail_panel.hpp"
#include "pipepp/gui/pipe_view.hpp"

#include "kangsw/zip.hxx"
#include "nana/gui/programming_interface.hpp"
#include "nana/gui/widgets/group.hpp"
#include "nana/gui/widgets/textbox.hpp"
#include "pipepp/pipeline.hpp"

struct pipepp::gui::pipe_view::data_type {
    pipe_view& self;

    std::weak_ptr<impl__::pipeline_base> pipeline;
    pipe_id_t pipe;

    std::shared_ptr<execution_context_data> exec_data;
    std::shared_ptr<pipe_detail_panel> detail_view;

    nana::place layout{self};
    nana::button button{self};
    nana::panel<true> label{self};

    std::string label_text_interval;
    std::string label_text_exec;
    double label_dur_interval = 0;
    double label_dur_exec = 0;

    nana::panel<true> executor_notes{self};
};

pipepp::gui::pipe_view::pipe_view(const nana::window& wd, const nana::rectangle& r, bool visible)
    : super(wd, r, visible)
    , impl_(std::make_unique<data_type>(*this))
{
    auto& m = *impl_;

    m.layout.div("vert<MAIN weight=20><INTERVAL weight=40><EXEC_COND>");
    m.layout["MAIN"] << m.button;
    m.layout["INTERVAL"] << m.label;
    m.layout["EXEC_COND"] << m.executor_notes;

    //  m.text.text_align(nana::align::center, nana::align_v::center);
    // m.button.typeface(nana::paint::font("consolas", 11.0));
    // m.label.typeface(nana::paint::font("consolas", 11.0));

    nana::drawing(m.label).draw_diehard([&](nana::paint::graphics& gp) {
        gp.round_rectangle(
          nana::rectangle{{}, gp.size()}, 3, 3,
          nana::colors::black, true, nana::color{}.from_rgb(35, 35, 35));
        auto extent = gp.text_extent_size(m.label_text_interval);
        nana::point str_draw_pos = {};
        str_draw_pos.y = m.label.size().height / 2 - extent.height - 2;
        str_draw_pos.x = m.label.size().width - 5 - extent.width;
        gp.string(str_draw_pos, m.label_text_interval, nana::colors::white);
        extent = gp.text_extent_size(m.label_text_exec);
        str_draw_pos.y = m.label.size().height / 2 + 2;
        str_draw_pos.x = m.label.size().width - 5 - extent.width;
        gp.string(str_draw_pos, m.label_text_exec, nana::colors::light_sky_blue);
    });

    m.button.events().click([&](auto) {
        if (details() == nullptr) {
            open_details(*this);
        }
        else {
            close_details();
        }
    });

    nana::drawing(m.executor_notes).draw_diehard([&](nana::paint::graphics& gp) {
        gp.round_rectangle(
          nana::rectangle{{}, gp.size()}, 3, 3,
          nana::colors::black, true, nana::colors::white_smoke);
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
        // auto time = m.exec_data->timers[0].elapsed;

        auto lst_tm = {proxy.output_interval(), m.exec_data->timers[0].elapsed};
        auto lst_tget = {&m.label_text_interval, &m.label_text_exec};
        auto lst_lerp = {&m.label_dur_interval, &m.label_dur_exec};
        for (
          auto [time, tget, base] :
          kangsw::zip(lst_tm, lst_tget, lst_lerp)) {
            auto dur = std::chrono::duration<double>(time).count() * 1000.0;
            *base = std::lerp(*base, dur, 0.33);
            *tget = kangsw::format("%10.4f ms ", *base);
        }
        nana::drawing(m.label).update();

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
          = std::make_shared<pipe_detail_panel>(wd, parent_rect, appear);

        p->reset_pipe(m.pipeline, m.pipe);
        if (m.exec_data) { p->update(m.exec_data); }

        p->events().destroy([&](auto) {
            m.button.bgcolor(nana::colors::antique_white);
        });
        m.button.bgcolor(nana::colors::light_green);
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
    auto& m = *impl_;
    m.label.bgcolor(c);
    m.executor_notes.bgcolor(c);
    super::_m_bgcolor(c);
}

void pipepp::gui::pipe_view::_m_typeface(const nana::paint::font& font)
{
    auto& m = *impl_;
    m.button.typeface(font);
    m.label.typeface(font);
    m.executor_notes.typeface(font);
    super::_m_typeface(font);
}
