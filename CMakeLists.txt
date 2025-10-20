cmake_minimum_required(VERSION 3.5)
project(ppstep)

# Use passed-in Boost paths if provided
if(DEFINED BOOST_ROOT)
    message(STATUS "Using BOOST_ROOT: ${BOOST_ROOT}")
    set(Boost_NO_SYSTEM_PATHS ON)
    set(Boost_NO_BOOST_CMAKE ON)
endif()

if(DEFINED BOOST_INCLUDEDIR)
    message(STATUS "Using BOOST_INCLUDEDIR: ${BOOST_INCLUDEDIR}")
endif()

if(DEFINED BOOST_LIBRARYDIR)
    message(STATUS "Using BOOST_LIBRARYDIR: ${BOOST_LIBRARYDIR}")
endif()

find_package(Boost COMPONENTS system filesystem program_options thread wave REQUIRED)

if(NOT Boost_FOUND)
    message(FATAL_ERROR "Boost not found! BOOST_ROOT=${BOOST_ROOT}")
endif()

message(STATUS "Boost found: ${Boost_VERSION}")
message(STATUS "Boost include: ${Boost_INCLUDE_DIRS}")
message(STATUS "Boost libraries: ${Boost_LIBRARIES}")

file(GLOB_RECURSE sources src/*.cpp src/*.hpp)
file(GLOB_RECURSE external_sources external/*.cpp external/*.hpp external/*.c external/*.h)

add_executable(ppstep ${sources} ${external_sources})

target_include_directories(ppstep PUBLIC src external)

target_compile_options(ppstep PUBLIC -std=c++17)

target_link_libraries(ppstep PUBLIC ${Boost_LIBRARIES})

install(TARGETS ppstep DESTINATION bin)
