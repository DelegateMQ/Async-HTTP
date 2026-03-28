// Unit tests for Async-HTTP (Group 3: Fire-and-Forget Callback API)

#include "async_http.h"
#include "DelegateMQ.h"
#include <iostream>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <string>

using namespace async;
using namespace dmq;

// -----------------------------------------------------------------------
// Helper: blocks until flag is set or timeout expires
// -----------------------------------------------------------------------
static bool WaitForFlag(std::atomic<bool>& flag, std::chrono::milliseconds timeout)
{
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!flag.load()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

// -----------------------------------------------------------------------------
// Test 1: Get() with a plain sync callback — fires on HTTP worker thread
// -----------------------------------------------------------------------------
static void Test_Get_SyncCallback()
{
    std::atomic<bool> fired{false};
    std::atomic<int>  receivedStatus{0};

    auto onResponse = +[](HttpResponse resp) {
        // This runs on the HTTP worker thread.
        // No access to caller-local variables — use statics to verify.
    };

    // For verification, use a lambda that captures by reference via a
    // std::function wrapper (sync delegate holds std::function).
    std::function<void(HttpResponse)> fn = [&](HttpResponse resp) {
        receivedStatus.store(resp.statusCode);
        fired.store(true);
    };

    Get("https://httpbin.org/get", MakeDelegate(fn));

    bool ok = WaitForFlag(fired, std::chrono::seconds(15));
    ASSERT_TRUE(ok);
    ASSERT_TRUE(receivedStatus.load() == 200);

    std::cout << "  Get() sync callback fired, status=" << receivedStatus.load() << std::endl;
}

// -----------------------------------------------------------------------------
// Test 2: Get() with an async callback — fires on callerThread
// -----------------------------------------------------------------------------
static void Test_Get_AsyncCallback()
{
    Thread callerThread("CallbackCallerThread");
    callerThread.CreateThread();

    std::atomic<bool> fired{false};
    std::atomic<int>  receivedStatus{0};

    // The lambda will be dispatched to callerThread
    auto onResponse = +[](HttpResponse resp) {
        // Executed on callerThread — nothing extra to verify here without
        // thread-local storage; the atomic update proves dispatch happened.
    };

    std::function<void(HttpResponse)> fn = [&](HttpResponse resp) {
        receivedStatus.store(resp.statusCode);
        fired.store(true);
    };

    Get("https://httpbin.org/get",
        MakeDelegate(fn, callerThread));

    bool ok = WaitForFlag(fired, std::chrono::seconds(15));
    ASSERT_TRUE(ok);
    ASSERT_TRUE(receivedStatus.load() == 200);

    callerThread.ExitThread();
    std::cout << "  Get() async callback fired on callerThread, status="
              << receivedStatus.load() << std::endl;
}

// -----------------------------------------------------------------------------
// Test 3: Post() with sync callback — body delivered to callback
// -----------------------------------------------------------------------------
static void Test_Post_SyncCallback()
{
    std::atomic<bool> fired{false};
    std::string       receivedBody;
    std::mutex        bodyMutex;

    std::function<void(HttpResponse)> fn = [&](HttpResponse resp) {
        {
            std::lock_guard<std::mutex> lock(bodyMutex);
            receivedBody = resp.body;
        }
        fired.store(true);
    };

    Post("https://httpbin.org/post",
         "{\"sensor\":42}",
         "application/json",
         MakeDelegate(fn));

    bool ok = WaitForFlag(fired, std::chrono::seconds(15));
    ASSERT_TRUE(ok);
    {
        std::lock_guard<std::mutex> lock(bodyMutex);
        ASSERT_TRUE(receivedBody.find("sensor") != std::string::npos);
    }
    std::cout << "  Post() sync callback fired, body contains posted data" << std::endl;
}

// -----------------------------------------------------------------------------
// Test 4: Multiple concurrent Get() calls — all callbacks arrive
// -----------------------------------------------------------------------------
static void Test_MultipleCallbacks()
{
    constexpr int N = 5;
    std::atomic<int> count{0};

    std::function<void(HttpResponse)> fn = [&](HttpResponse resp) {
        ASSERT_TRUE(resp.ok());
        count.fetch_add(1);
    };

    for (int i = 0; i < N; ++i)
        Get("https://httpbin.org/get", MakeDelegate(fn));

    // Wait up to 30 s for all N callbacks
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (count.load() < N && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ASSERT_TRUE(count.load() == N);
    std::cout << "  " << N << " concurrent Get() callbacks all received" << std::endl;
}

// -----------------------------------------------------------------------------
// Main entry point
// -----------------------------------------------------------------------------
void Callback_UT()
{
    std::cout << "Running HTTP Fire-and-Forget Callback Tests..." << std::endl;

    http_init();

    Test_Get_SyncCallback();
    Test_Get_AsyncCallback();
    Test_Post_SyncCallback();
    Test_MultipleCallbacks();

    std::cout << "HTTP Fire-and-Forget Callback Tests Passed!" << std::endl;
}
