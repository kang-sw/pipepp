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
          // ��� ��� �������� iterate�ϰ�, conntection�� ����մϴ�.
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
                // ���� ������ �ִ� ������ ���� ������, �׸��� ó�� ������ ��쿡��
                //��ȿ�� ��ġ�� Ĩ�ϴ�.
                positions.emplace_back(id, nana::size{width_order, hierarchy});
            }
        }
    }
}

void pipepp::gui::pipeline_board::reset_pipeline(std::shared_ptr<pipepp::impl__::pipeline_base> pipeline)
{
    // TODO
    // 0. �̹� �����ϴ� pipe_view ��ü�� ��� �Ұ��մϴ�.
    // 1. pipeline�� ù pipe_proxy���� iterate�Ͽ�, ���� ������ �����մϴ�.
    // 2. ���� ������ �������� �� ������ ��ġ�� ����մϴ�.
    // 3. �� ������ �����ϴ� ���� ������ �����մϴ�.
    // 4. ��� ������ �����ϰ�, �����Ǵ� �������� �����մϴ�.
    auto& m = *impl_;

    // 0.
    _clear_views();

    m.pipe = pipeline;
    if (!pipeline) {
        return;
    }

    // 1. ���� ���� ��� ���
    //      1. ��Ʈ ���Ͻú��� �ڼ� ���Ͻ÷� iterate�� ����ġ�� ��� id�� hierarchy_occurences��
    //        �����Ǵ� �ε����� �ֽ��ϴ�. �ش� �迭�� �ε����� ���� ���̸� �ǹ��մϴ�.
    //       . ����, id ������ �����ϴ� �ִ� ���� ��ȣ�� �����մϴ�.
    //       . hierarchy_occurrences����, �ش� hierarchy index�� max_hierarchy�� ���� ����
    //        ��쿡�� �ν��Ͻ��� �����մϴ�.
    //      2. ������ ��, ���� ������ hierarchy�� ���̰� �˴ϴ�.
    //       . ���� ������ ���������� �Ʒ��� �������� ����� ���ϴµ�, ���� ������ ���̴�
    //        hierarchy_occurences���� ���� ���� �ش� id�� ��Ÿ���� �������� �����մϴ�.
    //        ��, ����� �ٿ� ���� ���� max_hierarchy�� ��ġ�ؾ� �մϴ�.
    //
    auto root_proxy = pipeline->get_first();
    using namespace std;

    unordered_map<pipe_id_t, pipe_id_t> connections;
    vector<tuple<pipe_id_t, nana::size>> positions;
    _calc_hierarchical_node_positions(root_proxy, connections, positions);

    // 3. TODO

    // 4. TODO
}
