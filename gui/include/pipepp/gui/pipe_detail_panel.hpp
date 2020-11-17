#pragma once
#include <memory>

#include "nana/gui/widgets/form.hpp"

namespace pipepp {
struct execution_context_data;
enum class pipe_id_t : unsigned long long;
namespace impl__ {
class pipeline_base;
}
} // namespace pipepp

namespace pipepp::gui {
/**
 * ���� �������� �ɼ��� �� ����� �����͸� ǥ���մϴ�.
 */
class pipe_detail_panel : public nana::form {
    using super = nana::form;

public:
    pipe_detail_panel(nana::window owner, const nana::rectangle& rectangle, const nana::appearance& appearance);
    ~pipe_detail_panel();

public:
    void reset_pipe(std::weak_ptr<impl__::pipeline_base> pl, pipe_id_t id);
    void update(std::shared_ptr<execution_context_data>);

private:
    struct data_type;
    std::unique_ptr<data_type> impl_;
};

} // namespace pipepp::gui
