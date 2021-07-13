#include <chrono>
#include <format>
#include "kangsw/helpers/misc.hxx"
#include "kangsw/helpers/zip.hxx"
#include "nana/basic_types.hpp"
#include "nana/gui/detail/general_events.hpp"
#include "nana/gui/drawing.hpp"
#include "nana/gui/place.hpp"
#include "nana/gui/programming_interface.hpp"
#include "nana/gui/widgets/button.hpp"
#include "nana/paint/graphics.hpp"
#include "pipepp/gui/pipe_detail_panel.hpp"
#include "pipepp/gui/pipe_view.hpp"

#include "kangsw/helpers/optional.hxx"
#include "nana/gui/widgets/form.hpp"
#include "nana/gui/widgets/label.hpp"
#include "nana/gui/widgets/menu.hpp"
#include "pipepp/gui/pipeline_board.hpp"
#include "pipepp/pipeline.hpp"

namespace nana {
class form;
}

using clock_type = std::chrono::system_clock;

struct pipepp::gui::pipe_view::data_type {
    pipe_view& self;
    pipeline_board* board_ref;

    std::weak_ptr<detail::pipeline_base> pipeline;
    pipe_id_t pipe;

    std::shared_ptr<execution_context_data> exec_data;
    std::shared_ptr<pipe_detail_panel> detail_view;
    std::unique_ptr<nana::form> detail_view_form;

    kangsw::optional<nana::menu> rclick_menu;

    nana::place layout{self};
    nana::panel<true> title{self};
    nana::panel<true> label{self};
    nana::button button{self};

    std::string label_text_interval = " ms";
    std::string label_text_exec = " ms";
    std::string label_text_latency = " ms";
    double label_dur_interval = 0;
    double label_dur_exec = 0;
    double label_dur_latency = 0;

    nana::panel<true> executor_notes{self};
    struct
    {
        std::vector<executor_condition_t> front = {};
        std::vector<executor_condition_t> back = {};
        kangsw::spinlock lock;
    } exec_conditions = {};

    clock_type::time_point latest_exec_receive;
    bool upper_node_paused = false;

    nana::color title_layer_color;
};

void pipepp::gui::pipe_view::_label_events()
{
    auto& m = *impl_;

    nana::drawing(m.label).draw_diehard([&](nana::paint::graphics& gp) {
        auto proxy = impl_->pipeline.lock()->get_pipe(impl_->pipe);
        auto tweak = proxy.tweaks();
        auto backcolor = proxy.is_paused()
                           ? nana::colors::dark_red
                         : m.upper_node_paused
                           ? nana::colors::yellow
                         : tweak.selective_input
                           ? nana::color(43, 100, 64)
                           : nana::color(64, 33, 64);

        auto bottomcolor = tweak.selective_output
                             ? nana::color(35, 48, 121)
                             : nana::color(35, 35, 35);

        gp.gradual_rectangle(
          nana::rectangle{{1, 1}, gp.size() + nana::size(-1, -1)}, backcolor, bottomcolor, true);
        gp.round_rectangle(
          nana::rectangle{{}, gp.size()}, 3, 3,
          nana::colors::black, false, backcolor);

        auto put_text = [&](int slot, nana::color color, std::string& text) {
            auto extent = gp.text_extent_size(text);
            nana::point str_draw_pos = {};
            str_draw_pos.y = slot * (extent.height + 2) + 3;
            str_draw_pos.x = m.label.size().width * 94 / 100 - extent.width;
            gp.string(str_draw_pos, text, color);
        };

        put_text(0, nana::colors::yellow, m.label_text_latency);
        put_text(1, nana::colors::light_sky_blue, m.label_text_exec);
        put_text(2, nana::colors::light_gray, m.label_text_interval);
    });

    m.button.events().mouse_down([&](nana::arg_mouse const& arg) {
        if (arg.right_button) {
            auto proxy = impl_->pipeline.lock()->get_pipe(impl_->pipe);

            if (!m.rclick_menu.empty()) { m.rclick_menu.reset(); }
            auto& mn = m.rclick_menu.emplace();
            std::string state_text = std::format("{0}{1}{2}{3}/",
                                                 proxy.name(),
                                                 proxy.is_optional() ? "/Optional" : "",
                                                 proxy.tweaks().selective_input ? "/Sel In" : "",
                                                 proxy.tweaks().selective_input ? "/Sel Out" : "");
            mn.append(state_text).enabled(false);
            mn.append(
              proxy.is_paused() ? "&Resume" : "Pause (&R)",
              [this, paused = proxy.is_paused(), proxy](auto) mutable {
                  paused ? proxy.unpause() : proxy.pause();
                  _refresh_btn_color(!!details());
                  nana::drawing(impl_->label).update();

                  auto& dirty = impl_->board_ref->option_changed;
                  if (dirty) { dirty(impl_->pipe, {}); }
              });

            if (details()) {
                mn.append_splitter();
                mn.append("&Close Detail View", [&](auto) { close_details(); });
            }

            mn.popup(*this, arg.pos.x, arg.pos.y);
        }
    });

    m.label.tooltip("Right click to suspend/resume.\n\n"
                    "Respectively from above: \n"
                    "- Output Latency from First Input\n"
                    "- Execution Time\n"
                    "- Output Interval\n");
}

