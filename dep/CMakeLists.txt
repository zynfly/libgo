


if(${DEPEND_BOOST})
    add_subdirectory(boost)
endif()

if(WIN32 AND NOT DISABLE_HOOK)
    add_subdirectory(minhook)
endif()