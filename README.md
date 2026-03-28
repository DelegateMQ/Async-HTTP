![License MIT](https://img.shields.io/github/license/BehaviorTree/BehaviorTree.CPP?color=blue)
[![conan Ubuntu](https://github.com/DelegateMQ/Async-HTTP/actions/workflows/cmake_ubuntu.yml/badge.svg)](https://github.com/DelegateMQ/Async-HTTP/actions/workflows/cmake_ubuntu.yml)
[![conan Ubuntu](https://github.com/DelegateMQ/Async-HTTP/actions/workflows/cmake_clang.yml/badge.svg)](https://github.com/DelegateMQ/Async-HTTP/actions/workflows/cmake_clang.yml)
[![conan Windows](https://github.com/DelegateMQ/Async-HTTP/actions/workflows/cmake_windows.yml/badge.svg)](https://github.com/DelegateMQ/Async-HTTP/actions/workflows/cmake_windows.yml)

# Asynchronous HTTP Client using C++ Delegates

A thread-safe asynchronous HTTP client wrapper around libcurl, implemented using the C++ DelegateMQ delegate library. Supports Windows, Linux, and embedded systems.

# Table of Contents

- [Asynchronous HTTP Client using C++ Delegates](#asynchronous-http-client-using-c-delegates)
- [Table of Contents](#table-of-contents)
- [Overview](#overview)
  - [References](#references)
- [Use Cases and Design Considerations](#use-cases-and-design-considerations)
  - [Target Environments](#target-environments)
  - [Why libcurl on Embedded](#why-libcurl-on-embedded)
  - [Threading Model](#threading-model)
  - [Memory Allocation](#memory-allocation)
  - [Pros and Cons](#pros-and-cons)
  - [When to Use Something Else](#when-to-use-something-else)
- [Getting Started](#getting-started)
- [Delegate Quick Start](#delegate-quick-start)
- [Asynchronous HTTP](#asynchronous-http)
- [Examples](#examples)
  - [Example 1: Blocking GET and POST](#example-1-blocking-get-and-post)
  - [Example 2: Uninterrupted Sequence on HTTP Thread](#example-2-uninterrupted-sequence-on-http-thread)
  - [Example 3: Multithreaded Blocking](#example-3-multithreaded-blocking)
  - [Example 4: Fire-and-Forget with Callback](#example-4-fire-and-forget-with-callback)
  - [Example 5: Future / Async](#example-5-future--async)
  - [Multiple Instances](#multiple-instances)
  - [Timing Comparison](#timing-comparison)

# Overview

libcurl is a powerful HTTP library, but using it safely from multiple threads requires careful management of the curl handle. Sharing a single `CURL*` handle across threads without synchronization leads to data races; creating a handle per thread wastes resources and loses the ability to reuse connections.

This project provides the `AsyncHttp` class, which wraps libcurl with the DelegateMQ delegate library. Each `AsyncHttp` instance owns a dedicated worker thread and `CURL*` handle. All HTTP operations are dispatched to that thread, which serializes curl access automatically. Callers on any thread invoke the API and the delegate framework handles thread marshalling.

**Multiple independent instances** may be created to run HTTP clients concurrently — requests to different instances execute in parallel on separate threads and curl handles.

**Architectural Benefits**

* **Eliminates Handle Contention:** A single reused `CURL*` handle is accessed only by the owning thread. Race conditions are architecturally impossible within an instance.

* **Parallel Instances:** Create multiple `AsyncHttp` instances to run independent request streams in parallel — each with its own thread and curl handle.

* **Sequential Integrity:** Requests dispatched from any thread are queued and executed in FIFO order on the instance's worker thread. Dependent sequences are guaranteed to run without interleaving.

* **Non-Blocking Callers:** The calling thread returns immediately. Results arrive via blocking wait, callback, or `std::future` depending on which API variant is used.

**Key Features**

1. **Thread Safety:** Each instance's background thread manages all libcurl interactions, preventing data races without any client-side locking.

2. **Blocking API:** `GetWait()` / `PostWait()` dispatch the request to the worker thread and block the caller until the response arrives or a timeout expires.

   * *Usage:* Ideal for sequential logic where the result is needed before proceeding.
   * *Signatures:*
     ```
     HttpResponse GetWait(url, timeout)
     HttpResponse PostWait(url, body, contentType, timeout)
     ```

3. **Fire-and-Forget API:** `Get()` / `Post()` return immediately. The caller supplies an `HttpCallback` delegate. If the callback targets a specific thread, the response is marshalled back to that thread; otherwise it fires directly on the worker thread.

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

* <a href="https://github.com/DelegateMQ/DelegateMQ">DelegateMQ</a> - Invoke any C++ callable function synchronously, asynchronously, or on a remote endpoint.
* <a href="https://curl.se/libcurl/">libcurl</a> - A free and easy-to-use client-side URL transfer library.

# Use Cases and Design Considerations

## Target Environments

AsyncHttp is designed for applications that are already using DelegateMQ as their threading framework. It is most valuable in two environments:

**Embedded / RTOS systems**
IoT devices, industrial controllers, and connected embedded systems that need to make periodic HTTP requests — telemetry uploads, configuration fetches, firmware update checks, status reporting — where request ordering matters and the calling task must not block the RTOS scheduler. DelegateMQ supports FreeRTOS, ThreadX, Zephyr, CMSIS-RTOS2, and Qt in addition to the standard C++ thread library, so AsyncHttp inherits that portability directly.

**Desktop / server applications using DelegateMQ**
Any application already paying the DelegateMQ dependency cost gets thread-safe HTTP with a consistent async dispatch pattern at low additional cost. A small number of independent `AsyncHttp` instances (one per target service or priority lane) run in parallel without any client-side synchronization.

## Why libcurl on Embedded

libcurl is written in C, which makes it the practical choice for embedded HTTP:

* **No C++ runtime required.** Compiles with any C89/C90 toolchain. No exceptions, no RTTI, no standard template library.
* **Explicit portability layer.** libcurl has a formal platform abstraction (`curl_setup.h`, `Curl_socket_t`) with production-tested ports for FreeRTOS/LwIP, ThreadX/NetX, Zephyr, and bare-metal TCP stacks.
* **Swappable TLS backends.** The same `curl_easy_setopt` API works over OpenSSL (desktop), mbedTLS, or WolfSSL (embedded) — the backend is a compile-time choice with no API changes.
* **Battle tested.** libcurl is in billions of deployed devices including routers, smart TVs, automotive systems, and industrial controllers. CVEs are tracked and patched with a long public history.

By contrast, C++ HTTP libraries such as cpp-httplib require `std::thread`, `std::mutex`, a full C++ standard library, and often `<regex>` — none of which are reliably available on embedded toolchains, and all of which contribute significant binary size on flash-constrained targets.

The full embedded stack looks like this:

```
Application code
    ↓
AsyncHttp          — DelegateMQ threading + async dispatch patterns
    ↓
libcurl            — HTTP/HTTPS protocol, portable C
    ↓
mbedTLS / WolfSSL  — TLS (hardware RNG and entropy source are platform-specific)
    ↓
LwIP / RTOS TCP    — Socket layer provided by the RTOS network stack
    ↓
Network driver     — Ethernet, Wi-Fi, cellular modem
```

Each layer has a single responsibility and is independently portable.

## Threading Model

Each `AsyncHttp` instance owns one worker thread and one `CURL*` handle. All curl operations are serialized through that thread — no locks are needed by the caller.

**Within a single instance:** requests are queued and executed in FIFO order. Two concurrent callers on different threads do not race; the second request simply waits behind the first in the message queue. This serialization is a feature when ordering matters (event logs, state sync sequences).

**Across instances:** requests to different instances execute in parallel. Each instance is fully independent — different threads, different curl handles, no shared state.

**Same-thread detection:** `GetWait()` and `PostWait()` detect when they are already called from the worker thread and invoke curl directly without re-dispatching. This allows a sequence of requests to be dispatched as an atomic block onto the HTTP thread (see Example 2), analogous to a database transaction.

```
Caller threads (any number)
    │
    │  GetWait / PostWait / Get / Post / Get_future / Post_future
    ↓
DelegateMQ message queue  ←── FIFO, thread-safe
    ↓
Worker thread (one per AsyncHttp instance)
    ↓
curl_easy_perform()  ←── single CURL* handle, no concurrent access
    ↓
Response returned via blocking wait / callback / std::future
```

For high-concurrency scenarios requiring many simultaneous requests to the same endpoint, libcurl's multi interface (`curl_multi_*`) is a better fit — it handles N concurrent connections from a single thread using non-blocking I/O.

## Memory Allocation

**Response bodies** are accumulated into `std::string` inside the libcurl write callback and moved into `HttpResponse`. On embedded systems with constrained heap, responses should be kept small or the application should set `CURLOPT_MAXFILESIZE` to enforce a limit.

**DelegateMQ allocator support:** DelegateMQ can be configured with a custom allocator (the `DMQ_ALLOCATOR` CMake option) to replace heap allocation for delegate objects with a fixed-size pool allocator. This eliminates heap fragmentation from delegate dispatch on systems without a general-purpose allocator.

**libcurl itself** performs internal heap allocation for connection state, headers, and buffers. On embedded targets this is typically managed through a custom `malloc`/`free` implementation backed by a memory pool (e.g., provided by the RTOS or a custom allocator).

## Pros and Cons

**Pros**

* Thread-safe HTTP with no client-side locking — thread safety is architectural, not lock-based
* Consistent async patterns (blocking, callback, future) that match DelegateMQ usage elsewhere
* Multiple independent instances for parallel request streams
* Portable to any platform DelegateMQ supports — RTOS targets included
* libcurl's swappable TLS backend works across desktop and embedded without API changes
* FIFO ordering within an instance — useful for dependent request sequences
* RAII lifecycle — `AsyncHttp` destructor calls `shutdown()` automatically

**Cons**

* One thread per instance — not suitable for high-concurrency scenarios (10+ simultaneous requests); use libcurl multi interface instead
* Response bodies held in `std::string` — not zero-copy; large responses consume heap proportional to body size
* DelegateMQ is a significant dependency for projects not already using it
* `std::future` API requires C++17 and a reasonably complete standard library — may not be available on all embedded toolchains

## When to Use Something Else

| Scenario | Better choice |
|---|---|
| Many concurrent requests to one endpoint | libcurl multi interface |
| Project not using DelegateMQ | cpp-httplib, libcurl directly |
| Full async I/O / coroutines | Boost.Asio + Beast |
| Qt application | QNetworkAccessManager |
| Bare-metal, no RTOS, no heap | Custom HTTP over raw sockets |

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

The DelegateMQ library provides delegates and delegate containers. The example below creates a delegate targeting `MyFunc()`. The only difference between a synchronous and asynchronous call is the addition of a thread argument. See the [DelegateMQ](https://github.com/DelegateMQ/DelegateMQ) repository for full details.

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

The file `async_http.h` defines the `AsyncHttp` class. All types live in the `async` namespace.

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

    class AsyncHttp {
    public:
        explicit AsyncHttp(std::string threadName = "HTTP Thread");
        ~AsyncHttp();  // calls shutdown() if still running

        // Initialization & Management
        int     init();
        int     shutdown(dmq::Duration timeout = MAX_WAIT);
        Thread* get_thread();

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
    private:
        Thread m_thread;
        CURL*  m_curl = nullptr;
        // ...
    };

} // namespace async
```

The file `async_http.cpp` implements each method. All curl operations execute inside private worker methods (`HttpGet`, `HttpPost`) that run exclusively on `m_thread`. The same-thread check in `GetWait`/`PostWait` allows code already running on `m_thread` to call the API without re-dispatching, enabling uninterrupted request sequences (see Example 2).

```cpp
HttpResponse AsyncHttp::GetWait(const std::string& url, dmq::Duration timeout) {
    if (m_thread.GetThreadId() != std::this_thread::get_id()) {
        auto fn = std::function<HttpResponse()>([this, url]() { return HttpGet(url); });
        auto delegate = MakeDelegate(fn, m_thread, timeout);
        auto retVal = delegate.AsyncInvoke();
        if (retVal.has_value())
            return std::any_cast<HttpResponse>(retVal.value());
        HttpResponse err; err.error = "Request timed out"; return err;
    }
    return HttpGet(url);  // already on m_thread — call directly
}
```

# Examples

The `main()` function creates an `AsyncHttp` instance, runs all five examples, then executes the unit test suite (which creates its own independent instance).

```cpp
int main()
{
    callbackThread.CreateThread();
    for (int i = 0; i < WORKER_THREAD_CNT; ++i)
        workerThreads[i].CreateThread();

    async::AsyncHttp http;
    http.init();

    example1(http);
    example2(http);
    auto blockingDuration    = example3(http);
    auto nonBlockingDuration = example4(http);
    example_future(http);

    // Wait for fire-and-forget callback to complete
    while (!completeFlag.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    RunUnitTests();   // creates its own AsyncHttp instance internally
    http.shutdown();
}
```

## Example 1: Blocking GET and POST

The simplest usage pattern. `GetWait()` and `PostWait()` dispatch the request to the worker thread and block the caller until the response arrives.

```cpp
void example1(async::AsyncHttp& http)
{
    async::HttpResponse resp = http.GetWait("https://httpbin.org/get");
    printf("GET  status=%d  ok=%s\n", resp.statusCode, resp.ok() ? "yes" : "no");

    resp = http.PostWait("https://httpbin.org/post",
                         "{\"example\":1}",
                         "application/json");
    printf("POST status=%d  ok=%s\n", resp.statusCode, resp.ok() ? "yes" : "no");
}
```

## Example 2: Uninterrupted Sequence on HTTP Thread

By dispatching a function directly onto the HTTP thread via a delegate, multiple requests execute as an atomic sequence. No other caller can interleave between them. This is analogous to a transaction.

```cpp
static int RunSequenceOnHttpThread(async::AsyncHttp* http)
{
    // Runs on m_thread — GetWait/PostWait detect same-thread and call curl directly
    auto r1 = http->GetWait("https://httpbin.org/get");
    auto r2 = http->PostWait("https://httpbin.org/post", "{}", "application/json");
    return r1.ok() && r2.ok() ? 0 : 1;
}

void example2(async::AsyncHttp& http)
{
    Thread* httpThread = http.get_thread();
    auto delegate = MakeDelegate(&RunSequenceOnHttpThread, *httpThread, async::MAX_WAIT);
    auto retVal = delegate.AsyncInvoke(&http);
    if (retVal.has_value())
        printf("Sequence return value: %d\n", any_cast<int>(retVal.value()));
}
```

## Example 3: Multithreaded Blocking

Multiple worker threads call `GetWait()` concurrently on the same instance. The HTTP wrapper serializes all requests through a single curl handle — no client-side locking required.

```cpp
static void WorkerGetWait(async::AsyncHttp* http, std::string threadName)
{
    async::HttpResponse resp = http->GetWait("https://httpbin.org/get",
                                             std::chrono::seconds(15));
    printf("[%s] GET status=%d\n", threadName.c_str(), resp.statusCode);
    // ... signal completion
}

std::chrono::microseconds example3(async::AsyncHttp& http)
{
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < WORKER_THREAD_CNT; ++i) {
        auto delegate = MakeDelegate(WorkerGetWait, workerThreads[i]);
        delegate(&http, workerThreads[i].GetThreadName());
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

std::chrono::microseconds example4(async::AsyncHttp& http)
{
    auto start = std::chrono::high_resolution_clock::now();

    // Callback targets callbackThread — response is marshalled back there
    http.Get("https://httpbin.org/get",
             MakeDelegate(&OnGetResponse, callbackThread));

    // Returns immediately — dispatch time is in microseconds
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
}
```

## Example 5: Future / Async

`Get_future()` returns a `std::future<HttpResponse>` immediately. The main thread can continue doing work and call `.get()` when it needs the result. `.get()` blocks only if the HTTP request is still in-flight.

```cpp
void example_future(async::AsyncHttp& http)
{
    // Launch HTTP request — returns immediately
    auto future = http.Get_future("https://httpbin.org/get");

    // Main thread continues working while HTTP runs in background
    for (int i = 0; i < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        printf("[Main] Working... %d%%\n", (i + 1) * 33);
    }

    // Blocks only if still in-flight
    async::HttpResponse resp = future.get();
    printf("[Main] Response: status=%d ok=%s\n", resp.statusCode, resp.ok() ? "yes" : "no");

    // POST future
    auto postFuture = http.Post_future("https://httpbin.org/post",
                                       "{\"future\":true}",
                                       "application/json");
    async::HttpResponse postResp = postFuture.get();
    printf("[Main] POST future: status=%d\n", postResp.statusCode);
}
```

## Multiple Instances

Each `AsyncHttp` instance owns its own worker thread and curl handle. Creating multiple instances allows independent request streams to run in parallel — requests to different instances do not queue behind each other.

```cpp
// Two independent HTTP clients — requests run in parallel
async::AsyncHttp client1("HTTP-Thread-1");
async::AsyncHttp client2("HTTP-Thread-2");
client1.init();
client2.init();

// These dispatch simultaneously to separate threads and curl handles
auto f1 = client1.Get_future("https://api.example.com/sensors");
auto f2 = client2.Get_future("https://api.example.com/config");

async::HttpResponse r1 = f1.get();
async::HttpResponse r2 = f2.get();
```

Typical use cases for multiple instances:
* One instance per target host for connection reuse and isolation
* Separate instances for high-priority and background requests
* Independent clients for different services in the same application

## Timing Comparison

Example 3 (blocking) and Example 4 (fire-and-forget) illustrate the difference in dispatch time from the calling thread's perspective. The blocking call waits for the full HTTP round-trip; the fire-and-forget call returns in microseconds.

```
Blocking dispatch time:      459135 us
Non-blocking dispatch time:  17 us
```
