


list(APPEND LIBGO_INCLUDE_DIRS ./)
#include_directories(./)
aux_source_directory(./ CO_SRC_LIST)

if (UNIX)
    # unix like platforms
    aux_source_directory(linux CO_SRC_LIST)
    #include_directories(linux)
    list(APPEND LIBGO_INCLUDE_DIRS linux)

    if (NOT DEPEND_BOOST)
        aux_source_directory(ctx_ucontext CO_SRC_LIST)
    endif()

    if (DISABLE_HOOK)
        list(REMOVE_ITEM CO_SRC_LIST linux/linux_glibc_hook.cpp)
        aux_source_directory(disable_hook CO_SRC_LIST)
    endif()
elseif (WIN32)
    #include_directories(windows)
    list(APPEND LIBGO_INCLUDE_DIRS windows)
    aux_source_directory(windows CO_SRC_LIST)
    aux_source_directory(windows/arpa CO_SRC_LIST)
    aux_source_directory(windows/sys CO_SRC_LIST)

    if (DEPEND_BOOST)
        message(WARNING "boost.coroutine was unstable in windows platform")
    else()
        aux_source_directory(ctx_win_fiber CO_SRC_LIST)
    endif()

    if (DISABLE_HOOK)
        list(REMOVE_ITEM CO_SRC_LIST windows/win_vc_hook.cpp)
        aux_source_directory(disable_hook CO_SRC_LIST)
    endif()
else ()
    # other platforms
    message(FATAL_ERROR "Not support current platform!")
endif()


include_directories(${LIBGO_INCLUDE_DIRS})

configure_file(${PROJECT_SOURCE_DIR}/libgo/cmake_config.h.in ${PROJECT_SOURCE_DIR}/libgo/cmake_config.h)


set(TARGET "libgo")
set(STATIC_T "libgo_static")

add_library("${STATIC_T}" STATIC ${CO_SRC_LIST})
target_link_libraries("${STATIC_T}" ws2_32)
target_include_directories("${STATIC_T}" INTERFACE ../ ${LIBGO_INCLUDE_DIRS})
if(DEPEND_BOOST)
    target_link_libraries("${STATIC_T}" boost)
endif()

if (WIN32 AND NOT DISABLE_HOOK)
    target_link_libraries("${STATIC_T}" minhook)
endif ()

if(NOT DISABLE_DYNAMIC_LIB)
    set(SHARED_T "libgo_dynamic")
    add_library("${SHARED_T}" SHARED ${CO_SRC_LIST})
    target_include_directories("${SHARED_T}" INTERFACE ../ ${LIBGO_INCLUDE_DIRS})
    target_link_libraries("${SHARED_T}" ws2_32)
    if(DEPEND_BOOST)
        target_link_libraries("${SHARED_T}" boost)
    endif()
    if (WIN32 AND NOT DISABLE_HOOK)
        target_link_libraries("${SHARED_T}" minhook)
    endif ()
endif()
