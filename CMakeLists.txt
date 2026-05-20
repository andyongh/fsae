cmake_minimum_required(VERSION 3.5)
project(fsae VERSION 0.1.0)

include(CTest)
enable_testing()

# Build the event loop library
add_library(fsae STATIC ae.c)
target_include_directories(fsae PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Build the example executable
add_executable(fsae_example examples/main.c)
target_link_libraries(fsae_example fsae)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
