#pragma once
#include "pipepp/options.hpp"
#include "pipepp/pipeline.hpp"

namespace pipepp {
template <typename SharedData_, typename InitialExec_> template <typename Exec_, typename Fn_, typename... Args_> auto& pipepp::pipeline<SharedData_, InitialExec_>::_create_pipe(std::string initial_pipe_name, bool is_optional, size_t num_execs, Fn_&& exec_factory, Args_&&... args)
{
    if (num_execs == 0) { throw pipe_exception("invalid number of executors"); }
    auto fn = std::ranges::find_if(pipes_, [&initial_pipe_name](std::unique_ptr<detail::pipe_base> const& pipe) {
        return pipe->name() == initial_pipe_name;
    });
    using namespace std::literals;
    if (fn != pipes_.end()) { throw pipe_exception(("name duplication detected: "s + (**fn).name()).c_str()); }

    auto index = pipes_.size();
    auto& pipe = pipes_.emplace_back(
      std::make_unique<detail::pipe_base>(
        std::move(initial_pipe_name), is_optional));
    pipe->_set_thread_pool_reference(&workers_);
    pipe->options().reset_as_default<Exec_>();
    pipe->mark_dirty();

    adapters_.emplace_back(
      num_execs,
      std::bind<factory_return_type>(
        std::forward<Fn_>(exec_factory),
        std::forward<Args_>(args)...));

    id_mapping_[pipe->id()] = index;

    return *pipes_.back();
}

template <typename SharedData_, typename InitialExec_> template <typename Fn_, typename... Args_> pipeline<SharedData_, InitialExec_>::pipeline(std::string initial_pipe_name, size_t num_exec, Fn_&& initial_executor_factory, Args_&&... args)
{
    _create_pipe<initial_executor_type, Fn_, Args_...>(
      std::move(initial_pipe_name),
      false,
      num_exec,
      std::forward<Fn_>(initial_executor_factory),
      std::forward<Args_>(args)...);

    global_options_->reset_as_default<shared_data_type>();
}

template <typename SharedData_, typename InitialExec_>
template <typename FactoryFn_, typename... FactoryArgs_>
pipe_proxy<SharedData_, typename std::invoke_result_t<FactoryFn_, FactoryArgs_...>::element_type::executor_type>
pipeline<SharedData_, InitialExec_>::create(std::string name, size_t num_executors, FactoryFn_&& factory, FactoryArgs_&&... args)
{
    using factory_invoke_type = std::invoke_result_t<FactoryFn_, FactoryArgs_...>;
    using executor_type = typename factory_invoke_type::element_type;
    using destination_type = typename executor_type::executor_type;

    auto& ref = _create_pipe<destination_type>(
      std::move(name), false, num_executors,
      std::forward<FactoryFn_>(factory), std::forward<FactoryArgs_>(args)...);

    return {weak_from_this(), ref};
}

template <typename SharedData_, typename Exec_>
template <typename LnkFn_, typename FactoryFn_, typename... FactoryArgs_>
pipe_proxy<SharedData_, typename std::invoke_result_t<FactoryFn_, FactoryArgs_...>::element_type::executor_type>
pipe_proxy<SharedData_, Exec_>::create_and_link_output(std::string name, size_t num_executors, LnkFn_&& linker, FactoryFn_&& factory, FactoryArgs_&&... args)
{
    auto pl = _lock();
    auto dest = pl->create(std::move(name), num_executors, std::forward<FactoryFn_>(factory), std::forward<FactoryArgs_>(args)...);
    return link_output(dest, std::forward<LnkFn_>(linker));
}

} // namespace pipepp