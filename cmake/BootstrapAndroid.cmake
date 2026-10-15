# Explicit dependency provisioning, separate from Gradle/CMake configuration.
cmake_minimum_required(VERSION 3.25)
if(NOT ANDROID_NDK OR NOT EXISTS "${ANDROID_NDK}/build/cmake/android.toolchain.cmake")
    message(FATAL_ERROR "Set ANDROID_NDK to an installed NDK 28.2.13676358 directory")
endif()
if(NOT ANDROID_ABI MATCHES "^(arm64-v8a|x86_64)$")
    message(FATAL_ERROR "ANDROID_ABI must be arm64-v8a or x86_64")
endif()
if(NOT BEAM_ANDROID_DEPS)
    message(FATAL_ERROR "Pass BEAM_ANDROID_DEPS (absolute output directory)")
endif()
find_program(GIT_EXECUTABLE git REQUIRED)
find_program(NINJA_EXECUTABLE ninja REQUIRED)
set(source "${BEAM_ANDROID_DEPS}/msquic")
set(output "${BEAM_ANDROID_DEPS}/${ANDROID_ABI}")
function(run)
    execute_process(COMMAND ${ARGV} COMMAND_ERROR_IS_FATAL ANY)
endfunction()
if(NOT EXISTS "${source}/.git")
    run("${GIT_EXECUTABLE}" clone --depth 1 --branch v2.6.2 https://github.com/microsoft/msquic.git "${source}")
endif()
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${source}" rev-parse HEAD
    OUTPUT_VARIABLE actual OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
if(NOT actual STREQUAL "819ab74f851ee168504cbc392ec32e7bed1d82e9")
    message(FATAL_ERROR "Android MsQuic must be pinned to v2.6.2")
endif()
run("${GIT_EXECUTABLE}" -C "${source}" submodule update --init --depth 1 submodules/quictls)
file(GLOB host_tools "${ANDROID_NDK}/toolchains/llvm/prebuilt/*/bin")
list(GET host_tools 0 tool_bin)
# Upstream quictls invokes the NDK tools by name, separately from CMake.
set(ENV{ANDROID_NDK_ROOT} "${ANDROID_NDK}")
set(ENV{PATH} "${tool_bin}:$ENV{PATH}")
run("${CMAKE_COMMAND}" -G Ninja -S "${source}" -B "${output}/build"
    "-DCMAKE_MAKE_PROGRAM=${NINJA_EXECUTABLE}"
    "-DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK}/build/cmake/android.toolchain.cmake"
    "-DANDROID_ABI=${ANDROID_ABI}" -DANDROID_PLATFORM=android-29
    -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=${output}/install"
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON
    -DQUIC_BUILD_SHARED=OFF -DQUIC_TLS_LIB=quictls
    -DQUIC_BUILD_TOOLS=OFF -DQUIC_BUILD_TEST=OFF -DQUIC_BUILD_PERF=OFF
    -DQUIC_ENABLE_LOGGING=OFF -DQUIC_USE_SYSTEM_LIBCRYPTO=OFF)
run("${CMAKE_COMMAND}" --build "${output}/build" --parallel 4)
run("${CMAKE_COMMAND}" --install "${output}/build")
# Reuse the same pinned TLS implementation for Beam's EVP/X.509 operations.
set(crypto "${output}/build/_deps/opensslquic-build/quictls")
file(COPY "${crypto}/include/" DESTINATION "${output}/install/include")
file(COPY "${crypto}/lib/libssl.a" "${crypto}/lib/libcrypto.a" DESTINATION "${output}/install/lib")
