#pragma once
#include "nana/basic_types.hpp"
#include "nana/gui/widgets/panel.hpp"
#include "pipepp/pipeline.hpp"

namespace pipepp::gui {
/**
 * 파이프 하나를 표시합니다.
 *
 * 기본 크기는 640x320이며, 줌 수준에 따라 요약 뷰/상세 뷰 사이를 전환합니다.
 */
class pipe_view : public nana::panel<true> {
public:
    pipe_view();
    pipe_view(const nana::window& wd, bool visible);
    pipe_view(const nana::window& wd, const nana::rectangle& r, bool visible);

public:
    /**
     * 파이프 프록시로 위젯 뷰를 리셋합니다.
     */
    void reset_view(std::weak_ptr<impl__::pipeline_base> pipeline, pipe_id_t pipe);

    /**
     * 위젯에 뷰를 표시합니다.
     */
    void display_view(bool is_detail_view);

    /**
     * 파이프 모니터링 패널을 엽니다.
     */

    /**
     * 파이프 모니터링 패널이 열려있다면, 닫습니다.
     */
};
} // namespace pipepp::gui
