// Asynchronous HTTP client using C++ Delegates
// @see https://github.com/endurodave/Async-HTTP
// @see https://github.com/endurodave/DelegateMQ
// David Lafreniere, 2026

// This application demonstrates the usage patterns for the Async-HTTP wrapper:
// 1. Blocking GET/POST   — caller blocks until the response arrives.
// 2. Internal thread     — run work directly on the HTTP thread (uninterrupted sequence).
// 3. Multithreaded blocking — multiple threads call GetWait() concurrently; timing comparison.
// 4. Fire-and-forget     — Get() returns immediately; response delivered via delegate callback.
// 5. Future/Async        — Get_future() returns a std::future to interleave main-thread work.

#include "DelegateMQ.h"
#include "async_http.h"
#include "async_http_ut.h"
#include <iostream>
#include <string>
#include <cstdarg>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <thread>

using namespace std;
using namespace dmq;

// ---------------------------------------------------------------------------
// Worker threads for multi-threaded examples
// ---------------------------------------------------------------------------
static const int WORKER_THREAD_CNT = 2;
Thread workerThreads[] = {
    { "WorkerThread1" },
    { "WorkerThread2" }
};

Thread callbackThread("CallbackThread");

static std::mutex        printMutex;
static std::mutex        cvMtx;
static std::condition_variable cv;
static bool              ready = false;
static MulticastDelegateSafe<void(int)> completeCallback;
static std::atomic<bool> completeFlag{false};

std::atomic<bool> processTimerExit{false};
static void ProcessTimers()
{
    while (!processTimerExit.load()) {
        Timer::ProcessTimers();
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
}

void printf_safe(const char* format, ...)
{
    std::lock_guard<std::mutex> lock(printMutex);
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}

// ---------------------------------------------------------------------------
// Example 1 — Simple blocking GET and POST
// ---------------------------------------------------------------------------
void example1()
{
    printf_safe("\n--- Example 1: Blocking GET / POST ---\n");

    async::HttpResponse resp = async::GetWait("https://httpbin.org/get");
    printf_safe("GET  status=%d  ok=%s\n", resp.statusCode, resp.ok() ? "yes" : "no");

    resp = async::PostWait("https://httpbin.org/post",
                           "{\"example\":1}",
                           "application/json");
    printf_safe("POST status=%d  ok=%s\n", resp.statusCode, resp.ok() ? "yes" : "no");
}

// ---------------------------------------------------------------------------
// Example 2 — Run a sequence of requests uninterrupted on the HTTP thread
//
// By dispatching directly onto the HTTP thread, several requests execute
// without interleaving from other callers, analogous to a transaction.
// ---------------------------------------------------------------------------
static int RunSequenceOnHttpThread()
{
    // This function runs on HttpThread — GetWait/PostWait detect same-thread
    // and call curl directly without re-dispatching.
    auto r1 = async::GetWait("https://httpbin.org/get");
    printf_safe("  seq GET  status=%d\n", r1.statusCode);

    auto r2 = async::PostWait("https://httpbin.org/post", "{}", "application/json");
    printf_safe("  seq POST status=%d\n", r2.statusCode);

    return r1.ok() && r2.ok() ? 0 : 1;
}

void example2()
{
    printf_safe("\n--- Example 2: Uninterrupted Sequence on HTTP Thread ---\n");

    Thread* httpThread = async::http_get_thread();
    auto delegate = MakeDelegate(&RunSequenceOnHttpThread, *httpThread, async::MAX_WAIT);
    auto retVal = delegate.AsyncInvoke();
    if (retVal.has_value())
        printf_safe("Sequence return value: %d\n", any_cast<int>(retVal.value()));
}

// ---------------------------------------------------------------------------
// Example 3 — Multithreaded blocking: N worker threads all calling GetWait()
// concurrently. The HTTP wrapper serializes them through a single curl handle.
// ---------------------------------------------------------------------------
static std::atomic<int> workersDone{0};

static void WorkerGetWait(std::string threadName)
{
    async::HttpResponse resp = async::GetWait("https://httpbin.org/get",
                                              std::chrono::seconds(15));
    printf_safe("  [%s] GET status=%d\n", threadName.c_str(), resp.statusCode);

    if (++workersDone >= WORKER_THREAD_CNT) {
        std::lock_guard<std::mutex> lock(cvMtx);
        ready = true;
        cv.notify_all();
    }
}

std::chrono::microseconds example3()
{
    printf_safe("\n--- Example 3: Multithreaded Blocking ---\n");

    workersDone.store(0);
    ready = false;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < WORKER_THREAD_CNT; ++i) {
        auto delegate = MakeDelegate(WorkerGetWait, workerThreads[i]);
        delegate(workerThreads[i].GetThreadName());
    }

    std::unique_lock<std::mutex> lock(cvMtx);
    while (!ready) cv.wait(lock);

    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
}

