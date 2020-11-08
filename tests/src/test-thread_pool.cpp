#include "catch.hpp"
#include <pipepp/thread_pool.hxx>

using namespace templates;
using namespace std;

TEST_CASE("thread pool default operation", "[thread_pool]")
{
    printf("THREAD POOL TEST --- \n");
    size_t num_cases = 1048;

    thread_pool thr{1024, 1};
    vector<pair<double, future<double>>> futures;

    futures.reserve(num_cases);

    for (int i = 0; i < num_cases; ++i) {
        futures.emplace_back(
          i, thr.launch_task(
               [](double c) {
                   this_thread::sleep_for(chrono::milliseconds(rand() % 50));
                   putchar('.');
                   return c * c;
               },
               i));
    }

    size_t num_error = 0;

    for (auto& pair : futures) {
        num_error += pair.first * pair.first != pair.second.get();
    }

    printf("\nNUM_THR: %llu", thr.num_workers());
    CHECK(thr.num_workers() != 2);
    REQUIRE(num_error == 0);

    printf("\n");
}