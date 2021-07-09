#include "sample_pipeline.hpp"

std::shared_ptr<my_pipeline_type> build_pipeline()
{
    auto pl = my_pipeline_type::make("Entry", 4);

    auto _0 = pl->front();

#define pew(FROM, TO) auto TO = FROM.create_and_link_output(#TO, 3, pipepp::link_as_is, &pipepp::make_executor<my_executor_0>);
#define pew_opt(FROM, TO) auto TO = FROM.create_and_link_output(#TO, 3, pipepp::link_as_is, &pipepp::make_executor<my_executor_0>);
    _0.link_to(pl->create<my_executor_0>("1"))
      .link_to(pl->create<my_executor_0>("2"))
      .link_to(pl->create<my_executor_0>("3"))
      .link_to(pl->create<my_executor_0>("4"));

    return pl;
}
