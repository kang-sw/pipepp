#include "pipepp/gui/pipe_detail_panel.hpp"
#include <any>
#include <chrono>
#include <functional>

#include "fmt/format.h"
#include "nana/basic_types.hpp"
#include "nana/gui/drawing.hpp"
#include "nana/gui/programming_interface.hpp"
#include "nana/gui/widgets/listbox.hpp"
#include "nana/gui/widgets/textbox.hpp"
#include "nana/gui/widgets/treebox.hpp"
#include "nana/paint/graphics.hpp"
#include "pipepp/execution_context.hpp"
#include "pipepp/gui/basic_utility.hpp"
#include "pipepp/gui/debug_data_panel.hpp"
#include "pipepp/gui/option_panel.hpp"
#include "pipepp/gui/pipeline_board.hpp"
#include "pipepp/pipeline.hpp"

#define COLUMN_CATEGORY 0
#define COLUMN_NAME 1
#define COLUMN_VALUE 2

using clock_type = std::chrono::system_clock;

struct pipepp::gui::pipe_detail_panel::data_type {
    pipe_detail_panel& self;
    pipeline_board* board_ref;

    std::weak_ptr<detail::pipeline_base> pipeline;
    pipe_id_t pipe;

    nana::textbox timers{self};
    option_panel option{self, true};
    debug_data_panel debug_data{self, true};
    nana::listbox values{self};
};

pipepp::gui::pipe_detail_panel::pipe_detail_panel(nana::window owner, const nana::rectangle& rectangle, const nana::appearance& appearance)
    : form(rectangle, appearance)
    , impl_(std::make_unique<data_type>(*this))
{
    auto& m = *impl_;

    auto hnd_pipe_board = nana::API::get_parent_window(owner);
    auto pipe_board = dynamic_cast<pipeline_board*>(nana::API::get_widget(hnd_pipe_board));
    m.board_ref = pipe_board;
    m.debug_data._set_board_ref(m.board_ref);

    div(""
        "margin=2"
        "<vert"
        // "   <w_timers weight=200 margin=[0,0,2,0]>"
        "   <w_vals margin=[0,0,2,0]>"
        "   <w_opts>"
        ">");

    (*this)["w_timers"] << m.timers;
    (*this)["w_opts"] << m.option;
    (*this)["w_vals"] << m.debug_data;
    collocate();

    m.timers.bgcolor(nana::colors::black);
    m.timers.fgcolor(nana::color{}.from_rgb(0, 255, 0));
    m.timers.typeface(DEFAULT_DATA_FONT);
    m.timers.text_align(nana::align::left);
    m.timers.editable(false);
    nana::drawing(m.timers).draw_diehard([&](nana::paint::graphics& gp) {
        auto size = gp.text_extent_size("0");
        auto n_chars = gp.width() / size.width;
        num_timer_text_view_horizontal_chars = n_chars;
    });

    auto header_div = m.values.size().width * 48 / 100;
    m.values.checkable(true);
    m.values.append_header("Name", header_div);
    m.values.append_header("Value", header_div);
    m.values.events().checked([&](nana::arg_listbox const& arg) {
        if (arg.item.checked() == false) {
            auto& unchecked_notify = m.board_ref->debug_data_unchecked;
            if (unchecked_notify) {
                auto proxy = m.pipeline.lock()->get_pipe(m.pipe);
                unchecked_notify(proxy.name(), arg.item.value<execution_context_data::debug_data_entity>());
            }
        } else {
            auto& notify = m.board_ref->debug_data_subscriber;
            if (notify) {
                auto proxy = m.pipeline.lock()->get_pipe(m.pipe);
                notify(proxy.name(), arg.item.value<execution_context_data::debug_data_entity>());
            }
        }
    });
    events().unload([&](auto&) {
        for (auto item : m.values.at(0)) {
            item.check(false);
        }
    });

    m.option.on_dirty = [&]<typename Ty_>(Ty_&& key) {
        if (m.board_ref->option_changed) {
            m.board_ref->option_changed(m.pipe, std::forward<Ty_>(key));
        }
        m.pipeline.lock()->get_pipe(m.pipe).mark_option_dirty();
    };

    events().resized([&](auto&&) {
        if (size().height > size().width) {
            div(""
                "margin=2"
                "<vert"
                // "   <w_timers weight=200 margin=[0,0,2,0]>"
                "   <w_vals margin=[0,0,2,0]>"
                "   <w_opts>"
                ">");
        } else {
            div(""
                "margin=2"
                "<"
                // "   <w_timers weight=200 margin=[0,0,2,0]>"
                "   <w_vals margin=[0,0,2,0]>"
                "   <w_opts>"
                ">");
        }
        collocate();
    });
}

