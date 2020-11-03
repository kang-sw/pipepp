#pragma once
#include <functional>
#include <memory>
#include <type_traits>

namespace pipepp {
namespace impl__ {
/**
 * @details
 * �α�, �������ϸ� ���� �Լ��� ���� \
 * �����̳ʿ� ��� ���� ���� �������̽�
 */
class pipe_base {
public:
    virtual ~pipe_base();
};
} // namespace impl__

/**
 * ������ ���� ����
 */
enum class pipe_error { ok, warning, error, fatal };

/**
 * ��� ������ Ŭ������ �⺻��.
 * ����ڴ� ������������
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