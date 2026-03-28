# Collect all .cpp and .h source files in this subdirectory
file(GLOB SUBDIR_SOURCES "*.cpp" "*.h")

# Create a library target
add_library(HttpLib STATIC ${SUBDIR_SOURCES})

# Include this directory so consumers can #include "async_http.h"
target_include_directories(HttpLib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# libcurl is a public dependency — consumers of HttpLib also need the headers/libs
target_link_libraries(HttpLib PUBLIC CURL::libcurl)

# Add /bigobj flag for MSVC
if (MSVC)
    target_compile_options(HttpLib PRIVATE /bigobj)
endif()
