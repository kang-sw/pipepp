#include "pipepp/gui/option_panel.hpp"

#include "fmt/format.h"
#include "nana/gui/drawing.hpp"
#include "nana/gui/place.hpp"
#include "nana/gui/widgets/button.hpp"
#include "nana/gui/widgets/group.hpp"
#include "nana/gui/widgets/label.hpp"
#include "nana/gui/widgets/listbox.hpp"
#include "nana/gui/widgets/textbox.hpp"
#include "nana/gui/widgets/treebox.hpp"
#include "nana/paint/graphics.hpp"
#include "nlohmann/json_fwd.hpp"
#include "pipepp/pipeline.hpp"

using namespace nana;

struct pipepp::gui::option_panel::body_type {
    option_panel& self;
    detail::option_base* option = {};
    std::weak_ptr<detail::pipeline_base> pipeline;

    /* WIDGETS */
    place layout{self};
    treebox items{self};

    panel<true> input{self.handle()};
    place input_layout{input};
    label input_title{input};
    textbox input_descr{input};

    listbox input_array_object_list{input};
    textbox input_enter{input};
    button input_enter_check{input};

    nana::treebox::item_proxy selected_proxy;

    bool is_vertical = false;
};

struct option_tree_arg {
    std::string key;
};

static auto const CONSOLAS = paint::font{"consolas", 11.0};

pipepp::gui::option_panel::option_panel(nana::window wd, bool visible)
    : super(wd, visible)
    , impl_(std::make_unique<body_type>(*this))
{
    auto& m = *impl_;
    bgcolor(colors::dim_gray);

    m.items.typeface(CONSOLAS);
    m.items.events().selected([&](auto& arg) { _cb_tree_selected(arg); });

    m.layout["MAIN"] << m.items;
    m.layout["INPUT"] << m.input;

    m.input.bgcolor(nana::colors::dim_gray);

    m.input_layout["TITLE"] << m.input_title;
    m.input_layout["DESC"] << m.input_descr;
    m.input_layout["LIST"] << m.input_array_object_list;
    m.input_layout["INPUT"] << m.input_enter;
    m.input_layout.collocate();

    m.input_title.typeface(CONSOLAS);
    m.input_title.bgcolor(colors::black);
    m.input_title.fgcolor(colors::white);
    m.input_title.text_align(align::center, align_v::center);

    m.input_descr.borderless(true);
    m.input_descr.multi_lines(true);
    m.input_descr.editable(false);
    m.input_descr.focus_behavior(widgets::skeletons::text_focus_behavior::none);
    m.input_descr.bgcolor(colors::light_gray);

    m.input_enter.multi_lines(false);
    m.input_enter.typeface(CONSOLAS);
    m.input_enter.bgcolor(colors::dim_gray);
    m.input_enter.editable(false);

    m.input_array_object_list.typeface(CONSOLAS);
    m.input_array_object_list.append_header("Key", 80);
    m.input_array_object_list.append_header("Value", 120);

    m.input_array_object_list.events().selected([&](auto& arg) { _cb_json_list_selected(arg); });
    _assign_enterbox_events();

    vertical(false);
}

pipepp::gui::option_panel::~option_panel() = default;

void pipepp::gui::option_panel::_refresh_item(nana::drawerbase::treebox::item_proxy& item)
{
    auto& keyval = item.key();
    auto& opts = *impl_->option;
    auto& name = opts.names().at(keyval);
    auto& value = opts.value().at(keyval);

    auto text = fmt::format("[{0:<15}] {1}", name, value.dump());
    item.text(std::move(text));
}

void pipepp::gui::option_panel::_cb_tree_selected(nana::arg_treebox const& a)
{
    bool const is_category_node = !a.item.child().empty();

    auto& m = *impl_;
    auto& opts = *m.option;
    auto& key = a.item.key();

    if (!a.item.selected() || is_category_node || opts.value().contains(key) == false) {
        m.selected_proxy = {};
        _expand(false);
        return;
    }

    m.selected_proxy = a.item;
    _expand(true);
    auto& value = opts.value().at(key);
    m.input_title.caption(opts.names().at(key));
    m.input_descr.reset(fmt::format("<{} [{}]>\n", value.type_name(), value.size()));
    m.input_descr.append(opts.description().at(key), true);

    auto& list = m.input_array_object_list;
    list.clear();
    list.auto_draw(false);
    auto cat = list.at(0);
    for (auto& json_value : value.items()) {
        cat.append({json_value.key(), json_value.value().dump()});
        auto item = cat.back();
        item.value(&json_value.value());
    }
    list.auto_draw(true);
    cat.at(0).select(true, true);
}

void pipepp::gui::option_panel::_cb_json_list_selected(nana::arg_listbox const& a)
{
    auto& m = *impl_;
    auto selections = m.input_array_object_list.selected();
    if (selections.empty()) {
        m.input_enter.editable(false);
        m.input_enter.reset();
        m.input_enter.bgcolor(colors::dim_gray);
        return;
    }

    m.input_enter.editable(true);
    m.input_enter.focus();
    m.input_layout.erase(m.input_enter_check);
    if (selections.size() == 1) {
        auto item = m.input_array_object_list.at(selections.front());
        m.input_enter.reset(item.text(1));

        if (item.value<nlohmann::json*>()->is_boolean()) {
            m.input_layout["INPUT"] << m.input_enter_check;
            _update_check_button();
        }
    }
    else {
        m.input_enter.reset("...");
    }

    m.input_layout.collocate();
    m.input_enter.select(true);
}

