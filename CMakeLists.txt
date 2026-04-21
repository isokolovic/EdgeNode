cmake_minimum_required(VERSION 3.20)

project(EdgeNode
    VERSION 1.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build options.
option(EDGENODE_MOCK_GPIO "Use mock GPIO for non-RPi builds" OFF)
option(EDGENODE_BUILD_TESTS "Build unit tests" ON)

# Use the mock GPIO backend on non-ARM machines.
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64")
    set(EDGENODE_MOCK_GPIO ON)
endif()

set(EDGENODE_SOURCES
    "src/main.cpp"
    src/rpi/gpio.cpp
    src/rpi/edge_protocol.cpp
    src/rpi/serial_comm.cpp
)

add_executable(EdgeNode ${EDGENODE_SOURCES})

target_include_directories(EdgeNode
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(EdgeNode PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

if(EDGENODE_MOCK_GPIO)
    target_compile_definitions(EdgeNode PRIVATE EDGENODE_MOCK_GPIO=1)
    message(STATUS "GPIO: MOCK (non-ARM platform)")
else()
    message(STATUS "GPIO: REAL memory-mapped registers")
endif()

if(EDGENODE_BUILD_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test/CMakeLists.txt)
    enable_testing()
    add_subdirectory(test)
else()
    if(EDGENODE_BUILD_TESTS)
        # Tests are optional when the test directory is not present.
        message(STATUS "Tests: skipped (test/CMakeLists.txt not found)")
    endif()
endif()
