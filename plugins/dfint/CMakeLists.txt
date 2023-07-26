project(dfint)

SET(SOURCES
        dfint.cpp)

dfhack_plugin(${PROJECT_NAME} ${SOURCES} LINK_LIBRARIES ${CMAKE_CURRENT_SOURCE_DIR}/deps/sdl_ttf/SDL2_ttf.lib ${CMAKE_CURRENT_SOURCE_DIR}/deps/detours/detours.lib lua)