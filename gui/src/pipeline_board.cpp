#include "pipepp/gui/pipeline_board.hpp"
#include <iostream>
#include <set>
#include <span>

#include "kangsw/helpers/zip.hxx"
#include "nana/basic_types.hpp"
#include "nana/gui/drawing.hpp"
#include "nana/gui/timer.hpp"
#include "nana/gui/widgets/group.hpp"
#include "nana/gui/widgets/label.hpp"
#include "nana/gui/widgets/tabbar.hpp"
#include "nana/paint/graphics.hpp"
#include "pipepp/gui/basic_utility.hpp"
#include "pipepp/gui/pipe_detail_panel.hpp"
#include "pipepp/gui/pipe_view.hpp"
#include "pipepp/pipeline.hpp"

using namespace std::literals;

struct pipe_widget_desc {
    std::unique_ptr<pipepp::gui::pipe_view> view;
    pipepp::pipe_id_t pipe_id;

    int slot_hierarchy_level;
    int slot_sibling_order;
};

struct line_desc {
    size_t index_offset;
    size_t index_count;
    bool is_optional_connection;
};

class trivial_subscriber_label : public nana::panel<true> {
public:
    explicit trivial_subscriber_label(nana::window par)
        : panel(par, true)
    {
        layout_.div("<NAME><VALUE> <margin=3 weight=20 <vert weight=20 <EXIT> > >");
        layout_["NAME"] << name_;
        layout_["VALUE"] << value_;
        layout_["EXIT"] << exit_;

        exit_.caption("-");
        exit_.transparent(true);

        nana::paint::font fnt("consolas", 11.0);
        name_.typeface(fnt);
        value_.typeface(fnt);
        exit_.typeface(fnt);
    }

    void set_name(std::string const& n) { name_.caption(n); }
    void set_value(std::string const& n) { value_.caption(n); }

    auto& on_exit() { return exit_.events().click; }

private:
    nana::place layout_{*this};
    nana::label name_{*this};
    nana::label value_{*this};
    nana::button exit_{*this};
};

struct tabbar_entity {
    pipepp::gui::pipe_view* view = nullptr;
    pipepp::gui::pipe_detail_panel* panel = nullptr;

    ~tabbar_entity()
    {
        if (view) {
            view->close_details();
        }
    }
};

struct pipepp::gui::pipeline_board::data_type {
    data_type(pipeline_board& b)
        : self(b)
    {}

    pipeline_board& self;
    nana::drawing drawing{self};

    std::weak_ptr<detail::pipeline_base> pipe;

    double zoom = 0;
    nana::point center;

    nana::size widget_default_size = {196, 100};
    nana::size widget_default_gap = {16, 36};

    std::vector<pipe_widget_desc> widgets;
    std::vector<nana::point> all_points;
    std::vector<line_desc> line_descriptions; // all_points

    std::map<std::string, std::unique_ptr<trivial_subscriber_label>> subscriptions;

    nana::panel<true> graph{nana::window{self}};
    nana::panel<false> tabwnd_placeholder{nana::window{self}};
    nana::button width_sizer{self};
    nana::timer destroy_update_timer{16ms};
    bool tabbar_visible = false;
    float tabbar_rate = 0.5f;

    nana::tabbar<std::shared_ptr<tabbar_entity>> detail_panel_tab{self};

    nana::place layout{self};

public:
    std::string divtext()
    {
        constexpr auto DIV_notab = "<GRAPH><SIZER weight=15>";
        constexpr auto DIV_tab = "<GRAPH arrange=[variable,16,repeated]>"
                                 "<vert weight=15 <SIZER>>"
                                 "<vert weight={0} <TABBAR weight=24 margin=[0, 0, 0, 0]><TABWND>>";
        if (tabbar_visible) {
            _cache.clear();
            _cache.reserve(64);

            int tabbar_weight = tabbar_rate * self.size().width;
            std::format_to(std::back_inserter(_cache), DIV_tab, tabbar_weight);
        } else {
            return DIV_notab;
        }
        return std::move(_cache);
    }

    void update_tabbar()
    {

        layout.div(divtext());
        layout.collocate();
    }

    void destroy_update()
    {
        for (size_t i = 0; i < detail_panel_tab.length(); ++i) {
            auto& ptr = detail_panel_tab.at(i);
            if (ptr->view == nullptr) {
                detail_panel_tab.erase(i);
                break;
            }
        }
    }

    void subscribe_update_notify(data_subscribe_arg const& arg)
    {

    }

    void unsubscribe_notify(data_subscribe_arg const& arg)
    {
    }

private:
    std::string _cache;
};

