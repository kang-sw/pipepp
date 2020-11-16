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
class pipe_view : public nana::panel<false> {
public:
    using super = nana::panel<false>;

public:
    pipe_view(const nana::window& wd, const nana::rectangle& r, bool visible);
    ~pipe_view();

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

protected:
    void _m_caption(native_string_type&&) override;
    void _m_bgcolor(const nana::color&) override;
    void _m_typeface(const nana::paint::font& font) override;

private:
    std::unique_ptr<struct pipe_view_data> impl_;
};
} // namespace pipepp::gui
