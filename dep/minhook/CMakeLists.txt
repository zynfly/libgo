
message("minhook has been include")

set(CMAKE_CXX_STANDARD 11)

aux_source_directory(minhook/src MINHOOK_SRCS)
aux_source_directory(minhook/src/hde MINHOOK_SRCS)

include_directories(minhook/include)


add_library(minhook_lib STATIC ${MINHOOK_SRCS})


add_library(minhook INTERFACE)

target_link_libraries(minhook
        INTERFACE
        minhook_lib)

target_include_directories(minhook
        INTERFACE
        minhook/include)

