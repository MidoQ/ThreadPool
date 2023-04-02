cmake_minimum_required(VERSION 3.1.0)
project(ThreadPool VERSION 0.1.0)

include(CTest)
enable_testing()

set(CMAKE_CXX_STANDARD 14)
# set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -fno-elide-constructors)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()
message(STATUS "Build type: [${CMAKE_BUILD_TYPE}]")

include_directories(include)
add_executable(test_basic ./test/test_basic.cpp)
add_executable(test_empty_task ./test/test_empty_task.cpp)
target_link_libraries(test_basic pthread)
target_link_libraries(test_empty_task pthread)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
