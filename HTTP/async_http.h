#ifndef ASYNC_HTTP_H
#define ASYNC_HTTP_H

// Asynchronous HTTP client using C++ Delegates
// @see https://github.com/endurodave/Async-HTTP
// @see https://github.com/endurodave/DelegateMQ
// David Lafreniere, 2026

// ------------------------------------------------------------------------------------------------
// ARCHITECTURE OVERVIEW:
// ------------------------------------------------------------------------------------------------
// This library wraps libcurl to execute all HTTP operations on a dedicated background thread.
// This ensures:
// 1. Thread Safety: The curl handle is owned and accessed serially by a single worker thread.
// 2. Non-Blocking Callers: HTTP requests complete in the background without stalling the caller.
//
// API CATEGORIES:
// 1. Blocking: GetWait()/PostWait() block the calling thread until the response arrives or
//    the timeout expires.
// 2. Fire-and-Forget: Get()/Post() return immediately. The caller supplies an HttpCallback
//    delegate. If the delegate carries a thread target the response is marshalled back to
//    that thread; otherwise it fires directly on the HTTP worker thread.
// 3. Future: Get_future()/Post_future() return a std::future<HttpResponse> immediately so
//    the caller can overlap work and retrieve the result later.
//
// SETUP:
// Call http_init() once at application startup before any other API.
// Call http_shutdown() at application exit.
// ------------------------------------------------------------------------------------------------

#include "DelegateMQ.h"
#include <future>
#include <string>
#include <chrono>

namespace async {

    // @TODO Change maximum async timeout duration if necessary. See WAIT_INFINITE
    // for comment on maximum value allowed.
    constexpr dmq::Duration MAX_WAIT = std::chrono::minutes(2);

    /// HTTP response returned by all API variants.
    struct HttpResponse {
        int         statusCode = 0;
        std::string body;
        std::string error;
        bool ok() const { return statusCode >= 200 && statusCode < 300; }
    };

    /// Callback type passed to fire-and-forget Get()/Post().
    /// Construct from any MakeDelegate() result with signature void(HttpResponse).
    /// If the delegate carries a thread target the response is marshalled to that thread;
    /// otherwise it fires on the HTTP worker thread.
    using HttpCallback = dmq::UnicastDelegate<void(HttpResponse)>;

    // -------------------------------------------------------------------------
    // Initialization & Management
    // -------------------------------------------------------------------------

    /// Initialize the async HTTP system. Call once at startup before any other API.
    /// @return 0 on success, non-zero on failure.
    int http_init();

    /// Shut down the async HTTP system. Blocks until the worker thread exits or
    /// the timeout expires.
    int http_shutdown(dmq::Duration timeout = MAX_WAIT);

    /// Accessor for the internal worker thread (e.g. to dispatch work directly).
    Thread* http_get_thread();

    // -------------------------------------------------------------------------
    // Blocking API — caller blocks until response arrives or timeout expires
    // -------------------------------------------------------------------------

    HttpResponse GetWait(const std::string& url,
                         dmq::Duration timeout = MAX_WAIT);

    HttpResponse PostWait(const std::string& url,
                          const std::string& body,
                          const std::string& contentType,
                          dmq::Duration timeout = MAX_WAIT);

    // -------------------------------------------------------------------------
    // Fire-and-Forget API — returns immediately; result delivered via callback
    // -------------------------------------------------------------------------

    /// @note If cb targets a specific thread, OnResponse fires on that thread.
    ///       If cb is a plain synchronous delegate, it fires on the HTTP worker thread.
    void Get(const std::string& url, HttpCallback cb);

    void Post(const std::string& url,
              const std::string& body,
              const std::string& contentType,
              HttpCallback cb);

    // -------------------------------------------------------------------------
    // Future API — returns immediately; caller calls .get() when ready
    // -------------------------------------------------------------------------

    std::future<HttpResponse> Get_future(const std::string& url);

    std::future<HttpResponse> Post_future(const std::string& url,
                                          const std::string& body,
                                          const std::string& contentType);

} // namespace async

#endif // ASYNC_HTTP_H
