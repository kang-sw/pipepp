#include "sample_pipeline.hpp"

std::shared_ptr<my_pipeline_type> build_pipeline()
{
    auto pipe = my_pipeline_type::create("Entry", 4, &pipepp::make_executor<my_executor_0>);

    auto _0 = pipe->front();

#define pew(FROM, TO) auto TO = FROM.create_and_link_output(#TO, false, 3, pipepp::link_as_is, &pipepp::make_executor<my_executor_0>);
#define pew_opt(FROM, TO) auto TO = FROM.create_and_link_output(#TO, true, 3, pipepp::link_as_is, &pipepp::make_executor<my_executor_0>);
    pew(_0, _1);
    pew(_0, _2);
    pew(_0, _3);
    pew(_1, _4);
    pew(_1, _5);
    pew(_4, _6);
    pew_opt(_6, _7);
    _5.link_output(_7, pipepp::link_as_is);
    _3.link_output(_7, pipepp::link_as_is);
    _2.link_output(_6, pipepp::link_as_is);
    pew(_7, _8);
    pew(_8, _9);

    return pipe;
}
