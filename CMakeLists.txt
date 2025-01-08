cmake_minimum_required(VERSION 3.14)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project("r4r")

add_executable(
  r4r
  backend/aptResolver.cpp
  backend/backEnd.cpp
  backend/dpkgResolver.cpp
  backend/rpkgResolver.cpp
  csv/serialisedFileInfo.cpp
  frontend/ptraceMainLoop.cpp
  frontend/syscalls/chroot.cpp
  frontend/syscalls/clone.cpp
  frontend/syscalls/dup.cpp
  frontend/syscalls/epoll.cpp
  frontend/syscalls/event.cpp
  frontend/syscalls/execve.cpp
  frontend/syscalls/fnctl.cpp
  frontend/syscalls/futex.cpp
  frontend/syscalls/mkdir.cpp
  frontend/syscalls/open.cpp
  frontend/syscalls/pipe.cpp
  frontend/syscalls/read.cpp
  frontend/syscalls/rename.cpp
  frontend/syscalls/socket.cpp
  frontend/syscalls/stat.cpp
  frontend/syscalls/syscallHandlerMapper.cpp
  frontend/syscalls/syscallHandlerMapperInline.cpp
  frontend/syscalls/timer.cpp
  frontend/syscalls/unlink.cpp
  middleend/middleEnd.cpp
  ptraceBasedTracer.cpp
  util.cpp
)

target_compile_options(r4r PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(generateMacro dispatchMacro.cpp)

# ------------------------------------------------------------------------------
# TESTING
# ------------------------------------------------------------------------------

include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(
  r4r_test
  tests/util_test.cpp
  util.cpp
)

target_link_libraries(
  r4r_test
  GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(r4r_test)
