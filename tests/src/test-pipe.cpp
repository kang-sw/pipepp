#define CATCH_CONFIG_DISABLE_EXCEPTIONS
#include <fmt/format.h>
#include "catch.hpp"
#include "pipepp/pipe.hpp"

namespace pipepp::pipe_test {
struct test_exec {
    struct input_type {
        int value = 0;
    };
    struct output_type {
        int value = 0;
    };

    pipe_error invoke(execution_context& exec, input_type const& input, output_type& output)
    {
        output.value = input.value + 1;
        auto to_print = fmt::format("{:>15}: {} -> {}", prefix, output.value, input.value);
        WARN(to_print);
        return pipe_error::ok;
    }

    static void recursive_adapter(base_fence_shared_object&, output_type const& result, input_type& next_input)
    {
        next_input.value = result.value;
    }

public:
    std::string prefix;
};

TEST_CASE("pipe initialization")
{
    using namespace impl__;
    kangsw::timer_thread_pool workers;

    auto pipe0 = std::make_shared<pipe_base>();
    auto pipe1 = std::make_shared<pipe_base>();
    auto pipe2_0 = std::make_shared<pipe_base>();
    auto pipe2_1 = std::make_shared<pipe_base>();
    auto pipe3_opt = std::make_shared<pipe_base>();
    auto pipe3_0 = std::make_shared<pipe_base>();

    // clang-format off
    auto pipes = {pipe0, pipe1, pipe2_0, pipe2_1, pipe3_opt, pipe3_0};
    auto pipe_names = {"pipe 0", "pipe 1", "pipe 2_0", "pipe 2_1", "pipe3_opt", "pipe3_0"};
    // clang-format on

    for (auto& [ref, name] : kangsw::zip(pipes, pipe_names)) {
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
}
} // namespace pipepp::pipe_test
