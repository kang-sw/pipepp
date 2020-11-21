#include "pipepp/gui/option_panel.hpp"

#include "fmt/format.h"
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
    impl__::option_base* option = {};
    std::weak_ptr<impl__::pipeline_base> pipeline;

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

    std::string input_active_key = {};
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
    m.input_layout.div(
      "vert gap=2"
      "<TITLE weight=20>"
      "<DESC margin=[1,0,1,0] weight=100>"
      "<LIST margin=2>"
      "<INPUT weight=20 arrange=[variable, 20]>");

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
        m.input_active_key = {};
        _expand(false);
        return;
    }

    _expand(true);
    auto& value = opts.value().at(key);
    m.input_title.caption(opts.names().at(key));
    m.input_descr.reset(fmt::format("<{} [{}]>\n", value.type_name(), value.size()));
    m.input_descr.append(opts.description().at(key), true);
    m.input_active_key = key;

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

void pipepp::gui::option_panel::_assign_enterbox_events()
{
    auto& m = *impl_;
    using nlohmann::json;
    static constexpr auto check_json = [](json const& compare, std::string const& parse) {
        try {
            auto parsed_json = nlohmann::json::parse(parse);
            return strcmp(compare.type_name(), parsed_json.type_name()) == 0;
        } catch (std::exception& e) {
            return false;
        }
    };

    m.input_enter.events().text_changed([&](arg_textbox const& arg) {
        auto selections = m.input_array_object_list.selected();
        if (selections.empty()) { return; }

        auto& key = m.input_active_key;
        auto& opts = *m.option;
        auto& root_value = opts.value().at(key);
        bool correct = true;

        if (root_value.is_object() || root_value.is_array()) {
            for (auto& index : selections) {
                auto sel = m.input_array_object_list.at(index);
                auto value = sel.value<json*>();
                if (!check_json(*value, arg.widget.text())) {
                    correct = false;
                    break;
                }
            }
        }
        else {
            correct = check_json(root_value, arg.widget.text());
        }

        arg.widget.bgcolor(correct ? colors::light_green : colors::orange_red);
    });
}

void pipepp::gui::option_panel::reload(std::weak_ptr<impl__::pipeline_base> pl, impl__::option_base* option)
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
        auto category = category_pair.second;
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

void pipepp::gui::option_panel::_expand(bool expanded)
{
    auto& m = *impl_;

    if (expanded) {
        m.layout.div("<MAIN margin=[0,4,0,0]><INPUT> margin=4 gap=4");
    }
    else {
        m.layout.div("<MAIN margin=4>");
    }

    m.layout.collocate();
}
