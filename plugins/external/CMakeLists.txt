# Add external plugins here - this file is ignored by git

# Recommended: use add_subdirectory() for folders that you have created within
# this folder, or dfhack_plugin() for single files that you have added here.

# See the end of /plugins/CMakeLists.txt for more details.

project(dfint)

SET(SOURCES
        dfint.cpp)

dfhack_plugin(${PROJECT_NAME} ${SOURCES} LINK_LIBRARIES ${CMAKE_CURRENT_SOURCE_DIR}/SDL2_ttf.lib ${CMAKE_CURRENT_SOURCE_DIR}/detours.lib lua)