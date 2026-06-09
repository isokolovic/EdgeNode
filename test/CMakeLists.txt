# Download GTest at configure time so tests build without a system install.
include(FetchContent)

FetchContent_Declare(
	googletest
	GIT_REPOSITORY https://github.com/google/googletest.git
	GIT_TAG v1.14.0
)

FetchContent_MakeAvailable(googletest)

# Helper: register a GTest-based test target.
# Mock GPIO is always enabled in test builds so HAL-touching tests never need real hardware.
function(edgenode_add_gtest name)
	add_executable(${name} ${ARGN})
	target_link_libraries(${name} PRIVATE GTest::gtest_main)
	target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/include)
	target_compile_definitions(${name} PRIVATE EDGENODE_MOCK_GPIO=1)
	target_compile_options(${name} PRIVATE
		$<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
	)
	add_test(NAME ${name} COMMAND ${name})
endfunction()

edgenode_add_gtest(test_sensor_reading test_sensor_reading.cpp)
edgenode_add_gtest(test_result test_result.cpp)
edgenode_add_gtest(test_ring_buffer test_ring_buffer.cpp)
edgenode_add_gtest(test_protocol test_protocol.cpp ${CMAKE_SOURCE_DIR}/src/rpi/edge_protocol.cpp)