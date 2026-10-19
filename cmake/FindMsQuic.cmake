# Shared, offline host discovery for Beam and make deps.
if(ANDROID)
    # android/native selects exact per-ABI archives and headers before including Beam.
    return()
endif()

function(beam_msquic_error reason)
    message(FATAL_ERROR "${reason}\n"
        "Beam requires an OpenSSL-backed MsQuic 2.x runtime, version 2.6.2 or newer.\n"
        "macOS: brew install libmsquic\n"
        "Debian/Ubuntu: sudo apt install libmsquic\n"
        "Fedora/RHEL: sudo dnf install libmsquic\n"
        "APT/DNF may need Microsoft's repository: https://learn.microsoft.com/en-us/linux/packages\n"
        "Arch: install an updated msquic AUR package (2.6.2+); older packages are unsupported.\n"
        "For a custom installation, set MSQUIC_ROOT or MSQUIC_INCLUDE_DIR/MSQUIC_LIBRARY.\n"
        "Source fallback: make deps-source, then explicitly select build/deps/install with MSQUIC_ROOT.\n"
        "Use a fresh build directory when changing dependency sources.")
endfunction()

set(MSQUIC_ROOT "$ENV{MSQUIC_ROOT}" CACHE PATH "Optional installed MsQuic prefix; defaults to system packages")
set(msquic_prefixes)
set(msquic_search_options)
if(MSQUIC_ROOT)
    list(APPEND msquic_prefixes "${MSQUIC_ROOT}")
    # An explicit prefix must not silently resolve to another runtime.
    set(msquic_search_options NO_DEFAULT_PATH)
elseif(APPLE)
    find_program(msquic_brew brew)
    if(msquic_brew)
        execute_process(COMMAND "${msquic_brew}" --prefix libmsquic
            RESULT_VARIABLE brew_result OUTPUT_VARIABLE brew_prefix
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(brew_result EQUAL 0)
            list(APPEND msquic_prefixes "${brew_prefix}")
        endif()
    endif()
endif()
set(msquic_library_hints)
set(msquic_header_hints)
foreach(prefix IN LISTS msquic_prefixes)
    list(APPEND msquic_header_hints "${prefix}/include")
    list(APPEND msquic_library_hints "${prefix}/lib" "${prefix}/lib64")
    if(CMAKE_LIBRARY_ARCHITECTURE)
        list(APPEND msquic_library_hints "${prefix}/lib/${CMAKE_LIBRARY_ARCHITECTURE}")
    endif()
endforeach()
find_library(MSQUIC_LIBRARY NAMES msquic libmsquic.so.2
    HINTS ${msquic_library_hints} ${msquic_search_options})
if(NOT MSQUIC_LIBRARY OR NOT EXISTS "${MSQUIC_LIBRARY}")
    beam_msquic_error("MsQuic runtime library was not found: ${MSQUIC_LIBRARY}")
endif()
find_path(MSQUIC_INCLUDE_DIR msquic.h HINTS ${msquic_header_hints} ${msquic_search_options})
set(msquic_vendored_include "${CMAKE_CURRENT_LIST_DIR}/../third_party/msquic/include")
if(NOT MSQUIC_INCLUDE_DIR)
    set(MSQUIC_INCLUDE_DIR "${msquic_vendored_include}" CACHE PATH "MsQuic API headers" FORCE)
endif()
foreach(header msquic.h msquic_posix.h quic_sal_stub.h)
    if(NOT EXISTS "${MSQUIC_INCLUDE_DIR}/${header}")
        beam_msquic_error("Incomplete MsQuic headers: missing ${MSQUIC_INCLUDE_DIR}/${header}")
    endif()
endforeach()
file(REAL_PATH "${MSQUIC_INCLUDE_DIR}" msquic_header_path)
file(REAL_PATH "${msquic_vendored_include}" msquic_vendored_path)
if(msquic_header_path STREQUAL msquic_vendored_path)
    set(msquic_header_source "vendored v2.6.2 public API")
else()
    set(msquic_header_source "installed SDK")
endif()
message(STATUS "MsQuic library: ${MSQUIC_LIBRARY}")
message(STATUS "MsQuic headers: ${MSQUIC_INCLUDE_DIR} (${msquic_header_source})")
if(CMAKE_CROSSCOMPILING)
    beam_msquic_error("Native MsQuic validation cannot run with this host cross-compilation toolchain")
endif()
# Re-run on every configure, including package upgrades and changed cached paths.
try_run(msquic_run msquic_compiled
    SOURCES "${CMAKE_CURRENT_LIST_DIR}/msquic-check/probe.cpp"
    CMAKE_FLAGS "-DINCLUDE_DIRECTORIES:STRING=${MSQUIC_INCLUDE_DIR}"
    LINK_LIBRARIES "${MSQUIC_LIBRARY}"
    CXX_STANDARD 23 NO_CACHE
    COMPILE_OUTPUT_VARIABLE msquic_compile_output
    RUN_OUTPUT_VARIABLE msquic_version)
if(NOT msquic_compiled)
    beam_msquic_error("MsQuic API/link validation failed:\n${msquic_compile_output}")
endif()
string(STRIP "${msquic_version}" msquic_version)
if(NOT msquic_run STREQUAL "0")
    beam_msquic_error("MsQuic runtime validation failed (${msquic_run}): ${msquic_version}")
endif()
message(STATUS "MsQuic runtime version: ${msquic_version}")
