#include "pipepp/gui/pipeline_board.hpp"
#include <iostream>
#include <set>

#include "nana/basic_types.hpp"
#include "nana/gui/drawing.hpp"
#include "nana/gui/widgets/label.hpp"
#include "nana/paint/graphics.hpp"
#include "pipepp/gui/basic_utility.hpp"
#include "pipepp/gui/pipe_view.hpp"
#include "pipepp/pipeline.hpp"

struct pipe_widget_desc {
    std::unique_ptr<nana::label> view;
    pipepp::pipe_id_t pipe_id;

    int slot_hierarchy_level;
    int slot_sibling_order;
};

struct line_desc {
    size_t index_begin;
    size_t index_end;
    bool is_optional_connection;
};

struct pipepp::gui::pipeline_board_data {
    pipeline_board& self;
    nana::drawing drawing{self};

    std::weak_ptr<impl__::pipeline_base> pipe;

    double zoom;
    nana::point center;

    std::vector<pipe_widget_desc> widgets;
    std::vector<nana::point> all_points;
    std::vector<line_desc> line_descriptions; // all_points
};

pipepp::gui::pipeline_board::pipeline_board()
    : pipeline_board({}, {}, false)
{
}

pipepp::gui::pipeline_board::pipeline_board(const nana::window& wd, bool visible)
    : pipeline_board(wd, {}, visible)
{
}

pipepp::gui::pipeline_board::pipeline_board(const nana::window& wd, const nana::rectangle& r, bool visible)
    : panel<true>(wd, r, visible)
    , impl_(std::make_unique<pipeline_board_data>(*this))
{
    auto& m = impl_;
}

pipepp::gui::pipeline_board::~pipeline_board() = default;

void pipepp::gui::pipeline_board::build_menu(nana::menu&) const
{
    // TODO
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
                position.width = width + (valid_output_index > 0);
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
    // TODO

    // 4. 위젯을 스폰하고 파이프 정보를 입력합니다.
    for (auto const& [id, slot] : positions) {
        auto& elem = m.widgets.emplace_back();
        elem.pipe_id = id;
        elem.slot_sibling_order = slot.width;
        elem.slot_hierarchy_level = slot.height;

        elem.view = std::make_unique<decltype(elem.view)::element_type>(*this, true);

        // TEST CODE
        if (true) {
            elem.view->typeface(nana::paint::font("consolas", 11.0));
            elem.view->move(elem.slot_hierarchy_level * 36, elem.slot_sibling_order * 20);
            elem.view->size({28, 16});
            elem.view->bgcolor(nana::colors::light_blue);
            elem.view->caption(pipeline->get_pipe(id).name());
        }
    }
}
