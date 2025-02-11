cmake_minimum_required(VERSION 3.14)

set(CMAKE_COLOR_DIAGNOSTICS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(r4r VERSION 0.1)

set(R4R_BIN "r4r")

configure_file(
    src/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/config.h
    @ONLY
)

option(ENABLE_COVERAGE "Enable coverage instrumentation" OFF)

if(ENABLE_COVERAGE)
  message(STATUS "Enabling code coverage")

  if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Coverage enabled: forcing CMAKE_BUILD_TYPE to Debug.")
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
  endif()

  # set compiler/linker flags for coverage
  set(COVERAGE_FLAGS "-g --coverage")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COVERAGE_FLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COVERAGE_FLAGS}")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${COVERAGE_FLAGS}")
endif()

file(GLOB R4R_BIN_SOURCES "src/*.cpp" "src/*.h")

add_executable(
    ${R4R_BIN}
    ${R4R_BIN_SOURCES}
)

find_package(CURL REQUIRED)

target_compile_options(${R4R_BIN} PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_include_directories(${R4R_BIN}
    PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(${R4R_BIN} PRIVATE CURL::libcurl)

install(TARGETS ${R4R_BIN})

enable_testing()
add_subdirectory(tests)
