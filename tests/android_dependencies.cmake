# Exercise real Android configuration with deliberately incompatible host metadata.
cmake_minimum_required(VERSION 3.25)
foreach(required BEAM_SOURCE_DIR BEAM_ANDROID_DEPS ANDROID_NDK)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Pass -D${required}=<absolute path>")
    endif()
endforeach()
find_program(PKG_CONFIG_EXECUTABLE NAMES pkg-config pkgconf REQUIRED)
find_program(NINJA_EXECUTABLE ninja REQUIRED)
set(output "${BEAM_ANDROID_DEPS}/dependency-test")
set(metadata "${output}/host-pkgconfig")
file(MAKE_DIRECTORY "${metadata}")
file(WRITE "${metadata}/openssl.pc" "Name: Host OpenSSL\nDescription: Incompatible host dependency fixture\nVersion: 3.6.0\nLibs: -lssl -lcrypto\nLibs.private: -l:libjitterentropy.a -lzstd\n")
set(environment "${CMAKE_COMMAND}" -E env --unset=PKG_CONFIG_SYSROOT_DIR
    "PKG_CONFIG_LIBDIR=${metadata}" "PKG_CONFIG_PATH=${metadata}")
execute_process(COMMAND ${environment} "${PKG_CONFIG_EXECUTABLE}" --static --libs openssl
    RESULT_VARIABLE status OUTPUT_VARIABLE libraries ERROR_VARIABLE error)
if(NOT status STREQUAL "0" OR NOT libraries MATCHES "jitterentropy" OR NOT libraries MATCHES "zstd")
    message(FATAL_ERROR "Host metadata fixture is not active: ${libraries}${error}")
endif()
foreach(abi arm64-v8a)
    set(build "${output}/${abi}")
    execute_process(COMMAND ${environment} "${CMAKE_COMMAND}" --fresh
        -G Ninja -S "${BEAM_SOURCE_DIR}/android/native" -B "${build}"
        "-DCMAKE_MAKE_PROGRAM=${NINJA_EXECUTABLE}"
        "-DPKG_CONFIG_EXECUTABLE=${PKG_CONFIG_EXECUTABLE}"
        "-DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK}/build/cmake/android.toolchain.cmake"
        "-DBEAM_ANDROID_DEPS=${BEAM_ANDROID_DEPS}" "-DANDROID_ABI=${abi}"
        -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=Debug
        COMMAND_ERROR_IS_FATAL ANY)
    file(READ "${build}/build.ninja" graph)
    if(graph MATCHES "jitterentropy|zstd")
        message(FATAL_ERROR "${abi} Android linkage inherited host OpenSSL dependencies")
    endif()
    message(STATUS "${abi}: Android link graph excludes incompatible host dependencies")
endforeach()
