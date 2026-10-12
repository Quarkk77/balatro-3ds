cmake_minimum_required(VERSION 3.16)
project(balatro_c C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# vendored raylib (replaces LÖVE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(PLATFORM Desktop CACHE STRING "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/raylib EXCLUDE_FROM_ALL)

file(GLOB SRC_CORE core/*.c)
file(GLOB SRC_GFX gfx/*.c)
file(GLOB SRC_ENGINE engine/*.c)
file(GLOB SRC_GAME game/*.c)

add_executable(balatro ${SRC_CORE} ${SRC_GFX} ${SRC_ENGINE} ${SRC_GAME} main.c)
target_include_directories(balatro PRIVATE core gfx engine game
  third_party/raylib/src)
target_link_libraries(balatro PRIVATE raylib m)
target_compile_options(balatro PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-override-init
  -Wno-missing-field-initializers -Wno-sign-compare -Wno-unused-function)

# Game resources (images, sounds, fonts, shaders, localization) are copied
# from the extracted original into the build tree next to the executable.
set(LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src_lua)
add_custom_command(TARGET balatro POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${LUA_SRC}/resources $<TARGET_FILE_DIR:balatro>/resources
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${LUA_SRC}/localization $<TARGET_FILE_DIR:balatro>/localization)
