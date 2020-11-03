#include <pipepp/log.hpp>
#include <chrono>
#include <ctime>
#include <iomanip>

using namespace std;

namespace utility {
logger_verbosity global_log_verbosity = logger_verbosity::none;

void default_pre_logger(logger_verbosity v, std::ostream& strm)
{
    char const* tags[] = {
      "none",
      "verbose",
      "debug",
      "info",
      "warning",
      "error",
      "fatal"};

    const char* tag = "always";

    if (v >= logger_verbosity::none && v <= logger_verbosity::fatal)
        tag = tags[static_cast<int>(v)];

    auto now = chrono::system_clock::now();
    auto time = chrono::system_clock::to_time_t(now);

    tm t;
    localtime_s(&t, &time);

    strm << put_time(&t, "[%H:%M:%S | ") << tag << "] ";
}
} // namespace ezipc