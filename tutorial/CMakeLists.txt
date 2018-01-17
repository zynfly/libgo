cmake_minimum_required(VERSION 2.8)

###################################################################################
project(samples)

set(Boost_USE_STATIC_LIBS        ON)
set(Boost_USE_MULTITHREADED      ON)
set(Boost_USE_STATIC_RUNTIME     ON)

find_package(Boost REQUIRED coroutine context thread system date_time chrono regex)


include_directories(${Boost_INCLUDE_DIRS})
#${Boost_LIBRARIES}

set(CMAKE_CXX_FLAGS "-fPIC -Wall")
set(CMAKE_CXX_FLAGS_DEBUG "-g -pg ${CMAKE_CXX_FLAGS} -Werror")
set(CMAKE_CXX_FLAGS_RELEASE "-g -O3 ${CMAKE_CXX_FLAGS} -Werror")

set(SAMPLE_1_SRC sample1_go.cpp)
add_executable(sample1 ${SAMPLE_1_SRC})
target_link_libraries(sample1 libgo_static ${Boost_LIBRARIES})