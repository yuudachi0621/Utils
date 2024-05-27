cmake_minimum_required(VERSION 3.0.0)
project(utils VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SRC_PATH ${CMAKE_SOURCE_DIR}/src)

include_directories(
	${SRC_PATH}/
	${CMAKE_SOURCE_DIR}/src/thread/
	${SRC_PATH}/thread/
)

file(GLOB_RECURSE CPP_UTILS_FILES ${SRC_PATH}/*.c*)
add_executable(${PROJECT_NAME}
	${CPP_UTILS_FILES}
)

# add_library(${PROJECT_NAME} STATIC
# )