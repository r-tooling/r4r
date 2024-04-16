# CMakeList.txt: Projekt CMake for ptraceBasedTracer, 
# depend on  the system having. dpkg, grep,apt-cache,R
#
cmake_minimum_required (VERSION 3.12)

project ("ptraceBasedTracer")

add_executable (ptraceBasedTracer "ptraceBasedTracer.cpp" "ptraceMainLoop.cpp" "middleend/middleEnd.cpp" "backend/backEnd.cpp" "syscalls/open.cpp" "syscalls/dup.cpp" "syscalls/read.cpp" "syscalls/clone.cpp" "syscalls/pipe.cpp" "syscalls/execve.cpp" "syscalls/fnctl.cpp" "syscalls/stat.cpp" "syscalls/socket.cpp" "syscalls/futex.cpp" "syscalls/mkdir.cpp" "syscalls/unlink.cpp" "syscalls/syscallHandlerMapper.cpp" "syscalls/timer.cpp" "syscalls/epoll.cpp" "backend/dpkgResolver.cpp" "syscalls/chroot.cpp" "syscalls/event.cpp" "backend/rpkgResolver.cpp" "backend/aptResolver.cpp")

set_property(TARGET ptraceBasedTracer PROPERTY CXX_STANDARD 20)

if(MSVC)
  target_compile_options(ptraceBasedTracer PRIVATE /W4 /WX)
else()
  target_compile_options(ptraceBasedTracer PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()