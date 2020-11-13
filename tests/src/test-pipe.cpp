#define CATCH_CONFIG_DISABLE_EXCEPTIONS
#include <fmt/format.h>
#include <iomanip>
#include <sstream>
#include "catch.hpp"
#include "pipepp/pipe.hpp"

namespace pipepp_test::pipes {
using namespace pipepp;

std::mutex lock;
static std::stringstream logger;

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
            // std::this_thread::sleep_for(10ms);
        }
        // std::this_thread::sleep_for(10ms + 1ms * (rand() % 3));

        output.value = input.value + 1;

        std::stringstream pref;
        for (auto& str : input.contributes) { pref << std::setw(10) << str << " "; }
        pref << ":: " << std::setw(10) << prefix;

        auto to_print = fmt::format("{:>50}: {} -> {}\n", pref.str(), input.value, output.value);
        lock.lock();
        logger << (to_print.c_str());
        lock.unlock();

        input.contributes.clear();
        output.contrib = prefix;
        return pipe_error::warning;
    }

    static void recursive_adapter(base_shared_context&, output_type const& result, input_type& next_input)
    {
        next_input.value = result.value;
        next_input.contributes.push_back(result.contrib);
    }

public:
    std::string prefix;
};

TEST_CASE("pipe initialization", "[.]")
{
    for (auto ITER = 0; ITER < 5; ++ITER) {
        using namespace impl__;
        using namespace std::chrono_literals;
        kangsw::timer_thread_pool workers{1024, 2};

        auto pipe0 = std::make_shared<pipe_base>("");
        auto pipe1 = std::make_shared<pipe_base>("");
        auto pipe2_0 = std::make_shared<pipe_base>("");
        auto pipe2_1 = std::make_shared<pipe_base>("");
        auto pipe3_opt = std::make_shared<pipe_base>("", true);
        auto pipe3_0 = std::make_shared<pipe_base>("");
        auto pipe4_0 = std::make_shared<pipe_base>("");

        // clang-format off
    auto pipes = {pipe0, pipe1, pipe2_0, pipe2_1, pipe3_opt, pipe3_0, pipe4_0};
    auto pipe_names = {"pipe 0", "pipe 1", "pipe 2_0", "pipe 2_1", "pipe 3_opt", "pipe 3_0", "pipe 4_0"};
        // clang-format on

        for (auto& [ref, _1] : kangsw::zip(pipes, pipe_names)) {
            ref->_set_thread_pool_reference(&workers);
        }

        pipe0->connect_output_to<
          base_shared_context, test_exec::output_type, test_exec::input_type>(
          *pipe1, &test_exec::recursive_adapter);
        pipe1->connect_output_to<
          base_shared_context, test_exec::output_type, test_exec::input_type>(
          *pipe2_0, &test_exec::recursive_adapter);
        pipe1->connect_output_to<
          base_shared_context, test_exec::output_type, test_exec::input_type>(
          *pipe2_1, &test_exec::recursive_adapter);
        pipe2_0->connect_output_to<
          base_shared_context, test_exec::output_type, test_exec::input_type>(
          *pipe3_opt, &test_exec::recursive_adapter);
        pipe2_1->connect_output_to<
          base_shared_context, test_exec::output_type, test_exec::input_type>(
          *pipe3_opt, &test_exec::recursive_adapter);
        pipe2_0->connect_output_to<
          base_shared_context, test_exec::output_type, test_exec::input_type>(
          *pipe3_0, &test_exec::recursive_adapter);
        pipe2_1->connect_output_to<
          base_shared_context, test_exec::output_type, test_exec::input_type>(
          *pipe3_0, &test_exec::recursive_adapter);

        pipe3_opt->connect_output_to<
          base_shared_context, test_exec::output_type, test_exec::input_type>(
          *pipe4_0, &test_exec::recursive_adapter);

        REQUIRE_THROWS( // SELF ERROR
          pipe1->connect_output_to<
            base_shared_context, test_exec::output_type, test_exec::input_type>(
            *pipe1, &test_exec::recursive_adapter));
        REQUIRE_THROWS( // CIRCULAR ERROR
          pipe1->connect_output_to<
            base_shared_context, test_exec::output_type, test_exec::input_type>(
            *pipe0, &test_exec::recursive_adapter));
        REQUIRE_THROWS( // CIRCULAR ERROR
          pipe2_0->connect_output_to<
            base_shared_context, test_exec::output_type, test_exec::input_type>(
            *pipe0, &test_exec::recursive_adapter));
        REQUIRE_THROWS( // OPTIONAL PARENT NOT EQUAL ERROR
          pipe0->connect_output_to<
            base_shared_context, test_exec::output_type, test_exec::input_type>(
            *pipe4_0, &test_exec::recursive_adapter));

        auto factory = [](std::string name) { return make_executor<test_exec>(name); };

        for (auto& [pipe, name] : kangsw::zip(pipes, pipe_names)) {
            pipe->launch_by(5, factory, name);
        }

        REQUIRE_THROWS( // ALREADY LAUNCHED ERROR
          pipe3_opt->connect_output_to<
            base_shared_context, test_exec::output_type, test_exec::input_type>(
            *pipe3_0, &test_exec::recursive_adapter));

        static constexpr size_t NUM_CASES = 1024;
        for (int i = 0; i < NUM_CASES; i++) {
            lock.lock(), logger << (fmt::format("{:->60}\n", ' ')), lock.unlock();
            while (!pipe0->try_submit(test_exec::input_type{i * 100}, std::make_shared<base_shared_context>())) {
                std::this_thread::sleep_for(0.1ms);
            }
        }

        for (auto& [pipe, name] : kangsw::zip(pipes, pipe_names)) {
            using namespace std::literals;

            while (pipe->is_async_operation_running()) { std::this_thread::sleep_for(1us); }
            lock.lock(), logger << (fmt::format("{:-^60}\n", name + " synched"s)), lock.unlock();
        }

        WARN(logger.str());
        logger = {};
    }
}

} // namespace pipepp_test::pipes
