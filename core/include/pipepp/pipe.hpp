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

// clang-format off
/**
 * ������ ��� ��ü �ܼ�Ʈ
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
 * �α�, �������ϸ� ���� �Լ��� ���� \
 * �����̳ʿ� ��� ���� ���� �������̽�
 */
class pipe_base {
public:
    bool is_busy() const { return is_busy_; }

private:
    template <Pipe Pipe_>
    friend class pipe;

    // ���� Ÿ�̹�, ���� ��� ���� �Ķ���͸� ���� �� ����(���÷��� �غ�)
    void reset_execution_states__();
    void swap_execution_states__();

public:
    virtual ~pipe_base() noexcept {}

private:
    std::atomic_bool is_busy_ = false;
};

// forward declaration of stage
template <Pipe Pipe_, typename SharedData_>
class stage;

} // namespace impl__

/**
 * ��� ������ Ŭ������ �⺻��.
 * ����ڴ� ������������ ����� �����մϴ�.
 */
template <Pipe Pipe_>
class pipe : public impl__::pipe_base {
    static_assert(std::is_reference_v<Pipe_> == false);
    static_assert(std::is_base_of_v<pipe<Pipe_>, Pipe_> == false);

    template <Pipe, typename>
    friend class impl__::stage;

public:
    using pipe_type = Pipe_;
    using input_type = typename pipe_type::input_type;
    using output_type = typename pipe_type::output_type;

private:
    /**
     * ������������ �� �� �����մϴ�.
     * �׻� �� ���� �� �����忡���� ������ ����˴ϴ�.
     */
    // virtual pipe_error exec_once(input_type const &in, output_type &out) = 0;

private:
    template <typename Fn_>
    requires std::is_invocable_v<Fn_, pipe_error, output_type const&> pipe_error invoke__(
      input_type const& in, output_type& out, Fn_&& handler = [](auto, auto) {})
    {
        // clang-format on
        if (is_busy()) {
            throw pipe_exception("Pipe should not be re-launched while running");
        }

        pipe_error result;

        is_busy_ = true;
        {
            reset_execution_states__();

            result = static_cast<pipe_type*>(this)->exec_once(in, out);
            std::invoke(std::forward<Fn_>(handler), result, out);

            swap_execution_states__();
        }
        is_busy_ = false;

        return result;
    }

public:
    virtual ~pipe() noexcept
    {
        if (is_busy()) {
            throw pipe_exception("Pipe must not be disposed while running");
        }
    }

private:
};
} // namespace pipepp