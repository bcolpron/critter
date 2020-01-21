cmake_minimum_required (VERSION 3.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O3")

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost 1.71.0 REQUIRED COMPONENTS system filesystem regex coroutine context thread)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

include_directories(
    ${Boost_INCLUDE_DIRS}
    ../include
)

link_libraries(${Boost_LIBRARIES})
link_libraries(Threads::Threads)

add_executable(example main.cpp)

