#include "pipepp/gui/option_panel.hpp"

#include "fmt/format.h"
#include "nana/gui/place.hpp"
#include "nana/gui/widgets/group.hpp"
#include "nana/gui/widgets/label.hpp"
#include "nana/gui/widgets/listbox.hpp"
#include "nana/gui/widgets/textbox.hpp"
#include "nana/gui/widgets/treebox.hpp"
#include "nana/paint/graphics.hpp"
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

    std::string input_active_key;
    size_t input_active_index;
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

    m.layout.div("<MAIN arrange=[variable, 50%] gap=4 margin=5>");
    m.layout["MAIN"] << m.items;

    m.input.bgcolor(nana::colors::dim_gray);
    m.input_layout.div(
      "vert gap=2"
      "<TITLE weight=20>"
      "<DESC margin=1>"
      "<INPUT weight=20>");

    m.input_layout["TITLE"] << m.input_title;
    m.input_layout["DESC"] << m.input_descr;
    m.input_layout["INPUT"] << m.input_enter;
    m.input_layout.collocate();

    m.input_title.typeface(CONSOLAS);
    m.input_title.bgcolor(colors::dim_gray);
    m.input_title.fgcolor(colors::white);

    m.input_descr.borderless(true);
    m.input_descr.multi_lines(true);
    m.input_descr.editable(false);
    m.input_descr.bgcolor(colors::light_gray);

    m.input_enter.multi_lines(false);
    m.input_enter.typeface(CONSOLAS);
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
        _expand(false);
        return;
    }

    _expand(true);
    m.input_title.caption(opts.names().at(key));
    m.input_descr.caption(opts.description().at(key));
    ;
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
    m.layout.erase(m.input);

    if (expanded) {
        m.layout["MAIN"] << m.input;
    }

    m.layout.collocate();
}
