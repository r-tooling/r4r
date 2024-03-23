# CMakeList.txt: Projekt CMake pro ptraceBasedTracer, sem přidejte logiku zdrojového
# kódu a definic specifickou pro projekt.
#
cmake_minimum_required (VERSION 3.12)

project ("ptraceBasedTracer")

add_executable (ptraceBasedTracer "ptraceBasedTracer.cpp" "ptraceMainLoop.cpp" "middleEnd.cpp" "backEnd.cpp" "syscalls/open.cpp" "syscalls/dup.cpp" "syscalls/read.cpp" "syscalls/clone.cpp" "syscalls/pipe.cpp" "syscalls/execve.cpp" "syscalls/fnctl.cpp")

set_property(TARGET ptraceBasedTracer PROPERTY CXX_STANDARD 20)
