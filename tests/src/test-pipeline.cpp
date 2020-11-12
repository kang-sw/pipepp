#include <algorithm>

#include "catch.hpp"
#include "pipepp/pipeline.hpp"

namespace pipepp::pipeline_test {
struct my_shared_data : public base_fence_shared_data {
    int layer;
};

struct exec_0 {
    using input_type = std::tuple<int, int>;
    using output_type = std::tuple<double, double>;

    pipe_error invoke(execution_context& so, input_type& i, output_type& o)
    {
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
        return pipe_error::ok;
    }

    static auto factory()
    {
        return make_executor<exec_1>();
    }
};

TEST_CASE("pipeline compilation")
{
    using pipeline_type = pipeline<my_shared_data, exec_0>;
    auto pl = pipeline_type::create("Hell", &exec_0::factory);
    auto front = pl->front();
    auto second = front.create_and_link_output(
      "World", false, [](my_shared_data&, exec_0::output_type const&, exec_1::input_type&) {}, &exec_1::factory);
}
} // namespace pipepp::pipeline_test
