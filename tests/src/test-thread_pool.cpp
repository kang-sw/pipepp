#include "catch.hpp"
#include <pipepp/thread_pool.hxx>

using namespace templates;
using namespace std;

TEST_CASE("thread pool default operation", "[thread_pool]")
{
    size_t num_cases = 102484;

    thread_pool thr{1024};
    vector<pair<double, future<double>>> futures;

    futures.reserve(num_cases);

    for (int i = 0; i < num_cases; ++i) {
        futures.emplace_back(i, thr.launch_task([](double c) { return c * c; }, i));
    }

    size_t num_error = 0;
    thr.resize_worker_pool(4);

    for (auto& pair : futures) {
        num_error += pair.first * pair.first != pair.second.get();
    }

    REQUIRE(num_error == 0);
}