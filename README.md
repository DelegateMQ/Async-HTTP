![License MIT](https://img.shields.io/github/license/BehaviorTree/BehaviorTree.CPP?color=blue)
[![conan Ubuntu](https://github.com/endurodave/Async-HTTP/actions/workflows/cmake_ubuntu.yml/badge.svg)](https://github.com/endurodave/Async-HTTP/actions/workflows/cmake_ubuntu.yml)
[![conan Ubuntu](https://github.com/endurodave/Async-HTTP/actions/workflows/cmake_clang.yml/badge.svg)](https://github.com/endurodave/Async-HTTP/actions/workflows/cmake_clang.yml)
[![conan Windows](https://github.com/endurodave/Async-HTTP/actions/workflows/cmake_windows.yml/badge.svg)](https://github.com/endurodave/Async-HTTP/actions/workflows/cmake_windows.yml)

# Asynchronous HTTP Client using C++ Delegates

A thread-safe asynchronous HTTP client wrapper around libcurl, implemented using the C++ DelegateMQ delegate library. Supports Windows, Linux, and embedded systems.

# Table of Contents

- [Asynchronous HTTP Client using C++ Delegates](#asynchronous-http-client-using-c-delegates)
- [Table of Contents](#table-of-contents)
- [Overview](#overview)
  - [References](#references)
- [Getting Started](#getting-started)
- [Delegate Quick Start](#delegate-quick-start)
- [Asynchronous HTTP](#asynchronous-http)
- [Examples](#examples)
  - [Example 1: Blocking GET and POST](#example-1-blocking-get-and-post)
  - [Example 2: Uninterrupted Sequence on HTTP Thread](#example-2-uninterrupted-sequence-on-http-thread)
  - [Example 3: Multithreaded Blocking](#example-3-multithreaded-blocking)
  - [Example 4: Fire-and-Forget with Callback](#example-4-fire-and-forget-with-callback)
  - [Example 5: Future / Async](#example-5-future--async)
  - [Timing Comparison](#timing-comparison)

# Overview

libcurl is a powerful HTTP library, but using it safely from multiple threads requires careful management of the curl handle. Sharing a single `CURL*` handle across threads without synchronization leads to data races; creating a handle per thread wastes resources and loses the ability to pipeline or reuse connections.

This project wraps libcurl with the DelegateMQ delegate library to create a thread-safe asynchronous HTTP client. All HTTP operations are dispatched to a single dedicated `HttpThread`, which owns the curl handle exclusively. Callers on any thread invoke the `async` API and the delegate framework serializes access automatically.

**Architectural Benefits**

* **Eliminates Handle Contention:** A single reused `CURL*` handle is accessed only by `HttpThread`. Race conditions are architecturally impossible.

* **Sequential Integrity:** Requests dispatched from any thread are queued and executed in FIFO order on `HttpThread`. Dependent sequences are guaranteed to run without interleaving.

* **Non-Blocking Callers:** The calling thread (e.g., UI or main thread) returns immediately. Results arrive via blocking wait, callback, or `std::future` depending on which API variant is used.

**Key Features**

1. **Thread Safety:** A single background thread manages all libcurl interactions, preventing data races without any client-side locking.

2. **Blocking API:** `GetWait()` / `PostWait()` dispatch the request to `HttpThread` and block the caller until the response arrives or a timeout expires.

   * *Usage:* Ideal for sequential logic where the result is needed before proceeding.
   * *Signatures:*
     ```
     HttpResponse GetWait(url, timeout)
     HttpResponse PostWait(url, body, contentType, timeout)
     ```

3. **Fire-and-Forget API:** `Get()` / `Post()` return immediately. The caller supplies an `HttpCallback` delegate. If the callback targets a specific thread, the response is marshalled back to that thread; otherwise it fires directly on `HttpThread`.

   * *Usage:* Ideal for UI responsiveness and event-driven architectures.
   * *Signatures:*
     ```
     void Get(url, HttpCallback)
     void Post(url, body, contentType, HttpCallback)
     ```

4. **Future API:** `Get_future()` / `Post_future()` return a `std::future<HttpResponse>` immediately, allowing the main thread to overlap work and retrieve the result later.

   * *Usage:* Ideal for concurrent processing where the result is needed eventually but not immediately.
   * *Signatures:*
     ```
     std::future<HttpResponse> Get_future(url)
     std::future<HttpResponse> Post_future(url, body, contentType)
     ```

5. **Unit Tested:** Comprehensive unit test suite covering initialization, blocking API, fire-and-forget callbacks, futures, and stress/concurrency scenarios.

## References

* <a href="https://github.com/endurodave/DelegateMQ">DelegateMQ</a> - Invoke any C++ callable function synchronously, asynchronously, or on a remote endpoint.
* <a href="https://curl.se/libcurl/">libcurl</a> - A free and easy-to-use client-side URL transfer library.

# Getting Started

[CMake](https://cmake.org/) is used to create the project build files on any Windows or Linux machine. [libcurl](https://curl.se/libcurl/) must be available on the system.

1. Clone the repository.
2. Install libcurl (via [vcpkg](https://vcpkg.io/) or a system package manager).
3. From the repository root, run one of the following CMake commands:
   ```
   cmake -B Build .
   cmake -B Build . -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake
   ```
4. Build and run the project within the `Build` directory.

# Delegate Quick Start

The DelegateMQ library provides delegates and delegate containers. The example below creates a delegate targeting `MyFunc()`. The only difference between a synchronous and asynchronous call is the addition of a thread argument. See the [DelegateMQ](https://github.com/endurodave/DelegateMQ) repository for full details.

```cpp
#include "DelegateMQ.h"

using namespace dmq;

void MyFunc(int val)
{
    printf("%d", val);
}

int main()
{
    // Synchronous delegate — calls MyFunc() on the calling thread
    auto syncDelegate = MakeDelegate(&MyFunc);
    syncDelegate(123);

    // Asynchronous non-blocking delegate — dispatches MyFunc() to myThread
    auto asyncDelegate = MakeDelegate(&MyFunc, myThread);
    asyncDelegate(123);

    // Asynchronous blocking delegate — dispatches to myThread and waits
    auto asyncDelegateWait = MakeDelegate(&MyFunc, myThread, WAIT_INFINITE);
    asyncDelegateWait(123);
}
```

# Asynchronous HTTP

The file `async_http.h` defines the public API. All functions live in the `async` namespace.

```cpp
namespace async {

    constexpr dmq::Duration MAX_WAIT = std::chrono::minutes(2);

    /// HTTP response returned by all API variants.
    struct HttpResponse {
        int         statusCode = 0;
        std::string body;
        std::string error;
        bool ok() const { return statusCode >= 200 && statusCode < 300; }
    };

    /// Callback type for fire-and-forget Get()/Post().
    /// If the delegate carries a thread target, the response is marshalled to that thread.
    using HttpCallback = dmq::UnicastDelegate<void(HttpResponse)>;

    // Initialization & Management
    int     http_init();
    int     http_shutdown(dmq::Duration timeout = MAX_WAIT);
    Thread* http_get_thread();

    // Blocking API
    HttpResponse GetWait(const std::string& url, dmq::Duration timeout = MAX_WAIT);
    HttpResponse PostWait(const std::string& url, const std::string& body,
                          const std::string& contentType, dmq::Duration timeout = MAX_WAIT);

    // Fire-and-Forget API
    void Get(const std::string& url, HttpCallback cb);
    void Post(const std::string& url, const std::string& body,
              const std::string& contentType, HttpCallback cb);

    // Future API
    std::future<HttpResponse> Get_future(const std::string& url);
    std::future<HttpResponse> Post_future(const std::string& url, const std::string& body,
                                          const std::string& contentType);

} // namespace async
```

The file `async_http.cpp` implements each function. All curl operations execute inside private worker functions (`HttpGet`, `HttpPost`) that run exclusively on `HttpThread`. Public API functions dispatch to that thread via DelegateMQ delegates.

```cpp
HttpResponse GetWait(const std::string& url, dmq::Duration timeout) {
    if (HttpThread.GetThreadId() != std::this_thread::get_id()) {
        auto delegate = MakeDelegate(HttpGet, HttpThread, timeout);
        auto retVal = delegate.AsyncInvoke(url);
        if (retVal.has_value())
            return std::any_cast<HttpResponse>(retVal.value());
        HttpResponse err; err.error = "Request timed out"; return err;
    }
    return HttpGet(url);  // already on HttpThread — call directly
}
```

The same-thread check (`GetThreadId() != get_id()`) allows code running directly on `HttpThread` to call the API without re-dispatching, enabling uninterrupted request sequences (see Example 2).

# Examples

The `main()` function initializes the system, runs all five examples, then executes the unit test suite.

```cpp
int main()
{
    callbackThread.CreateThread();
    for (int i = 0; i < WORKER_THREAD_CNT; ++i)
        workerThreads[i].CreateThread();

    async::http_init();

    example1();
    example2();
    auto blockingDuration    = example3();
    auto nonBlockingDuration = example4();
    example_future();

    // Wait for fire-and-forget callback to complete
    while (!completeFlag.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    RunUnitTests();
    async::http_shutdown();
}
```

## Example 1: Blocking GET and POST

The simplest usage pattern. `GetWait()` and `PostWait()` dispatch the request to `HttpThread` and block the caller until the response arrives.

```cpp
void example1()
{
    async::HttpResponse resp = async::GetWait("https://httpbin.org/get");
    printf("GET  status=%d  ok=%s\n", resp.statusCode, resp.ok() ? "yes" : "no");

    resp = async::PostWait("https://httpbin.org/post",
                           "{\"example\":1}",
                           "application/json");
    printf("POST status=%d  ok=%s\n", resp.statusCode, resp.ok() ? "yes" : "no");
}
```

## Example 2: Uninterrupted Sequence on HTTP Thread

By dispatching a function directly onto `HttpThread` via a delegate, multiple requests execute as an atomic sequence. No other caller can interleave between them. This is analogous to a transaction.

```cpp
static int RunSequenceOnHttpThread()
{
    // Runs on HttpThread — GetWait/PostWait detect same-thread and call curl directly
    auto r1 = async::GetWait("https://httpbin.org/get");
    auto r2 = async::PostWait("https://httpbin.org/post", "{}", "application/json");
    return r1.ok() && r2.ok() ? 0 : 1;
}

void example2()
{
    Thread* httpThread = async::http_get_thread();
    auto delegate = MakeDelegate(&RunSequenceOnHttpThread, *httpThread, async::MAX_WAIT);
    auto retVal = delegate.AsyncInvoke();
    if (retVal.has_value())
        printf("Sequence return value: %d\n", any_cast<int>(retVal.value()));
}
```

## Example 3: Multithreaded Blocking

Multiple worker threads call `GetWait()` concurrently. The HTTP wrapper serializes all requests through a single curl handle on `HttpThread` — no client-side locking required.

```cpp
static void WorkerGetWait(std::string threadName)
{
    async::HttpResponse resp = async::GetWait("https://httpbin.org/get",
                                              std::chrono::seconds(15));
    printf("[%s] GET status=%d\n", threadName.c_str(), resp.statusCode);

    if (++workersDone >= WORKER_THREAD_CNT) {
        std::lock_guard<std::mutex> lock(cvMtx);
        ready = true;
        cv.notify_all();
    }
}

std::chrono::microseconds example3()
{
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < WORKER_THREAD_CNT; ++i) {
        auto delegate = MakeDelegate(WorkerGetWait, workerThreads[i]);
        delegate(workerThreads[i].GetThreadName());
    }

    std::unique_lock<std::mutex> lock(cvMtx);
    while (!ready) cv.wait(lock);

    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
}
```

## Example 4: Fire-and-Forget with Callback

`Get()` returns immediately. The `HttpCallback` delegate targets `callbackThread`, so the response is automatically marshalled to that thread when the HTTP operation completes.

```cpp
static void OnGetResponse(async::HttpResponse resp)
{
    // Executes on callbackThread
    printf("[callbackThread] GET status=%d ok=%s\n",
           resp.statusCode, resp.ok() ? "yes" : "no");
    completeCallback(0);
}

std::chrono::microseconds example4()
{
    auto start = std::chrono::high_resolution_clock::now();

    // Callback targets callbackThread — response is marshalled back there
    async::Get("https://httpbin.org/get",
               MakeDelegate(&OnGetResponse, callbackThread));

    // Returns immediately — dispatch time is in microseconds
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
}
```

## Example 5: Future / Async

`Get_future()` returns a `std::future<HttpResponse>` immediately. The main thread can continue doing work and call `.get()` when it needs the result. `.get()` blocks only if the HTTP request is still in-flight.

```cpp
void example_future()
{
    // Launch HTTP request — returns immediately
    auto future = async::Get_future("https://httpbin.org/get");

    // Main thread continues working while HTTP runs in background
    for (int i = 0; i < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        printf("[Main] Working... %d%%\n", (i + 1) * 33);
    }

    // Blocks only if still in-flight
    async::HttpResponse resp = future.get();
    printf("[Main] Response: status=%d ok=%s\n", resp.statusCode, resp.ok() ? "yes" : "no");

    // POST future
    auto postFuture = async::Post_future("https://httpbin.org/post",
                                         "{\"future\":true}",
                                         "application/json");
    async::HttpResponse postResp = postFuture.get();
    printf("[Main] POST future: status=%d\n", postResp.statusCode);
}
```

## Timing Comparison

Example 3 (blocking) and Example 4 (fire-and-forget) illustrate the difference in dispatch time from the calling thread's perspective. The blocking call waits for the full HTTP round-trip; the fire-and-forget call returns in microseconds.

```
Blocking dispatch time:      459135 us
Non-blocking dispatch time:  17 us
```