void pipepp::gui::option_panel::_update_enterbox(bool trig_modify)
{
    auto& m = *impl_;
    using nlohmann::json;
    auto& widget = m.input_enter;
    auto selections = m.input_array_object_list.selected();
    if (selections.empty()) { return; }

    bool correct = true;

    json parsed_json;
    try {
        parsed_json = nlohmann::json::parse(widget.text());

        for (auto& index : selections) {
            auto sel = m.input_array_object_list.at(index);
            auto value = sel.value<json*>();
            if (strcmp(parsed_json.type_name(), value->type_name())) {
                correct = false;
                break;
            }
        }
    } catch (std::exception& e) {
        correct = false;
    }

    if (trig_modify && correct) {
        auto _lck = m.option->lock_write();
        for (auto& index : selections) {
            auto sel = m.input_array_object_list.at(index);
            auto value = sel.value<json*>();
            value->merge_patch(parsed_json);

            sel.text(1, value->dump());
        }
        auto& key = m.selected_proxy.key();
        if (!m.option->verify(key)) {
            m.selected_proxy.select(false);
            m.selected_proxy.select(true);
            return;
        }

        _refresh_item(m.selected_proxy);
        API::refresh_window(m.items);

        if (on_dirty) { on_dirty(m.selected_proxy.key()); }

        m.input_enter.select(true);
    }

    widget.bgcolor(correct ? colors::light_green : colors::orange_red);
}

void pipepp::gui::option_panel::_assign_enterbox_events()
{
    auto& m = *impl_;
    m.input_enter.events().text_changed([&](arg_textbox const& arg) { _update_enterbox(false); });
    m.input_enter.events().key_press([&](arg_keyboard const& arg) {
        if (arg.key == keyboard::enter) { _update_enterbox(true); }
        if (arg.key == keyboard::os_arrow_down) { m.input_array_object_list.move_select(false); }
        if (arg.key == keyboard::os_arrow_up) { m.input_array_object_list.move_select(true); }
    });
    m.input_enter_check.events().click([&](auto&&) {
        _update_check_button(true);
    });
}

void pipepp::gui::option_panel::_update_check_button(bool operate)
{
    auto& m = *impl_;
    bool status;
    if (m.input_enter.text() == "true") {
        if (operate) { m.input_enter.reset("false"); }
        status = operate ? false : true;
    }
    else if (m.input_enter.text() == "false") {
        if (operate) { m.input_enter.reset("true"); }
        status = operate ? true : false;
    }
    else {
        return;
    }
    m.input_enter_check.bgcolor(status ? colors::black : colors::white);
    _update_enterbox(operate);
}

void pipepp::gui::option_panel::reload(std::weak_ptr<detail::pipeline_base> pl, detail::option_base* option)
{
    auto& m = *impl_;
    m.option = option;
    m.pipeline = pl;

    // rebuild option tree
    _expand(false);

    m.items.clear();
    if (option == nullptr) { return; }

    auto _lck = option->lock_read();
    auto& tree = m.items;

    auto& opts = option->value();
    auto& names = option->names();
    for (auto category_pair : option->categories()) {
        nana::drawerbase::treebox::item_proxy item;
        auto& key = category_pair.first;
        auto category = category_pair.second.empty() ? "Misc" : category_pair.second;
        std::string name;

        for (; !category.empty();) {
            auto first_dot = category.find_first_of('.', 1);
            std::string category_name;
            if (first_dot != npos) {
                category_name = category.substr(0, first_dot);
                name += category_name;
                category = category.substr(first_dot);
            }
            else {
                category_name = category;
                name = category;
                category = {};
            }

            if (category_name[0] == '.') { category_name.erase(0, 1); };
            item = item.empty() ? tree.insert(name, std::move(category_name)) : item.append(name, std::move(category_name));
        }

        item = item.append(key, "");
        _refresh_item(item);
    }
}

void pipepp::gui::option_panel::vertical(bool do_vertical)
{
    auto& m = *impl_;
    m.is_vertical = do_vertical;

    if (do_vertical) {
        m.input_layout.div(
          "<"
          "  vert gap=2"
          "  <TITLE weight=20>"
          "  <DESC margin=[1,0,1,0] weight=30%>"
          ">"
          "<"
          "  vert gap=2"
          "  <LIST margin=2>"
          "  <INPUT weight=20 arrange=[variable, 20]>"
          ">");
    }
    else {
        m.input_layout.div(
          "vert gap=2"
          "<TITLE weight=20>"
          "<DESC margin=[1,0,1,0] weight=30%>"
          "<LIST margin=2>"
          "<INPUT weight=20 arrange=[variable, 20]>");
    }

    m.input_layout.collocate();
}

void pipepp::gui::option_panel::_expand(bool expanded)
{
    auto& m = *impl_;

    if (expanded) {
        m.layout.div(fmt::format("{1} {0}", "<MAIN margin=[0,4,0,0]><INPUT> margin=4 gap=4", m.is_vertical ? "vert" : ""));
    }
    else {
        m.layout.div("<MAIN margin=4>");
    }

    m.layout.collocate();
}
