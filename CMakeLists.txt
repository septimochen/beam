cmake_minimum_required(VERSION 3.25)
if(CMAKE_HOST_APPLE AND BEAM_ENABLE_DESKTOP)
    # Desktop builds follow the selected SDK, including existing build trees
    # that cached the former macOS 13 baseline. Homebrew/runtime dependencies
    # built for this macOS must not be linked into an older deployment target.
    set(beam_macos_sdk macosx)
    if(CMAKE_OSX_SYSROOT)
        set(beam_macos_sdk "${CMAKE_OSX_SYSROOT}")
    endif()
    execute_process(COMMAND xcrun --sdk "${beam_macos_sdk}" --show-sdk-version
        OUTPUT_VARIABLE beam_macos_version OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "${beam_macos_version}" CACHE STRING
        "Desktop deployment version (follows the selected macOS SDK)" FORCE)
    message(STATUS "Beam desktop targets macOS ${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()
project(Beam VERSION 0.9.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use an out-of-tree build: cmake -S . -B build")
endif()

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(ANDROID)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

add_library(beam-core STATIC src/core/beam.cpp src/core/config.cpp)
add_library(Beam::Core ALIAS beam-core)
target_compile_features(beam-core PUBLIC cxx_std_23)
target_include_directories(beam-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(beam-core PRIVATE BEAM_VERSION="${PROJECT_VERSION}")

option(BEAM_ENABLE_QUIC "Build macOS/Linux QUIC transfers" ${UNIX})
if(BEAM_ENABLE_QUIC)
    if(NOT UNIX)
        message(FATAL_ERROR "Receive storage currently supports macOS/Linux only")
    endif()
    if(NOT TARGET OpenSSL::Crypto)
        find_package(OpenSSL REQUIRED COMPONENTS Crypto)
    endif()
    find_package(Threads REQUIRED)
    set(MSQUIC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/build/deps/install" CACHE PATH "Installed MsQuic prefix")
    find_path(MSQUIC_INCLUDE_DIR msquic.h HINTS "${MSQUIC_ROOT}/include" REQUIRED)
    find_library(MSQUIC_LIBRARY NAMES msquic HINTS "${MSQUIC_ROOT}/lib" "${MSQUIC_ROOT}/lib64" REQUIRED)
    target_sources(beam-core PRIVATE src/core/identity.cpp src/core/routing.cpp src/core/transfer.cpp src/core/errors.cpp src/protocol/codec.cpp
        src/transfer/files.cpp src/transfer/content.cpp src/transport/msquic.cpp)
    target_include_directories(beam-core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_include_directories(beam-core SYSTEM PRIVATE "${MSQUIC_INCLUDE_DIR}")
    target_link_libraries(beam-core PRIVATE OpenSSL::Crypto Threads::Threads "${MSQUIC_LIBRARY}")
    target_compile_definitions(beam-core PUBLIC BEAM_HAS_QUIC=1)
endif()

set(beam_optional_discovery ${BEAM_ENABLE_QUIC})
if(ANDROID)
    set(beam_optional_discovery OFF)
endif()
option(BEAM_ENABLE_TAILSCALE "Build optional local Tailscale CLI discovery" ${beam_optional_discovery})
if(BEAM_ENABLE_TAILSCALE)
    if(NOT BEAM_ENABLE_QUIC)
        message(FATAL_ERROR "Tailscale discovery requires the macOS/Linux QUIC core")
    endif()
    find_package(nlohmann_json 3.11 REQUIRED)
    target_sources(beam-core PRIVATE src/discovery/tailscale.cpp)
    target_link_libraries(beam-core PRIVATE nlohmann_json::nlohmann_json)
    target_compile_definitions(beam-core PUBLIC BEAM_HAS_TAILSCALE=1)
endif()

option(BEAM_ENABLE_DISCOVERY "Build LAN discovery with Bonjour/Avahi DNS-SD" ${beam_optional_discovery})
if(BEAM_ENABLE_DISCOVERY)
    if(NOT UNIX)
        message(FATAL_ERROR "LAN discovery currently supports macOS/Linux only")
    endif()
    find_package(Threads REQUIRED)
    find_path(DNSSD_INCLUDE_DIR dns_sd.h REQUIRED)
    if(NOT APPLE)
        find_library(DNSSD_LIBRARY NAMES dns_sd REQUIRED)
        target_link_libraries(beam-core PRIVATE "${DNSSD_LIBRARY}")
    endif()
    target_sources(beam-core PRIVATE src/discovery/dnssd.cpp)
    target_include_directories(beam-core SYSTEM PRIVATE "${DNSSD_INCLUDE_DIR}")
    target_link_libraries(beam-core PRIVATE Threads::Threads)
    target_compile_definitions(beam-core PUBLIC BEAM_HAS_DISCOVERY=1)
endif()

function(beam_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    endif()
endfunction()
beam_enable_warnings(beam-core)

if(BEAM_ENABLE_QUIC)
    add_library(beam-app-support STATIC src/desktop/service.cpp src/android/session.cpp)
    target_include_directories(beam-app-support PUBLIC "${PROJECT_SOURCE_DIR}/src")
    target_link_libraries(beam-app-support PUBLIC Beam::Core PRIVATE Threads::Threads)
    beam_enable_warnings(beam-app-support)
endif()

if(ANDROID)
    if(NOT BEAM_ENABLE_QUIC OR BEAM_ENABLE_DISCOVERY OR BEAM_ENABLE_TAILSCALE)
        message(FATAL_ERROR "Android requires QUIC with desktop DNS-SD/Tailscale CLI discovery disabled")
    endif()
    add_library(beam-android SHARED src/android/jni.cpp)
    target_link_libraries(beam-android PRIVATE beam-app-support)
    target_link_options(beam-android PRIVATE "-Wl,-z,max-page-size=16384")
    beam_enable_warnings(beam-android)
    return()
endif()

add_executable(beam src/cli/main.cpp)
target_link_libraries(beam PRIVATE Beam::Core)
beam_enable_warnings(beam)

option(BEAM_ENABLE_DESKTOP "Build the optional native macOS or Qt Linux tray app" OFF)
include(CTest)
if(BEAM_ENABLE_DESKTOP)
    add_subdirectory(src/desktop)
endif()

if(BUILD_TESTING)
    add_executable(beam-config-tests tests/config_tests.cpp)
    target_link_libraries(beam-config-tests PRIVATE Beam::Core)
    beam_enable_warnings(beam-config-tests)
    add_test(NAME config COMMAND beam-config-tests)
    if(UNIX)
        add_test(NAME uninstall COMMAND "${CMAKE_COMMAND}"
            "-DBEAM_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/uninstall-test"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/uninstall_tests.cmake")
        add_test(NAME android-installer COMMAND "${CMAKE_COMMAND}"
            "-DBEAM_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DTEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/android-installer-test"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/android_install_tests.cmake")
        add_test(NAME install COMMAND "${CMAKE_COMMAND}"
            "-DBEAM_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            "-DBEAM_HAS_DESKTOP=${BEAM_ENABLE_DESKTOP}"
            "-DBEAM_HAS_QUIC=${BEAM_ENABLE_QUIC}"
            "-DBEAM_MACOS_VERSION=${CMAKE_OSX_DEPLOYMENT_TARGET}"
            "-DBEAM_CONFIG=$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/install_tests.cmake")
    endif()
    add_executable(beam-core-tests tests/core_tests.cpp)
    target_link_libraries(beam-core-tests PRIVATE Beam::Core)
    beam_enable_warnings(beam-core-tests)
    add_test(NAME core COMMAND beam-core-tests)
    add_test(NAME cli COMMAND "${CMAKE_COMMAND}"
        "-DBEAM_EXECUTABLE=$<TARGET_FILE:beam>"
        "-DBEAM_VERSION=${PROJECT_VERSION}"
        "-DBEAM_HAS_QUIC=${BEAM_ENABLE_QUIC}"
        "-DBEAM_HAS_DISCOVERY=${BEAM_ENABLE_DISCOVERY}"
        "-DBEAM_HAS_TAILSCALE=${BEAM_ENABLE_TAILSCALE}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_tests.cmake")
    if(BEAM_ENABLE_DISCOVERY)
        add_executable(beam-discovery-tests tests/discovery_tests.cpp src/discovery/dnssd.cpp)
        target_compile_features(beam-discovery-tests PRIVATE cxx_std_23)
        target_include_directories(beam-discovery-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
        target_include_directories(beam-discovery-tests SYSTEM PRIVATE "${DNSSD_INCLUDE_DIR}")
        target_link_libraries(beam-discovery-tests PRIVATE Threads::Threads)
        beam_enable_warnings(beam-discovery-tests)
        add_test(NAME discovery COMMAND beam-discovery-tests)
        set_tests_properties(discovery PROPERTIES TIMEOUT 10)
        add_executable(beam-discovery-live-tests tests/discovery_live_tests.cpp)
        target_link_libraries(beam-discovery-live-tests PRIVATE Beam::Core)
        beam_enable_warnings(beam-discovery-live-tests)
    endif()
    if(BEAM_ENABLE_QUIC)
        add_executable(beam-android-tests tests/android_tests.cpp)
        target_link_libraries(beam-android-tests PRIVATE beam-app-support)
        beam_enable_warnings(beam-android-tests)
        add_test(NAME android-session COMMAND beam-android-tests)
        set_tests_properties(android-session PROPERTIES TIMEOUT 45)
        add_executable(beam-routing-tests tests/routing_tests.cpp)
        target_include_directories(beam-routing-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(beam-routing-tests PRIVATE Beam::Core)
        beam_enable_warnings(beam-routing-tests)
        add_test(NAME routing COMMAND beam-routing-tests)
        add_executable(beam-identity-tests tests/identity_tests.cpp)
        target_link_libraries(beam-identity-tests PRIVATE Beam::Core OpenSSL::Crypto)
        beam_enable_warnings(beam-identity-tests)
        add_test(NAME identity COMMAND beam-identity-tests)
        set_tests_properties(identity PROPERTIES TIMEOUT 15)
        add_executable(beam-transfer-tests tests/transfer_tests.cpp)
        target_include_directories(beam-transfer-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(beam-transfer-tests PRIVATE Beam::Core)
        beam_enable_warnings(beam-transfer-tests)
        add_test(NAME transfer COMMAND beam-transfer-tests)
        add_executable(beam-content-tests tests/content_tests.cpp)
        target_include_directories(beam-content-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(beam-content-tests PRIVATE Beam::Core)
        beam_enable_warnings(beam-content-tests)
        add_test(NAME content COMMAND beam-content-tests)
        add_executable(beam-phase2-tests tests/phase2_tests.cpp)
        target_include_directories(beam-phase2-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
        target_link_libraries(beam-phase2-tests PRIVATE Beam::Core)
        beam_enable_warnings(beam-phase2-tests)
        find_program(OPENSSL_EXECUTABLE openssl REQUIRED)
        find_program(BASH_EXECUTABLE bash REQUIRED)
        add_test(NAME routing-localhost COMMAND "${BASH_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/routing_integration.sh"
            "$<TARGET_FILE:beam>" "$<TARGET_FILE:beam-routing-tests>")
        set_tests_properties(routing-localhost PROPERTIES TIMEOUT 45)
        if(BEAM_ENABLE_TAILSCALE)
            add_executable(beam-tailscale-tests tests/tailscale_tests.cpp)
            target_link_libraries(beam-tailscale-tests PRIVATE Beam::Core)
            beam_enable_warnings(beam-tailscale-tests)
            add_test(NAME tailscale COMMAND beam-tailscale-tests)
            set_tests_properties(tailscale PROPERTIES TIMEOUT 15)
            add_test(NAME tailscale-cli COMMAND "${BASH_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/tailscale_cli.sh" "$<TARGET_FILE:beam>")
            set_tests_properties(tailscale-cli PROPERTIES TIMEOUT 15)
        endif()
        add_test(NAME content-localhost COMMAND "${BASH_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/content_integration.sh"
            "$<TARGET_FILE:beam>" "$<TARGET_FILE:beam-content-tests>")
        set_tests_properties(content-localhost PROPERTIES TIMEOUT 90)
        add_test(NAME pairing-localhost COMMAND "${BASH_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/pairing_integration.sh" "$<TARGET_FILE:beam>" "${OPENSSL_EXECUTABLE}")
        set_tests_properties(pairing-localhost PROPERTIES TIMEOUT 45)
        add_test(NAME quic-localhost COMMAND "${BASH_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.sh" "$<TARGET_FILE:beam>"
            "${OPENSSL_EXECUTABLE}" "$<TARGET_FILE:beam-transfer-tests>" "$<TARGET_FILE:beam-phase2-tests>")
        set_tests_properties(quic-localhost PROPERTIES TIMEOUT 120)
    endif()
    if(BEAM_ENABLE_DISCOVERY)
        if(BEAM_ENABLE_QUIC)
            add_custom_target(discovery-test
                COMMAND "$<TARGET_FILE:beam-discovery-live-tests>"
                COMMAND "${BASH_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.sh"
                    "$<TARGET_FILE:beam>" "${OPENSSL_EXECUTABLE}"
                    "$<TARGET_FILE:beam-transfer-tests>" "$<TARGET_FILE:beam-phase2-tests>" discovery
                DEPENDS beam-discovery-live-tests beam beam-transfer-tests beam-phase2-tests
                USES_TERMINAL)
        else()
            add_custom_target(discovery-test COMMAND "$<TARGET_FILE:beam-discovery-live-tests>"
                DEPENDS beam-discovery-live-tests USES_TERMINAL)
        endif()
    endif()
endif()

include(cmake/Install.cmake)
