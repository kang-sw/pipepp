#include "sample_pipeline.hpp"

std::shared_ptr<my_pipeline_type> build_pipeline()
{
    auto pipe = my_pipeline_type::create("Entry", 4, &pipepp::make_executor<my_executor_0>);

    auto _0 = pipe->front();

#define pew(FROM, TO) auto TO = FROM.create_and_link_output(#TO, 3, pipepp::link_as_is, &pipepp::make_executor<my_executor_0>);
#define pew_opt(FROM, TO) auto TO = FROM.create_and_link_output(#TO, 3, pipepp::link_as_is, &pipepp::make_executor<my_executor_0>);
    /*pew(_0, _1);
    pew(_0, _2);
    pew(_0, _3);
    pew(_2, _11);
    pew(_2, _12);
    pew(_2, _13);
    pew(_11, _14);
    pew(_11, _15);
    pew(_11, _16);
    pew(_11, _17);
    pew(_12, _18);
    pew(_12, _19);
    _11.link_output(_18, pipepp::link_as_is);
    _11.link_output(_19, pipepp::link_as_is);
    _13.link_output(_17, pipepp::link_as_is);
    _13.link_output(_18, pipepp::link_as_is);
    _13.link_output(_19, pipepp::link_as_is);
    _19.prelaunch_tweaks().selective_input = true;
    pew(_12, _20);
    pew(_13, _21);
    pew(_13, _22);
    pew(_13, _23);
    _19.link_output(_20, pipepp::link_as_is);
    _19.link_output(_21, pipepp::link_as_is);
    _19.link_output(_22, pipepp::link_as_is);
    _21.link_output(_23, pipepp::link_as_is);
    pew(_1, _4);
    pew(_1, _5);
    pew(_4, _6);
    pew_opt(_6, _7);
    _5.link_output(_7, pipepp::link_as_is);
    _7.prelaunch_tweaks().selective_input = true;
    pew(_7, _8);
    pew(_8, _9);*/

    pew(_0, _1);
    pew(_0, _2);
    pew(_0, _3);
    pew(_3, _4);
    _1.link_output(_4, pipepp::link_as_is);
    _2.link_output(_4, pipepp::link_as_is);

    _4.prelaunch_tweaks().selective_input = true;
    _0.prelaunch_tweaks().selective_output = true;

    return pipe;
}
