cmake_minimum_required (VERSION 3.12)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project ("r4r")

add_executable (r4r "ptraceBasedTracer.cpp" "frontend/ptraceMainLoop.cpp" "middleend/middleEnd.cpp" "backend/backEnd.cpp" "frontend/syscalls/syscallHandlerMapperInline.cpp" "frontend/syscalls/open.cpp" "frontend/syscalls/dup.cpp" "frontend/syscalls/read.cpp" "frontend/syscalls/clone.cpp" "frontend/syscalls/pipe.cpp" "frontend/syscalls/execve.cpp" "frontend/syscalls/fnctl.cpp" "frontend/syscalls/stat.cpp" "frontend/syscalls/socket.cpp" "frontend/syscalls/futex.cpp" "frontend/syscalls/mkdir.cpp" "frontend/syscalls/unlink.cpp" "frontend/syscalls/syscallHandlerMapper.cpp" "frontend/syscalls/timer.cpp" "frontend/syscalls/epoll.cpp" "backend/dpkgResolver.cpp" "frontend/syscalls/chroot.cpp" "frontend/syscalls/event.cpp" "backend/rpkgResolver.cpp" "backend/aptResolver.cpp" "frontend/syscalls/rename.cpp" "csv/serialisedFileInfo.cpp")

set_property(TARGET r4r PROPERTY CXX_STANDARD 20)
target_compile_options(r4r PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(generateMacro "dispatchMacro.cpp")
set_property(TARGET generateMacro PROPERTY CXX_STANDARD 20)