pipepp::gui::pipe_view::pipe_view(const nana::window& wd, const nana::rectangle& r, bool visible)
    : super(wd, r, visible)
    , impl_(std::make_unique<data_type>(*this))
{
    auto& m = *impl_;
    
    nana::window hnd_pipe_board = wd;
    pipeline_board* pipe_board = nullptr;
    do {
        pipe_board = dynamic_cast<pipeline_board*>(nana::API::get_widget(hnd_pipe_board));
        hnd_pipe_board = nana::API::get_parent_window(hnd_pipe_board);
    } while (hnd_pipe_board != nullptr && pipe_board == nullptr);

    assert(!!pipe_board);
    m.board_ref = pipe_board;

    m.layout.div("vert<MAIN weight=23><INTERVAL weight=60 margin=[0,1,0,1]><EXEC_COND margin=[0,1,0,1]>");
    m.layout["MAIN"] << m.title;
    m.layout["INTERVAL"] << m.label;
    m.layout["EXEC_COND"] << m.executor_notes;

    m.button.transparent(true);
    //  m.text.text_align(nana::align::center, nana::align_v::center);
    // m.button.typeface(nana::paint::font("consolas", 11.0));
    // m.label.typeface(nana::paint::font("consolas", 11.0));
    _label_events();

    // m.button.events().click([&](nana::arg_click const& arg) {
    //     details() == nullptr ? open_details(*this) : details()->focus();
    //     _refresh_btn_color(!!details());
    // });

    nana::drawing(m.executor_notes).draw_diehard([&](nana::paint::graphics& gp) {
        auto bgcol = nana::color(34, 22, 34);
        gp.gradual_rectangle(
          //nana::rectangle{{1, 1}, gp.size() + nana::size(-1, -1)},
          nana::rectangle{{}, gp.size()},
          bgcol, nana::color(87, 76, 86), true);

        int top = 0, bottom = gp.size().height - 0;
        int left = 0, right = gp.size().width - 0;

        if (!(right < left || bottom < top)) {
            std::lock_guard _lck{m.exec_conditions.lock};
            auto& cond = m.exec_conditions.front;

            auto divider = std::max<int>(1, cond.size());
            auto step_w = (right - left + divider - 1) / divider;
            for (auto i : kangsw::iota(cond.size())) {
                nana::rectangle rect;
                rect.y = top;
                rect.height = bottom - top;
                rect.width = step_w - 1;
                rect.x = left + step_w * i;
                if (rect.x + rect.width > right) {
                    rect.width -= (rect.x + rect.width) - right;
                }

                nana::color color;
                switch (cond[i]) {
                    case executor_condition_t::idle: color = nana::color(45, 45, 45); break;
                    case executor_condition_t::busy: color = nana::colors::yellow; break;
                    case executor_condition_t::busy_output: color = nana::colors::orange; break;
                    case executor_condition_t::idle_aborted: color = nana::colors::red; break;
                    case executor_condition_t::idle_output: [[fallthrough]];
                    default: color = nana::colors::lawn_green;
                }

                // gp.rectangle(r, true, color);
                gp.gradual_rectangle(rect, bgcol, color, true);
                // gp.rectangle(r, false, nana::colors::dim_gray);
            }
        }

        gp.round_rectangle(
          nana::rectangle{{}, gp.size()}, 2, 2,
          nana::colors::black, false, nana::colors::dim_gray);
    });

    m.title.fgcolor(nana::colors::alice_blue);
    nana::drawing(m.title).draw_diehard([&](nana::paint::graphics& gp) {
        auto my_rect = nana::rectangle({}, m.title.size());
        auto bg = m.title.bgcolor();
        auto fg = m.title.fgcolor();

        using namespace nana;
        gp.gradual_rectangle(my_rect, m.title_layer_color, bg, true);
        gp.round_rectangle(my_rect, 3, 3, nana::colors::black, false, {});

        auto str = m.title.caption();
        auto extent = gp.text_extent_size(str);
        gp.string(nana::point((m.title.size().width - extent.width) / 2, (m.title.size().height - extent.height) / 2), str, fg);
    });

    events().destroy([&](nana::arg_destroy const& a) {
        close_details();
    });

    events().resized([&](auto&&) {
        m.button.size(size());
    });
}

