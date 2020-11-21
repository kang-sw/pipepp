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

    // 4. ������ �����ϰ� ������ ������ �Է��մϴ�.
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