pipepp::gui::pipeline_board::pipeline_board(const nana::window& wd, const nana::rectangle& r, bool visible)
    : super(wd, r, visible)
    , impl_(std::make_unique<data_type>(*this))
{

    auto& m = *impl_;

    m.graph.events().mouse_move([&, prev_mouse_pos_ = nana::point{}](nana::arg_mouse const& arg) mutable {
        if (arg.left_button) {
            auto delta = arg.pos - prev_mouse_pos_;
            m.center += delta;
            _update_widget_pos();
            nana::drawing{m.graph}.update();
        }
        prev_mouse_pos_ = arg.pos;
    });

    nana::drawing{m.graph}.draw_diehard([&](nana::paint::graphics& gp) {
        std::span<nana::point> all_pts{m.all_points};
        auto offset = m.center;

        for (auto& ld : m.line_descriptions) {
            auto line_color = ld.is_optional_connection ? optional_connection_line_color : main_connection_line_color;
            auto points = all_pts.subspan(ld.index_offset + 1, ld.index_count - 1);
            auto begin_pt = all_pts[ld.index_offset] + offset;

            for (int ofst_y : kangsw::iota(0, 1)) {
                gp.line_begin(begin_pt.x + ofst_y, begin_pt.y + ofst_y);
                auto ofst = nana::point{ofst_y, 0};
                for (auto& pt : points) { gp.line_to(pt + offset + ofst, line_color); }
            }
        }
    });

    events().destroy([&m](auto&) {
        for (size_t i = 0; i < m.detail_panel_tab.length(); ++i) {
            m.detail_panel_tab.at(i)->view = nullptr;
        }
    });

    events().resized([&m, this](nana::arg_resized arg) {
        auto x = size().width;
        m.update_tabbar();
    });

    m.layout["GRAPH"] << m.graph;
    m.layout["SIZER"] << m.width_sizer;
    m.layout["TABBAR"] << m.detail_panel_tab;
    m.layout["TABWND"] << m.tabwnd_placeholder;

    m.width_sizer.edge_effects(false);
    m.width_sizer.caption("||");
    m.width_sizer.borderless(true);
    m.width_sizer.cursor(nana::cursor::size_we);

    m.width_sizer.events().mouse_move([this, &m, prv = nana::point{}](nana::arg_mouse const& arg) mutable {
        auto cursor_pos = m.width_sizer.pos() + arg.pos;
        if (arg.left_button) {
            auto delta = cursor_pos - prv;
            auto tabbar_weight = m.tabbar_rate * size().width;
            tabbar_weight = std::clamp<int>(tabbar_weight - delta.x, 55, size().width - 40);
            m.tabbar_rate = tabbar_weight / size().width;

            m.layout.div(m.divtext());
            m.layout.collocate();
        }
        prv = cursor_pos;
    });

    m.width_sizer.events().dbl_click([this, &m](auto) {
        m.tabbar_visible = !m.tabbar_visible;
        m.update_tabbar();

        if (!m.tabbar_visible) {
            for (size_t i = 0; i < m.detail_panel_tab.length(); ++i) {
                auto& arg = *m.detail_panel_tab.at(i);
                if (arg.view) { arg.panel->hide(); }
            }
        } else if (m.detail_panel_tab.length()) {
            m.detail_panel_tab.at(m.detail_panel_tab.activated())->panel->show();
        }
    });

    m.tabwnd_placeholder.events().resized([&m](nana::arg_resized rz) {
        nana::rectangle rt{m.tabwnd_placeholder.pos(), m.tabwnd_placeholder.size()};
        for (size_t i = 0; i < m.detail_panel_tab.length(); ++i) {
            m.detail_panel_tab.at(i)->panel->move(rt);
        }
    });

    m.detail_panel_tab.events().tab_click([&m](auto) { m.update_tabbar(); });
    m.detail_panel_tab.events().activated([&m](auto) {
        m.update_tabbar();
        for (size_t i = 0; i < m.detail_panel_tab.length(); ++i) {
            bool is_active = i == m.detail_panel_tab.activated();
            m.detail_panel_tab.tab_bgcolor(i, is_active ? nana::colors::white : nana::colors::gray);
        }
    });

    m.detail_panel_tab.events().removed([&m](auto) {
        m.tabbar_visible &= m.detail_panel_tab.length() > 1;
        m.update_tabbar();
    });

    m.layout.div(m.divtext());
    m.layout.collocate();

    using kit_t = decltype(m.detail_panel_tab)::kits;
    m.detail_panel_tab.toolbox(kit_t::scroll, true);
    m.detail_panel_tab.toolbox(kit_t::list, true);
    m.detail_panel_tab.toolbox(kit_t::close, true);

    m.detail_panel_tab.close_fly(true);
    m.destroy_update_timer.elapse([&]() { m.destroy_update(), m.destroy_update_timer.stop(); });

    typeface({"consolas", 11.0});

    debug_data_subscriber(std::bind(&data_type::subscribe_update_notify, &m, std::placeholders::_1));
    debug_data_unchecked(std::bind(&data_type::unsubscribe_notify, &m, std::placeholders::_1));
}

