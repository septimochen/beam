# Explicit, pinned dependency provisioning; regular configure never downloads code.
cmake_minimum_required(VERSION 3.25)
if(NOT DEFINED BEAM_DEPS_DIR)
    message(FATAL_ERROR "Pass -DBEAM_DEPS_DIR=<absolute build/deps path>")
endif()
find_program(GIT_EXECUTABLE git REQUIRED)
set(source "${BEAM_DEPS_DIR}/msquic")
set(pinned_commit "819ab74f851ee168504cbc392ec32e7bed1d82e9") # v2.6.2
function(run)
    execute_process(COMMAND ${ARGV} COMMAND_ERROR_IS_FATAL ANY)
endfunction()
if(NOT EXISTS "${source}/.git")
    run("${GIT_EXECUTABLE}" clone --depth 1 --branch v2.6.2
        https://github.com/microsoft/msquic.git "${source}")
endif()
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source}" rev-parse HEAD
    OUTPUT_VARIABLE actual_commit OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT actual_commit STREQUAL pinned_commit)
    message(FATAL_ERROR "MsQuic checkout must be pinned to ${pinned_commit}; found ${actual_commit}")
endif()
# Only initialize the TLS submodule we build, without its unrelated test submodules.
run("${GIT_EXECUTABLE}" -C "${source}" submodule update --init --depth 1 submodules/quictls)
run("${CMAKE_COMMAND}" -S "${source}" -B "${BEAM_DEPS_DIR}/msquic-build"
    -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=${BEAM_DEPS_DIR}/install"
    -UQUIC_OPENSSL_* -DQUIC_TLS_LIB=quictls -DQUIC_USE_EXTERNAL_OPENSSL=OFF
    -DQUIC_BUILD_TOOLS=OFF -DQUIC_BUILD_TEST=OFF -DQUIC_BUILD_PERF=OFF
    -DQUIC_ENABLE_LOGGING=OFF -DQUIC_USE_SYSTEM_LIBCRYPTO=OFF)
run("${CMAKE_COMMAND}" --build "${BEAM_DEPS_DIR}/msquic-build" --parallel 4)
run("${CMAKE_COMMAND}" --install "${BEAM_DEPS_DIR}/msquic-build")
