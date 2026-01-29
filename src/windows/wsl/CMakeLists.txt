set(SOURCES
    main.cpp
    main.rc)

set(HEADERS
    resource.h)

# WSLX Fork: Renamed from wsl to wslx for SxS operation
add_executable(wslx ${SOURCES} ${HEADERS})

target_link_libraries(wslx
                      ${COMMON_LINK_LIBRARIES}
                      common
                      runtimeobject.lib)

target_precompile_headers(wslx REUSE_FROM common)
set_target_properties(wslx PROPERTIES FOLDER windows)