pipepp::gui::pipe_view::~pipe_view() = default;

void pipepp::gui::pipe_view::reset_view(std::weak_ptr<detail::pipeline_base> pipeline, pipe_id_t pipe)
{
    auto& m = *impl_;
    m.pipeline = pipeline;
    m.pipe = pipe;

    auto pl = m.pipeline.lock();
    if (pl == nullptr) { return; }

    auto proxy = pl->get_pipe(m.pipe);
    m.title.caption(proxy.name());
    _refresh_btn_color(false);
}

void pipepp::gui::pipe_view::update()
{
    using namespace std::chrono_literals;

    auto& m = *impl_;
    auto pl = m.pipeline.lock();
    if (pl == nullptr) { return; }

    auto proxy = pl->get_pipe(m.pipe);
    if (proxy.execution_result_available()) {
        m.exec_data = proxy.consume_execution_result();
        // auto time = m.exec_data->timers[0].elapsed;

        auto lst_tm = {proxy.output_interval(), m.exec_data->timers[1].elapsed, proxy.output_latency()};
        auto lst_tget = {&m.label_text_interval, &m.label_text_exec, &m.label_text_latency};
        auto lst_lerp = {&m.label_dur_interval, &m.label_dur_exec, &m.label_dur_latency};
        for (
          auto [time, tget, base] :
          kangsw::zip(lst_tm, lst_tget, lst_lerp)) {
            auto dur = std::chrono::duration<double>(time).count() * 1000.0;
            *base = std::lerp(*base, dur, 0.33);
            *tget = std::format("{0:>10.4f} ms", *base);
        }
        nana::drawing(m.label).update();

        if (auto detail_view = details()) {
            detail_view->update(m.exec_data);
        }
        m.latest_exec_receive = clock_type::now();
        m.upper_node_paused = false;
        _refresh_btn_color(!!details());
    } else if (clock_type::now() - m.latest_exec_receive > 500ms) {
        m.latest_exec_receive = clock_type::now();
        auto has_any_paused_input = [&](auto recurse, detail::pipe_proxy_base const& prx) -> bool {
            if (prx.is_paused()) { return true; }
            for (auto index : kangsw::iota(prx.num_input_nodes())) {
                if (recurse(recurse, prx.get_input_node(index))) {
                    return true;
                }
            }
            return false;
        };

        m.upper_node_paused = has_any_paused_input(has_any_paused_input, proxy) || proxy.recently_aborted();
        nana::drawing(m.label).update();
        _refresh_btn_color(!!details());
    }

    {
        auto& conds = m.exec_conditions;
        proxy.executor_conditions(conds.back);
        std::lock_guard{m.exec_conditions.lock}, std::swap(conds.front, conds.back);
        if (conds.front != conds.back) {
            nana::drawing(m.executor_notes).update();
        }
    }
}

