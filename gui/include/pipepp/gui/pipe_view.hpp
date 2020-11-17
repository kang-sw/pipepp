#pragma once
#include <memory>

#include "nana/basic_types.hpp"
#include "nana/gui/widgets/panel.hpp"
#include "pipepp/pipeline.hpp"

namespace pipepp::gui {
/**
 * ������ �ϳ��� ǥ���մϴ�.
 *
 * �⺻ ũ��� 640x320�̸�, �� ���ؿ� ���� ��� ��/�� �� ���̸� ��ȯ�մϴ�.
 */
class pipe_view : public nana::panel<false>, public std::enable_shared_from_this<pipe_view> {
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
     * �� ������Ʈ
     */
    void update();

    /**
     * ������ �並 ǥ���մϴ�.
     */
    void display_view(bool is_detail_view);

    /**
     * ������ ����͸� �г��� ȹ���մϴ�.
     * open_details()�� ���� ������ ��쿡�� ���۷����� ��ȯ�մϴ�. 
     */
    std::shared_ptr<class pipe_detail_panel> details() const;

    /**
     * ������ ����͸� �г��� ���ϴ�.
     * �̹� �� ���, �Ű� ������ ���õǸ� ������ ���۷����� ��ȯ�մϴ�.
     */
    void open_details(const nana::window& wd = {});

    /**
     * ������ ����͸� �г��� �����ִٸ�, �ݽ��ϴ�.
     */
    void close_details();

protected:
    void _m_caption(native_string_type&&) override;
    void _m_bgcolor(const nana::color&) override;
    void _m_typeface(const nana::paint::font& font) override;

private:
    struct data_type;
    std::unique_ptr<struct data_type> impl_;
};
} // namespace pipepp::gui
