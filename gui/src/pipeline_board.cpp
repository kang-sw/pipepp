#include "pipepp/gui/pipeline_board.hpp"
#include <set>

#include "pipepp/gui/basic_utility.hpp"
#include "pipepp/gui/pipe_view.hpp"
#include "pipepp/pipeline.hpp"

struct pipe_widget_desc {
    std::unique_ptr<pipepp::gui::pipe_view> view;
    pipepp::pipe_id_t pipe_id;

    nana::point base_location;
    nana::size base_size;
};

struct line_desc {
    size_t begin;
    size_t end;
    bool is_dashed;
};

struct pipepp::gui::pipeline_board_data {
    pipeline_board& self;
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
}

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

void pipepp::gui::pipeline_board::_calc_hierarchical_node_positions(pipepp::impl__::pipe_proxy_base root_proxy, std::unordered_map<pipepp::pipe_id_t, pipepp::pipe_id_t>& connections, std::vector<std::tuple<pipepp::pipe_id_t, nana::size>>& positions)
{
    using namespace std;
    vector<vector<pipe_id_t>> hierarchy_occurences;
    map<pipe_id_t, size_t> max_hierarchy;

    kangsw::recurse_for_each(
      std::move(root_proxy),
      [&](impl__::pipe_proxy_base p, auto append) {
          // 모든 출력 파이프를 iterate하고, conntection을 기록합니다.
          for (size_t i = 0, end = p.num_output_nodes(); i < end; ++i) {
              auto next = p.get_output_node(i);
              append(p.get_output_node(i));
              connections.emplace(p.id(), next.id());
          }
      },
      [&](impl__::pipe_proxy_base const& p, size_t hierarchy) {
          if (hierarchy_occurences.size() <= hierarchy) {
              hierarchy_occurences.resize(hierarchy + 1);
          }
          hierarchy_occurences[hierarchy].push_back(p.id());
          auto& max_h = max_hierarchy[p.id()];
          max_h = std::max(max_h, hierarchy);
      });

    set<pipe_id_t> occurence_mask;
    for (int hierarchy = hierarchy_occurences.size() - 1; hierarchy >= 0; --hierarchy) {
        auto& layer = hierarchy_occurences[hierarchy];

        for (size_t counter = 0; auto id : layer) {
            auto width_order = counter++;
            if (max_hierarchy.at(id) == hierarchy && occurence_mask.emplace(id).second) {
                // 등장 계층과 최대 계층이 같을 때에만, 그리고 처음 등장한 경우에만
                //유효한 위치로 칩니다.
                positions.emplace_back(id, nana::size{width_order, hierarchy});
            }
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
    auto root_proxy = pipeline->get_first();
    using namespace std;

    unordered_map<pipe_id_t, pipe_id_t> connections;
    vector<tuple<pipe_id_t, nana::size>> positions;
    _calc_hierarchical_node_positions(root_proxy, connections, positions);

    // 3. TODO

    // 4. TODO
}
