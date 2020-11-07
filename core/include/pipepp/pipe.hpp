#pragma once
#include <atomic>
#include <concepts>
#include <memory>
#include <type_traits>

namespace pipepp {
/**
 * 파이프 에러 형식
 */
enum class pipe_error {
    ok,
    warning,
    error,
    fatal
};

/**
 * 파이프 예외 형식
 */
class pipe_exception : public std::exception {
public:
    explicit pipe_exception(const char* msg)
        : exception(msg)
    {}
};

namespace impl__ {
/**
 * 파이프 기본 클래스
 */
class pipe_base {
    friend class pipeline_base;

public:

private:
    void loop__();

private:
};

} // namespace impl__
} // namespace pipepp
