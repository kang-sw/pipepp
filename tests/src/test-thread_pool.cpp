#include "catch.hpp"
#include <pipepp/thread_pool.hxx>

using namespace templates;
using namespace std;

TEST_CASE("thread pool default operation", "[thread_pool]")
{
    printf("THREAD POOL TEST --- \n");
    size_t num_cases = 1022;

    thread_pool thr{1024};
    vector<pair<double, future<double>>> futures;

    futures.reserve(num_cases);

    for (int i = 0; i < num_cases; ++i) {
        futures.emplace_back(
          i, thr.launch_task(
               [](double c) {
                   this_thread::sleep_for(1ms);
                   putchar('.');
                   return c * c;
               },
               i));
    }

    size_t num_error = 0;
    thr.resize_worker_pool(1);
    std::this_thread::sleep_for(1000ms);
    thr.resize_worker_pool(24);
    REQUIRE(thr.num_workers() == 24);

    for (auto& pair : futures) {
        num_error += pair.first * pair.first != pair.second.get();
    }

    REQUIRE(num_error == 0);

    printf("\n");
}