// Asynchronous HTTP client using C++ Delegates
// @see https://github.com/endurodave/Async-HTTP
// @see https://github.com/endurodave/DelegateMQ
// David Lafreniere, 2026

#include "async_http.h"
#include "DelegateMQ.h"
#include <curl/curl.h>
#include <any>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>

using namespace dmq;

namespace async {

    // -----------------------------------------------------------------------
    // Private state — all curl access serialized through HttpThread
    // -----------------------------------------------------------------------

    // Single worker thread that owns the curl handle.
    static Thread HttpThread("HTTP Thread");

    // Reused curl easy handle. Owned exclusively by HttpThread.
    static CURL* s_curl = nullptr;

    // -----------------------------------------------------------------------
    // Internal: libcurl write callback — accumulates response body
    // -----------------------------------------------------------------------

    static size_t WriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
    {
        auto* body = static_cast<std::string*>(userdata);
        body->append(ptr, size * nmemb);
        return size * nmemb;
    }

    // -----------------------------------------------------------------------
    // Internal worker functions — these run on HttpThread
    // -----------------------------------------------------------------------

    static HttpResponse HttpGet(const std::string& url)
    {
        HttpResponse resp;
        if (!s_curl) {
            resp.error = "Not initialized";
            return resp;
        }

        std::string body;
        curl_easy_reset(s_curl);
        curl_easy_setopt(s_curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(s_curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(s_curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(s_curl, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(s_curl, CURLOPT_FOLLOWLOCATION, 1L);

        CURLcode res = curl_easy_perform(s_curl);
        if (res != CURLE_OK) {
            resp.error = curl_easy_strerror(res);
        } else {
            long code = 0;
            curl_easy_getinfo(s_curl, CURLINFO_RESPONSE_CODE, &code);
            resp.statusCode = static_cast<int>(code);
            resp.body = std::move(body);
        }
        return resp;
    }

    static HttpResponse HttpPost(const std::string& url,
                                  const std::string& body,
                                  const std::string& contentType)
    {
        HttpResponse resp;
        if (!s_curl) {
            resp.error = "Not initialized";
            return resp;
        }

        std::string respBody;
        struct curl_slist* headers = nullptr;
        std::string ctHeader = "Content-Type: " + contentType;
        headers = curl_slist_append(headers, ctHeader.c_str());

        curl_easy_reset(s_curl);
        curl_easy_setopt(s_curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(s_curl, CURLOPT_POST, 1L);
        curl_easy_setopt(s_curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(s_curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        curl_easy_setopt(s_curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(s_curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(s_curl, CURLOPT_WRITEDATA, &respBody);
        curl_easy_setopt(s_curl, CURLOPT_FOLLOWLOCATION, 1L);

        CURLcode res = curl_easy_perform(s_curl);
        curl_slist_free_all(headers);

        if (res != CURLE_OK) {
            resp.error = curl_easy_strerror(res);
        } else {
            long code = 0;
            curl_easy_getinfo(s_curl, CURLINFO_RESPONSE_CODE, &code);
            resp.statusCode = static_cast<int>(code);
            resp.body = std::move(respBody);
        }
        return resp;
    }

    // Runs on HttpThread: execute GET then invoke callback (marshals to cb's thread if set)
    static void DoGetCallback(const std::string& url, HttpCallback cb)
    {
        HttpResponse resp = HttpGet(url);
        if (cb) cb(resp);
    }

    // Runs on HttpThread: execute POST then invoke callback
    static void DoPostCallback(const std::string& url,
                                const std::string& body,
                                const std::string& contentType,
                                HttpCallback cb)
    {
        HttpResponse resp = HttpPost(url, body, contentType);
        if (cb) cb(resp);
    }

    // Runs on HttpThread: release the curl handle
    static void HttpCleanup()
    {
        if (s_curl) {
            curl_easy_cleanup(s_curl);
            s_curl = nullptr;
        }
    }

    // -----------------------------------------------------------------------
    // AsyncInvokeFuture: dispatch func(args...) to HttpThread, return future
    // -----------------------------------------------------------------------

    template <typename Func, typename... Args>
    auto AsyncInvokeFuture(Func func, Args&&... args)
    {
        using RetType = std::invoke_result_t<std::decay_t<Func>, std::decay_t<Args>...>;

        auto promise = std::make_shared<std::promise<RetType>>();
        std::future<RetType> future = promise->get_future();

        if (HttpThread.GetThreadId() == std::thread::id()) {
            try {
                throw std::runtime_error("HTTP Thread is not running. Call http_init() first.");
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
            return future;
        }

        auto taskLambda = [func, promise, args = std::make_tuple(std::forward<Args>(args)...)]() mutable {
            try {
                if constexpr (std::is_void_v<RetType>) {
                    std::apply(func, std::move(args));
                    promise->set_value();
                } else {
                    promise->set_value(std::apply(func, std::move(args)));
                }
            } catch (...) {
                promise->set_exception(std::current_exception());
            }
        };

        auto delegate = dmq::MakeDelegate(
            std::function<void()>(taskLambda),
            HttpThread
        );
        delegate.AsyncInvoke();
        return future;
    }

    // -----------------------------------------------------------------------
    // Public API implementation
    // -----------------------------------------------------------------------

    Thread* http_get_thread()
    {
        return &HttpThread;
    }

    int http_init()
    {
        CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (res != CURLE_OK) return static_cast<int>(res);
        s_curl = curl_easy_init();
        if (!s_curl) return -1;
        HttpThread.CreateThread();
        return 0;
    }

    int http_shutdown(dmq::Duration timeout)
    {
        if (HttpThread.GetThreadId() != std::thread::id()) {
            auto delegate = MakeDelegate(HttpCleanup, HttpThread, timeout);
            delegate.AsyncInvoke();
            HttpThread.ExitThread();
        }
        curl_global_cleanup();
        return 0;
    }

    HttpResponse GetWait(const std::string& url, dmq::Duration timeout)
    {
        if (HttpThread.GetThreadId() != std::this_thread::get_id()) {
            auto delegate = MakeDelegate(HttpGet, HttpThread, timeout);
            auto retVal = delegate.AsyncInvoke(url);
            if (retVal.has_value())
                return std::any_cast<HttpResponse>(retVal.value());
            HttpResponse err;
            err.error = "Request timed out";
            return err;
        }
        return HttpGet(url);
    }

    HttpResponse PostWait(const std::string& url,
                           const std::string& body,
                           const std::string& contentType,
                           dmq::Duration timeout)
    {
        if (HttpThread.GetThreadId() != std::this_thread::get_id()) {
            auto delegate = MakeDelegate(HttpPost, HttpThread, timeout);
            auto retVal = delegate.AsyncInvoke(url, body, contentType);
            if (retVal.has_value())
                return std::any_cast<HttpResponse>(retVal.value());
            HttpResponse err;
            err.error = "Request timed out";
            return err;
        }
        return HttpPost(url, body, contentType);
    }

    void Get(const std::string& url, HttpCallback cb)
    {
        auto delegate = dmq::MakeDelegate(DoGetCallback, HttpThread);
        delegate.AsyncInvoke(url, cb);
    }

    void Post(const std::string& url,
              const std::string& body,
              const std::string& contentType,
              HttpCallback cb)
    {
        auto delegate = dmq::MakeDelegate(DoPostCallback, HttpThread);
        delegate.AsyncInvoke(url, body, contentType, cb);
    }

    std::future<HttpResponse> Get_future(const std::string& url)
    {
        return AsyncInvokeFuture(HttpGet, url);
    }

    std::future<HttpResponse> Post_future(const std::string& url,
                                           const std::string& body,
                                           const std::string& contentType)
    {
        return AsyncInvokeFuture(HttpPost, url, body, contentType);
    }

} // namespace async
