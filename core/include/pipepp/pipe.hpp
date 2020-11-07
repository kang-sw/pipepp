#pragma once
#include <atomic>
#include <concepts>
#include <memory>
#include <type_traits>

namespace pipepp {
/**
 * ������ ���� ����
 */
enum class pipe_error {
    ok,
    warning,
    error,
    fatal
};

/**
 * ������ ���� ����
 */
class pipe_exception : public std::exception {
public:
    explicit pipe_exception(const char* msg)
        : exception(msg)
    {}
};

namespace impl__ {
/**
 * ������ �⺻ Ŭ����
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
