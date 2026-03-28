// Unit tests for Async-HTTP (Group 5: Thread Safety Stress Tests)

#include "async_http.h"
#include "DelegateMQ.h"
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <string>
#include <chrono>

using namespace async;
using namespace dmq;

static const int THREAD_COUNT  = 8;
static const int OPS_PER_THREAD = 5;  // Keep total low to avoid hammering httpbin

// -----------------------------------------------------------------------------
// Test 1: Multiple std::threads call GetWait() concurrently.
// The wrapper serializes all requests through m_thread — no curl race conditions,
// no mutex needed by the caller.
// -----------------------------------------------------------------------------
static void Test_Stress_BlockingConcurrent(AsyncHttp& http)
{
    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};
    std::atomic<int> failCount{0};

    for (int t = 0; t < THREAD_COUNT; ++t) {
        threads.emplace_back([&http, &successCount, &failCount]() {
            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                HttpResponse resp = http.GetWait("https://httpbin.org/get",
                                                 std::chrono::seconds(15));
                if (resp.ok())
                    successCount.fetch_add(1);
                else
                    failCount.fetch_add(1);
            }
        });
    }

    for (auto& th : threads)
        if (th.joinable()) th.join();

    int expected = THREAD_COUNT * OPS_PER_THREAD;
    if (failCount.load() > 0)
        std::cout << "  Note: " << failCount.load() << " requests failed (network?)" << std::endl;

    // All requests must complete — no hangs or crashes
    ASSERT_TRUE(successCount.load() + failCount.load() == expected);

    std::cout << "  Blocking stress: " << successCount.load() << "/" << expected
              << " succeeded" << std::endl;
}

// -----------------------------------------------------------------------------
// Test 2: Multiple std::threads issue fire-and-forget Get() calls.
// Each callback fires (on the HTTP worker thread) and increments a counter.
// -----------------------------------------------------------------------------
static void Test_Stress_CallbackConcurrent(AsyncHttp& http)
{
    constexpr int TOTAL = THREAD_COUNT * OPS_PER_THREAD;
    std::atomic<int> callbackCount{0};

    std::function<void(HttpResponse)> fn = [&](HttpResponse resp) {
        callbackCount.fetch_add(1);
    };

    std::vector<std::thread> threads;
    for (int t = 0; t < THREAD_COUNT; ++t) {
        threads.emplace_back([&http, &fn]() {
            for (int i = 0; i < OPS_PER_THREAD; ++i)
                http.Get("https://httpbin.org/get", MakeDelegate(fn));
        });
    }

    for (auto& th : threads)
        if (th.joinable()) th.join();

    // Wait for all callbacks — requests are queued on m_thread, allow generous time
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(TOTAL * 3 + 10);
    while (callbackCount.load() < TOTAL &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_TRUE(callbackCount.load() == TOTAL);
    std::cout << "  Callback stress: all " << TOTAL << " callbacks received" << std::endl;
}

// -----------------------------------------------------------------------------
// Test 3: Mix of blocking and fire-and-forget from different threads
// -----------------------------------------------------------------------------
static void Test_Stress_Mixed(AsyncHttp& http)
{
    std::atomic<int> blockingDone{0};
    std::atomic<int> callbackDone{0};
    constexpr int N = 4;

    std::function<void(HttpResponse)> fn = [&](HttpResponse) {
        callbackDone.fetch_add(1);
    };

    std::vector<std::thread> threads;

    // Half do blocking GETs
    for (int t = 0; t < N; ++t) {
        threads.emplace_back([&http, &blockingDone]() {
            HttpResponse resp = http.GetWait("https://httpbin.org/get",
                                             std::chrono::seconds(15));
            if (resp.ok() || !resp.error.empty())
                blockingDone.fetch_add(1);
        });
    }

    // Half do fire-and-forget GETs
    for (int t = 0; t < N; ++t) {
        threads.emplace_back([&http, &fn]() {
            http.Get("https://httpbin.org/get", MakeDelegate(fn));
        });
    }

    for (auto& th : threads)
        if (th.joinable()) th.join();

    // All blocking calls must have finished (threads joined above)
    ASSERT_TRUE(blockingDone.load() == N);

    // Wait for the non-blocking callbacks
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (callbackDone.load() < N && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_TRUE(callbackDone.load() == N);
    std::cout << "  Mixed stress: " << blockingDone.load() << " blocking + "
              << callbackDone.load() << " callback requests completed" << std::endl;
}

// -----------------------------------------------------------------------------
// Main entry point
// -----------------------------------------------------------------------------
void Stress_UT(AsyncHttp& http)
{
    std::cout << "Running HTTP Thread Safety Stress Tests..." << std::endl;

    Test_Stress_BlockingConcurrent(http);
    Test_Stress_CallbackConcurrent(http);
    Test_Stress_Mixed(http);

    std::cout << "HTTP Thread Safety Stress Tests Passed!" << std::endl;
}
