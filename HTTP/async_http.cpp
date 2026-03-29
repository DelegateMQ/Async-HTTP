// Asynchronous HTTP client using C++ Delegates
// @see https://github.com/DelegateMQ/Async-HTTP
// @see https://github.com/DelegateMQ/DelegateMQ
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

// ---------------------------------------------------------------------------
// Reference-counted curl global init/cleanup — safe for multiple AsyncHttp
// instances in the same process.
// ---------------------------------------------------------------------------
static std::mutex  g_curlMutex;
static int         g_curlRefCount = 0;

static int curl_global_addref()
{
    std::lock_guard<std::mutex> lock(g_curlMutex);
    if (g_curlRefCount == 0) {
        CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (res != CURLE_OK) return static_cast<int>(res);
    }
    ++g_curlRefCount;
    return 0;
}

static void curl_global_release()
{
    std::lock_guard<std::mutex> lock(g_curlMutex);
    if (g_curlRefCount > 0 && --g_curlRefCount == 0)
        curl_global_cleanup();
}

namespace async {

    // -----------------------------------------------------------------------
    // Free template helper — dispatches a zero-argument callable to a thread
    // and returns a std::future for the result. Not part of the public API.
    // -----------------------------------------------------------------------
    template <typename Func>
    auto AsyncInvokeFuture(Thread& thread, Func func)
        -> std::future<std::invoke_result_t<Func>>
    {
        using RetType = std::invoke_result_t<Func>;
        auto promise = std::make_shared<std::promise<RetType>>();
        std::future<RetType> future = promise->get_future();

        if (thread.GetThreadId() == std::thread::id()) {
            promise->set_exception(std::make_exception_ptr(
                std::runtime_error("HTTP Thread is not running. Call init() first.")));
            return future;
        }

        auto taskLambda = std::function<void()>(
            [func = std::move(func), promise]() mutable {
                try {
                    if constexpr (std::is_void_v<RetType>) {
                        func();
                        promise->set_value();
                    } else {
                        promise->set_value(func());
                    }
                } catch (...) {
                    promise->set_exception(std::current_exception());
                }
            });

        auto delegate = dmq::MakeDelegate(taskLambda, thread);
        delegate.AsyncInvoke();
        return future;
    }

    // -----------------------------------------------------------------------
    // AsyncHttp implementation
    // -----------------------------------------------------------------------

    AsyncHttp::AsyncHttp(std::string threadName)
        : m_thread(std::move(threadName))
    {}

    AsyncHttp::~AsyncHttp()
    {
        if (m_running)
            shutdown();
    }

    // -----------------------------------------------------------------------
    // Internal: libcurl write callback — accumulates response body
    // -----------------------------------------------------------------------
    size_t AsyncHttp::WriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
    {
        auto* body = static_cast<std::string*>(userdata);
        body->append(ptr, size * nmemb);
        return size * nmemb;
    }

    // -----------------------------------------------------------------------
    // Private worker functions — these run on m_thread
    // -----------------------------------------------------------------------

    HttpResponse AsyncHttp::HttpGet(const std::string& url)
    {
        HttpResponse resp;
        if (!m_curl) { resp.error = "Not initialized"; return resp; }

        std::string body;
        curl_easy_reset(m_curl);
        curl_easy_setopt(m_curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(m_curl, CURLOPT_HTTPGET, 1L);
        curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(m_curl, CURLOPT_FOLLOWLOCATION, 1L);

        CURLcode res = curl_easy_perform(m_curl);
        if (res != CURLE_OK) {
            resp.error = curl_easy_strerror(res);
        } else {
            long code = 0;
            curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &code);
            resp.statusCode = static_cast<int>(code);
            resp.body = std::move(body);
        }
        return resp;
    }

