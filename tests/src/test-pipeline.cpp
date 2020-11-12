#include <algorithm>

#include "catch.hpp"
#include "pipepp/pipeline.hpp"

namespace pipepp::pipeline_test {
struct my_shared_data : public base_shared_context {
    int level = 0;
};

struct exec_0 {
    using input_type = std::tuple<int, int>;
    using output_type = std::tuple<double, double>;

    pipe_error invoke(execution_context& so, input_type& i, output_type& o)
    {
        o = i;
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

static void link_1_0(my_shared_data&, exec_1::output_type const& a, exec_0::input_type& b)
{
}

TEST_CASE("pipeline compilation")
{
    using pipeline_type = pipeline<my_shared_data, exec_0>;
    auto pl
      = pipeline_type::create("0.0", 1, &exec_0::factory);
    auto _0 = pl->front();
    auto _1_0
      = _0.create_and_link_output(
        "1.0", false, 1, link_as_is, &exec_1::factory);
    auto _1_1
      = _0.create_and_link_output(
        "1.1", false, 2, link_as_is, &exec_1::factory);

    pl->launch();
}
} // namespace pipepp::pipeline_test
