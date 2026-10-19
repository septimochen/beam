cmake_minimum_required(VERSION 3.25)
find_program(make NAMES gmake make REQUIRED)
file(REMOVE_RECURSE "${TEST_DIR}")
set(headers "${BEAM_SOURCE_DIR}/third_party/msquic/include")
set(fixture "${TEST_DIR}/fixture")
file(MAKE_DIRECTORY "${fixture}")
file(WRITE "${fixture}/library.cpp" [=[
#include <msquic.h>
#include <cstring>
static QUIC_STATUS QUIC_API get_param(HQUIC, uint32_t param, uint32_t* size, void* buffer) {
    if (param != QUIC_PARAM_GLOBAL_LIBRARY_VERSION || *size != sizeof(uint32_t) * 4)
        return QUIC_STATUS_INVALID_PARAMETER;
    const uint32_t version[4] = {MAJOR, MINOR, PATCH, 0};
    std::memcpy(buffer, version, sizeof(version));
    return QUIC_STATUS_SUCCESS;
}
QUIC_STATUS QUIC_API MsQuicOpenVersion(uint32_t version, const void** api) {
    if (version != 2 || FAIL_OPEN) return QUIC_STATUS_NOT_SUPPORTED;
    static const QUIC_API_TABLE table = [] { QUIC_API_TABLE t{}; t.GetParam = get_param; return t; }();
    *api = &table;
    return QUIC_STATUS_SUCCESS;
}
void QUIC_API MsQuicClose(const void*) {}
]=])
file(WRITE "${fixture}/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.25)
project(MsQuicFixtures LANGUAGES CXX)
foreach(kind sdk runtime old future broken)
    add_library(${kind} SHARED library.cpp)
    target_include_directories(${kind} SYSTEM PRIVATE "${HEADERS}")
    target_compile_features(${kind} PRIVATE cxx_std_17)
    set(major 2)
    set(patch 2)
    set(fail_open 0)
    if(kind STREQUAL old)
        set(patch 1)
    elseif(kind STREQUAL future)
        set(major 3)
    elseif(kind STREQUAL broken)
        set(fail_open 1)
    endif()
    target_compile_definitions(${kind} PRIVATE MAJOR=${major} MINOR=6 PATCH=${patch} FAIL_OPEN=${fail_open})
    set_target_properties(${kind} PROPERTIES OUTPUT_NAME msquic VERSION 2.6.2 SOVERSION 2
        LIBRARY_OUTPUT_DIRECTORY "${OUTPUT}/${kind}/lib64")
endforeach()
]=])
execute_process(COMMAND "${CMAKE_COMMAND}" -G "Unix Makefiles" -S "${fixture}" -B "${fixture}/build"
    "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}" "-DCMAKE_MAKE_PROGRAM=${make}"
    "-DHEADERS=${headers}" "-DOUTPUT=${TEST_DIR}/prefixes with spaces"
    OUTPUT_QUIET COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${fixture}/build" --parallel 4
    OUTPUT_QUIET COMMAND_ERROR_IS_FATAL ANY)
set(prefixes "${TEST_DIR}/prefixes with spaces")
file(COPY "${headers}/" DESTINATION "${prefixes}/sdk/include")
if(APPLE)
    set(library libmsquic.dylib)
else()
    set(library libmsquic.so.2)
    # Emulate Microsoft's runtime-only DEB/RPM: no unversioned linker symlink.
    file(REMOVE "${prefixes}/runtime/lib64/libmsquic.so")
endif()

function(configure name success expected)
    execute_process(COMMAND "${CMAKE_COMMAND}" -G "Unix Makefiles"
        -S "${BEAM_SOURCE_DIR}/cmake/msquic-check" -B "${TEST_DIR}/check-${name}"
        "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}" "-DCMAKE_MAKE_PROGRAM=${make}" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if((success AND NOT status EQUAL 0) OR (NOT success AND status EQUAL 0) OR
       NOT "${output}${error}" MATCHES "${expected}")
        message(FATAL_ERROR "Unexpected ${name} result (${status}):\n${output}${error}")
    endif()
endfunction()

configure(sdk TRUE "installed SDK" "-DMSQUIC_ROOT=${prefixes}/sdk")
configure(runtime TRUE "vendored v2.6.2" "-DMSQUIC_ROOT=${prefixes}/runtime")
configure(override TRUE "2.6.2"
    "-DMSQUIC_LIBRARY=${prefixes}/sdk/lib64/${library}" "-DMSQUIC_INCLUDE_DIR=${headers}")
configure(missing FALSE "runtime library was not found.*brew install libmsquic"
    "-DMSQUIC_ROOT=${TEST_DIR}/absent")
configure(headers FALSE "Incomplete MsQuic headers"
    "-DMSQUIC_ROOT=${prefixes}/sdk" "-DMSQUIC_INCLUDE_DIR=${TEST_DIR}/absent")
configure(old FALSE "runtime validation failed.*2.6.1" "-DMSQUIC_ROOT=${prefixes}/old")
configure(future FALSE "runtime validation failed.*3.6.2" "-DMSQUIC_ROOT=${prefixes}/future")
configure(broken FALSE "MsQuicOpen2 failed" "-DMSQUIC_ROOT=${prefixes}/broken")
# Changing an explicit library in an existing CMake tree must rerun the version probe.
configure(sdk FALSE "runtime validation failed.*2.6.1"
    "-DMSQUIC_LIBRARY=${prefixes}/old/lib64/${library}")
file(REMOVE_RECURSE "${TEST_DIR}")
