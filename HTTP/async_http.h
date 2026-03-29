#ifndef ASYNC_HTTP_H
#define ASYNC_HTTP_H

// Asynchronous HTTP client using C++ Delegates
// @see https://github.com/DelegateMQ/Async-HTTP
// @see https://github.com/DelegateMQ/DelegateMQ
// David Lafreniere, 2026

// ------------------------------------------------------------------------------------------------
// ARCHITECTURE OVERVIEW:
// ------------------------------------------------------------------------------------------------
// AsyncHttp wraps libcurl to execute all HTTP operations on a dedicated background thread.
// Each instance owns a Thread and a CURL* handle, ensuring thread safety without any
// client-side locking.
//
// Multiple instances may be created to run independent HTTP clients concurrently — each
// has its own worker thread and curl handle so their requests execute in parallel.
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
// Construct an AsyncHttp instance, call init() once before any other API, and call
// shutdown() when done (or let the destructor handle it).
// ------------------------------------------------------------------------------------------------

#include "DelegateMQ.h"
#include <curl/curl.h>
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

    /// Asynchronous HTTP client. Each instance owns a dedicated worker thread and curl handle.
    /// All HTTP operations on this instance are serialized through its worker thread.
    ///
    /// Create multiple instances to run independent clients concurrently — requests to
    /// different instances execute in parallel on separate threads and curl handles.
    class AsyncHttp {
    public:
        /// @param threadName Identifies the worker thread in debuggers and logs.
        explicit AsyncHttp(std::string threadName = "HTTP Thread");

        /// Calls shutdown() if the client is still running.
        ~AsyncHttp();

        // Non-copyable, non-movable — owns a thread and a curl handle.
        AsyncHttp(const AsyncHttp&)            = delete;
        AsyncHttp& operator=(const AsyncHttp&) = delete;

        // -------------------------------------------------------------------------
        // Initialization & Management
        // -------------------------------------------------------------------------

        /// Initialize the HTTP client. Safe to call more than once (no-op if already running).
        /// @return 0 on success, non-zero on failure.
        int init();

        /// Shut down the HTTP client. Blocks until the worker thread exits or timeout expires.
        int shutdown(dmq::Duration timeout = MAX_WAIT);

        /// Accessor for the internal worker thread (e.g. to dispatch work directly onto it).
        Thread* get_thread();

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

    private:
        Thread m_thread;
        CURL*  m_curl = nullptr;

        static size_t WriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata);

        HttpResponse HttpGet(const std::string& url);
        HttpResponse HttpPost(const std::string& url,
                              const std::string& body,
                              const std::string& contentType);
        void DoGetCallback(const std::string& url, HttpCallback cb);
        void DoPostCallback(const std::string& url,
                            const std::string& body,
                            const std::string& contentType,
                            HttpCallback cb);
        void HttpCleanup();
    };

} // namespace async

#endif // ASYNC_HTTP_H
