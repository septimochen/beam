cmake_minimum_required(VERSION 3.25)
project(Beam VERSION 0.2.0 LANGUAGES CXX)

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

option(BEAM_ENABLE_QUIC "Build macOS/Linux QUIC transfers" ${UNIX})
if(BEAM_ENABLE_QUIC)
    if(NOT UNIX)
        message(FATAL_ERROR "Receive storage currently supports macOS/Linux only")
    endif()
    find_package(OpenSSL REQUIRED COMPONENTS Crypto)
    find_package(Threads REQUIRED)
    set(MSQUIC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/build/deps/install" CACHE PATH "Installed MsQuic prefix")
    find_path(MSQUIC_INCLUDE_DIR msquic.h HINTS "${MSQUIC_ROOT}/include" REQUIRED)
    find_library(MSQUIC_LIBRARY NAMES msquic HINTS "${MSQUIC_ROOT}/lib" "${MSQUIC_ROOT}/lib64" REQUIRED)
    target_sources(beam-core PRIVATE src/core/transfer.cpp src/core/errors.cpp src/protocol/codec.cpp
        src/transfer/files.cpp src/transport/msquic.cpp)
    target_include_directories(beam-core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_include_directories(beam-core SYSTEM PRIVATE "${MSQUIC_INCLUDE_DIR}")
    target_link_libraries(beam-core PRIVATE OpenSSL::Crypto Threads::Threads "${MSQUIC_LIBRARY}")
    target_compile_definitions(beam-core PUBLIC BEAM_HAS_QUIC=1)
endif()

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
        "-DBEAM_HAS_QUIC=${BEAM_ENABLE_QUIC}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_tests.cmake")
    if(BEAM_ENABLE_QUIC)
        add_executable(beam-transfer-tests tests/transfer_tests.cpp)
        target_include_directories(beam-transfer-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(beam-transfer-tests PRIVATE Beam::Core)
        beam_enable_warnings(beam-transfer-tests)
        add_test(NAME transfer COMMAND beam-transfer-tests)
        add_executable(beam-phase2-tests tests/phase2_tests.cpp)
        target_include_directories(beam-phase2-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(beam-phase2-tests PRIVATE Beam::Core)
        beam_enable_warnings(beam-phase2-tests)
        find_program(OPENSSL_EXECUTABLE openssl REQUIRED)
        find_program(BASH_EXECUTABLE bash REQUIRED)
        add_test(NAME quic-localhost COMMAND "${BASH_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.sh" "$<TARGET_FILE:beam>"
            "${OPENSSL_EXECUTABLE}" "$<TARGET_FILE:beam-transfer-tests>" "$<TARGET_FILE:beam-phase2-tests>")
        set_tests_properties(quic-localhost PROPERTIES TIMEOUT 120)
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS beam RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
