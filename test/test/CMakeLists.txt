project(test)

set(Boost_USE_STATIC_LIBS        ON)
set(Boost_USE_MULTITHREADED      ON)
set(Boost_USE_STATIC_RUNTIME     ON)

find_package(Boost REQUIRED coroutine context thread system date_time chrono regex)


include_directories(${Boost_INCLUDE_DIRS})
#${Boost_LIBRARIES}

set(TEST_SLEEP_SRC sleep.cpp)
add_executable(sleep ${TEST_SLEEP_SRC})
target_link_libraries(sleep libgo_static ${Boost_LIBRARIES})