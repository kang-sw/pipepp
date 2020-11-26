#pragma once
#include <cstdint>

namespace pipepp ::detail {
class pipeline_base;
class pipe_base;
class pipeline_base;
class pipe_proxy_base;
class option_base;
} // namespace pipepp::detail

namespace pipepp {
template <typename SharedData_, typename Exec_>
class pipe_proxy;
enum class pipe_error : int;
enum class fence_index_t : size_t;
enum class pipe_id_t : size_t;
struct base_shared_context;
enum class executor_condition_t : uint8_t;
class execution_context;
} // namespace pipepp
