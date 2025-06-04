cmake_minimum_required(VERSION 3.14)

set(CMAKE_COLOR_DIAGNOSTICS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(r4r VERSION 0.1)

set(R4R_BIN "r4r")
set(R4R_LIB "r4r_lib")


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
  set(COVERAGE_FLAGS "-g -O0 --coverage")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COVERAGE_FLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COVERAGE_FLAGS}")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${COVERAGE_FLAGS}")
  add_compile_definitions(CODE_COVERAGE_ENABLED)
endif()


file(GLOB R4R_COMMON_SOURCES "src/*.cpp" "src/*.h")
list(REMOVE_ITEM R4R_COMMON_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
list(REMOVE_ITEM R4R_COMMON_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/r4r_lib.cpp")


# Binary sources
set(R4R_BIN_SOURCES
    src/main.cpp
    ${R4R_COMMON_SOURCES}
)

# Library sources
set(R4R_LIB_SOURCES
    src/r4r_lib.cpp
    ${R4R_COMMON_SOURCES}
)

add_executable(
    ${R4R_BIN}
    ${R4R_BIN_SOURCES}
)

add_library(${R4R_LIB} SHARED ${R4R_LIB_SOURCES})
set_target_properties(${R4R_LIB} PROPERTIES OUTPUT_NAME "r4r")

find_package(CURL REQUIRED)

target_compile_options(${R4R_BIN} PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_include_directories(${R4R_BIN}
    PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(${R4R_BIN} PRIVATE CURL::libcurl)

target_compile_options(${R4R_LIB} PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_include_directories(${R4R_LIB}
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
)
target_link_libraries(${R4R_LIB} PRIVATE CURL::libcurl)



install(TARGETS ${R4R_BIN})

# Install the library
install(TARGETS ${R4R_LIB}
    EXPORT r4rTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

# Install header files
install(DIRECTORY include/
    DESTINATION include
)

# Export target configuration
install(EXPORT r4rTargets
    FILE r4rTargets.cmake
    NAMESPACE r4r::
    DESTINATION lib/cmake/r4r
)


enable_testing()
add_subdirectory(tests)
