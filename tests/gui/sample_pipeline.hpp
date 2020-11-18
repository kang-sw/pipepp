#pragma once
#include "pipepp/execution_context.hpp"
#include "pipepp/pipe.hpp"
#include "pipepp/pipeline.hpp"

struct my_shared_context : public pipepp::base_shared_context {
};

struct my_executor_0 {
    PIPEPP_DEFINE_OPTION_CLASS(my_executor_0);
    PIPEPP_DEFINE_OPTION(bool, initial_flag, false, "MyCategory.Flags", "Inital flag type check");
    PIPEPP_DEFINE_OPTION(int, initial_int, 15, "MyCategory.Integers", "Initial integer type check");
    PIPEPP_DEFINE_OPTION(int, wait_ms, 150, "MyCategory.Integers", "Initial integer type check");

    using input_type = int;
    using output_type = int;

    pipepp::pipe_error invoke(pipepp::execution_context& o, input_type a, output_type b)
    {
        PIPEPP_REGISTER_CONTEXT(o);

        PIPEPP_ELAPSE_SCOPE("MyTimer");
        PIPEPP_ELAPSE_SCOPE("Elapse0");

        PIPEPP_ELAPSE_BLOCK("ElapseA") {}
        PIPEPP_ELAPSE_BLOCK("ElapseB") {}
        PIPEPP_ELAPSE_BLOCK("ElapseC") {}
        PIPEPP_ELAPSE_BLOCK("ElapseD") {}

        PIPEPP_ELAPSE_BLOCK("Elapse1")
        {
            using namespace std::chrono_literals;
            PIPEPP_ELAPSE_BLOCK("Elapse2")
            {
                std::this_thread::sleep_for(wait_ms(o) * 1ms);
            }
        }
        return pipepp::pipe_error::ok;
    }
};

using my_pipeline_type = pipepp::pipeline<my_shared_context, my_executor_0>;
std::shared_ptr<my_pipeline_type> build_pipeline();