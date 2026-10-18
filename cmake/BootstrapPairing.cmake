# Explicit, checksum-pinned provisioning. Normal configuration never downloads.
cmake_minimum_required(VERSION 3.25)
if(NOT BEAM_DEPS_DIR)
    set(BEAM_DEPS_DIR "${CMAKE_CURRENT_LIST_DIR}/../build/deps")
endif()
set(archive "${BEAM_DEPS_DIR}/mbedtls-4.2.0.tar.bz2")
set(source "${BEAM_DEPS_DIR}/mbedtls-4.2.0")
file(MAKE_DIRECTORY "${BEAM_DEPS_DIR}")
if(NOT EXISTS "${archive}")
    file(DOWNLOAD "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-4.2.0/mbedtls-4.2.0.tar.bz2"
        "${archive}" EXPECTED_HASH SHA256=2bed9d713b4668f76553b097e72b8aa30bc8f112a940d7ae228d524bbde6ffea TLS_VERIFY ON)
endif()
file(SHA256 "${archive}" actual)
if(NOT actual STREQUAL "2bed9d713b4668f76553b097e72b8aa30bc8f112a940d7ae228d524bbde6ffea")
    message(FATAL_ERROR "Mbed TLS archive checksum mismatch")
endif()
if(NOT EXISTS "${source}/CMakeLists.txt")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${BEAM_DEPS_DIR}")
endif()
set(build "${BEAM_DEPS_DIR}/pairing-build")
set(extra)
if(ANDROID_ABI)
    set(build "${BEAM_DEPS_DIR}/pairing-${ANDROID_ABI}")
    list(APPEND extra "-DCMAKE_TOOLCHAIN_FILE=${ANDROID_NDK}/build/cmake/android.toolchain.cmake"
        "-DANDROID_ABI=${ANDROID_ABI}" -DANDROID_PLATFORM=android-29)
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${source}" -B "${build}"
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_INSTALL_PREFIX=${build}/install -DENABLE_PROGRAMS=OFF -DENABLE_TESTING=OFF
    -DMBEDTLS_USER_CONFIG_FILE=${CMAKE_CURRENT_LIST_DIR}/PairingMbedConfig.h ${extra}
    -DTF_PSA_CRYPTO_USER_CONFIG_FILE=${CMAKE_CURRENT_LIST_DIR}/PairingCryptoConfig.h
    COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${build}" --parallel 4 COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${build}" COMMAND_ERROR_IS_FATAL ANY)
