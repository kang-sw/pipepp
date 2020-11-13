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
 * ���� ���� Ŭ����.
 * ����� �� ����͸��� ���� Ŭ������,
 *
 * 1) ����� �÷��� ����
 * 2) ����� ������ ����
 * 3) ������ ������ ���� �ð� ����
 * 4) �ɼǿ� ���� ���� ����(�ɼ� �ν��Ͻ��� pipe�� ����, ���� ������ ���۷��� �б� ����)
 *
 * ���� ����� �����մϴ�.
 *
 * ���ο� �� ���� ������ ���۸� ���� ������,
 */
class execution_context {
public:
    using debug_data_element_type = std::variant<bool, long, double, std::string, std::any>;
    template <typename Ty_> using lock_type = std::unique_lock<Ty_>;

public:
    //
    void clear_records() {}

    // TODO: ����� �÷��� ����
    // TODO: ����� ������ ����(variant<bool, long, double, string, any> [])
    // TODO: ���� �ð� ������

public:
    auto const& option() const { return *options_; }
    operator impl__::option_base const &() const { return *options_; }

public:
    void _internal__set_option(impl__::option_base const* opt) { options_ = opt; }

private:
    class impl__::option_base const* options_ = {};
};
} // namespace pipepp