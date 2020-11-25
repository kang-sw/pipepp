#pragma once
#include "pipepp/pipepp.h"

struct my_shared_context : public pipepp::base_shared_context {
};

struct my_executor_0 {
    PIPEPP_DECLARE_OPTION_CLASS(my_executor_0);
    PIPEPP_OPTION_FULL(bool, initial_flag, false, "MyCategory.Flags", "Inital flag type check");
    PIPEPP_OPTION_FULL(int, initial_int, 15, "MyCategory.Integers", "Initial integer type check");
    PIPEPP_OPTION_FULL(int, wait_ms, 150, "MyCategory.Integers", "Initial integer type check");
    PIPEPP_OPTION_AUTO(values, std::vector<int>({1, 2, 3, 4, 5}), "Else.Integers", "Initial integer type check");

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

            PIPEPP_STORE_DEBUG_DATA("My Int", 150 + rand() % 15);
            PIPEPP_STORE_DEBUG_DATA("My Float", 150.3f + rand() % 22);
            PIPEPP_STORE_DEBUG_DATA("My String", "Hell, world!");
            PIPEPP_STORE_DEBUG_DATA("My Boolean", false);
            PIPEPP_STORE_DEBUG_DATA("My Any", std::vector<int>{});
            PIPEPP_STORE_DEBUG_DATA("My Int", 150 + rand() % 15);
            PIPEPP_STORE_DEBUG_DATA("My Float", 150.3f + rand() % 22);
            PIPEPP_STORE_DEBUG_DATA("My String", "Hell, world!");
            PIPEPP_STORE_DEBUG_DATA("My Boolean", false);
            PIPEPP_STORE_DEBUG_DATA("My Any", std::vector<int>{});
            PIPEPP_STORE_DEBUG_DATA("My Int", 150 + rand() % 15);
            PIPEPP_STORE_DEBUG_DATA("My Float", 150.3f + rand() % 22);
            PIPEPP_STORE_DEBUG_DATA("My String", "Hell, world!");
            PIPEPP_STORE_DEBUG_DATA("My Boolean", false);
            PIPEPP_STORE_DEBUG_DATA("My Any", std::vector<int>{});
            PIPEPP_STORE_DEBUG_DATA("My Int", 150 + rand() % 15);
            PIPEPP_STORE_DEBUG_DATA("My Float", 150.3f + rand() % 22);
            PIPEPP_STORE_DEBUG_DATA("My String", "Hell, world!");
            PIPEPP_STORE_DEBUG_DATA("My Boolean", false);
            PIPEPP_STORE_DEBUG_DATA("My Any", std::vector<int>{});
        }
        return pipepp::pipe_error::ok;
    }
};

using my_pipeline_type = pipepp::pipeline<my_shared_context, my_executor_0>;
std::shared_ptr<my_pipeline_type> build_pipeline();