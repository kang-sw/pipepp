#pragma once
#include <algorithm>
#include <functional>
#include <memory>
#include <typeinfo>
#include "kangsw/misc.hxx"
#include "kangsw/thread_pool.hxx"
#include "pipepp/pipe.hpp"

namespace pipepp {
namespace detail {

class pipeline_base : public std::enable_shared_from_this<pipeline_base> {
protected:
    pipeline_base();
    virtual ~pipeline_base() = default;

public:
    decltype(auto) get_first();
    decltype(auto) get_pipe(pipe_id_t);

    auto get_pipe(std::string_view s);

    auto& _thread_pool() { return workers_; }
    void sync();

public:
    auto& options() const { return global_options_; }
    auto& options() { return global_options_; }

    nlohmann::json export_options();
    void import_options(nlohmann::json const&);

protected:
    // shared data object allocator
    std::shared_ptr<base_shared_context> _fetch_shared();
    virtual std::shared_ptr<base_shared_context> _new_shared_object() = 0;

protected:
    std::vector<std::unique_ptr<pipe_base>> pipes_;
    std::vector<std::shared_ptr<base_shared_context>> fence_objects_;
    std::unordered_map<pipe_id_t, size_t> id_mapping_;
    std::mutex fence_object_pool_lock_;
    option_base global_options_;
    kangsw::timer_thread_pool workers_;
};

class pipe_proxy_base {
    friend class pipeline_base;
    friend class std::optional<pipe_proxy_base>;

protected:
    pipe_proxy_base(
      std::weak_ptr<pipeline_base> pipeline,
      pipe_base& pipe_ref)
        : pipeline_(pipeline)
        , pipe_(&pipe_ref)
    {
    }

public:
    virtual ~pipe_proxy_base() = default;

protected:
    auto& pipe() { return *pipe_; }
    auto& pipe() const { return *pipe_; }

public:
    // size of output nodes
    // output nodes[index]
    size_t num_output_nodes() const { return pipe().output_links().size(); }
    pipe_proxy_base get_output_node(size_t index) const { return {pipeline_, *pipe().output_links().at(index).pipe}; }

    size_t num_input_nodes() const { return pipe().input_links().size(); }
    pipe_proxy_base get_input_node(size_t index) const { return {pipeline_, *pipe().input_links().at(index).pipe}; }

    // get previous execution context
    std::shared_ptr<execution_context_data> consume_execution_result();
    bool execution_result_available() const;

    // get options
    auto& options() const { return pipe().options(); }

    // get id
    auto id() const { return pipe().id(); }

    // get name
    auto& name() const { return pipe().name(); }

    // check validity
    bool is_valid() const { return pipeline_.expired() == false; }
    bool is_optional() const { return pipe().is_optional_input(); }

    // executor conditions
    size_t num_executors() const { return pipe().num_executors(); }
    void executor_conditions(std::vector<executor_condition_t>& out) const { pipe().executor_conditions(out); }

    // return latest output interval
    auto output_interval() const { return pipe().output_interval(); }
    auto output_latency() const { return pipe().output_latency(); }

    // pause functionality
    bool is_paused() const { return pipe().is_paused(); }
    void pause() { pipe().pause(); }
    void unpause() { pipe().unpause(); }
    bool recently_aborted() const { return pipe().recently_aborted(); }

    // mark dirty
    void mark_option_dirty() { pipe().mark_dirty(); }

