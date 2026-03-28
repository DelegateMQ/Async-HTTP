# Example CMake command line to create project build files:
#
# cmake -B Build .
# cmake -B Build . -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake

# Specify the minimum CMake version required
cmake_minimum_required(VERSION 3.10)

# Project name and language
project(Async-HTTP VERSION 1.0 LANGUAGES C CXX)

# Set build options
set (DMQ_ALLOCATOR "OFF")
set (DMQ_UTIL "ON")
set (DMQ_THREAD "DMQ_THREAD_STDLIB")
set (DMQ_SERIALIZE "DMQ_SERIALIZE_NONE")
set (DMQ_TRANSPORT "DMQ_TRANSPORT_NONE")
include("${CMAKE_SOURCE_DIR}/DelegateMQ/DelegateMQ.cmake")

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Find libcurl (installed via vcpkg, system package manager, or manual build)
find_package(CURL REQUIRED)

# Collect all .cpp and *.h source files in the current directory
file(GLOB SOURCES "*.cpp" "*.h")

# Collect DelegateMQ predef source files
list(APPEND SOURCES ${DMQ_PREDEF_SOURCES})

# Organize delegate source files within IDE (Visual Studio)
source_group("Delegate Files" FILES ${DMQ_LIB_SOURCES})

# Add subdirectories to include path
include_directories(
    ${DMQ_ROOT_DIR}
    HTTP
    UnitTest
)

# Add an executable target
add_executable(Async-HTTPApp ${SOURCES} ${DMQ_LIB_SOURCES})

# Add subdirectories to build
add_subdirectory(HTTP)
add_subdirectory(UnitTest)

target_link_libraries(Async-HTTPApp PRIVATE
    HttpLib
    UnitTestLib
)
