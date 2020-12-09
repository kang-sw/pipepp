#include "pipepp/gui/debug_data_panel.hpp"
#include <cassert>
#include <iterator>
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
#include "pipepp/gui/pipeline_board.hpp"
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
    unsigned indent = 2;

    /********* data     *********/
    std::shared_ptr<execution_context_data> data;
    std::unordered_map<kangsw::hash_index, std::weak_ptr<inline_widget>> timers;
};

class pipepp::gui::inline_widget : public panel<false>, public std::enable_shared_from_this<inline_widget> {
    inline static size_t ID_GEN = 0;

public:
    inline_widget(debug_data_panel& owner, unsigned lv = 0)
        : panel<false>(owner.m.self, true)
        , level(lv)
        , owner_(owner)
        , m(owner.m)
    {
        layout_.div(fmt::format("TEXT"));
        layout_.collocate();
        layout_["TEXT"] << text_;

        // text_.transparent(true);
        text_.caption("hell, world!");
        text_.typeface(DEFAULT_DATA_FONT);

        text_.bgcolor(colors::black);

        // -- Events
        events().mouse_wheel([&](arg_wheel const& a) { m.scroll.make_scroll(!a.upwards); });
        text_.events().click([&](auto&&) {
            if (is_timer_slot()) {
                if (children_.empty() == false) {
                    colapsed_or_subscribed_ = !colapsed_or_subscribed_;
                }
                owner_._refresh_layout();
                relocate();
            } else {
                colapsed_or_subscribed_ = !colapsed_or_subscribed_;
                _perform_subscribe(true);
            }
            refresh();
        });

        drawing{text_}.draw_diehard([&](paint::graphics& gp) {
            auto singlechar_extent = gp.text_extent_size("a");
            text_extent_ = text_.size().width / std::max<int>(1, singlechar_extent.width) - 1;

            auto fg = text_.fgcolor();
            auto extent = gp.text_extent_size(string_);

            auto size = text_.size();
            point draw_pt(size.width - extent.width - singlechar_extent.width, (size.height - extent.height) / 2);
            gp.string(draw_pt, string_, fg);
        });

        events().destroy([&](arg_destroy const& d) {
            colapsed_or_subscribed_ = false;
            _perform_subscribe(true);
        });
    }

public:
    unsigned update_root_height(unsigned root = 0, bool colapse = false)
    {
        colapse ? hide() : show();

        root_height_ = root;
        auto height = root + !colapse * m.elem_height;
        for (auto& w : children_) {
            height = w->update_root_height(height, colapse || colapsed_or_subscribed_);
        }

        return height;
    }

    void relocate() { m.root->move(m.root->pos()); }

    void refresh_recursive()
    {
        refresh();
        for (auto& w : children_) { w->refresh_recursive(); }
    }

