#pragma once
#include <atomic>
#include <memory>
#include <type_traits>

namespace pipepp {
/**
 * 파이프 에러 형식
 */
enum class pipe_error { ok, warning, error, fatal };

/**
 * 파이프 예외 형식
 */
class pipe_exception : public std::exception {
public:
    explicit pipe_exception(const char *msg) : exception(msg) {}
};

// clang-format off
/**
 * 파이프 상속 객체 콘셉트
 */
template <typename Pipe_>
concept Pipe = requires(Pipe_ pipe, typename Pipe_::input_type input, typename Pipe_::output_type output) {
    std::is_base_of_v<class pipe_base, Pipe_>;
    {pipe.exec_once(input, output, [](pipe_error, typename Pipe_::output_type const &) {})}
        ->std::convertible_to<pipe_error>;
};
// clang-format on

template <Pipe Pipe_>
class pipe;

namespace impl__ {
/**
 * @details
 * 로깅, 프로파일링 관련 함수성 제공 \
 * 컨테이너에 담기 위한 공통 인터페이스
 */
class pipe_base {
public:
    bool is_busy() const { return is_busy_; }

private:
    template <Pipe Pipe_>
    friend class pipe;

    // 내부 타이밍, 변수 목록 등의 파라미터를 리셋 및 스왑(디스플레이 준비)
    void reset_execution_states__();
    void swap_execution_states__();

public:
    virtual ~pipe_base() noexcept {}

private:
    std::atomic_bool is_busy_ = false;
};
} // namespace impl__

/**
 * 모든 파이프 클래스의 기본형.
 * 사용자는 파이프라인의 기능을 구현합니다.
 */
template <Pipe Pipe_>
class pipe : public impl__::pipe_base {
    static_assert(std::is_reference_v<Pipe_> == false);
    static_assert(std::is_base_of_v<pipe<Pipe_>, Pipe_> == false);

public:
    using pipe_type = Pipe_;
    using input_type = typename pipe_type::input_type;
    using output_type = typename pipe_type::output_type;

private:
    /**
     * 파이프라인을 한 번 실행합니다.
     * 항상 한 번에 한 스레드에서의 접근이 보장됩니다.
     */
    // virtual pipe_error exec_once(input_type const &in, output_type &out) = 0;

private:
    friend class pipe_proxy;
    // clang-format off
    template <typename Fn_>
    requires std::is_invocable_v<Fn_, pipe_error, output_type const &>
    void invoke__(input_type const &in, output_type &out, Fn_ &&handler) {
        // clang-format on
        if (is_busy()) {
            throw pipe_exception("Pipe should not be re-launched while running");
        }

        is_busy_ = true;
        {
            reset_execution_states__();

            auto result = static_cast<pipe_type *>(this)->exec_once(in, out);
            std::invoke(std::forward<Fn_>(handler), result, out);

            swap_execution_states__();
        }
        is_busy_ = false;
    }

public:
    virtual ~pipe() noexcept {
        if (is_busy()) {
            throw pipe_exception("Pipe must not be disposed while running");
        }
    }

private:
};
} // namespace pipepp