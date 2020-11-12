#pragma once
#include <algorithm>
#include <functional>
#include <memory>
#include <typeinfo>
#include "kangsw/misc.hxx"
#include "kangsw/thread_pool.hxx"
#include "pipepp/pipe.hpp"

namespace pipepp {
namespace impl__ {

class pipeline_base : public std::enable_shared_from_this<pipeline_base> {
protected:
    pipeline_base() = default;
    virtual ~pipeline_base() = default;

public:
    decltype(auto) get_first();
    auto& _thread_pool() { return workers_; }
    void sync();

protected:
    // shared data object allocator
    std::shared_ptr<base_shared_context> _fetch_shared();
    virtual std::shared_ptr<base_shared_context> _new_shared_object() = 0;

protected:
    std::vector<std::unique_ptr<pipe_base>> pipes_;
    std::vector<std::shared_ptr<base_shared_context>> fence_objects_;
    std::mutex fence_object_pool_lock_;
    inline static kangsw::timer_thread_pool workers_;
};

class pipe_proxy_base {
    friend class pipeline_base;

protected:
    pipe_proxy_base(
      std::weak_ptr<pipeline_base> pipeline,
      pipe_base& pipe_ref)
        : pipeline_(pipeline)
        , pipe_(pipe_ref)
        , type_hash_(typeid(pipe_ref).hash_code())
    {
    }
    virtual ~pipe_proxy_base() = default;

public:
    // size of output nodes
    // output nodes[index]
    size_t num_output_nodes() const { return pipe_.output_links().size(); }
    pipe_proxy_base get_output_node(size_t index) { return {pipeline_, *pipe_.output_links().at(index).pipe}; }

    // get previous execution context
    auto& execution_result() const { return pipe_.latest_execution_context(); }

    // get options
    auto options() const { return pipe_.options(); }

    // get id
    auto id() const { return pipe_.id(); }

    // get name
    auto& name() const { return pipe_.name(); }

    // check validity
    bool is_valid() const { return pipeline_.expired() == false; }

protected:
    std::weak_ptr<pipeline_base> pipeline_;
    pipe_base& pipe_;
    size_t type_hash_;
};

inline decltype(auto) pipeline_base::get_first()
{
    return pipe_proxy_base(weak_from_this(), *pipes_.front());
}

} // namespace impl__

// template <typename SharedData_, typename InitialExec_>
// class pipeline;

template <typename SharedData_, typename Exec_>
class pipe_proxy final : public impl__::pipe_proxy_base {
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
    pipe_proxy(const std::weak_ptr<impl__::pipeline_base>& pipeline, impl__::pipe_base& pipe_ref)
        : pipe_proxy_base(pipeline, pipe_ref)
    {
    }

public:
    // link to
    // 1. creation
    template <typename LnkFn_, typename FactoryFn_, typename... FactoryArgs_>
    pipe_proxy<SharedData_, typename std::invoke_result_t<FactoryFn_, FactoryArgs_...>::element_type::executor_type>
    create_and_link_output(
      std::string name, bool optional_input, size_t num_executors, LnkFn_&& linker, FactoryFn_&& factory, FactoryArgs_&&... args);

    // 2. simple linkage
    template <typename Dest_, typename LnkFn_>
    pipe_proxy<shared_data_type, Dest_> link_output(pipe_proxy<shared_data_type, Dest_> dest, LnkFn_&& linker);

