// Async-HTTP unit test entry point
// @see https://github.com/DelegateMQ/Async-HTTP
// @see https://github.com/DelegateMQ/DelegateMQ
// David Lafreniere, 2026

#include "async_http_ut.h"
#include "async_http.h"
#include "DelegateMQ.h"
#include <iostream>
#include <cassert>

// These are declared in their respective translation units
extern void Initialization_UT(async::AsyncHttp& http);
extern void Blocking_UT(async::AsyncHttp& http);
extern void Callback_UT(async::AsyncHttp& http);
extern void Future_UT(async::AsyncHttp& http);
extern void Stress_UT(async::AsyncHttp& http);

int RunUnitTests()
{
    std::cout << "========================================" << std::endl;
    std::cout << "    STARTING ASYNC-HTTP TEST SUITE      " << std::endl;
    std::cout << "========================================" << std::endl;

    // Note: tests below issue real HTTP requests to httpbin.org.
    // Network access is required.
    async::AsyncHttp http("HTTP Test Thread");
    http.init();

    Initialization_UT(http);
    Blocking_UT(http);
    Callback_UT(http);
    Future_UT(http);
    Stress_UT(http);

    http.shutdown();

    std::cout << "========================================" << std::endl;
    std::cout << "    ALL TESTS PASSED SUCCESSFULLY       " << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
