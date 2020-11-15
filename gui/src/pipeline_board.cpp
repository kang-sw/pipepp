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
    using namespace std;
    using impl__::pipe_proxy_base;
    set<pipe_id_t> visit_mask;
    auto recursive_build_tree
      = [&](auto& recall, pipe_proxy_base const& proxy, size_t width, size_t hierarchy) -> size_t {
        // post-order recursive ������ ���� ���� �귣ġ�� �������� ���մϴ�.
        size_t output_index = 0, num_output_link = proxy.num_output_nodes();
        for (size_t valid_output_index = 0; output_index < num_output_link; ++output_index) {
            auto output = proxy.get_output_node(output_index);
            auto& position = positions[output.id()];
            connections.emplace(proxy.id(), output.id());

            if (visit_mask.emplace(output.id()).second) {
                // ó�� ����ġ�� ��忡 ���ؼ��� ��������� Ž���� �����մϴ�.
                // ����� �ʺ� 1�� ��������� ������ŵ�ϴ�.
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

    // 1, 2.
    auto root_proxy = pipeline->get_first();
    using namespace std;

    unordered_multimap<pipe_id_t, pipe_id_t> connections;
    map<pipe_id_t, nana::size> positions;
    _calc_hierarchical_node_positions(root_proxy, connections, positions);

    // 3. ���� ������ ���� �������� ����ϴ�.
    //  . ����, ������ � ���� �� ������ ���� ���ɼ��� ���ο� �ΰ�, �� ��ϰ� ���� ������
    //   �и��Ͽ����ϴ�.
    // TODO

    // 4. ������ �����ϰ� ������ ������ �Է��մϴ�.
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
