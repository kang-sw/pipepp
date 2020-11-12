#pragma once

namespace pipepp {
namespace impl__ {
class executor_option_base;
}

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
 */
class execution_context {
public:
    void clear_records() {} // TODO implement

    // TODO: ����� �÷��� ����
    // TODO: ����� ������ ����(variant<bool, long, double, string, any> [])
    // TODO: ���� �ð� ������

    class impl__::executor_option_base* options_;
};
} // namespace pipepp