std::shared_ptr<pipepp::gui::pipe_detail_panel> pipepp::gui::pipe_view::details() const
{
    return !impl_->detail_view || impl_->detail_view->empty() ? nullptr : impl_->detail_view;
}

void pipepp::gui::pipe_view::_refresh_btn_color(bool detail_open)
{
    auto& m = *impl_;
    auto proxy = m.pipeline.lock()->get_pipe(m.pipe);

    m.title_layer_color = detail_open ? nana::color(141, 131, 123) : nana::color(84, 53, 84);
    m.title.bgcolor(m.upper_node_paused ? nana::colors::dark_red : nana::color(43, 121, 61));

    nana::API::refresh_window(m.title);
}

void pipepp::gui::pipe_view::open_details(const nana::window& wd)
{
    auto& m = *impl_;
    if (m.detail_view == nullptr || m.detail_view->empty()) {
        nana::rectangle parent_rect;
        // parent_rect.position(nana::API::cursor_position());
        parent_rect.width = 640;
        parent_rect.height = 480;
        // parent_rect.x -= parent_rect.width;
        // parent_rect.y -= 5;

        nana::appearance appear;
        appear.floating = false;
        appear.decoration = true;
        appear.maximize = false;
        appear.minimize = true;
        appear.sizable = true;
        appear.taskbar = false;

        auto& f = m.detail_view_form = std::make_unique<nana::form>(wd, parent_rect, appear);
        auto p = m.detail_view = std::make_shared<pipe_detail_panel>(*f);

        f->icon({});
        f->caption(m.title.caption());
        // f->move(parent_rect.position());

        p->reset_pipe(m.pipeline, m.pipe);
        if (m.exec_data) { p->update(m.exec_data); }

        f->events().unload([&](auto) {
            _refresh_btn_color(false);
        });
        _refresh_btn_color(true);
        f->show();

        f->div("MAIN");
        f->get_place()["MAIN"] << *p;
        p->collocate();
        f->collocate();
    }
}

std::weak_ptr<pipepp::gui::pipe_detail_panel> pipepp::gui::pipe_view::create_panel(nana::window const& wd)
{
    auto& m = *impl_;
    if (m.detail_view == nullptr || m.detail_view->empty()) {
        auto& p = m.detail_view = std::make_shared<pipe_detail_panel>(wd);
        p->reset_pipe(m.pipeline, m.pipe);
        if (m.exec_data) { p->update(m.exec_data); }

        _refresh_btn_color(true);
        return p;
    }

    _refresh_btn_color(false);
    return {};
}

nana::button::event_type& pipepp::gui::pipe_view::btn_events()
{
    return impl_->button.events();
}

void pipepp::gui::pipe_view::close_details()
{
    auto& m = *impl_;
    if (m.detail_view_form) {
        m.detail_view_form.reset();
    }

    if (m.detail_view) {
        m.detail_view->close();
        m.detail_view.reset();
    }

    _refresh_btn_color(false);
}

void pipepp::gui::pipe_view::_m_caption(native_string_type&& f)
{
    impl_->title.caption(f);
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
    m.title.typeface(font);
    m.label.typeface(font);
    m.executor_notes.typeface(font);
    super::_m_typeface(font);
}