    HttpResponse AsyncHttp::HttpPost(const std::string& url,
                                      const std::string& body,
                                      const std::string& contentType)
    {
        HttpResponse resp;
        if (!m_curl) { resp.error = "Not initialized"; return resp; }

        std::string respBody;
        struct curl_slist* headers = nullptr;
        std::string ctHeader = "Content-Type: " + contentType;
        headers = curl_slist_append(headers, ctHeader.c_str());

        curl_easy_reset(m_curl);
        curl_easy_setopt(m_curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(m_curl, CURLOPT_POST, 1L);
        curl_easy_setopt(m_curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(m_curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        curl_easy_setopt(m_curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &respBody);
        curl_easy_setopt(m_curl, CURLOPT_FOLLOWLOCATION, 1L);

        CURLcode res = curl_easy_perform(m_curl);
        curl_slist_free_all(headers);

        if (res != CURLE_OK) {
            resp.error = curl_easy_strerror(res);
        } else {
            long code = 0;
            curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &code);
            resp.statusCode = static_cast<int>(code);
            resp.body = std::move(respBody);
        }
        return resp;
    }

    // Runs on m_thread: execute GET then invoke callback
    void AsyncHttp::DoGetCallback(const std::string& url, HttpCallback cb)
    {
        HttpResponse resp = HttpGet(url);
        if (cb) cb(resp);
    }

    // Runs on m_thread: execute POST then invoke callback
    void AsyncHttp::DoPostCallback(const std::string& url,
                                    const std::string& body,
                                    const std::string& contentType,
                                    HttpCallback cb)
    {
        HttpResponse resp = HttpPost(url, body, contentType);
        if (cb) cb(resp);
    }

    // Runs on m_thread: release the curl handle
    void AsyncHttp::HttpCleanup()
    {
        if (m_curl) {
            curl_easy_cleanup(m_curl);
            m_curl = nullptr;
        }
    }

    // -----------------------------------------------------------------------
    // Public API implementation
    // -----------------------------------------------------------------------

    Thread* AsyncHttp::get_thread()
    {
        return &m_thread;
    }

    int AsyncHttp::init()
    {
        if (m_running) return 0;  // already initialized

        int res = curl_global_addref();
        if (res != 0) return res;

        m_curl = curl_easy_init();
        if (!m_curl) return -1;

        m_thread.CreateThread();
        m_running = true;
        return 0;
    }

    int AsyncHttp::shutdown(dmq::Duration timeout)
    {
        if (m_running) {
            auto fn = std::function<void()>([this]() { HttpCleanup(); });
            auto delegate = MakeDelegate(fn, m_thread, timeout);
            delegate.AsyncInvoke();
            m_thread.ExitThread();
            m_running = false;
        }
        curl_global_release();
        return 0;
    }

    HttpResponse AsyncHttp::GetWait(const std::string& url, dmq::Duration timeout)
    {
        if (m_thread.GetThreadId() != std::this_thread::get_id()) {
            auto fn = std::function<HttpResponse()>([this, url]() { return HttpGet(url); });
            auto delegate = MakeDelegate(fn, m_thread, timeout);
            auto retVal = delegate.AsyncInvoke();
            if (retVal.has_value())
                return std::any_cast<HttpResponse>(retVal.value());
            HttpResponse err;
            err.error = "Request timed out";
            return err;
        }
        return HttpGet(url);
    }

    HttpResponse AsyncHttp::PostWait(const std::string& url,
                                      const std::string& body,
                                      const std::string& contentType,
                                      dmq::Duration timeout)
    {
        if (m_thread.GetThreadId() != std::this_thread::get_id()) {
            auto fn = std::function<HttpResponse()>([this, url, body, contentType]() {
                return HttpPost(url, body, contentType);
            });
            auto delegate = MakeDelegate(fn, m_thread, timeout);
            auto retVal = delegate.AsyncInvoke();
            if (retVal.has_value())
                return std::any_cast<HttpResponse>(retVal.value());
            HttpResponse err;
            err.error = "Request timed out";
            return err;
        }
        return HttpPost(url, body, contentType);
    }

    void AsyncHttp::Get(const std::string& url, HttpCallback cb)
    {
        auto fn = std::function<void()>([this, url, cb]() mutable {
            DoGetCallback(url, cb);
        });
        auto delegate = dmq::MakeDelegate(fn, m_thread);
        delegate.AsyncInvoke();
    }

    void AsyncHttp::Post(const std::string& url,
                          const std::string& body,
                          const std::string& contentType,
                          HttpCallback cb)
    {
        auto fn = std::function<void()>([this, url, body, contentType, cb]() mutable {
            DoPostCallback(url, body, contentType, cb);
        });
        auto delegate = dmq::MakeDelegate(fn, m_thread);
        delegate.AsyncInvoke();
    }

    std::future<HttpResponse> AsyncHttp::Get_future(const std::string& url)
    {
        return AsyncInvokeFuture(m_thread, [this, url]() { return HttpGet(url); });
    }

    std::future<HttpResponse> AsyncHttp::Post_future(const std::string& url,
                                                      const std::string& body,
                                                      const std::string& contentType)
    {
        return AsyncInvokeFuture(m_thread, [this, url, body, contentType]() {
            return HttpPost(url, body, contentType);
        });
    }

} // namespace async
