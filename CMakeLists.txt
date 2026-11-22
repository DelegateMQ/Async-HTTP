# Example CMake command line to create project build files:
#
# cmake -B build .
# cmake -B build . -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake
#
# DelegateMQ is fetched from GitHub at the release tag DMQ_GIT_TAG when CMake
# configures. To build against a local DelegateMQ checkout instead, add
# -DFETCHCONTENT_SOURCE_DIR_DELEGATEMQ=<path>. To configure again without
# network access, add -DFETCHCONTENT_FULLY_DISCONNECTED=ON.

# Specify the minimum CMake version required
cmake_minimum_required(VERSION 3.16)

# Project name and language
project(Async-HTTP VERSION 1.0 LANGUAGES C CXX)

# Set build options
set (DMQ_ALLOCATOR "OFF")
set (DMQ_UTIL "ON")
set (DMQ_THREAD "DMQ_THREAD_STDLIB")
set (DMQ_SERIALIZE "DMQ_SERIALIZE_NONE")
set (DMQ_TRANSPORT "DMQ_TRANSPORT_NONE")

# DelegateMQ release to build against
set(DMQ_GIT_TAG "v3.0.0" CACHE STRING "DelegateMQ release tag (or branch) to fetch")

include(FetchContent)
FetchContent_Declare(
    delegatemq
    GIT_REPOSITORY https://github.com/DelegateMQ/DelegateMQ.git
    GIT_TAG        ${DMQ_GIT_TAG}
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(delegatemq)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Find libcurl (installed via vcpkg, system package manager, or manual build)
find_package(CURL REQUIRED)

# Collect all .cpp and *.h source files in the current directory
file(GLOB SOURCES "*.cpp" "*.h")

# Add subdirectories to include path
include_directories(
    HTTP
    UnitTest
)

# Add an executable target
add_executable(Async-HTTPApp ${SOURCES})

# Add subdirectories to build
add_subdirectory(HTTP)
add_subdirectory(UnitTest)

target_link_libraries(Async-HTTPApp PRIVATE
    HttpLib
    UnitTestLib
)
