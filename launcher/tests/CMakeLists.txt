# Snapshot tests for the launcher's parsing code. Standalone on Linux and
# macOS, where the launcher's path helpers need no SDL:
#   cmake -S launcher/tests -B build/launcher-tests && cmake --build build/launcher-tests && ctest --test-dir build/launcher-tests -V
# A test run with --update rewrites its fixtures/*.expected.txt.
cmake_minimum_required(VERSION 3.25)
project(onevs100_launcher_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()
if(WIN32)
  message(FATAL_ERROR "Build the launcher tests on Linux or macOS; on Windows files.cpp needs SDL.")
endif()

include(FetchContent)
FetchContent_Declare(tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG v3.4.0
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(tomlplusplus)

set(LAUNCHER ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

add_executable(content_check_test content_check_test.cpp ${LAUNCHER}/content_check.cpp ${LAUNCHER}/files.cpp)
target_include_directories(content_check_test PRIVATE ${LAUNCHER} ${LAUNCHER}/../native/src)
target_link_libraries(content_check_test PRIVATE tomlplusplus::tomlplusplus)
add_test(NAME content_check COMMAND content_check_test ${FIXTURES})

add_executable(game_config_test game_config_test.cpp ${LAUNCHER}/game_config.cpp ${LAUNCHER}/files.cpp)
target_include_directories(game_config_test PRIVATE ${LAUNCHER})
target_link_libraries(game_config_test PRIVATE tomlplusplus::tomlplusplus)
add_test(NAME game_config COMMAND game_config_test ${FIXTURES})
