cmake_minimum_required(VERSION 3.5)
project(ppstep)

# Option to enable AddressSanitizer
option(ENABLE_ASAN "Enable AddressSanitizer for debugging memory issues" ON)

# Use passed-in Boost paths if provided
if(DEFINED BOOST_ROOT)
    message(STATUS "Using BOOST_ROOT: ${BOOST_ROOT}")
    
    # Bypass CMake's FindBoost version checks for newer Boost versions
    set(Boost_NO_SYSTEM_PATHS ON)
    set(Boost_NO_BOOST_CMAKE ON)
    set(Boost_ADDITIONAL_VERSIONS "1.89.0" "1.89" "1.88.0" "1.88")
    
    # Manually set include and library directories
    if(DEFINED BOOST_INCLUDEDIR)
        set(Boost_INCLUDE_DIR "${BOOST_INCLUDEDIR}")
        message(STATUS "Using BOOST_INCLUDEDIR: ${BOOST_INCLUDEDIR}")
    else()
        set(Boost_INCLUDE_DIR "${BOOST_ROOT}/include")
    endif()
    
    if(DEFINED BOOST_LIBRARYDIR)
        set(Boost_LIBRARY_DIR "${BOOST_LIBRARYDIR}")
        message(STATUS "Using BOOST_LIBRARYDIR: ${BOOST_LIBRARYDIR}")
    else()
        set(Boost_LIBRARY_DIR "${BOOST_ROOT}/lib")
    endif()
    
    # Find libraries manually - NOTE: system is header-only in Boost 1.69+
    find_library(Boost_FILESYSTEM_LIBRARY 
        NAMES boost_filesystem
        PATHS ${Boost_LIBRARY_DIR} 
        NO_DEFAULT_PATH
        REQUIRED)
    find_library(Boost_PROGRAM_OPTIONS_LIBRARY 
        NAMES boost_program_options
        PATHS ${Boost_LIBRARY_DIR} 
        NO_DEFAULT_PATH
        REQUIRED)
    find_library(Boost_THREAD_LIBRARY 
        NAMES boost_thread
        PATHS ${Boost_LIBRARY_DIR} 
        NO_DEFAULT_PATH
        REQUIRED)
    find_library(Boost_WAVE_LIBRARY 
        NAMES boost_wave
        PATHS ${Boost_LIBRARY_DIR} 
        NO_DEFAULT_PATH
        REQUIRED)
    
    # Set Boost variables manually
    set(Boost_FOUND TRUE)
    set(Boost_INCLUDE_DIRS "${Boost_INCLUDE_DIR}")
    set(Boost_LIBRARIES 
        ${Boost_FILESYSTEM_LIBRARY}
        ${Boost_PROGRAM_OPTIONS_LIBRARY}
        ${Boost_THREAD_LIBRARY}
        ${Boost_WAVE_LIBRARY}
    )
    
    message(STATUS "✅ Boost configured manually (system is header-only)")
    message(STATUS "   Include: ${Boost_INCLUDE_DIRS}")
    message(STATUS "   Libraries: ${Boost_LIBRARIES}")
else()
    # Fall back to standard FindBoost
    find_package(Boost COMPONENTS filesystem program_options thread wave REQUIRED)
    
    if(NOT Boost_FOUND)
        message(FATAL_ERROR "Boost not found!")
    endif()
    
    message(STATUS "Boost found: ${Boost_VERSION}")
    message(STATUS "Boost include: ${Boost_INCLUDE_DIRS}")
    message(STATUS "Boost libraries: ${Boost_LIBRARIES}")
endif()

file(GLOB_RECURSE sources src/*.cpp src/*.hpp)
file(GLOB_RECURSE external_sources external/*.cpp external/*.hpp external/*.c external/*.h)

add_executable(ppstep ${sources} ${external_sources})

target_include_directories(ppstep PUBLIC src external ${Boost_INCLUDE_DIRS})

target_compile_options(ppstep PUBLIC -std=c++17)

# AddressSanitizer configuration
if(ENABLE_ASAN)
    message(STATUS "🔍 AddressSanitizer ENABLED")
    
    # Compiler flags for ASan
    target_compile_options(ppstep PUBLIC 
        -fsanitize=address 
        -fno-omit-frame-pointer
        -g
        -O1
    )
    
    # Linker flags for ASan
    target_link_options(ppstep PUBLIC 
        -fsanitize=address
    )
    
    message(STATUS "   Compile flags: -fsanitize=address -fno-omit-frame-pointer -g -O1")
    message(STATUS "   Link flags: -fsanitize=address")
else()
    message(STATUS "AddressSanitizer disabled (use -DENABLE_ASAN=ON to enable)")
endif()

target_link_libraries(ppstep PUBLIC ${Boost_LIBRARIES})

install(TARGETS ppstep DESTINATION bin)
