#include "pipepp/gui/pipe_view.hpp"

#include "nana/gui/place.hpp"
#include "nana/gui/widgets/button.hpp"
#include "nana/gui/widgets/label.hpp"
#include "pipepp/pipeline.hpp"

struct pipepp::gui::pipe_view_data {
    pipe_view& self;

    std::weak_ptr<impl__::pipeline_base> pipeline;
    pipe_id_t pipe;

    nana::place layout{self};
    nana::button label{self};
};

pipepp::gui::pipe_view::pipe_view(const nana::window& wd, const nana::rectangle& r, bool visible)
    : super(wd, r, visible)
    , impl_(std::make_unique<pipe_view_data>(*this))
{
    auto& m = *impl_;

    m.layout.div("<MAIN>");
    m.layout["MAIN"] << m.label;
}

pipepp::gui::pipe_view::~pipe_view() = default;

void pipepp::gui::pipe_view::reset_view(std::weak_ptr<impl__::pipeline_base> pipeline, pipe_id_t pipe)
{
    auto& m = *impl_;
    m.pipeline = pipeline;
    m.pipe = pipe;

    auto pl = m.pipeline.lock();
    if (pl == nullptr) { return; }

    auto proxy = pl->get_pipe(m.pipe);
    m.label.caption(proxy.name());
}

void pipepp::gui::pipe_view::update()
{
    auto& m = *impl_;
    auto pl = m.pipeline.lock();
    if (pl == nullptr) { return; }

    auto proxy = pl->get_pipe(m.pipe);
}

void pipepp::gui::pipe_view::_m_caption(native_string_type&& f)
{
    impl_->label.caption(f);
    super::_m_caption(std::move(f));
}

void pipepp::gui::pipe_view::_m_bgcolor(const nana::color& c)
{
    impl_->label.bgcolor(c);
    super::_m_bgcolor(c);
}

void pipepp::gui::pipe_view::_m_typeface(const nana::paint::font& font)
{
    impl_->label.typeface(font);
    super::_m_typeface(font);
}