pipepp::gui::pipeline_board::~pipeline_board() = default;

void pipepp::gui::pipeline_board::build_menu(nana::menu&) const
{
    // TODO
}

void pipepp::gui::pipeline_board::update()
{
    for (auto& widget : impl_->widgets) {
        if (widget.view) {
            widget.view->update();
        }
    }
}

void pipepp::gui::pipeline_board::center(nana::point center)
{
    impl_->center = center;
    _update_widget_pos();
    nana::drawing{impl_->graph}.update();
}

nana::point pipepp::gui::pipeline_board::center() const
{
    return impl_->center;
}

nana::panel<true>& pipepp::gui::pipeline_board::graph_panel()
{
    return impl_->graph;
}

void pipepp::gui::pipeline_board::_clear_views()
{
    auto& m = *impl_;

    if (m.widgets.empty() == false) {
        for (auto& wg : m.widgets) {
            wg.view.reset();
        }
    }
    m.line_descriptions.clear();
    m.all_points.clear();
}

void pipepp::gui::pipeline_board::_calc_hierarchical_node_positions(pipepp::detail::pipe_proxy_base root_proxy, std::unordered_multimap<pipepp::pipe_id_t, pipepp::pipe_id_t>& connections, std::map<pipepp::pipe_id_t, nana::size>& positions)
{
    // 1. 계층 구조 계산 방식
    //      1. 루트 프록시부터 자손 프록시로 iterate해 마주치는 모든 id를 hierarchy_occurences의
    //        대응되는 인덱스에 넣습니다. 해당 배열의 인덱스는 계층 높이를 의미합니다.
    //       . 또한, id 각각이 등장하는 최대 계층 번호를 지정합니다.
    //       . hierarchy_occurrences에서, 해당 hierarchy index와 max_hierarchy의 값이 같은
    //        경우에만 인스턴스가 존재합니다.
    //      2. 렌더링 시, 수평 방향이 hierarchy의 깊이가 됩니다.
    //       . 수직 방향은 위에서부터 아래로 내려오는 방식을 취하는데, 수직 방향의 높이는
    //        hierarchy_occurences에서 가장 먼저 해당 id가 나타나는 지점으로 설정합니다.
    //        단, 상기한 바와 같이 먼저 max_hierarchy가 일치해야 합니다.
    //
    using namespace std;
    using detail::pipe_proxy_base;
    set<pipe_id_t> visit_mask;
    auto recursive_build_tree
      = [&](auto& recall, pipe_proxy_base const& proxy, size_t width, size_t hierarchy) -> size_t {
        // post-order recursive 연산을 통해 다음 브랜치의 오프셋을 구합니다.
        size_t output_index = 0, num_output_link = proxy.num_output_nodes();
        for (size_t valid_output_index = 0; output_index < num_output_link; ++output_index) {
            auto output = proxy.get_output_node(output_index);
            auto& position = positions[output.id()];
            connections.emplace(proxy.id(), output.id());

            if (visit_mask.emplace(output.id()).second) {
                // 처음 마주치는 노드에 대해서만 재귀적으로 탐색을 수행합니다.
                // 노드의 너비를 1씩 재귀적으로 증가시킵니다.
                position.width = (int)width + (valid_output_index > 0);
                width = recall(recall, output, width + (valid_output_index > 0), hierarchy + 1);
                ++valid_output_index;
            }
            position.height = (int)max<size_t>(position.height, hierarchy + 1);
        }

        return width;
    };

    positions[root_proxy.id()] = {};
    recursive_build_tree(recursive_build_tree, root_proxy, 0, 0);
}

void pipepp::gui::pipeline_board::_update_widget_pos()
{
    auto& m = *impl_;
    for (auto& elem : m.widgets) {
        auto gap = m.widget_default_size + m.widget_default_gap;
        auto center = m.center;
        auto x = center.x + elem.slot_sibling_order * gap.width;
        auto y = center.y + elem.slot_hierarchy_level * gap.height;
        elem.view->move(x, y);
    }
}

void pipepp::gui::pipeline_board::_m_bgcolor(const nana::color& color)
{
    super::_m_bgcolor(color);
    impl_->graph.bgcolor(color);
    impl_->detail_panel_tab.bgcolor(color);
    for (auto& widget : impl_->widgets) {
        if (widget.view) {
            widget.view->bgcolor(color);
        }
    }
}

