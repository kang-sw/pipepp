#include <algorithm>
#include <memory>
#include <vector>
#include <xutility>

#include "catch.hpp"
#include "fmt/format.h"
#include "pipepp/pipeline.hpp"

namespace pipepp_test::pipelines {
using namespace pipepp;

struct my_shared_data : public base_shared_context {
    int level = 0;
};

struct exec_0 {
    using input_type = std::tuple<double>;
    using output_type = std::tuple<double, double>;

    pipe_error invoke(execution_context& so, input_type& i, output_type& o)
    {
        using namespace std::literals;
        //std::this_thread::sleep_for(1ms);
        auto [val] = i;
        auto& [a, b] = o;
        a = val;
        b = val * 2.0;
        return pipe_error::ok;
    }

    static auto factory()
    {
        return make_executor<exec_0>();
    }
};

struct exec_1 {
    using input_type = std::tuple<double, double>;
    using output_type = std::tuple<double>;

    pipe_error invoke(execution_context& so, input_type& i, output_type& o)
    {
        using namespace std::literals;
        //std::this_thread::sleep_for(1ms);
        auto& [a, b] = i;
        auto& [D] = o;
        D = sqrt(a * b);
        return pipe_error::ok;
    }

    static auto factory()
    {
        return make_executor<exec_1>();
    }
};

static void link_1_0(my_shared_data&, exec_1::output_type const& i, exec_0::input_type& o)
{
    auto& [val] = i;
    auto& [a] = o;

    a = val;
}

TEST_CASE("pipeline compilation")
{
    constexpr int NUM_CASE = 1024;
    std::vector<char> cases(NUM_CASE);
    std::vector<int> order(NUM_CASE);
    std::atomic_int ordering = 0;

    using pipeline_type = pipeline<my_shared_data, exec_0>;
    auto pl = pipeline_type::create("0.0", 64, &exec_0::factory);
    auto _0 = pl->front();
    auto _1_0
      = _0.create_and_link_output(
        "1.0", false, 64, link_as_is, &exec_1::factory);
    auto _1_1
      = _0.create_and_link_output(
        "1.1", false, 64, link_as_is, &exec_1::factory);
    auto _2
      = _1_1.create_and_link_output(
              "2.0", false, 64, &link_1_0, &exec_0::factory)
          .add_output_handler([&](pipe_error, my_shared_data const& so, exec_0::output_type const& val) {
              auto [a, b] = val;
              order[ordering++] = so.level;
              fmt::print("level {:<4}: {:>10.3}, {:>10.3}\n", so.level, a, b);
              cases[so.level] += 1;
          });

    pl->launch();

    using namespace std::literals;
    for (int iter = 0; iter < cases.size(); ++iter) {
        while (!pl->can_suply()) { std::this_thread::sleep_for(100us); }
        pl->suply({iter * 0.1}, [iter](my_shared_data& so) { so.level = iter; });
    }

    pl->sync();
    REQUIRE(std::ranges::count(cases, 1) == cases.size());
    REQUIRE(std::is_sorted(cases.begin(), cases.end()));
}
} // namespace pipepp_test::pipelines