// ---------------------------------------------------------------------------
// Example 4 — Fire-and-forget: Get() returns immediately; the response is
// delivered to callbackThread via delegate dispatch.
// ---------------------------------------------------------------------------
static void OnGetResponse(async::HttpResponse resp)
{
    // Executes on callbackThread
    printf_safe("  [callbackThread] GET callback status=%d ok=%s\n",
                resp.statusCode, resp.ok() ? "yes" : "no");

    completeCallback(0);
}

std::chrono::microseconds example4()
{
    printf_safe("\n--- Example 4: Fire-and-Forget with Callback ---\n");

    completeFlag.store(false);

    auto CompleteLambda = +[](int) { completeFlag.store(true); };
    completeCallback += MakeDelegate(CompleteLambda);

    auto start = std::chrono::high_resolution_clock::now();

    // Callback targets callbackThread — response is marshalled back there
    async::Get("https://httpbin.org/get",
               MakeDelegate(&OnGetResponse, callbackThread));

    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
}

// ---------------------------------------------------------------------------
// Example 5 — Future/Async: main thread does work while HTTP runs in background
// ---------------------------------------------------------------------------
void example_future()
{
    printf_safe("\n--- Example 5: Future / Async ---\n");

    // Launch HTTP request — returns immediately
    auto future = async::Get_future("https://httpbin.org/get");
    printf_safe("[Main] HTTP request dispatched, doing other work...\n");

    // Simulate concurrent main-thread work
    for (int i = 0; i < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        printf_safe("[Main] Working... %d%%\n", (i + 1) * 33);
    }

    printf_safe("[Main] Waiting for HTTP response...\n");
    async::HttpResponse resp = future.get();  // blocks only if still in-flight
    printf_safe("[Main] Response received: status=%d ok=%s\n",
                resp.statusCode, resp.ok() ? "yes" : "no");

    // POST future
    auto postFuture = async::Post_future("https://httpbin.org/post",
                                         "{\"future\":true}",
                                         "application/json");
    async::HttpResponse postResp = postFuture.get();
    printf_safe("[Main] POST future: status=%d\n", postResp.statusCode);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main()
{
    std::thread timerThread(ProcessTimers);

    // Start worker threads
    callbackThread.CreateThread();
    for (int i = 0; i < WORKER_THREAD_CNT; ++i)
        workerThreads[i].CreateThread();

    // Initialize async HTTP (starts the HTTP worker thread)
    int rc = async::http_init();
    if (rc != 0) {
        fprintf(stderr, "http_init() failed: %d\n", rc);
        return 1;
    }

    // Run examples
    example1();
    example2();
    auto blockingDuration    = example3();
    auto nonBlockingDuration = example4();
    example_future();

    // Wait for the fire-and-forget callback in example4 to complete
    while (!completeFlag.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    printf_safe("\nBlocking dispatch time:     %lld us\n", (long long)blockingDuration.count());
    printf_safe("Non-blocking dispatch time: %lld us\n",  (long long)nonBlockingDuration.count());

    // Exit worker threads
    callbackThread.ExitThread();
    for (int i = 0; i < WORKER_THREAD_CNT; ++i)
        workerThreads[i].ExitThread();

    // Run unit tests
    RunUnitTests();

    // Shutdown HTTP client
    async::http_shutdown();

    processTimerExit.store(true);
    if (timerThread.joinable())
        timerThread.join();

    return 0;
}
