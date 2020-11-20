#include "pipepp/gui/pipe_detail_panel.hpp"
#include <any>
#include <chrono>
#include <functional>

#include "fmt/format.h"
#include "nana/basic_types.hpp"
#include "nana/gui/msgbox.hpp"
#include "nana/gui/programming_interface.hpp"
#include "nana/gui/widgets/listbox.hpp"
#include "nana/gui/widgets/textbox.hpp"
#include "nana/gui/widgets/treebox.hpp"
#include "nana/paint/graphics.hpp"
#include "pipepp/execution_context.hpp"
#include "pipepp/gui/pipeline_board.hpp"
#include "pipepp/pipeline.hpp"

#define COLUMN_CATEGORY 0
#define COLUMN_KEY 1
#define COLUMN_VALUE 2

using clock_type = std::chrono::system_clock;

struct pipepp::gui::pipe_detail_panel::data_type {
    pipe_detail_panel& self;
    pipeline_board* board_ref;

    std::weak_ptr<impl__::pipeline_base> pipeline;
    pipe_id_t pipe;

    nana::textbox timers{self};
    nana::listbox options{self};
    nana::listbox values{self};
};

pipepp::gui::pipe_detail_panel::pipe_detail_panel(nana::window owner, const nana::rectangle& rectangle, const nana::appearance& appearance)
    : form(owner, rectangle, appearance)
    , impl_(std::make_unique<data_type>(*this))
{
    auto& m = *impl_;

    auto hnd_pipe_board = nana::API::get_parent_window(owner);
    auto pipe_board = dynamic_cast<pipeline_board*>(nana::API::get_widget(hnd_pipe_board));
    m.board_ref = pipe_board;

    div(""
        "margin=2"
        "<vert"
        "   <w_timers weight=25% margin=[0,0,2,0]>"
        "   <w_vals margin=[0,0,2,0]>"
        "   <w_opts weight=40%>"
        ">");

    (*this)["w_timers"] << m.timers;
    (*this)["w_opts"] << m.options;
    (*this)["w_vals"] << m.values;
    collocate();

    m.options.checkable(true);
    m.options.events().checked(std::bind(&pipe_detail_panel::_cb_option_arg_selected, this, std::placeholders::_1));

    auto header_div = m.options.size().width * 32 / 100;
    m.options.append_header("Category", header_div);
    m.options.append_header("Key", header_div);
    m.options.append_header("Value", header_div);

    m.timers.bgcolor(nana::colors::black);
    m.timers.fgcolor(nana::color{}.from_rgb(0, 255, 0));
    m.timers.typeface(nana::paint::font{"consolas", 10.0});
    m.timers.text_align(nana::align::left);
    m.timers.editable(false);
    m.timers.enable_caret();

    header_div = m.options.size().width * 48 / 100;
    m.values.checkable(true);
    m.values.append_header("Name", header_div);
    m.values.append_header("Value", header_div);
}

pipepp::gui::pipe_detail_panel::~pipe_detail_panel() = default;

void pipepp::gui::pipe_detail_panel::_reload_options(pipepp::impl__::option_base const& opt)
{
    auto& m = *impl_;
    auto option_read_lock = opt.lock_read();
    m.options.auto_draw(false);
    m.options.clear();

    auto list = m.options.at(0);
    auto& categories = opt.categories();
    auto& val = opt.value();
    for (auto& pair : val.items()) {
        auto& category_str = categories.at(pair.key());
        auto& value = pair.value();

        list.append({category_str, pair.key(), value.dump()});
        if (value.type() == nlohmann::detail::value_t::boolean) {
            list.back().check((bool)value);
        }
    }
    m.options.auto_draw(true);
}