void pipepp::gui::pipeline_board::_m_typeface(const nana::paint::font& font)
{
    super::_m_typeface(font);
    impl_->graph.typeface(font);
    impl_->detail_panel_tab.typeface(font);
    for (auto& widget : impl_->widgets) {
        if (widget.view) {
            widget.view->typeface(font);
        }
    }
}

void pipepp::gui::pipeline_board::reset_pipeline(std::shared_ptr<pipepp::detail::pipeline_base> pipeline)
{
    // TODO
    // 0. 이미 존재하는 pipe_view 개체를 모두 소거합니다.
    // 1. pipeline의 첫 pipe_proxy부터 iterate하여, 계층 정보를 수집합니다.
    // 2. 계층 정보를 바탕으로 각 위젯의 위치를 계산합니다.
    // 3. 각 위젯을 연결하는 직선 정보를 빌드합니다.
    // 4. 모든 위젯을 스폰하고, 대응되는 파이프를 공급합니다.
    auto& m = *impl_;

    // 0.
    _clear_views();

    m.pipe = pipeline;
    if (!pipeline) {
        return;
    }

    // 1, 2.
    auto root_proxy = pipeline->get_first();
    using namespace std;

    unordered_multimap<pipe_id_t, pipe_id_t> connections;
    map<pipe_id_t, nana::size> positions;
    _calc_hierarchical_node_positions(root_proxy, connections, positions);

    // 3. 연결 정보를 직선 집합으로 만듭니다.
    //  . 추후, 복잡한 곡선 등의 점 정보를 만들 가능성을 염두에 두고, 점 목록과 직선 정보를
    //   분리하였습니다.
    {
        auto gap = m.widget_default_size + m.widget_default_gap;
        map<pipe_id_t, int> slot_order_from;
        map<pipe_id_t, int> slot_order_to;

        for (auto [idx_from, idx_to] : connections) {
            auto from = positions.at(idx_from);
            auto to = positions.at(idx_to);

            // TODO: Find if it's optional
            // auto proxy = pipeline->get_pipe(idx_to);

            nana::point l_dest[2];
            auto l_pos = {from, to};
            auto l_is_starting_pt = {true, false};
            auto l_offset = {slot_order_from[idx_from]++, slot_order_to[idx_from]++};
            for (auto [dest, pos, is_starting, offset] :
                 kangsw::zip(l_dest, l_pos, l_is_starting_pt, l_offset)) {
                dest.x = pos.width * gap.width + m.widget_default_size.width / 2 + offset * 4;
                dest.y = pos.height * gap.height + is_starting * m.widget_default_size.height;
            }

            size_t indices[] = {m.all_points.size(), 2};
            m.all_points.insert(m.all_points.end(), std::begin(l_dest), std::end(l_dest));
            m.line_descriptions.push_back(
              line_desc{.index_offset = indices[0], .index_count = indices[1], .is_optional_connection = pipeline->get_pipe(idx_to).is_optional()});
        }
    }

    // 4. 위젯을 스폰하고 파이프 정보를 입력합니다.
    for (auto const& [id, slot] : positions) {
        auto& elem = m.widgets.emplace_back();
        elem.pipe_id = id;
        elem.slot_sibling_order = slot.width;
        elem.slot_hierarchy_level = slot.height;

        elem.view = std::make_unique<decltype(elem.view)::element_type>(m.graph, nana::rectangle{}, true);
        elem.view->reset_view(pipeline, id);
        elem.view->size(m.widget_default_size);

        elem.view->typeface({"consolas", 11.0});
        elem.view->bgcolor(bgcolor());

        // 디테일 뷰 띄우기
        elem.view->btn_events().click([this, view = elem.view.get(), &m](auto) {
            //if (!view->details()) {
            //    view->open_details(*view);
            //} else {
            //    view->close_details();
            //}
            if (!view->details()) {
                auto details = view->create_panel(*this).lock();
                m.detail_panel_tab.append(details->caption(), *details, std::make_shared<tabbar_entity>(view, details.get()));
                details->move(nana::rectangle{m.tabwnd_placeholder.pos(), m.tabwnd_placeholder.size()});

                details->events().destroy([&](auto) {
                    for (size_t i = 0; i < m.detail_panel_tab.length(); ++i) {
                        auto& ptr = m.detail_panel_tab.at(i);
                        if (ptr->view == view) {
                            ptr->view = nullptr;
                            break;
                        }
                    }
                    m.destroy_update_timer.start();
                });
            } else {
                for (size_t i = 0; i < m.detail_panel_tab.length(); ++i) {
                    if (m.detail_panel_tab.at(i)->view == view) {
                        m.detail_panel_tab.activated(i);
                        m.detail_panel_tab.at(i)->panel->show();
                        break;
                    }
                }
            }

            m.tabbar_visible = true;
            m.update_tabbar();
        });
    }

    _update_widget_pos();
}
