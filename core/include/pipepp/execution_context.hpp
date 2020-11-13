#pragma once
#include <any>
#include <shared_mutex>
#include <string>
#include <variant>
namespace pipepp {
namespace impl__ {
class option_base;
} // namespace impl__

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
 *
 * 내부에 두 개의 데이터 버퍼를 갖고 있으며,
 */
class execution_context {
public:
    using debug_data_element_type = std::variant<bool, long, double, std::string, std::any>;
    template <typename Ty_> using lock_type = std::unique_lock<Ty_>;

public:
    //
    void clear_records() {}

    // TODO: 디버그 플래그 제어
    // TODO: 디버그 데이터 저장(variant<bool, long, double, string, any> [])
    // TODO: 실행 시간 계측기

public:
    auto const& option() const { return *options_; }
    operator impl__::option_base const &() const { return *options_; }

public:
    void _internal__set_option(impl__::option_base const* opt) { options_ = opt; }

private:
    class impl__::option_base const* options_ = {};
};
} // namespace pipepp