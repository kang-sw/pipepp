#pragma once

namespace pipepp::impl__ {
class executor_option_base {
};
} // namespace pipepp::impl__

namespace pipepp {
template <typename Ty_>
class executor_option : public impl__::executor_option_base {
    // static variable container ~~
    // execution class ������ template Ư��ȭ�� ���� static option table�� initialize
};

} // namespace pipepp
