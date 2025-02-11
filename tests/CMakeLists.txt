set(INSTALL_GTEST OFF CACHE BOOL "Disable installation of GoogleTest" FORCE)

include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
)
FetchContent_MakeAvailable(googletest)

enable_testing()

file(GLOB TEST_SOURCES "*.cpp" "*.h")

add_executable(
        ${PROJECT_NAME}_test
        ${TEST_SOURCES}
)

target_link_libraries(
        ${PROJECT_NAME}_test
        GTest::gtest_main
)
target_link_libraries(
        ${PROJECT_NAME}_test
        CURL::libcurl
)

target_include_directories(
        ${PROJECT_NAME}_test
        PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

include(GoogleTest)
gtest_discover_tests(${PROJECT_NAME}_test)

include(CTest)

