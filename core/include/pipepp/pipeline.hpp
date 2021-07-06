#pragma once
#include <algorithm>
#include <functional>
#include <memory>
#include <typeinfo>
#include "kangsw/helpers/misc.hxx"
#include "kangsw/thread/thread_pool.hxx"
#include "nlohmann/json_fwd.hpp"
#include "pipepp/pipe.hpp"

namespace pipepp {
static constexpr auto link_as_is =
  [](auto&& prev_out, auto&& next_in) { next_in = std::forward<decltype(prev_out)>(prev_out);  return true; };

namespace detail {

class pipeline_base : public std::enable_shared_from_this<pipeline_base> {
public:
    using factory_return_type = std::unique_ptr<detail::executor_base>;

protected:
    pipeline_base();
    virtual ~pipeline_base();

public:
    decltype(auto) get_first();
    decltype(auto) get_pipe(pipe_id_t);

    auto get_pipe(std::string_view s);

    auto& _thread_pool() { return workers_; }
    void sync();

    // launcher
    void launch();

public:
    auto& options() const { return *global_options_; }
    auto& options() { return *global_options_; }

    void export_options(nlohmann::json&);
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
    std::unique_ptr<option_base> global_options_;
    kangsw::timer_thread_pool workers_;

    std::vector<std::tuple<size_t, std::function<factory_return_type(void)>>> adapters_;
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

    return rval;
}
} // namespace detail

// template <typename SharedData_, typename InitialExec_>
// class pipeline;

template <typename SharedData_, typename Exec_, typename Prev_ = nullptr_t>
class pipe_proxy final : public detail::pipe_proxy_base {
    template <typename, typename>
    friend class pipeline;
    template <typename, typename, typename>
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

    pipe_proxy(const std::weak_ptr<detail::pipeline_base>& pipeline, detail::pipe_base& pipe_ref, Prev_ prv)
        : pipe_proxy_base(pipeline, pipe_ref)
        , _prv(prv)
    {
    }

public:
    template <typename OtherPrev_>
    auto& operator=(pipe_proxy<SharedData_, Exec_, OtherPrev_> const& o)
    {
        return pipe_proxy_base::operator=(o), *this;
    }

    auto& operator=(pipe_proxy const& o)
    {
        return memcpy(this, &o, sizeof *this), *this;
    }

    /**
     * Traverse previous nodes
     */
    template <size_t N_ = 1>
    auto& prev()
    {
        if constexpr (N_ <= 1) { return _prv; }
        if constexpr (N_ > 1) { return _prv.template prev<N_ - 1>(); }
    }

    auto set_optional_input() { return configure_tweaks().is_optional = true, *this; }
    auto set_selective_input() { return configure_tweaks().selective_input = true, *this; }
    auto set_selective_output() { return configure_tweaks().selective_output = true, *this; }

    /**
     * AVAILABLE LINKER SIGNATURES
     *  (                                                           )\n
     *  (Next Input                                                 )\n
     *  (SharedData,    Prev Output,    Next Input                  )\n
     *  (SharedData,    Next Input                                  )\n
     *  (Prev Output,   Next Input                                  )\n
     *  (Exec Context,  Prev Output,    Next Input                  )\n
     *  (Shared Data,   Exec Context,   Prev Output,    Next Input  )\n
     */
    template <typename Dest_, typename OtherPrev_, typename LnkFn_>
    auto link_to(pipe_proxy<shared_data_type, Dest_, OtherPrev_> dest, LnkFn_&& linker)
    {
        using prev_output_type = output_type;
        using next_input_type = typename Dest_::input_type;
        pipe().connect_output_to<shared_data_type, prev_output_type, next_input_type>(
          dest.pipe(), std::forward<LnkFn_>(linker));

        pipe_proxy<shared_data_type, Dest_, pipe_proxy> retval{pipeline_, *dest.pipe_, *this};
        return retval;
    }

    /**
     * Searches linker function automatically.
     *
     * Priority:
     *      1) prev out == next in := link_as_is
     *      2) Exec_ has link_to and is invocable with Shared, PrevOut, NextIn
     *      3) Dest_ has link_from and is invocable with Shared, PrevOut, NextIn
     *
     */
    template <typename Dest_, typename OtherPrev_>
    auto link_to(pipe_proxy<shared_data_type, Dest_, OtherPrev_> dest)
    {
        if constexpr (std::is_same_v<output_type, typename Dest_::input_type>) {
            return link_to(dest, link_as_is);
        } else if constexpr (requires() { this->link_to(dest, &Exec_::link_to); }) {
            return link_to(dest, &Exec_::link_to);
        } else if constexpr (requires() { this->link_to(dest, &Dest_::link_from); }) {
            return link_to(dest, &Dest_::link_from);
        } else {
            static_assert(false);
        }
    }

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
    auto& add_output_handler(Fn_&& handler)
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
            if constexpr (std::is_invocable_v<Fn_>) { if (okay) fn_(); }
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

private:
    std::shared_ptr<pipeline_type> _lock() const
    {
        auto ref = pipeline_.lock();
        if (ref == nullptr) { throw pipe_exception("pipeline reference destroied"); }
        return std::static_pointer_cast<pipeline_type>(ref);
    }

private:
    Prev_ _prv;
};

template <typename SharedData_, typename InitialExec_>
class pipeline final : public detail::pipeline_base {
    template <typename, typename, typename>
    friend class pipe_proxy;

public:
    using shared_data_type = SharedData_;
    using initial_executor_type = InitialExec_;
    using input_type = typename initial_executor_type::input_type;
    using initial_proxy_type = pipe_proxy<shared_data_type, initial_executor_type>;

    ~pipeline() override { sync(); }

private:
    template <typename Exec_, typename Fn_, typename... Args_> auto& _create_pipe(std::string initial_pipe_name, bool is_optional, size_t num_execs, Fn_&& exec_factory, Args_&&... args);

    template <typename Fn_, typename... Args_> pipeline(std::string initial_pipe_name, size_t num_exec, Fn_&& initial_executor_factory, Args_&&... args);

public:
    template <typename FactoryFn_, typename... FactoryArgs_>
    pipe_proxy<SharedData_, typename std::invoke_result_t<FactoryFn_, FactoryArgs_...>::element_type::executor_type>
    create(std::string name, size_t num_executors, FactoryFn_&& factory, FactoryArgs_&&... args);

    template <typename Exec_, size_t NumExec_ = 1, typename... ContructorArgs_>
    pipe_proxy<SharedData_, Exec_>
    create(std::string name, ContructorArgs_&&... args);

    template <typename Fn_, typename... Args_>
    static std::shared_ptr<pipeline> make(std::string initial_pipe_name, size_t num_initial_exec, Fn_&& factory, Args_&&... factory_args)
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
    bool can_suply() const { return !pipes_.front()->is_paused() && pipes_.front()->can_submit_input_direct(); }

    // supply input (trigger)
    template <typename Fn_>
    bool suply(
      input_type input, Fn_&& shared_data_init_func = [](auto&&) {})
    {
        auto shared = _fetch_shared();
        shared_data_init_func(static_cast<shared_data_type&>(*shared));
        shared->reload();
        return pipes_.front()->try_submit(std::move(input), std::move(shared));
    }

    bool wait_supliable(std::chrono::milliseconds timeout = std::chrono::milliseconds{10}) const
    {
        return !pipes_.front()->is_paused() && pipes_.front()->wait_active_slot_idle(timeout);
    }

protected:
    std::shared_ptr<base_shared_context> _new_shared_object() override
    {
        return std::make_shared<shared_data_type>();
    }

private:
};

} // namespace pipepp