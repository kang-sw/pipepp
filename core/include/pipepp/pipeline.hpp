#pragma once
#include <memory>

#include "pipepp/pipe.hpp"

namespace pipepp {
namespace impl__ {

class pipeline_base : public std::enable_shared_from_this<pipeline_base> {
protected:
    pipeline_base() = default;

protected:
    // shared data object allocator

protected:
    std::vector<std::unique_ptr<pipe_base>> pipes_;
};

} // namespace impl__

// template <typename SharedData_, typename InitialExec_>
// class pipeline;

template <typename SharedData_, typename Exec_>
class pipe_proxy {
    template <typename SharedData_, typename InitialExec_>
    friend class pipeline;
    template <typename, typename>
    friend class pipe_proxy;

public:
    using shared_data_type = SharedData_;
    using executor_type = Exec_;
    using input_type = typename executor_type::input_type;
    using output_type = typename executor_type::output_type;
    using pipeline_type = pipeline<SharedData_, Exec_>;

private:
    pipe_proxy(std::weak_ptr<impl__::pipeline_base> pipeline, impl__::pipe_base& pipe_ref)
        : pipeline_(pipeline)
        , pipe_(pipe_ref)
    {
    }

public:
    // link to
    // 1. creation
    template <typename LnkFn_, typename FactoryFn_, typename... FactoryArgs_>
    decltype(auto) create_and_link_output(
      std::string name, bool optional_input, LnkFn_&& linker, FactoryFn_&& factory, FactoryArgs_&&... args);

    // 2. simple linkage
    template <typename Dest_, typename LnkFn_>
    pipe_proxy<shared_data_type, Dest_> link_output(pipe_proxy<shared_data_type, Dest_> dest, LnkFn_&& linker);

    // size of output nodes
    // output nodes[index]

    // get previous execution context

    // get options

    // get id

    bool is_valid() const { return pipeline_.expired() == false; }

private:
    std::shared_ptr<pipeline_type> _lock() const
    {
        auto ref = pipeline_.lock();
        if (ref == nullptr) { throw pipe_exception("pipeline reference destroied"); }
        return std::static_pointer_cast<pipeline_type>(ref);
    }

private:
    std::weak_ptr<impl__::pipeline_base> pipeline_;
    impl__::pipe_base& pipe_;
};

template <typename SharedData_, typename Exec_>
template <typename Dest_, typename LnkFn_>
pipe_proxy<typename pipe_proxy<SharedData_, Exec_>::shared_data_type, Dest_>
pipe_proxy<SharedData_, Exec_>::link_output(pipe_proxy<shared_data_type, Dest_> dest, LnkFn_&& linker)
{
    using prev_output_type = output_type;
    using next_input_type = typename Dest_::input_type;
    pipe_.connect_output_to<shared_data_type, prev_output_type, next_input_type>(
      dest.pipe_, std::forward<LnkFn_>(linker));

    return dest;
}

template <typename SharedData_, typename InitialExec_>
class pipeline final : public impl__::pipeline_base {
    template <typename, typename>
    friend class pipe_proxy;

public:
    using shared_data_type = SharedData_;
    using initial_executor_type = InitialExec_;
    using input_type = typename initial_executor_type::input_type;
    using initial_proxy_type = pipe_proxy<shared_data_type, initial_executor_type>;

    using factory_return_type = std::unique_ptr<impl__::executor_base>;

private:
    template <typename Exec_, typename Fn_, typename... Args_>
    auto& _create_pipe(std::string initial_pipe_name, bool is_optional, Fn_&& exec_factory, Args_&&... args)
    {
        pipes_.emplace_back(
          std::make_unique<impl__::pipe_base>(
            std::move(initial_pipe_name), is_optional,
            std::make_unique<executor_option<Exec_>>()));
        adapters_.emplace_back(
          std::bind<factory_return_type>(
            std::forward<Fn_>(exec_factory),
            std::forward<Args_>(args)...));

        return *pipes_.back();
    }

    template <typename Fn_, typename... Args_>
    pipeline(std::string initial_pipe_name, Fn_&& initial_executor_factory, Args_&&... args)
    {
        _create_pipe<initial_executor_type, Fn_, Args_...>(
          std::move(initial_pipe_name),
          false,
          std::forward<Fn_>(initial_executor_factory),
          std::forward<Args_>(args)...);
    }

public:
    template <typename Fn_, typename... Args_>
    static std::shared_ptr<pipeline> create(std::string initial_pipe_name, Fn_&& factory, Args_&&... factory_args)
    {
        return std::shared_ptr<pipeline>{
          new pipeline(
            std::move(initial_pipe_name),
            std::forward<Fn_>(factory),
            std::forward<Args_>(factory_args)...)};
    }

public:
    initial_proxy_type front()
    {
        return initial_proxy_type{
          std::static_pointer_cast<impl__::pipeline_base>(shared_from_this()),
          *pipes_.front()};
    }

public:
    // check if suppliable

    // supply input (trigger)

    // launcher

public:
    std::vector<impl__::pipe_base::output_link_adapter_type> adapters_;
};

template <typename SharedData_, typename Exec_>
template <typename LnkFn_, typename FactoryFn_, typename... FactoryArgs_>
decltype(auto)
pipe_proxy<SharedData_, Exec_>::create_and_link_output(std::string name, bool optional_input, LnkFn_&& linker, FactoryFn_&& factory, FactoryArgs_&&... args)
{
    using factory_invoke_type = std::invoke_result_t<FactoryFn_, FactoryArgs_...>;
    using executor_type = typename factory_invoke_type::element_type;
    using destination_type = typename executor_type::executor_type;

    auto pl = _lock();
    auto& ref = pl->_create_pipe<destination_type>(
      std::move(name), optional_input,
      std::forward<FactoryFn_>(factory), std::forward<FactoryArgs_>(args)...);

    pipe_proxy<shared_data_type, destination_type> dest(pipeline_, ref);
    return link_output(dest, std::forward<LnkFn_>(linker));
}
} // namespace pipepp
