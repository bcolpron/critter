cmake_minimum_required (VERSION 3.1.0)

set(app_name http_sample_app)
project (${app_name})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O3")

set(Boost_USE_STATIC_LIBS ON)
find_package(Boost 1.65.0 REQUIRED COMPONENTS system filesystem regex coroutine context thread)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(${app_name} main.cpp)

target_include_directories(${app_name} PRIVATE
    ${Boost_INCLUDE_DIRS}
    ../include
    include/
)

target_link_libraries(${app_name} ${Boost_LIBRARIES})
target_link_libraries(${app_name} Threads::Threads)

