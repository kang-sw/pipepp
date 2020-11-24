#pragma once
#include <memory>

#include "nana/gui/widgets/form.hpp"
#include "pipepp/execution_context.hpp"
#include "pipepp/options.hpp"

namespace nana {
struct arg_listbox;
}

namespace pipepp {
struct execution_context_data;
enum class pipe_id_t : unsigned long long;
namespace detail {
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
    void reset_pipe(std::weak_ptr<detail::pipeline_base> pl, pipe_id_t id);
    void update(std::shared_ptr<execution_context_data>);

public:
    size_t num_timer_text_view_horizontal_chars = 58;

private:
    struct data_type;
    std::unique_ptr<data_type> impl_;
};

} // namespace pipepp::gui