    void refresh()
    {
        using std::string;
        using namespace fmt;
        using insert = std::back_insert_iterator<string>;

        thread_local static string str_left;
        thread_local static string str_right;
        str_left.clear(), str_right.clear();

        if (is_timer_slot()) {
            auto& data = std::get<timer_data_desc>(slot_);
            using namespace std::chrono;

            format_to(insert(str_left), "{0:>{1}}{3} {2} ", "",
                      level * m.indent, data.name, colapsed_or_subscribed_ ? '=' : '|');
            format_to(insert(str_right), "{0:.4f} ms", duration<double, std::milli>{data.elapsed}.count());

            // timer color set
            const static nana::color category_colors[] = {
              colors::white,
              colors::light_gray,
              colors::light_blue,
              colors::sky_blue,
              color{111, 111, 161},
              colors::blue_violet,
              colors::light_pink,
              colors::light_yellow,
              colors::light_green,
              colors::lawn_green,
              colors::gray,
            };
            const size_t max_category = *(&category_colors + 1) - category_colors - 1;
            text_.fgcolor(
              is_obsolete()
                ? colors::dim_gray
                : category_colors[std::min<int>(max_category, level)]);

            text_.bgcolor(colapsed_or_subscribed_ ? colors(0x333333) : colors::black);
        } else if (is_debug_slot()) {
            auto& data = std::get<debug_data_desc>(slot_);
            format_to(insert(str_left), "{0:>{1}}[{2}]", "", level * m.indent, data.name);

            std::visit(
              [&]<typename T0>(T0&& arg) {
                  using type = std::decay_t<T0>;

                  if constexpr (std::is_same_v<std::any, type>)
                      str_right = arg.type().name();
                  else if constexpr (std::is_same_v<std::string, type>)
                      str_right = arg;
                  else if constexpr (std::is_same_v<bool, type>)
                      str_right = (arg ? "true" : "false");
                  else
                      str_right = std::to_string(arg);
              },
              data.data);

            text_.fgcolor(is_obsolete()
                            ? colors::dim_gray
                            : colapsed_or_subscribed_
                                ? colors::yellow
                                : std::get_if<std::any>(&data.data)
                                    ? colors::orange
                                    : std::get_if<std::string>(&data.data)
                                        ? color(204, 102, 0)
                                        : colors::green);
            text_.bgcolor(colapsed_or_subscribed_ ? colors::dark_green : colors::black);
        }

        if (str_right.size() + str_left.size() > text_extent_) {
            auto& longer = str_right.size() > str_left.size() ? str_right : str_left;
            auto& shorter = str_right.size() < str_left.size() ? str_right : str_left;

            longer.erase(std::clamp(text_extent_ - (int)shorter.size() - 3, 3, (int)longer.size()));
            longer.append("...");
            if (str_right.size() + str_left.size() > text_extent_) {
                shorter.erase(std::clamp(text_extent_ - (int)longer.size() - 3, 0, (int)shorter.size()));
                shorter.append("...");
            }
        }

        size_t n_dots = std::max<int>(0, text_extent_ - (int)str_left.size() - (int)str_right.size());
        string_.clear(), string_.reserve(text_extent_ + 10);
        format_to(insert(string_), "{2}{0:^{1}}{3}", "", n_dots, str_left, str_right);
        API::refresh_window(text_);
    }

    void make_obsolete()
    {
        sibling_order_ = npos;
        for (auto& w : children_) { w->make_obsolete(); }
    }

    bool is_obsolete() const { return sibling_order_ == npos; }
    void sibling_order(size_t v) { sibling_order_ = v; }

    void reorder()
    {
        using ptr_t = std::shared_ptr<inline_widget> const&;
        std::sort(children_.begin(), children_.end(), [](ptr_t a, ptr_t b) { return a->sibling_order_ < b->sibling_order_; });
        for (auto& w : children_) { w->reorder(); }
    }

    void width(unsigned v)
    {
        size({v, m.elem_height});
        for (auto& w : children_) { w->width(v); }
    }

    auto name()
    {
        std::string_view name;
        std::visit([&](auto&& v) { name = v.name; }, slot_);
        return name;
    }

    template <typename Dt_ = timer_data_desc>
    auto append(Dt_&& data)
    {
        assert(is_timer_slot());

        auto r = _create();
        r->slot_ = std::forward<Dt_>(data);

        // Category name build
        std::vector<std::string_view> cat_names;
        for (auto rt = root_.lock(); rt; rt = rt->root_.lock()) {
            cat_names.emplace_back(rt->name());
        }

        size_t sum = 0;
        for (auto& s : cat_names) { sum += s.size(); }
        r->category_.reserve(sum + 4 * cat_names.size() + 2);
        for (auto& s : cat_names) { r->category_.append(s), r->category_.append("::"); }

        return r;
    }

    template <typename Dt_ = timer_data_desc>
    void put(Dt_&& data)
    {
        slot_ = std::forward<Dt_>(data);
        _perform_subscribe();
    }

    bool is_timer_slot() const { return slot_.index() == 0; }
    bool is_debug_slot() const { return !is_timer_slot(); }

