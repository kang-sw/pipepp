#pragma once
#include <memory>

#include "nana/basic_types.hpp"
#include "nana/gui/widgets/panel.hpp"
#include "pipepp/pipeline.hpp"

namespace pipepp::gui {
/**
 * 파이프 하나를 표시합니다.
 *
 * 기본 크기는 640x320이며, 줌 수준에 따라 요약 뷰/상세 뷰 사이를 전환합니다.
 */
class pipe_view : public nana::panel<false>, public std::enable_shared_from_this<pipe_view> {
public:
    using super = nana::panel<false>;

public:
    pipe_view(const nana::window& wd, const nana::rectangle& r, bool visible);
    ~pipe_view();

public:
    /**
     * 파이프 프록시로 위젯 뷰를 리셋합니다.
     */
    void reset_view(std::weak_ptr<impl__::pipeline_base> pipeline, pipe_id_t pipe);

    /**
     * 뷰 업데이트
     */
    void update();

    /**
     * 위젯에 뷰를 표시합니다.
     */
    void display_view(bool is_detail_view);

    /**
     * 파이프 모니터링 패널을 획득합니다.
     * open_details()를 통해 생성된 경우에만 레퍼런스를 반환합니다. 
     */
    std::shared_ptr<class pipe_detail_panel> details() const;

    /**
     * 파이프 모니터링 패널을 엽니다.
     * 이미 연 경우, 매개 변수는 무시되며 기존의 레퍼런스를 반환합니다.
     */
    void open_details(const nana::window& wd = {});

    /**
     * 파이프 모니터링 패널이 열려있다면, 닫습니다.
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
