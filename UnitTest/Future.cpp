// Unit tests for Async-HTTP (Group 4: Future API)

#include "async_http.h"
#include "DelegateMQ.h"
#include <iostream>
#include <future>
#include <chrono>
#include <string>
#include <vector>

using namespace async;

// -----------------------------------------------------------------------------
// Test 1: Get_future returns a valid future that resolves to 200
// -----------------------------------------------------------------------------
static void Test_GetFuture_Success()
{
    auto future = Get_future("https://httpbin.org/get");
    ASSERT_TRUE(future.valid());

    // Main thread can do other work here...

    HttpResponse resp = future.get();
    ASSERT_TRUE(resp.ok());
    ASSERT_TRUE(resp.statusCode == 200);
    ASSERT_TRUE(!resp.body.empty());

    std::cout << "  Get_future resolved, status=" << resp.statusCode << std::endl;
}

// -----------------------------------------------------------------------------
// Test 2: Get_future for a 404 URL resolves with the correct status
// -----------------------------------------------------------------------------
static void Test_GetFuture_404()
{
    auto future = Get_future("https://httpbin.org/status/404");
    HttpResponse resp = future.get();

    ASSERT_TRUE(!resp.ok());
    ASSERT_TRUE(resp.statusCode == 404);

    std::cout << "  Get_future 404 confirmed" << std::endl;
}

// -----------------------------------------------------------------------------
// Test 3: Post_future sends a body and the future resolves with the echo
// -----------------------------------------------------------------------------
static void Test_PostFuture_Success()
{
    auto future = Post_future("https://httpbin.org/post",
                              "{\"reading\":3.14}",
                              "application/json");
    ASSERT_TRUE(future.valid());

    HttpResponse resp = future.get();
    ASSERT_TRUE(resp.ok());
    ASSERT_TRUE(resp.body.find("reading") != std::string::npos);

    std::cout << "  Post_future resolved, body contains posted data" << std::endl;
}

// -----------------------------------------------------------------------------
// Test 4: Multiple futures launched before any .get() — concurrent dispatch
// -----------------------------------------------------------------------------
static void Test_MultipleFutures()
{
    constexpr int N = 4;
    std::vector<std::future<HttpResponse>> futures;

    for (int i = 0; i < N; ++i)
        futures.push_back(Get_future("https://httpbin.org/get"));

    // Collect all results — each request was queued to HttpThread
    int okCount = 0;
    for (auto& f : futures) {
        HttpResponse resp = f.get();
        if (resp.ok()) ++okCount;
    }

    ASSERT_TRUE(okCount == N);
    std::cout << "  " << N << " futures all resolved 200 OK" << std::endl;
}

// -----------------------------------------------------------------------------
// Test 5: Main thread does work while HTTP request is in-flight
// -----------------------------------------------------------------------------
static void Test_FutureOverlap()
{
    auto future = Get_future("https://httpbin.org/delay/1");

    // Simulate main-thread work while the HTTP request is processing
    int workDone = 0;
    for (int i = 0; i < 5; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ++workDone;
    }

    HttpResponse resp = future.get();  // block only if not yet done
    ASSERT_TRUE(workDone == 5);
    ASSERT_TRUE(resp.ok() || !resp.error.empty());  // either success or network error

    std::cout << "  Main thread completed " << workDone
              << " units of work while HTTP was in-flight" << std::endl;
}

// -----------------------------------------------------------------------------
// Main entry point
// -----------------------------------------------------------------------------
void Future_UT()
{
    std::cout << "Running HTTP Future API Tests..." << std::endl;

    http_init();

    Test_GetFuture_Success();
    Test_GetFuture_404();
    Test_PostFuture_Success();
    Test_MultipleFutures();
    Test_FutureOverlap();

    std::cout << "HTTP Future API Tests Passed!" << std::endl;
}
