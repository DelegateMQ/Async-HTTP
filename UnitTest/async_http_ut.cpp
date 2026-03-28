// Async-HTTP unit test entry point
// @see https://github.com/endurodave/Async-HTTP
// @see https://github.com/endurodave/DelegateMQ
// David Lafreniere, 2026

#include "async_http_ut.h"
#include "async_http.h"
#include "DelegateMQ.h"
#include <iostream>
#include <cassert>

using namespace async;

// These are declared in their respective translation units
extern void Initialization_UT();
extern void Blocking_UT();
extern void Callback_UT();
extern void Future_UT();
extern void Stress_UT();

int RunUnitTests()
{
    std::cout << "========================================" << std::endl;
    std::cout << "    STARTING ASYNC-HTTP TEST SUITE      " << std::endl;
    std::cout << "========================================" << std::endl;

    // Note: tests below issue real HTTP requests to httpbin.org.
    // Network access is required.
    Initialization_UT();
    Blocking_UT();
    Callback_UT();
    Future_UT();
    Stress_UT();

    std::cout << "========================================" << std::endl;
    std::cout << "    ALL TESTS PASSED SUCCESSFULLY       " << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
