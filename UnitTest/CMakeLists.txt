# Collect all .cpp and .h source files in this subdirectory
file(GLOB SUBDIR_SOURCES "*.cpp" "*.h")

# Create a library target
add_library(UnitTestLib STATIC ${SUBDIR_SOURCES})

# Include this directory so consumers can #include "async_http_ut.h"
target_include_directories(UnitTestLib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# HttpLib is a dependency — pulls in async_http.h and curl headers transitively
target_link_libraries(UnitTestLib PUBLIC HttpLib)

# Add /bigobj flag for MSVC
if (MSVC)
    target_compile_options(UnitTestLib PRIVATE /bigobj)
endif()
