#pragma once
#include "nana/basic_types.hpp"
#include "nana/gui/widgets/group.hpp"
#include "pipepp/pipeline.hpp"

namespace pipepp {
namespace impl__ {
class pipeline_base;
}
} // namespace pipepp

namespace nana {
class menu;
}

/**
 * Pipeline�� ��� ������ ��带 ǥ���ϴ� GUI ����� �ֻ��� Ŭ�����Դϴ�.
 *
 * +------------------+
 * |  [panel]         |     +---------------+
 * |  Pipeline Board  +---->| pipeline_base |
 * |                  |     +---------------+
 * +-+----------------+
 *   |
 *   | +-------------+                +------------------+
 *   | |  [panel]    |                |  [panel]         |         +-----------------+
 *   +>|  Pipe View  +<>-------+----->|  Pipe Zoom View  +------+->| pipe_proxy_base |
 *     |             |         |      |                  |      |  +-----------------+
 *     +-----+-------+         |      +------------------+      |  
 *           |                 |                                |
 *           |                 |                                |
 *           |                 |      +---------------------+   |
 *           |                 |      |  [panel]            |   |
 *           |                 +----->|  Pipe Summary View  +---+
 *           |                        |                     |
 *           |                        +---------------------+
 *           |
 *           |            +---------------------+
 *           |            |  [panel]            |
 *           +----------->|  Pipe Detail Panel  |
 *                        |                     |
 *                        +---------------------+
 */
namespace pipepp::gui {
/**
 * Pipeline�� ��� ������ ��带 ǥ���ϴ� GUI ����� �ֻ��� Ŭ�����Դϴ�.
 * ����Ʈ �� �ϴ��� ���� �ٷ� �����Ǿ� �ֽ��ϴ�.
 * �޴� �ٴ� ����Ʈ�� embed ���� ������, �� �޴��ٿ� attach �����մϴ�.
 *
 * ����Ʈ:
 *      ������������ �� �������� ���� ������ ���� (Ʈ�� ���·�) ǥ���մϴ�.
 *      �� �� ���д� ����� �����Ͽ�, ���콺�� �巡���ϰų� ���� ���� �Ϻθ� ����ȭ�� �� �ֽ��ϴ�.
 *      �� 100% �� ���� �� ������ �� ���� �̺�Ʈ�� Ʈ���� �Ͽ�, ��� ��� �� �� ���̸� ��ȯ�� �� �ֽ��ϴ�.
 *          ��� ��: ������ �̸�, ���� ����
 *          �� ��: ������ �̸�, �ҿ� �ð�, ���� ����(idle: �ʷ�, running: ����, suspended: ����)
 *
 *
 *      ������ ��:
 *          ������ ������ ����մϴ�. ��� ��� �� �� �� ���� ��带 �����ϸ�, ����Ʈ�� �� ���¿� ����
 *           �� ��� ���̿� ��ȯ�� �Ͼ�ϴ�. Ŭ�� �� ������ �г��� �˾��Ǹ�, ������ �ɼ� ����, ����� ������
 *           Ȯ��, ���� �ð� Ȯ�� ���� �����մϴ�.
 */
class pipeline_board : public nana::panel<true> {
public:
    using super = nana::panel<true>;

public:
    pipeline_board(const nana::window& wd, const nana::rectangle& r, bool visible);
    ~pipeline_board();

    /**
     * �޴��� ����� �����մϴ�.
     */
    void build_menu(nana::menu&) const;

public:
    /**
     * �������̽� �� ������������ �����մϴ�.
     */
    void reset_pipeline(std::shared_ptr<impl__::pipeline_base> pipeline);

    /**
     * �� ���� ����
     */
    double zoom() const;
    void zoom(double = 1.0);

    /**
     * ���� �߽� ����
     */
    nana::point center() const;
    void center(nana::point = {});

    /**
     * ������ ���� �⺻ ũ�� ����
     */
    nana::size pipe_widget_default_size() const;
    void pipe_widget_default_size(nana::size = {}) const;

    /**
     * ������ �⺻ ���� ����
     */
    nana::size pipe_widget_default_gap() const;
    void pipe_widget_default_gap(nana::size = {}) const;

private:
    void _clear_views();
    void _calc_hierarchical_node_positions(impl__::pipe_proxy_base root_proxy, std::unordered_multimap<pipepp::pipe_id_t, pipepp::pipe_id_t>& connections, std::map<pipepp::pipe_id_t, nana::size>& positions);

private:
    std::unique_ptr<struct pipeline_board_data> impl_;
};

} // namespace pipepp::gui