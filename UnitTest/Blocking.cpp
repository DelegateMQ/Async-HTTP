// Unit tests for Async-HTTP (Group 2: Blocking API — GetWait / PostWait)

#include "async_http.h"
#include "DelegateMQ.h"
#include <iostream>
#include <string>

using namespace async;

// -----------------------------------------------------------------------------
// Test 1: GetWait returns 200 for a known-good URL
// -----------------------------------------------------------------------------
static void Test_GetWait_Success()
{
    HttpResponse resp = GetWait("https://httpbin.org/get", std::chrono::seconds(10));

    ASSERT_TRUE(resp.ok());
    ASSERT_TRUE(resp.statusCode == 200);
    ASSERT_TRUE(!resp.body.empty());
    ASSERT_TRUE(resp.error.empty());

    std::cout << "  GetWait 200 OK (body length=" << resp.body.size() << ")" << std::endl;
}

// -----------------------------------------------------------------------------
// Test 2: GetWait reflects non-2xx status codes correctly
// -----------------------------------------------------------------------------
static void Test_GetWait_404()
{
    HttpResponse resp = GetWait("https://httpbin.org/status/404", std::chrono::seconds(10));

    ASSERT_TRUE(!resp.ok());
    ASSERT_TRUE(resp.statusCode == 404);
    ASSERT_TRUE(resp.error.empty());  // HTTP error, not a transport error

    std::cout << "  GetWait 404 status confirmed" << std::endl;
}

// -----------------------------------------------------------------------------
// Test 3: GetWait with bad host returns a transport error
// -----------------------------------------------------------------------------
static void Test_GetWait_ConnectionError()
{
    HttpResponse resp = GetWait("http://this.host.does.not.exist.invalid/",
                                std::chrono::seconds(10));

    ASSERT_TRUE(!resp.ok());
    ASSERT_TRUE(!resp.error.empty());

    std::cout << "  GetWait connection error: " << resp.error << std::endl;
}

// -----------------------------------------------------------------------------
// Test 4: PostWait sends body and receives echo from httpbin
// -----------------------------------------------------------------------------
static void Test_PostWait_Success()
{
    std::string body        = "{\"key\":\"value\"}";
    std::string contentType = "application/json";

    HttpResponse resp = PostWait("https://httpbin.org/post", body, contentType,
                                 std::chrono::seconds(10));

    ASSERT_TRUE(resp.ok());
    ASSERT_TRUE(resp.statusCode == 200);
    // httpbin echoes the posted body in the "data" field of the JSON response
    ASSERT_TRUE(resp.body.find("key") != std::string::npos);

    std::cout << "  PostWait 200 OK (body length=" << resp.body.size() << ")" << std::endl;
}

// -----------------------------------------------------------------------------
// Test 5: PostWait reflects 400 status
// -----------------------------------------------------------------------------
static void Test_PostWait_StatusError()
{
    HttpResponse resp = PostWait("https://httpbin.org/status/400", "", "text/plain",
                                 std::chrono::seconds(10));

    ASSERT_TRUE(!resp.ok());
    ASSERT_TRUE(resp.statusCode == 400);

    std::cout << "  PostWait 400 status confirmed" << std::endl;
}

// -----------------------------------------------------------------------------
// Test 6: Sequential requests reuse the curl handle correctly
// -----------------------------------------------------------------------------
static void Test_SequentialRequests()
{
    for (int i = 0; i < 3; ++i) {
        HttpResponse resp = GetWait("https://httpbin.org/get", std::chrono::seconds(10));
        ASSERT_TRUE(resp.ok());
    }
    std::cout << "  3 sequential requests succeeded" << std::endl;
}

// -----------------------------------------------------------------------------
// Main entry point
// -----------------------------------------------------------------------------
void Blocking_UT()
{
    std::cout << "Running HTTP Blocking API Tests..." << std::endl;

    http_init();

    Test_GetWait_Success();
    Test_GetWait_404();
    Test_GetWait_ConnectionError();
    Test_PostWait_Success();
    Test_PostWait_StatusError();
    Test_SequentialRequests();

    std::cout << "HTTP Blocking API Tests Passed!" << std::endl;
}