    // simple output handler
    template <typename Fn_>
    pipe_proxy& add_output_handler(Fn_&& handler);

private:
    std::shared_ptr<pipeline_type> _lock() const
    {
        auto ref = pipeline_.lock();
        if (ref == nullptr) { throw pipe_exception("pipeline reference destroied"); }
        return std::static_pointer_cast<pipeline_type>(ref);
    }

private:
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

template <typename SharedData_, typename Exec_>
template <typename Fn_>
pipe_proxy<SharedData_, Exec_>&
pipe_proxy<SharedData_, Exec_>::add_output_handler(Fn_&& handler)
{
    auto wrapper = [fn_ = std::move(handler)](pipe_error e, base_shared_context const& s, std::any const& o) {
        fn_(e, static_cast<SharedData_ const&>(s), std::any_cast<output_type const&>(o));
    };
    pipe_.add_output_handler(std::move(wrapper));
    return *this;
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
    ~pipeline() { sync(); }

private:
    template <typename Exec_, typename Fn_, typename... Args_>
    auto& _create_pipe(std::string initial_pipe_name, bool is_optional, size_t num_execs, Fn_&& exec_factory, Args_&&... args)
    {
        if (num_execs == 0) { throw pipe_exception("invalid number of executors"); }

        pipes_.emplace_back(
          std::make_unique<impl__::pipe_base>(
            std::move(initial_pipe_name), is_optional,
            std::make_unique<executor_option<Exec_>>()));
        pipes_.back()->_set_thread_pool_reference(&workers_);

        adapters_.emplace_back(
          num_execs,
          std::bind<factory_return_type>(
            std::forward<Fn_>(exec_factory),
            std::forward<Args_>(args)...));

        return *pipes_.back();
    }

    template <typename Fn_, typename... Args_>
    pipeline(std::string initial_pipe_name, size_t num_exec, Fn_&& initial_executor_factory, Args_&&... args)
    {
        _create_pipe<initial_executor_type, Fn_, Args_...>(
          std::move(initial_pipe_name),
          false,
          num_exec,
          std::forward<Fn_>(initial_executor_factory),
          std::forward<Args_>(args)...);
    }

public:
    template <typename Fn_, typename... Args_>
    static std::shared_ptr<pipeline> create(std::string initial_pipe_name, size_t num_initial_exec, Fn_&& factory, Args_&&... factory_args)
    {
        return std::shared_ptr<pipeline>{
          new pipeline(
            std::move(initial_pipe_name),
            num_initial_exec,
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
    bool can_suply() const { return pipes_.front()->can_submit_input_direct(); }

    // supply input (trigger)
    template <typename Fn_>
    bool suply(input_type input, Fn_&& shared_data_init_func)
    {
        auto shared = _fetch_shared();
        shared_data_init_func(static_cast<shared_data_type&>(*shared));
        return pipes_.front()->try_submit(std::move(input), std::move(shared));
    }

    // launcher
    void launch()
    {
        for (auto& [pipe, tuple] : kangsw::zip(pipes_, adapters_)) {
            auto& [n_ex, handler] = tuple;
            pipe->launch(n_ex, std::move(handler));
        }

        adapters_.clear();
    }

protected:
    std::shared_ptr<base_shared_context> _new_shared_object() override
    {
        return std::make_shared<shared_data_type>();
    }

private:
    std::vector<std::tuple<
      size_t, std::function<factory_return_type()>>>
      adapters_;
};

template <typename SharedData_, typename Exec_>
template <typename LnkFn_, typename FactoryFn_, typename... FactoryArgs_>
pipe_proxy<SharedData_, typename std::invoke_result_t<FactoryFn_, FactoryArgs_...>::element_type::executor_type>
pipe_proxy<SharedData_, Exec_>::create_and_link_output(std::string name, bool optional_input, size_t num_executors, LnkFn_&& linker, FactoryFn_&& factory, FactoryArgs_&&... args)
{
    using factory_invoke_type = std::invoke_result_t<FactoryFn_, FactoryArgs_...>;
    using executor_type = typename factory_invoke_type::element_type;
    using destination_type = typename executor_type::executor_type;

    auto pl = _lock();
    auto& ref = pl->_create_pipe<destination_type>(
      std::move(name), optional_input, num_executors,
      std::forward<FactoryFn_>(factory), std::forward<FactoryArgs_>(args)...);

    pipe_proxy<shared_data_type, destination_type> dest(pipeline_, ref);
    return link_output(dest, std::forward<LnkFn_>(linker));
}

static constexpr auto link_as_is = [](auto, auto& prev_out, auto& next_in) { next_in = prev_out; };

} // namespace pipepp