    std::shared_ptr<inline_widget> find_debug_slot(std::string_view s) const
    {
        for (auto& w : children_) {
            if (w->is_timer_slot()) { continue; }
            if (std::get<debug_data_desc>(w->slot_).name == s) { return w; }
        }

        return {};
    }

protected:
    void _m_move(int x, int y) override
    {
        API::move_window(handle(), {x, root_height_ + y});
        for (auto& w : children_) { w->move(x, y); }
    }

private:
    void _perform_subscribe(bool handle_unchecked = false)
    {
        if (is_timer_slot()) { return; }
        if (colapsed_or_subscribed_) {
            auto subscriber = m.board_ref->debug_data_subscriber;
            if (subscriber) {
                auto& data = std::get<debug_data_desc>(slot_);
                colapsed_or_subscribed_ = subscriber(category_, data);
            }
        } else if (handle_unchecked && !colapsed_or_subscribed_) {
            auto uncheck_handler = m.board_ref->debug_data_unchecked;
            if (uncheck_handler) {
                auto& data = std::get<debug_data_desc>(slot_);
                uncheck_handler(category_, data);
            }
        }
    }

    auto _create()
    {
        auto& r = children_.emplace_back(std::make_shared<inline_widget>(owner_, level + 1));
        r->root_ = weak_from_this();
        return r;
    }

public:
    size_t const id = ID_GEN++;
    unsigned const level;

private:
    debug_data_panel& owner_;
    debug_data_panel::data_type& m;

    place layout_{*this};
    panel<true> text_{*this};

    int root_height_ = 0;
    bool colapsed_or_subscribed_ = false;
    int text_extent_ = 0;

    size_t sibling_order_ = false;

    std::variant<timer_data_desc, debug_data_desc> slot_;
    std::string string_;
    std::string category_;

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

    auto font = DEFAULT_DATA_FONT;

    // -- Events assign
    events().resized([&](arg_resized const& s) {
        m.scroll.move(s.width - m.scroll_width, 0);
        m.scroll.size({m.scroll_width, s.height});

        _refresh_layout();
    });

    m.scroll.events().value_changed([&](arg_scroll const& arg) {
        int y_val = -(int)m.scroll.value();
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
    std::map<int, std::shared_ptr<inline_widget>> root_stack;
    root_stack[0] = m.root;

    auto& timers = data->timers;

    m.root->make_obsolete();
    m.root->sibling_order(0);
    m.root->put(timers[0]);
    m.root->refresh();
    for (auto index : kangsw::iota{(size_t)1, timers.size()}) {
        auto& tm = timers[index];

        // Category ID 기반으로 탐색 및 삽입 시도
        auto [it, is_new] = m.timers.try_emplace(tm.category_id);

        // 카테고리에 새로 삽입
        if (is_new) {
            auto root = root_stack.at(tm.category_level - 1);
            it->second = root->append(tm);
        } else {
            // 이미 존재하는 엔터티를 업데이트하는 경우, 동 레벨 카테고리의 이전 sibling을 방문,
            //
            it->second.lock()->put(tm);
        }

        auto ptr = it->second.lock();
        root_stack[tm.category_level] = ptr;
        // sibling_stack[tm.category_level + 1] = 0; // 다음 단계의 카테고리 인덱스 초기화
        ptr->sibling_order(tm.order); // 현재 단계의 카테고리 인덱스 증가
    }

    for (auto& dt : data->debug_data) {
        auto root_widget = m.timers.at(dt.category_id).lock();
        decltype(root_widget) widget = root_widget->find_debug_slot(dt.name);

        if (widget)
            widget->put(dt);
        else
            widget = root_widget->append(dt);

        widget->sibling_order(dt.order);
    }

    m.root->reorder();
    _refresh_layout();
}

void pipepp::gui::debug_data_panel::_refresh_layout()
{
    m.root->width(size().width - m.scroll_width);
    m.scroll.range(size().height);
    m.scroll.amount(m.root->update_root_height());
    m.scroll.step(m.elem_height);
    m.scroll.value(m.scroll.value());
    m.root->relocate();
    m.root->refresh_recursive();
}
