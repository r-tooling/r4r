# CMakeList.txt: Projekt CMake for ptraceBasedTracer, 
# depend on  the system having. dpkg, grep,apt-cache,R
#
cmake_minimum_required (VERSION 3.12)

project ("ptraceBasedTracer")

add_executable (ptraceBasedTracer "ptraceBasedTracer.cpp" "frontend/ptraceMainLoop.cpp" "middleend/middleEnd.cpp" "backend/backEnd.cpp" "frontend/syscalls/syscallHandlerMapperInline.cpp" "frontend/syscalls/open.cpp" "frontend/syscalls/dup.cpp" "frontend/syscalls/read.cpp" "frontend/syscalls/clone.cpp" "frontend/syscalls/pipe.cpp" "frontend/syscalls/execve.cpp" "frontend/syscalls/fnctl.cpp" "frontend/syscalls/stat.cpp" "frontend/syscalls/socket.cpp" "frontend/syscalls/futex.cpp" "frontend/syscalls/mkdir.cpp" "frontend/syscalls/unlink.cpp" "frontend/syscalls/syscallHandlerMapper.cpp" "frontend/syscalls/timer.cpp" "frontend/syscalls/epoll.cpp" "backend/dpkgResolver.cpp" "frontend/syscalls/chroot.cpp" "frontend/syscalls/event.cpp" "backend/rpkgResolver.cpp" "backend/aptResolver.cpp" "frontend/syscalls/rename.cpp" "csv/serialisedFileInfo.cpp")

set_property(TARGET ptraceBasedTracer PROPERTY CXX_STANDARD 20)

if(MSVC)
  target_compile_options(ptraceBasedTracer PRIVATE /W4 /WX)
else()
  target_compile_options(ptraceBasedTracer PRIVATE -Wall -Wextra -Wpedantic -Werror -g) #TODO: for actual release remove the -g
endif()

add_executable(generateMacro "dispatchMacro.cpp")
set_property(TARGET generateMacro PROPERTY CXX_STANDARD 20)