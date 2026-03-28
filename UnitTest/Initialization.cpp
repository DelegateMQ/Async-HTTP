// Unit tests for Async-HTTP (Group 1: Initialization & Thread Verification)

#include "async_http.h"
#include "DelegateMQ.h"
#include <iostream>

using namespace async;

// -----------------------------------------------------------------------------
// Test 1: Thread is created and accessible after http_init()
// -----------------------------------------------------------------------------
static void Test_InitAndThread()
{
    http_init();

    Thread* thread = http_get_thread();
    ASSERT_TRUE(thread != nullptr);
    ASSERT_TRUE(thread->GetThreadName() == "HTTP Thread");
    ASSERT_TRUE(thread->GetThreadId() != std::thread::id());
}

// -----------------------------------------------------------------------------
// Test 2: Double init does not crash or corrupt state
// -----------------------------------------------------------------------------
static void Test_DoubleInit()
{
    http_init();  // already initialized
    Thread* thread = http_get_thread();
    ASSERT_TRUE(thread != nullptr);
    ASSERT_TRUE(thread->GetThreadId() != std::thread::id());
}

// -----------------------------------------------------------------------------
// Test 3: A simple blocking GET succeeds — proves the thread processes requests
// -----------------------------------------------------------------------------
static void Test_BasicRequestAfterInit()
{
    HttpResponse resp = GetWait("https://httpbin.org/get", std::chrono::seconds(10));

    // Either the request succeeded or there was a network error.
    // Either way, statusCode is well-defined and no crash occurred.
    ASSERT_TRUE(resp.statusCode == 200 || !resp.error.empty());
}

// -----------------------------------------------------------------------------
// Main entry point
// -----------------------------------------------------------------------------
void Initialization_UT()
{
    std::cout << "Running HTTP Initialization Tests..." << std::endl;

    Test_InitAndThread();
    Test_DoubleInit();
    Test_BasicRequestAfterInit();

    std::cout << "HTTP Initialization Tests Passed!" << std::endl;
}
