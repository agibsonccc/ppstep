cmake_minimum_required(VERSION 3.5)
project(ppstep)

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
    
    # Find libraries manually with flexible naming (handles -mt, version suffixes, etc.)
    find_library(Boost_SYSTEM_LIBRARY 
        NAMES boost_system-mt boost_system boost_system-mt-x64 boost_system-x64
        PATHS ${Boost_LIBRARY_DIR} 
        NO_DEFAULT_PATH)
    find_library(Boost_FILESYSTEM_LIBRARY 
        NAMES boost_filesystem-mt boost_filesystem boost_filesystem-mt-x64 boost_filesystem-x64
        PATHS ${Boost_LIBRARY_DIR} 
        NO_DEFAULT_PATH)
    find_library(Boost_PROGRAM_OPTIONS_LIBRARY 
        NAMES boost_program_options-mt boost_program_options boost_program_options-mt-x64 boost_program_options-x64
        PATHS ${Boost_LIBRARY_DIR} 
        NO_DEFAULT_PATH)
    find_library(Boost_THREAD_LIBRARY 
        NAMES boost_thread-mt boost_thread boost_thread-mt-x64 boost_thread-x64
        PATHS ${Boost_LIBRARY_DIR} 
        NO_DEFAULT_PATH)
    find_library(Boost_WAVE_LIBRARY 
        NAMES boost_wave-mt boost_wave boost_wave-mt-x64 boost_wave-x64
        PATHS ${Boost_LIBRARY_DIR} 
        NO_DEFAULT_PATH)
    
    # Diagnostic: list what's actually in the lib directory
    file(GLOB LIB_FILES "${Boost_LIBRARY_DIR}/libboost_*")
    message(STATUS "Libraries found in ${Boost_LIBRARY_DIR}:")
    foreach(lib ${LIB_FILES})
        get_filename_component(lib_name ${lib} NAME)
        message(STATUS "  ${lib_name}")
    endforeach()
    
    # Check that we found all required libraries
    if(NOT Boost_SYSTEM_LIBRARY)
        message(FATAL_ERROR "Could not find boost_system in ${Boost_LIBRARY_DIR}")
    endif()
    if(NOT Boost_FILESYSTEM_LIBRARY)
        message(FATAL_ERROR "Could not find boost_filesystem in ${Boost_LIBRARY_DIR}")
    endif()
    if(NOT Boost_PROGRAM_OPTIONS_LIBRARY)
        message(FATAL_ERROR "Could not find boost_program_options in ${Boost_LIBRARY_DIR}")
    endif()
    if(NOT Boost_THREAD_LIBRARY)
        message(FATAL_ERROR "Could not find boost_thread in ${Boost_LIBRARY_DIR}")
    endif()
    if(NOT Boost_WAVE_LIBRARY)
        message(FATAL_ERROR "Could not find boost_wave in ${Boost_LIBRARY_DIR}")
    endif()
    
    # Set Boost variables manually
    set(Boost_FOUND TRUE)
    set(Boost_INCLUDE_DIRS "${Boost_INCLUDE_DIR}")
    set(Boost_LIBRARIES 
        ${Boost_SYSTEM_LIBRARY}
        ${Boost_FILESYSTEM_LIBRARY}
        ${Boost_PROGRAM_OPTIONS_LIBRARY}
        ${Boost_THREAD_LIBRARY}
        ${Boost_WAVE_LIBRARY}
    )
    
    message(STATUS "✅ Boost configured manually")
    message(STATUS "   Include: ${Boost_INCLUDE_DIRS}")
    message(STATUS "   Libraries: ${Boost_LIBRARIES}")
else()
    # Fall back to standard FindBoost
    find_package(Boost COMPONENTS system filesystem program_options thread wave REQUIRED)
    
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

target_link_libraries(ppstep PUBLIC ${Boost_LIBRARIES})

install(TARGETS ppstep DESTINATION bin)
