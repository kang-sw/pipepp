#pragma once

namespace pipepp {
namespace impl__ {
class option_base;
}

/**
 * 실행 문맥 클래스.
 * 디버깅 및 모니터링을 위한 클래스로,
 *
 * 1) 디버그 플래그 제어
 * 2) 디버그 데이터 저장
 * 3) 구간별 계층적 실행 시간 계측
 * 4) 옵션에 대한 접근 제어(옵션 인스턴스는 pipe에 존재, 실행 문맥은 레퍼런스 읽기 전용)
 *
 * 등의 기능을 제공합니다.
 */
class execution_context {
public:
    void clear_records() {} // TODO implement

    // TODO: 디버그 플래그 제어
    // TODO: 디버그 데이터 저장(variant<bool, long, double, string, any> [])
    // TODO: 실행 시간 계측기

    class impl__::option_base* options_;
};
} // namespace pipepp