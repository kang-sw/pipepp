#pragma once
#include "nana/basic_types.hpp"
#include "nana/gui/widgets/panel.hpp"
#include "pipepp/pipeline.hpp"

namespace nana {
class menu;
}

/**
 * Pipeline의 모든 파이프 노드를 표시하는 GUI 모듈의 최상위 클래스입니다.
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
 * Pipeline의 모든 파이프 노드를 표시하는 GUI 모듈의 최상위 클래스입니다.
 * 뷰포트 및 하단의 상태 바로 구성되어 있습니다.
 * 메뉴 바는 뷰포트에 embed 되진 않지만, 주 메뉴바에 attach 가능합니다.
 *
 * 뷰포트:
 *      파이프라인의 각 파이프를 연결 순서에 따라 (트리 형태로) 표시합니다.
 *      줌 및 스패닝 기능을 제공하여, 마우스로 드래그하거나 줌을 통해 일부만 가시화할 수 있습니다.
 *      줌 100% 일 때와 그 이하일 때 각각 이벤트를 트리거 하여, 요약 뷰와 상세 뷰 사이를 전환할 수 있습니다.
 *          요약 뷰: 파이프 이름, 상태 점등
 *          상세 뷰: 파이프 이름, 소요 시간, 상태 점등(idle: 초록, running: 빨강, suspended: 검정)
 *
 *
 *      파이프 뷰:
 *          파이프 정보를 요약합니다. 요약 뷰와 상세 뷰 두 가지 모드를 제공하며, 뷰포트의 줌 상태에 따라
 *           두 모드 사이에 전환이 일어납니다. 클릭 시 디테일 패널이 팝업되며, 파이프 옵션 설정, 디버그 데이터
 *           확인, 계측 시간 확인 등이 가능합니다.
 */
class pipeline_board : public nana::panel<true> {
public:
    pipeline_board();

    pipeline_board(const nana::window& wd, bool visible);

    pipeline_board(const nana::window& wd, const nana::rectangle& r, bool visible);

    /**
     * 메뉴에 기능을 연결합니다.
     */
    void build_menu(nana::menu&) const;

public:
    /**
     * 인터페이스 할 파이프라인을 지정합니다.
     */
    void reset_pipeline(std::weak_ptr<impl__::pipeline_base> pipeline);

    /**
     * 줌 수준 설정
     */
    double zoom() const;
    void zoom(double = 1.0);

    /**
     * 스팬 중심 설정
     */
    nana::point center() const;
    void center(nana::point = {});

private:
    std::unique_ptr<struct pipeline_board_data> impl_;
};

} // namespace pipepp::gui