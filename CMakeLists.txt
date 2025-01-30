cmake_minimum_required(VERSION 3.14)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_COLOR_DIAGNOSTICS ON)

project(r4r VERSION 0.1)

file(GLOB BIN_SOURCES "src/*.cpp" "src/*.h")

add_executable(
        ${PROJECT_NAME}
        ${BIN_SOURCES}
)

target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic -Werror)

install(TARGETS ${PROJECT_NAME})

add_subdirectory(tests)
