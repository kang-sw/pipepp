#pragma once
#include "nana/gui/widgets/panel.hpp"
#include "pipepp/options.hpp"

namespace nana {
struct arg_treebox;
struct arg_listbox;
} // namespace nana

namespace nana {
namespace drawerbase {
namespace treebox {
class item_proxy;
}
} // namespace drawerbase
} // namespace nana

namespace pipepp {
namespace impl__ {
class pipeline_base;
}
} // namespace pipepp

namespace pipepp::gui {

class option_panel : public nana::panel<true> {
public:
    using super = nana::panel<true>;
    option_panel(nana::window wd, bool visible);
    ~option_panel();

public:
    /**
     * \brief Initialize option panel object
     * \param pl Pipeline pointer to manage object lifetime
     * \param option Option reference
     */
    void reload(std::weak_ptr<impl__::pipeline_base> pl, impl__::option_base* option);

private:
    void _expand(bool expanded);
    void _refresh_item(nana::drawerbase::treebox::item_proxy& item);
    void _cb_tree_selected(nana::arg_treebox const& a);
    void _cb_json_list_selected(nana::arg_listbox const& a);
    void _update_enterbox(bool trig_modify);
    void _assign_enterbox_events();
    void _update_check_button(bool operate = false);

public:
    std::function<void(std::string_view)> on_dirty;

private:
    struct body_type;
    std::unique_ptr<body_type> impl_;
};

} // namespace pipepp::gui
