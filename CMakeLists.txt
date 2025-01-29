cmake_minimum_required(VERSION 3.14)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)

project("r4r")

add_executable(
  ${PROJECT_NAME}
  main.cpp
  util.cpp
)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic -Werror)

install(TARGETS ${PROJECT_NAME})

# ------------------------------------------------------------------------------
# TESTING
# ------------------------------------------------------------------------------

set(INSTALL_GTEST OFF CACHE BOOL "Disable installation of GoogleTest" FORCE)

include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
)
FetchContent_MakeAvailable(googletest)

enable_testing()

file(GLOB TEST_SOURCES "tests/*.cpp")

add_executable(
  ${PROJECT_NAME}_test
  ${TEST_SOURCES}
  util.cpp
)

target_link_libraries(
  ${PROJECT_NAME}_test
  GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME}_test)
