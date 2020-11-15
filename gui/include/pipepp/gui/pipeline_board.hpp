#pragma once
#include "nana/basic_types.hpp"
#include "nana/gui/widgets/panel.hpp"
#include "pipepp/pipeline.hpp"

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
    pipeline_board();

    pipeline_board(const nana::window& wd, bool visible);

    pipeline_board(const nana::window& wd, const nana::rectangle& r, bool visible);

    /**
     * �޴��� ����� �����մϴ�.
     */
    void build_menu(nana::menu&) const;

public:
    /**
     * �������̽� �� ������������ �����մϴ�.
     */
    void reset_pipeline(std::weak_ptr<impl__::pipeline_base> pipeline);

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

private:
    std::unique_ptr<struct pipeline_board_data> impl_;
};

} // namespace pipepp::gui