    auto configure_tweaks() { return pipe().get_prelaunch_tweaks(); }
    auto tweaks() { return pipe().read_tweaks(); }

protected:
    std::weak_ptr<pipeline_base> pipeline_;
    pipe_base* pipe_;
};

inline decltype(auto) pipeline_base::get_first()
{
    return pipe_proxy_base(weak_from_this(), *pipes_.front());
}

inline decltype(auto) pipepp::detail::pipeline_base::get_pipe(pipe_id_t id)
{
    auto index = id_mapping_.at(id);
    return pipe_proxy_base(weak_from_this(), *pipes_.at(index));
}

inline auto pipepp::detail::pipeline_base::get_pipe(std::string_view s)
{
    std::optional<pipe_proxy_base> rval;

    for (auto& pipe : pipes_) {
        if (pipe->name() == s) {
            rval = pipe_proxy_base(weak_from_this(), *pipe);
            break;
        }
    }

    return std::move(rval);
}

} // namespace detail

// template <typename SharedData_, typename InitialExec_>
// class pipeline;

template <typename SharedData_, typename Exec_>
class pipe_proxy final : public detail::pipe_proxy_base {
    template <typename, typename>
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
    pipe_proxy(const std::weak_ptr<detail::pipeline_base>& pipeline, detail::pipe_base& pipe_ref)
        : pipe_proxy_base(pipeline, pipe_ref)
    {
    }

public:
    /**
     * AVAILABLE LINKER SIGNATURES
     *
     *  (                                                           )\n
     *  (Next Input                                                 )\n
     *  (SharedData,    Prev Output,    Next Input                  )\n
     *  (SharedData,    Next Input                                  )\n
     *  (Prev Output,   Next Input                                  )\n
     *  (Exec Context,  Prev Output,    Next Input                  )\n
     *  (Shared Data,   Exec Context,   Prev Output,    Next Input  )\n
     */
    template <typename LnkFn_, typename FactoryFn_, typename... FactoryArgs_>
    pipe_proxy<SharedData_, typename std::invoke_result_t<FactoryFn_, FactoryArgs_...>::element_type::executor_type>
    create_and_link_output(
      std::string name, size_t num_executors, LnkFn_&& linker, FactoryFn_&& factory, FactoryArgs_&&... args);

    /**
     * AVAILABLE LINKER SIGNATURES
     *
     *  (                                                           )\n
     *  (Next Input                                                 )\n
     *  (SharedData,    Prev Output,    Next Input                  )\n
     *  (SharedData,    Next Input                                  )\n
     *  (Prev Output,   Next Input                                  )\n
     *  (Exec Context,  Prev Output,    Next Input                  )\n
     *  (Shared Data,   Exec Context,   Prev Output,    Next Input  )\n
     */
    template <typename Dest_, typename LnkFn_>
    pipe_proxy<shared_data_type, Dest_> link_output(pipe_proxy<shared_data_type, Dest_> dest, LnkFn_&& linker);

    /**
     * AVAILABLE OUTPUT HANDLER SIGNATURES
     *
     *   (                                                      )\n
     *   (Pipe Err,     SharedData,     Result                  )\n
     *   (SharedData                                            )\n
     *   (SharedData,   Exec Context                            )\n
     *   (Pipe Err,     Result                                  )\n
     *   (SharedData,   Result                                  )\n
     *   (SharedData,   Exec Context,   Result                  )\n
     *   (Pipe Err,     SharedData,     Exec Context,   Result  )\n
     */
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
    pipe().connect_output_to<shared_data_type, prev_output_type, next_input_type>(
      dest.pipe(), std::forward<LnkFn_>(linker));

