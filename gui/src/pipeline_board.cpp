#include "pipepp/gui/pipeline_board.hpp"
#include <iostream>
#include <set>
#include <span>

#include "kangsw/zip.hxx"
#include "nana/basic_types.hpp"
#include "nana/gui/drawing.hpp"
#include "nana/gui/widgets/group.hpp"
#include "nana/paint/graphics.hpp"
#include "pipepp/gui/basic_utility.hpp"
#include "pipepp/gui/pipe_view.hpp"
#include "pipepp/pipeline.hpp"

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

struct pipepp::gui::pipeline_board::data_type {
    pipeline_board& self;
    nana::drawing drawing{self};

    std::weak_ptr<impl__::pipeline_base> pipe;

    double zoom;
    nana::point center;

    nana::size widget_default_size = {196, 100};
    nana::size widget_default_gap = {16, 36};

    std::vector<pipe_widget_desc> widgets;
    std::vector<nana::point> all_points;
    std::vector<line_desc> line_descriptions; // all_points
};

pipepp::gui::pipeline_board::pipeline_board(const nana::window& wd, const nana::rectangle& r, bool visible)
    : super(wd, r, visible)
    , impl_(std::make_unique<data_type>(*this))
{

    auto& m = impl_;

    events().mouse_move([&, prev_mouse_pos_ = nana::point{}](nana::arg_mouse const& arg) mutable {
        if (arg.left_button) {
            auto delta = arg.pos - prev_mouse_pos_;
            m->center += delta;
            _update_widget_pos();
            nana::drawing{*this}.update();
        }
        prev_mouse_pos_ = arg.pos;
    });

    nana::drawing{*this}.draw_diehard([&](nana::paint::graphics& gp) {
        std::span<nana::point> all_pts{m->all_points};
        auto offset = m->center;

        for (auto& ld : m->line_descriptions) {
            auto line_color = ld.is_optional_connection ? nana::colors::light_gray : nana::colors::green;
            auto points = all_pts.subspan(ld.index_offset + 1, ld.index_count - 1);
            auto begin_pt = all_pts[ld.index_offset] + offset;

            for (int ofst_y : kangsw::iota(-1, 2)) {
                gp.line_begin(begin_pt.x + ofst_y, begin_pt.y + ofst_y);
                auto ofst = nana::point{ofst_y, 0};
                for (auto& pt : points) { gp.line_to(pt + offset + ofst, line_color); }
            }
        }
    });
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
    nana::drawing{*this}.update();
}

nana::point pipepp::gui::pipeline_board::center() const
{
    return impl_->center;
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

void pipepp::gui::pipeline_board::_calc_hierarchical_node_positions(pipepp::impl__::pipe_proxy_base root_proxy, std::unordered_multimap<pipepp::pipe_id_t, pipepp::pipe_id_t>& connections, std::map<pipepp::pipe_id_t, nana::size>& positions)
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
    using impl__::pipe_proxy_base;
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
    for (auto& widget : impl_->widgets) {
        if (widget.view) {
            widget.view->bgcolor(color);
        }
    }
}

void pipepp::gui::pipeline_board::_m_typeface(const nana::paint::font& font)
{
    super::_m_typeface(font);
    for (auto& widget : impl_->widgets) {
        if (widget.view) {
            widget.view->typeface(font);
        }
    }
}

void pipepp::gui::pipeline_board::reset_pipeline(std::shared_ptr<pipepp::impl__::pipeline_base> pipeline)
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

        for (auto [idx_from, idx_to] : connections) {
            auto from = positions.at(idx_from);
            auto to = positions.at(idx_to);

            // TODO: Find if it's optional
            // auto proxy = pipeline->get_pipe(idx_to);

            nana::point l_dest[2];
            auto l_pos = {from, to};
            auto l_is_starting_pt = {true, false};
            for (auto [dest, pos, is_starting] :
                 kangsw::zip(l_dest, l_pos, l_is_starting_pt)) {
                dest.x = pos.width * gap.width + m.widget_default_size.width / 2;
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

        elem.view = std::make_unique<decltype(elem.view)::element_type>(*this, nana::rectangle{}, true);
        elem.view->reset_view(pipeline, id);
        elem.view->size(m.widget_default_size);

        elem.view->typeface({"consolas", 11.0});
        elem.view->bgcolor(bgcolor());
    }

    _update_widget_pos();
}
