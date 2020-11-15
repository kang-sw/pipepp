#pragma once
#include "nana/basic_types.hpp"
#include "nana/gui/widgets/panel.hpp"
#include "pipepp/pipeline.hpp"

namespace pipepp::gui {
/**
 * ������ �ϳ��� ǥ���մϴ�.
 *
 * �⺻ ũ��� 640x320�̸�, �� ���ؿ� ���� ��� ��/�� �� ���̸� ��ȯ�մϴ�.
 */
class pipe_view : public nana::panel<true> {
public:
    pipe_view();
    pipe_view(const nana::window& wd, bool visible);
    pipe_view(const nana::window& wd, const nana::rectangle& r, bool visible);

public:
    /**
     * ������ ���Ͻ÷� ���� �並 �����մϴ�.
     */
    void reset_view(std::weak_ptr<impl__::pipeline_base> pipeline, pipe_id_t pipe);

    /**
     * ������ �並 ǥ���մϴ�.
     */
    void display_view(bool is_detail_view);

    /**
     * ������ ����͸� �г��� ���ϴ�.
     */

    /**
     * ������ ����͸� �г��� �����ִٸ�, �ݽ��ϴ�.
     */
};
} // namespace pipepp::gui
