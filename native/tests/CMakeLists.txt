# Equivalence and speed tests for the renderer's capture helpers, and the avatar code parser. Standalone, so it builds without the SDK:
#   cmake -S native/tests -B build/tests -DCMAKE_BUILD_TYPE=Release && cmake --build build/tests && ctest --test-dir build/tests -V
cmake_minimum_required(VERSION 3.25)
project(onevs100_tests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()

add_executable(constant_snapshot_test constant_snapshot_test.cpp)
target_include_directories(constant_snapshot_test PRIVATE ../src)
add_test(NAME constant_snapshot COMMAND constant_snapshot_test)

add_executable(stream_copy_test stream_copy_test.cpp)
target_include_directories(stream_copy_test PRIVATE ../src)
add_test(NAME stream_copy COMMAND stream_copy_test)

add_executable(floor_ceil_test floor_ceil_test.cpp)
target_include_directories(floor_ceil_test PRIVATE ../src)
add_test(NAME floor_ceil COMMAND floor_ceil_test)

add_executable(fatal_dialog_test fatal_dialog_test.cpp)
target_include_directories(fatal_dialog_test PRIVATE ../src)
add_test(NAME fatal_dialog COMMAND fatal_dialog_test)

add_executable(fma_test fma_test.cpp)
add_test(NAME fma COMMAND fma_test)

# The avatar library needs fmt; the game build gets it from the SDK.
include(FetchContent)
FetchContent_Declare(fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG 11.2.0
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(fmt)
add_subdirectory(../src/avatar_native onevs100_avatar)
add_executable(sona_code_test sona_code_test.cpp)
target_link_libraries(sona_code_test PRIVATE onevs100_avatar)
add_test(NAME sona_code COMMAND sona_code_test ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)
