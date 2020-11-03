#pragma once
#include <pipepp/pipe.hpp>

namespace pipepp {
namespace impl__ {
class pipeline_factory_base {
};
} // namespace impl__

template <typename SharedData_, typename InitialPipe_>
class pipe_factory : public impl__::pipeline_factory_base {
public:
    using shared_data_type = SharedData_;
    using initial_pipe_type = InitialPipe_;
    using pipeline_input_type = typename initial_pipe_type::input_type;

public:

};
} // namespace pipepp