    return dest;
}

template <typename SharedData_, typename Exec_>
template <typename Fn_>
pipe_proxy<SharedData_, Exec_>&
pipe_proxy<SharedData_, Exec_>::add_output_handler(Fn_&& handler)
{
    auto wrapper = [fn_ = std::move(handler)](pipe_error e, base_shared_context& s, execution_context& ec, std::any const& o) {
        auto& sd = static_cast<SharedData_&>(s);
        auto& out = std::any_cast<output_type const&>(o);

        using PE = pipe_error;
        using SD = SharedData_&;
        using EC = execution_context&;
        using OUT = output_type const&;

        bool const okay = e <= pipe_error::warning;
        // clang-format off
        if constexpr (std::is_invocable_v<Fn_>) { if(okay) fn_(); }
        if constexpr (std::is_invocable_v<Fn_, PE, SD, OUT>) { fn_(e, sd, out); }
        else if constexpr (std::is_invocable_v<Fn_, SD>) { if (okay) { fn_(sd); } }
        else if constexpr (std::is_invocable_v<Fn_, SD, EC>) { if (okay) { fn_(sd, ec); } }
        else if constexpr (std::is_invocable_v<Fn_, PE, OUT>) { fn_(e, o); }
        else if constexpr (std::is_invocable_v<Fn_, SD, OUT>) { if (okay) { fn_(sd, out); } }
        else if constexpr (std::is_invocable_v<Fn_, SD, EC, OUT>) { if (okay) { fn_(sd, ec, out); } }
        else if constexpr (std::is_invocable_v<Fn_, PE, SD, EC, OUT>) { fn_(e, sd, ec, out); }
        else { static_assert(false, "No invocable method"); }
        // clang-format on
    };
    pipe().add_output_handler(std::move(wrapper));
    return *this;
} // namespace pipepp

template <typename SharedData_, typename InitialExec_>
class pipeline final : public detail::pipeline_base {
    template <typename, typename>
    friend class pipe_proxy;

public:
    using shared_data_type = SharedData_;
    using initial_executor_type = InitialExec_;
    using input_type = typename initial_executor_type::input_type;
    using initial_proxy_type = pipe_proxy<shared_data_type, initial_executor_type>;

    using factory_return_type = std::unique_ptr<detail::executor_base>;
    ~pipeline() { sync(); }

private:
    template <typename Exec_, typename Fn_, typename... Args_>
    auto& _create_pipe(std::string initial_pipe_name, bool is_optional, size_t num_execs, Fn_&& exec_factory, Args_&&... args)
    {
        if (num_execs == 0) { throw pipe_exception("invalid number of executors"); }
        auto fn = std::ranges::find_if(pipes_, [&initial_pipe_name](std::unique_ptr<detail::pipe_base> const& pipe) {
            return pipe->name() == initial_pipe_name;
        });
        if (fn != pipes_.end()) { throw pipe_exception("name duplication detected"); }

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

    template <typename Fn_, typename... Args_>
    pipeline(std::string initial_pipe_name, size_t num_exec, Fn_&& initial_executor_factory, Args_&&... args)
    {
        _create_pipe<initial_executor_type, Fn_, Args_...>(
          std::move(initial_pipe_name),
          false,
          num_exec,
          std::forward<Fn_>(initial_executor_factory),
          std::forward<Args_>(args)...);

        global_options_.reset_as_default<shared_data_type>();
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
          std::static_pointer_cast<detail::pipeline_base>(shared_from_this()),
          *pipes_.front()};
    }

public:
    // check if suppliable
    bool can_suply() const { return pipes_.front()->can_submit_input_direct(); }

    // supply input (trigger)
    template <typename Fn_>
    bool suply(
      input_type input, Fn_&& shared_data_init_func = [](auto) {})
    {
        auto shared = _fetch_shared();
        shared_data_init_func(static_cast<shared_data_type&>(*shared));
        return pipes_.front()->try_submit(std::move(input), std::move(shared));
    }

    // launcher
    void launch()
    {
        for (auto [pipe, tuple] : kangsw::zip(pipes_, adapters_)) {
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
pipe_proxy<SharedData_, Exec_>::create_and_link_output(std::string name, size_t num_executors, LnkFn_&& linker, FactoryFn_&& factory, FactoryArgs_&&... args)
{
    using factory_invoke_type = std::invoke_result_t<FactoryFn_, FactoryArgs_...>;
    using executor_type = typename factory_invoke_type::element_type;
    using destination_type = typename executor_type::executor_type;

    auto pl = _lock();
    auto& ref = pl->template _create_pipe<destination_type>(
      std::move(name), false, num_executors,
      std::forward<FactoryFn_>(factory), std::forward<FactoryArgs_>(args)...);

    pipe_proxy<shared_data_type, destination_type> dest(pipeline_, ref);
    return link_output(dest, std::forward<LnkFn_>(linker));
}

static constexpr auto link_as_is = [](auto&&, execution_context&, auto&& prev_out, auto&& next_in) { next_in = std::forward<decltype(prev_out)>(prev_out); };

} // namespace pipepp