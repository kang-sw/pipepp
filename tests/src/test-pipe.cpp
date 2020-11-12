#define CATCH_CONFIG_DISABLE_EXCEPTIONS
#include <fmt/format.h>
#include <iomanip>
#include <sstream>
#include "catch.hpp"
#include "pipepp/pipe.hpp"

namespace pipepp::pipe_test {

struct test_exec {
    struct input_type {
        int value = 0;
        std::vector<std::string> contributes;
    };
    struct output_type {
        int value = 0;
        std::string contrib;
    };

    test_exec(std::string pfx)
        : prefix(pfx)
    {}

    pipe_error invoke(execution_context& exec, input_type& input, output_type& output)
    {
        using namespace std::chrono_literals;
        if (prefix.ends_with("opt")) {
            std::this_thread::sleep_for(1000ms);
        }
        std::this_thread::sleep_for(1000ms + 10ms * (rand() % 30));

        output.value = input.value + 1;

        std::stringstream pref;
        for (auto& str : input.contributes) { pref << std::setw(8) << str << ", "; }
        pref << prefix;

        auto to_print = fmt::format("{:>35}: {} -> {}\n", pref.str(), input.value, output.value);
        printf(to_print.c_str());

        input.contributes.clear();
        output.contrib = prefix;
        return pipe_error::ok;
    }

    static void recursive_adapter(base_fence_shared_object&, output_type const& result, input_type& next_input)
    {
        next_input.value = result.value;
        next_input.contributes.push_back(result.contrib);
    }

public:
    std::string prefix;
};

TEST_CASE("pipe initialization")
{
    using namespace impl__;
    using namespace std::chrono_literals;
    kangsw::timer_thread_pool workers{1024, 2};

    auto pipe0 = std::make_shared<pipe_base>();
    auto pipe1 = std::make_shared<pipe_base>();
    auto pipe2_0 = std::make_shared<pipe_base>();
    auto pipe2_1 = std::make_shared<pipe_base>();
    auto pipe3_opt = std::make_shared<pipe_base>(true);
    auto pipe3_0 = std::make_shared<pipe_base>();

    // clang-format off
    auto pipes = {pipe0, pipe1, pipe2_0, pipe2_1, pipe3_opt, pipe3_0};
    auto pipe_names = {"pipe 0", "pipe 1", "pipe 2_0", "pipe 2_1", "pipe3_opt", "pipe3_0"};
    // clang-format on

    for (auto& [ref, _1] : kangsw::zip(pipes, pipe_names)) {
        ref->set_thread_pool_reference(&workers);
    }

    pipe0->connect_output_to<
      base_fence_shared_object, test_exec::output_type, test_exec::input_type>(
      *pipe1, &test_exec::recursive_adapter);
    pipe1->connect_output_to<
      base_fence_shared_object, test_exec::output_type, test_exec::input_type>(
      *pipe2_0, &test_exec::recursive_adapter);
    pipe1->connect_output_to<
      base_fence_shared_object, test_exec::output_type, test_exec::input_type>(
      *pipe2_1, &test_exec::recursive_adapter);
    pipe2_0->connect_output_to<
      base_fence_shared_object, test_exec::output_type, test_exec::input_type>(
      *pipe3_opt, &test_exec::recursive_adapter);
    pipe2_1->connect_output_to<
      base_fence_shared_object, test_exec::output_type, test_exec::input_type>(
      *pipe3_opt, &test_exec::recursive_adapter);

    REQUIRE_THROWS(
      pipe1->connect_output_to<
        base_fence_shared_object, test_exec::output_type, test_exec::input_type>(
        *pipe1, &test_exec::recursive_adapter));
    REQUIRE_THROWS(
      pipe1->connect_output_to<
        base_fence_shared_object, test_exec::output_type, test_exec::input_type>(
        *pipe0, &test_exec::recursive_adapter));
    REQUIRE_THROWS(
      pipe2_0->connect_output_to<
        base_fence_shared_object, test_exec::output_type, test_exec::input_type>(
        *pipe0, &test_exec::recursive_adapter));

    auto factory = [](std::string name) { return create_executor<test_exec>(name); };

    for (auto& [pipe, name] : kangsw::zip(pipes, pipe_names)) {
        pipe->launch_by(1, factory, name);
    }

    REQUIRE_THROWS(
      pipe3_opt->connect_output_to<
        base_fence_shared_object, test_exec::output_type, test_exec::input_type>(
        *pipe3_0, &test_exec::recursive_adapter));

    fmt::print("{:->60}\n", ' ');
    pipe0->try_submit(test_exec::input_type{100}, std::make_shared<base_fence_shared_object>());
    while (pipe0->is_async_operation_running()) { std::this_thread::sleep_for(1us); }

    fmt::print("{:->60}\n", ' ');
    pipe0->try_submit(test_exec::input_type{200}, std::make_shared<base_fence_shared_object>());
    while (pipe0->is_async_operation_running()) { std::this_thread::sleep_for(1us); }

    fmt::print("{:->60}\n", ' ');
    pipe0->try_submit(test_exec::input_type{300}, std::make_shared<base_fence_shared_object>());
    while (pipe0->is_async_operation_running()) { std::this_thread::sleep_for(1us); }

    for (auto& [pipe, name] : kangsw::zip(pipes, pipe_names)) {
        using namespace std::literals;

        while (pipe->is_async_operation_running()) { std::this_thread::sleep_for(1us); }
        fmt::print("{:-^60}\n", name + " synched"s);
    }
}

} // namespace pipepp::pipe_test
