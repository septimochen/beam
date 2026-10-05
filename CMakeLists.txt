cmake_minimum_required(VERSION 3.25)
project(Beam VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use an out-of-tree build: cmake -S . -B build")
endif()

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(beam-core STATIC src/core/beam.cpp)
add_library(Beam::Core ALIAS beam-core)
target_compile_features(beam-core PUBLIC cxx_std_23)
target_include_directories(beam-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(beam-core PRIVATE BEAM_VERSION="${PROJECT_VERSION}")

add_executable(beam src/cli/main.cpp)
target_link_libraries(beam PRIVATE Beam::Core)

function(beam_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    endif()
endfunction()
beam_enable_warnings(beam-core)
beam_enable_warnings(beam)

include(CTest)
if(BUILD_TESTING)
    add_executable(beam-core-tests tests/core_tests.cpp)
    target_link_libraries(beam-core-tests PRIVATE Beam::Core)
    beam_enable_warnings(beam-core-tests)
    add_test(NAME core COMMAND beam-core-tests)
    add_test(NAME cli COMMAND "${CMAKE_COMMAND}"
        "-DBEAM_EXECUTABLE=$<TARGET_FILE:beam>"
        "-DBEAM_VERSION=${PROJECT_VERSION}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_tests.cmake")
endif()

include(GNUInstallDirs)
install(TARGETS beam RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
