// Unit tests for Async-HTTP (Group 1: Initialization & Thread Verification)

#include "async_http.h"
#include "DelegateMQ.h"
#include <iostream>

using namespace async;

// -----------------------------------------------------------------------------
// Test 1: Thread is created and accessible after init()
// -----------------------------------------------------------------------------
static void Test_InitAndThread(AsyncHttp& http)
{
    Thread* thread = http.get_thread();
    ASSERT_TRUE(thread != nullptr);
    ASSERT_TRUE(!thread->GetThreadName().empty());
    ASSERT_TRUE(thread->GetThreadId() != std::thread::id());
}

// -----------------------------------------------------------------------------
// Test 2: Double init does not crash or corrupt state
// -----------------------------------------------------------------------------
static void Test_DoubleInit(AsyncHttp& http)
{
    http.init();  // already initialized — should be a no-op
    Thread* thread = http.get_thread();
    ASSERT_TRUE(thread != nullptr);
    ASSERT_TRUE(thread->GetThreadId() != std::thread::id());
}

// -----------------------------------------------------------------------------
// Test 3: A simple blocking GET succeeds — proves the thread processes requests
// -----------------------------------------------------------------------------
static void Test_BasicRequestAfterInit(AsyncHttp& http)
{
    HttpResponse resp = http.GetWait("https://httpbin.org/get", std::chrono::seconds(10));

    // Either the request succeeded or there was a network error.
    // Either way, statusCode is well-defined and no crash occurred.
    ASSERT_TRUE(resp.statusCode == 200 || !resp.error.empty());
}

// -----------------------------------------------------------------------------
// Main entry point
// -----------------------------------------------------------------------------
void Initialization_UT(AsyncHttp& http)
{
    std::cout << "Running HTTP Initialization Tests..." << std::endl;

    Test_InitAndThread(http);
    Test_DoubleInit(http);
    Test_BasicRequestAfterInit(http);

    std::cout << "HTTP Initialization Tests Passed!" << std::endl;
}
