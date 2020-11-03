#pragma once
#include <functional>
#include <memory>
#include <type_traits>

namespace pipepp {
namespace impl__ {
/**
 * @details
 * 로깅, 프로파일링 관련 함수성 제공 \
 * 컨테이너에 담기 위한 공통 인터페이스
 */
class pipe_base {
public:
    virtual ~pipe_base();
};
} // namespace impl__

/**
 * 파이프 에러 형식
 */
enum class pipe_error { ok, warning, error, fatal };

/**
 * 모든 파이프 클래스의 기본형.
 * 사용자는 파이프라인의
 */
template <typename Pipe_>
class pipe : public impl__::pipe_base {
    static_assert(std::is_reference_v<Pipe_> == false);
    static_assert(std::is_base_of_v<pipe<Pipe_>, Pipe_> == false);

public:
    using pipe_type = Pipe_;
    using init_param_type = typename pipe_type::init_param_type;
    using input_type = typename pipe_type::input_type;
    using output_type = typename pipe_type::output_type;

private:
    virtual pipe_error exec_once(input_type const &in, output_type &out) = 0;
    virtual pipe_error initialize(init_param_type const &param) {
        return pipe_error::ok;
    };
};
} // namespace pipepp