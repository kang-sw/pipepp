#include "catch.hpp"
#include <iomanip>
#include <iostream>
#include <pipepp/thread_pool.hxx>

using namespace templates;
using namespace std;

TEST_CASE("thread pool default operation", "[thread_pool]")
{
    printf("THREAD POOL TEST --- \n");
    size_t num_cases = 1024;

    thread_pool thr{1024, 1};
    vector<pair<double, future<double>>> futures;

    auto ff = thr.launch_task([]() {
        vector<int> fs;
        fs.reserve(1024);
        fs.resize(512);
        fs.at(5661) = 3;
    });

    thread([&ff]() { ff.get_future().get(); }).detach();

    auto fut = thr.launch_task([]() { throw exception("hell, world!"); });
    REQUIRE_THROWS(fut.get_future().get());

    futures.reserve(num_cases);

    for (int i = 0; i < num_cases; ++i) {
        futures.emplace_back(
          i, thr.launch_task(
                  [](double c) {
                      this_thread::sleep_for(chrono::milliseconds(rand() % 128));
                      return c * c;
                  },
                  i)
               .get_future());
    }

    size_t num_error = 0;

    int index = 0;
    using chrono::system_clock;
    auto elapse_begin = system_clock::now();
    while (thr.num_pending_task() > 0) {
        cout << setw(8) << chrono::duration_cast<chrono::milliseconds>(system_clock::now() - elapse_begin).count() << " ms "
             << ">> Threads (" << setw(4) << thr.num_workers()
             << ") Count [" << setw(6) << thr.num_pending_task()
             << "] Avg Wait: "
             << chrono::duration<float>(thr.average_wait()).count()
             << (index++ % 10 == 0 ? "\n" : "\r");
        this_thread::sleep_for(33ms);
    }

    for (auto& pair : futures) {
        num_error += pair.first * pair.first != pair.second.get();
    }

    CHECK(thr.num_workers() != 2);
    REQUIRE(num_error == 0);

    printf("\n");
}