void pipepp::gui::pipe_detail_panel::_cb_option_arg_selected(nana::arg_listbox const& arg)
{
    auto& m = *impl_;
    auto sel = arg.item.pos();

    // 옵션 선택 시, 다이얼로그 및 설명을 띄웁니다.
    auto pl = m.pipeline.lock();
    if (pl == nullptr) { return; }

    auto& opts = pl->get_pipe(m.pipe).options();
    auto key = arg.item.text(COLUMN_KEY);
    auto val = arg.item.text(COLUMN_VALUE);
    auto& json = opts.value().at(key);

    // Boolean 형식인 경우, 특별히 체크박스 자체가 값을 나타냅니다.
    if (json.type() == nlohmann::detail::value_t::boolean) {
        if (arg.item.checked() == (bool)json) { return; }
        auto _lck0 = opts.lock_write();
        json = arg.item.checked();
        arg.item.text(COLUMN_VALUE, json.dump());
    }
    else {
        if (arg.item.checked() == false) { return; }

        auto desc = opts.description().at(key);
        nana::inputbox ib{*this, desc, key};

        nlohmann::json json_parsed;
        nana::inputbox::text value{"Value", val};

        // verification ...
        // 1. parsing에 성공해야 합니다.
        // 2. 요소 속성이 일치해야 합니다.
        ib.verify([&](nana::window handle) {
            auto input_str = value.value();
            try {
                json_parsed = nlohmann::json::parse(input_str);
                if (strcmp(json_parsed.type_name(), json.type_name()) != 0) {
                    auto _ = (nana::msgbox{*this, "Error "} << "Type mismatch").show();
                    return false;
                }
            } catch (std::exception e) {
                auto _ = (nana::msgbox{*this, "Error ", nana::msgbox::ok} << "Parse Failed").show();
                return false;
            }

            return true;
        });

        if (ib.show(value)) {
            auto _lck0 = opts.lock_write();
            json = json_parsed;
            arg.item.text(COLUMN_VALUE, json.dump());
        }

        arg.item.check(false);
    }

    auto& notify = m.board_ref->option_changed;
    if (notify) { notify(m.pipe, key); }
}

void pipepp::gui::pipe_detail_panel::reset_pipe(std::weak_ptr<impl__::pipeline_base> pl, pipe_id_t id)
{
    auto& m = *impl_;
    m.pipeline = pl;
    m.pipe = id;

    if (auto pipeline = pl.lock()) {
        auto proxy = pipeline->get_pipe(id);
        auto const& opt = proxy.options();
        caption(proxy.name());

        _reload_options(opt);
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
    auto proxy = m.pipeline.lock()->get_pipe(m.pipe);

    // -- 타이머 문자열 빌드
    {
        auto pos = m.timers.text_position().front();

        size_t horizontal_chars = num_timer_text_view_horizontal_chars;
        std::string text;
        text.reserve(1024);
        auto& timers = data->timers;
        fmt::format_to(std::back_inserter(text), "{0:<{1}}\n", "", horizontal_chars + 3);

        auto left_chars = horizontal_chars - 15;

        const static nana::color category_colors[] = {
          nana::colors::white,
          nana::colors::light_gray,
          nana::colors::light_green,
          nana::colors::light_blue,
          nana::colors::sky_blue,
          nana::colors::blue,
          nana::colors::cadet_blue,
          nana::colors::purple,
          nana::colors::pink,
          nana::colors::gray,
        };
        const size_t max_category = *(&category_colors + 1) - category_colors - 1;

        for (size_t line = 1; auto& tm : timers) {
            auto left_indent = tm.category_level;

            fmt::format_to(
              std::back_inserter(text),
              " {0:<{3}}{1:.<{4}}{2:.>15.4f} ms\n", "",
              tm.name, 1000.0 * std::chrono::duration<double>{tm.elapsed}.count(),
              left_indent, left_chars - left_indent);

            m.timers.colored_area_access()->get(line)->count = 1;
            m.timers.colored_area_access()->get(line)->fgcolor = category_colors[std::min(tm.category_level, max_category)];
            ++line;
        }

        m.timers.select(true);
        m.timers.append(text, true);
        m.timers.caret_pos(pos), m.timers.append("+", true);
    }

    // -- 디버그 옵션 빌드
    // pipeline_board 탐색
    auto subscr_ptr = m.board_ref ? &m.board_ref->debug_data_subscriber : nullptr;
    m.values.auto_draw(false);
    auto showing = m.values.first_visible();
    auto list = m.values.at(0);
    for (auto& dbg : data->debug_data) {
        // 이름 및 카테고리로 탐색
        [&]() { // 내부 루프를 간편하게 빠져나가기 위해 lambda로 감쌈
            for (auto& item : list) {
                // 아이템을 검색해 이름과 카테고리가 중복된 경우 데이터만 갱신합니다.
                auto& entry = item.value<execution_context_data::debug_data_entity>();
                if (entry.category_id == dbg.category_id && entry.name == dbg.name) {
                    item.resolve_from(dbg);
                    if (item.checked() && subscr_ptr && *subscr_ptr) {
                        // 만약 체크되었고, 구독 함수가 존재한다면 호출합니다.
                        // 이 때 구독 함수가 false 반환 시 uncheck
                        if ((*subscr_ptr)(proxy.name(), dbg) == false) {
                            item.check(false);
                        }
                    }
                    return;
                }
            }

            // 대응되는 아이템을 찾지 못한 경우에만 새로운 요소 삽입
            list.append(dbg, true);
        }();
    }

    try {
        m.values.scroll(false, showing);
    } catch (std::exception&) {}
    m.values.auto_draw(true);
}
