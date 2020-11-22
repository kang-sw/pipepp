#include "pipepp/gui/debug_data_panel.hpp"
#include <variant>

#include "fmt/format.h"
#include "nana/basic_types.hpp"
#include "nana/gui/drawing.hpp"
#include "nana/gui/place.hpp"
#include "nana/gui/widgets/button.hpp"
#include "nana/gui/widgets/label.hpp"
#include "nana/gui/widgets/scroll.hpp"
#include "nana/gui/widgets/textbox.hpp"
#include "nana/gui/widgets/treebox.hpp"
#include "pipepp/gui/basic_utility.hpp"
#include "pipepp/pipeline.hpp"

using namespace nana;
class inline_widget;

using timer_data_desc = pipepp::execution_context_data::timer_entity;
using debug_data_desc = pipepp::execution_context_data::debug_data_entity;

struct pipepp::gui::debug_data_panel::data_type {
    explicit data_type(debug_data_panel* p)
        : self(*p)
    {}

public:
    debug_data_panel& self;
    pipeline_board* board_ref = {};

    std::weak_ptr<detail::pipeline_base> pipeline;
    pipe_id_t pipe = pipe_id_t::none;

    /********* widgets  *********/
    scroll<true> scroll{self};

    std::shared_ptr<inline_widget> root;

    /********* property *********/
    unsigned scroll_width = 20;
    unsigned elem_height = 20;
    unsigned indent = 10;

    /********* data     *********/
    std::shared_ptr<execution_context_data> data;
    std::unordered_map<kangsw::hash_index, std::weak_ptr<inline_widget>> timers;
};

class pipepp::gui::inline_widget : public panel<true>, public std::enable_shared_from_this<inline_widget> {
    inline static size_t ID_GEN = 0;

public:
    inline_widget(debug_data_panel& owner, unsigned level = 0)
        : panel<true>(owner.m.self, true)
        , owner_(owner)
        , m(owner.m)
        , level_(level)
    {
        layout_.div(fmt::format("TEXT margin=[0,0,0,{}]", m.indent * level_));
        layout_.collocate();
        layout_["TEXT"] << text_;

        text_.transparent(true);
        text_.caption("hell, world!");
        text_.typeface(DEFAULT_DATA_FONT);
        text_.text_align(align::right);

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
        text_.fgcolor(category_colors[std::min<int>(max_category, level)]);
        bgcolor(colors::black);

        // -- Events
        events().mouse_wheel([&](arg_wheel const& a) { m.scroll.make_scroll(!a.upwards); });
        text_.events().click([&](auto&&) {
            if (is_timer_slot()) {
                colapsed_ = !colapsed_;
                owner_._update_scroll();
                relocate();
            }
        });

        drawing{text_}.draw_diehard([&](paint::graphics& gp) {
            {
                auto singlechar_extent = gp.text_extent_size("a");
                text_extent_ = size().width / std::max<int>(1, singlechar_extent.width);
            }
        });
    }

public:
    unsigned update_root_height(unsigned root = 0, bool colapse = false)
    {
        colapse ? hide() : show();

        root_height_ = root;
        auto height = root + !colapse * m.elem_height;
        for (auto& w : children_) {
            height = w->update_root_height(height, colapse || colapsed_);
        }

        return height;
    }

    void relocate()
    {
        m.root->move(m.root->pos());
    }

    void refresh()
    {
    }

    void width(unsigned v)
    {
        size({v, m.elem_height});
        for (auto& w : children_) { w->width(v); }
    }

    template <typename Dt_ = timer_data_desc>
    auto put(Dt_&& data)
    {
        auto r = _create();
        r->slot_ = std::forward<Dt_>(data);
        return r;
    }

    bool is_timer_slot() const
    {
        return slot_.index() == 0;
    }

    bool is_debug_slot() const
    {
        return !is_timer_slot();
    }

protected:
    void _m_move(int x, int y) override
    {
        API::move_window(handle(), {x, root_height_ + y});
        for (auto& w : children_) { w->move(x, y); }
    }

private:
    auto _create()
    {
        auto& r = children_.emplace_back(std::make_shared<inline_widget>(owner_, level_ + 1));
        r->root_ = weak_from_this();
        return r;
    }

public:
    size_t const id = ID_GEN++;

private:
    debug_data_panel& owner_;
    debug_data_panel::data_type& m;

    place layout_{*this};
    label text_{*this};

    int root_height_ = 0;
    unsigned level_ = 0;
    bool colapsed_ = false;
    int text_extent_ = 0;

    std::variant<timer_data_desc, debug_data_desc> slot_;

    std::weak_ptr<inline_widget> root_;
    std::vector<std::shared_ptr<inline_widget>> children_;
};

pipepp::gui::debug_data_panel::debug_data_panel(window wd, bool visible)
    : super(wd, visible)
    , impl_(std::make_unique<data_type>(this))
    , m(*impl_)
{
    // -- Init values
    bgcolor(colors::black);
    m.root = std::make_shared<inline_widget>(*this);

    //for (auto i : kangsw::iota{10}) {
    //    auto v = m.root->append();
    //    for (auto j : kangsw::iota{10}) {
    //        v->append();
    //    }
    //}

    // -- Events assign
    events().resized([&](arg_resized const& s) {
        m.root->width(s.width - m.scroll_width);
        m.scroll.move(s.width - m.scroll_width, 0);
        m.scroll.size({m.scroll_width, s.height});

        _update_scroll();
        m.root->relocate();
    });

    m.scroll.events().value_changed([&](arg_scroll const& arg) {
        int y_val = -m.scroll.value();
        m.root->move(0, y_val);
    });
}

pipepp::gui::debug_data_panel::~debug_data_panel() = default;

void pipepp::gui::debug_data_panel::_set_board_ref(pipeline_board* ref)
{
    m.board_ref = ref;
}

void pipepp::gui::debug_data_panel::scroll_width(size_t v)
{
    m.scroll_width = v;
    size(size());
}

void pipepp::gui::debug_data_panel::elem_height(size_t v)
{
    m.elem_height = v;
}

void pipepp::gui::debug_data_panel::_reset_pipe(std::weak_ptr<detail::pipeline_base> pl, pipe_id_t id)
{
    m.pipeline = pl;
    m.pipe = id;
}

void pipepp::gui::debug_data_panel::_update(std::shared_ptr<execution_context_data> data)
{
    m.data = data;
}

void pipepp::gui::debug_data_panel::_update_scroll()
{
    m.scroll.range(size().height);
    m.scroll.amount(m.root->update_root_height());
    m.scroll.step(m.elem_height);
    m.scroll.value(m.scroll.value());
}
