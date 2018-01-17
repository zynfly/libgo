

set(Boost_USE_STATIC_LIBS        ON)
set(Boost_USE_MULTITHREADED      ON)
set(Boost_USE_STATIC_RUNTIME     ON)

find_package(Boost REQUIRED coroutine context thread system date_time chrono regex)


add_library(boost INTERFACE)

if(WIN32)
    target_link_libraries(boost
            INTERFACE
            ${Boost_LIBRARIES} wsock32 ws2_32)
else()
    target_link_libraries(boost
            INTERFACE
            ${Boost_LIBRARIES})
endif(WIN32)

target_include_directories(boost
        INTERFACE
        ${Boost_INCLUDE_DIRS})
