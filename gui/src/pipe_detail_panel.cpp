#include "pipepp/gui/pipe_detail_panel.hpp"
#include <chrono>
#include <functional>

#include "fmt/format.h"
#include "nana/basic_types.hpp"
#include "nana/gui/msgbox.hpp"
#include "nana/gui/widgets/listbox.hpp"
#include "nana/gui/widgets/textbox.hpp"
#include "nana/gui/widgets/treebox.hpp"
#include "nana/paint/graphics.hpp"
#include "pipepp/pipeline.hpp"

#define COLUMN_CATEGORY 0
#define COLUMN_KEY 1
#define COLUMN_VALUE 2

using clock_type = std::chrono::system_clock;

struct pipepp::gui::pipe_detail_panel::data_type {
    pipe_detail_panel& self;

    std::weak_ptr<impl__::pipeline_base> pipeline;
    pipe_id_t pipe;

    nana::textbox timers{self};
    nana::listbox options{self};
    nana::treebox values{self};
};

pipepp::gui::pipe_detail_panel::pipe_detail_panel(nana::window owner, const nana::rectangle& rectangle, const nana::appearance& appearance)
    : form(owner, rectangle, appearance)
    , impl_(std::make_unique<data_type>(*this))
{
    div(""
        "margin=2"
        "<vert"
        "   <w_timers weight=25% margin=[0,0,2,0]>"
        "   <w_vals margin=[0,0,2,0]>"
        "   <w_opts>"
        ">");

    auto& m = *impl_;
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

    m.timers.bgcolor(nana::colors::dim_gray);
    m.timers.fgcolor(nana::colors::white);
    m.timers.typeface(nana::paint::font{"consolas", 10.0});
    m.timers.text_align(nana::align::left);
    m.timers.editable(false);
    m.timers.enable_caret();

    m.values.typeface(nana::paint::font("consolas", 10.0));
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
    auto& val = opt.option();
    for (auto& pair : val.items()) {
        auto& category_str = categories.at(pair.key());
        auto& value = pair.value();

        list.append({category_str, pair.key(), value.dump()});
    }
    m.options.auto_draw(true);
}

void pipepp::gui::pipe_detail_panel::_cb_option_arg_selected(nana::arg_listbox const& arg)
{
    if (arg.item.checked() == false) { return; }

    auto& m = *impl_;
    auto sel = arg.item.pos();

    // 옵션 선택 시, 다이얼로그 및 설명을 띄웁니다.
    auto pl = m.pipeline.lock();
    if (pl == nullptr) { return; }

    auto& opts = pl->get_pipe(m.pipe).options();
    auto key = arg.item.text(COLUMN_KEY);
    auto val = arg.item.text(COLUMN_VALUE);
    auto desc = opts.description().at(key);
    nana::inputbox ib{*this, desc, key};

    auto& json = opts.option().at(key);
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

void pipepp::gui::pipe_detail_panel::update(std::shared_ptr<execution_context_data> data)
{
    auto& m = *impl_;

    // -- 타이머 문자열 빌드
    auto pos = m.timers.text_position().front();

    size_t horizontal_chars = num_timer_text_view_horizontal_chars;
    std::string text;
    text.reserve(1024);
    auto& timers = data->timers;
    fmt::format_to(std::back_inserter(text), "\n {0:<{1}}\n\n", "Timer Records", horizontal_chars + 3);

    auto left_chars = horizontal_chars - 15;

    for (auto& tm : timers) {
        auto left_indent = tm.category_level;

        fmt::format_to(
          std::back_inserter(text),
          " {0:-<{3}}{1:.<{4}}{2:.>15.4f} ms\n", "",
          tm.name, 1000.0 * std::chrono::duration<double>{tm.elapsed}.count(),
          left_indent, left_chars - left_indent);
    }

    m.timers.select(true);
    m.timers.append(text, true);
    m.timers.caret_pos(pos), m.timers.del(), m.timers.append(" ", true);

    // -- 디버그 옵션 빌드
}
