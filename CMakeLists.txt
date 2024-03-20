# CMakeList.txt: Projekt CMake pro ptraceBasedTracer, sem přidejte logiku zdrojového
# kódu a definic specifickou pro projekt.
#
cmake_minimum_required (VERSION 3.12)

project ("ptraceBasedTracer")

add_executable (ptraceBasedTracer "ptraceBasedTracer.cpp" "ptraceMainLoop.cpp" "middleEnd.cpp" "backEnd.cpp")

set_property(TARGET ptraceBasedTracer PROPERTY CXX_STANDARD 20)