pipepp::gui::pipe_detail_panel::~pipe_detail_panel() = default;

void pipepp::gui::pipe_detail_panel::reset_pipe(std::weak_ptr<detail::pipeline_base> pl, pipe_id_t id)
{
    auto& m = *impl_;
    m.debug_data._reset_pipe(pl, id);

    m.pipeline = pl;
    m.pipe = id;

    if (auto pipeline = pl.lock()) {
        auto proxy = pipeline->get_pipe(id);
        auto& opt = proxy.options();
        caption(proxy.name());

        m.option.reload(pl, &opt);
        // _reload_options(opt);
    }
}

nana::listbox::oresolver& operator<<(
  nana::listbox::oresolver& o,
  const pipepp::execution_context_data::debug_data_entity& i)
{
    o << std::string(i.name);

    std::visit(
      [&]<typename T0>(T0&& arg) {
          using type = std::decay_t<T0>;

          if constexpr (std::is_same_v<std::any, type>)
              o << arg.type().name();
          else if constexpr (std::is_same_v<std::string, type>)
              o << arg;
          else if constexpr (std::is_same_v<bool, type>)
              o << (arg ? "true" : "false");
          else
              o << std::to_string(arg);
      },
      i.data);
    return o;
}

void pipepp::gui::pipe_detail_panel::update(std::shared_ptr<execution_context_data> data)
{
    auto& m = *impl_;
    m.debug_data._update(std::move(data));
    return;
    auto proxy = m.pipeline.lock()->get_pipe(m.pipe);

    // -- Ÿ�̸� ���ڿ� ����
    {
        auto pos = m.timers.text_position().front();
        size_t horizontal_chars = num_timer_text_view_horizontal_chars - 8;

        std::string text;
        text.reserve(1024);
        auto& timers = data->timers;
        fmt::format_to(std::back_inserter(text), "{0:<{1}}\n", "", horizontal_chars + 3);

        auto left_chars = horizontal_chars - 15;

        const static nana::color category_colors[] = {
          nana::colors::white,
          nana::colors::light_gray,
          nana::colors::light_blue,
          nana::colors::sky_blue,
          nana::colors::blue,
          nana::colors::blue_violet,
          nana::colors::light_pink,
          nana::colors::light_yellow,
          nana::colors::light_green,
          nana::colors::lawn_green,
          nana::colors::gray,
        };
        const size_t max_category = *(&category_colors + 1) - category_colors - 1;

        for (size_t line = 1; auto& tm : timers) {
            auto left_indent = tm.category_level;

            fmt::format_to(
              std::back_inserter(text),
              "|{0:<{3}}{1:.<{4}}{2:.>15.4f} ms\n", "",
              tm.name, 1000.0 * std::chrono::duration<double>{tm.elapsed}.count(),
              left_indent, left_chars - left_indent);

            m.timers.colored_area_access()->get(line)->count = 1;
            m.timers.colored_area_access()->get(line)->fgcolor = category_colors[std::min(tm.category_level, max_category)];
            ++line;
        }

        m.timers.reset(text, true);
        m.timers.append("\n", true);
        m.timers.caret_pos(pos);
        m.timers.append(" ", true);
    }

    // -- ����� �ɼ� ����
    // pipeline_board Ž��
    auto subscr_ptr = m.board_ref ? &m.board_ref->debug_data_subscriber : nullptr;
    m.values.auto_draw(false);
    // auto showing = m.values.first_visible();
    auto list = m.values.at(0);
    for (auto& dbg : data->debug_data) {
        // �̸� �� ī�װ��� Ž��
        [&]() { // ���� ������ �����ϰ� ���������� ���� lambda�� ����
            for (auto& item : list) {
                // �������� �˻��� �̸��� ī�װ��� �ߺ��� ��� �����͸� �����մϴ�.
                auto& entry = item.value<execution_context_data::debug_data_entity>();
                if (entry.category_id == dbg.category_id && entry.name == dbg.name) {
                    item.resolve_from(dbg);
                    if (item.checked() && subscr_ptr && *subscr_ptr) {
                        // ���� üũ�Ǿ���, ���� �Լ��� �����Ѵٸ� ȣ���մϴ�.
                        // �� �� ���� �Լ��� false ��ȯ �� uncheck
                        if ((*subscr_ptr)(proxy.name(), dbg) == false) {
                            item.check(false);
                        }
                    }
                    return;
                }
            }

            // �����Ǵ� �������� ã�� ���� ��쿡�� ���ο� ��� ����
            list.append(dbg, true);
        }();
    }

    try {
        //    m.values.scroll(false, showing);
    } catch (std::exception&) {}
    m.values.auto_draw(true);
}
