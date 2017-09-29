cmake_minimum_required(VERSION 3.8)
project(wdeps)

set(CMAKE_CXX_STANDARD 17)

add_executable(wdeps wdeps.cpp pe-parse/parse.cpp pe-parse/buffer.cpp)

find_package(Boost 1.60 COMPONENTS program_options filesystem REQUIRED )

target_include_directories(wdeps PRIVATE ${Boost_INCLUDE_DIR})
target_link_libraries(wdeps ${Boost_LIBRARIES})
