cmake_minimum_required(VERSION 3.25)
if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "Beam Android supports only arm64-v8a")
endif()
project(BeamAndroid LANGUAGES CXX)
if(NOT ANDROID)
    message(FATAL_ERROR "Use the Android NDK toolchain")
endif()
set(prefix "${BEAM_ANDROID_DEPS}/${ANDROID_ABI}/install")
foreach(required lib/libmsquic.a lib/libcrypto.a lib/libssl.a include/openssl/ssl.h)
    if(NOT EXISTS "${prefix}/${required}")
        message(FATAL_ERROR "Missing ${prefix}/${required}; run make android-deps ANDROID_ABI=${ANDROID_ABI} first")
    endif()
endforeach()
set(MSQUIC_ROOT "${prefix}" CACHE PATH "" FORCE)
set(MSQUIC_INCLUDE_DIR "${prefix}/include" CACHE PATH "" FORCE)
set(MSQUIC_LIBRARY "${prefix}/lib/libmsquic.a" CACHE FILEPATH "" FORCE)
# These archives come from the same pinned quictls build as MsQuic. Do not use
# FindOpenSSL here: it can append host pkg-config dependencies even when its
# include/library paths explicitly name Android archives.
add_library(OpenSSL::Crypto STATIC IMPORTED GLOBAL)
set_target_properties(OpenSSL::Crypto PROPERTIES
    IMPORTED_LOCATION "${prefix}/lib/libcrypto.a"
    INTERFACE_INCLUDE_DIRECTORIES "${prefix}/include"
    INTERFACE_LINK_LIBRARIES "${CMAKE_DL_LIBS}")
add_library(OpenSSL::SSL STATIC IMPORTED GLOBAL)
set_target_properties(OpenSSL::SSL PROPERTIES
    IMPORTED_LOCATION "${prefix}/lib/libssl.a"
    INTERFACE_INCLUDE_DIRECTORIES "${prefix}/include"
    INTERFACE_LINK_LIBRARIES OpenSSL::Crypto)
set(BEAM_ENABLE_DISCOVERY OFF CACHE BOOL "" FORCE)
set(BEAM_ENABLE_TAILSCALE OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BEAM_PAIRING_ROOT "${BEAM_ANDROID_DEPS}/pairing/pairing-${ANDROID_ABI}/install" CACHE PATH "" FORCE)
# Android libc++ provides stop_token/jthread in its experimental library.
add_compile_options(-fexperimental-library)
add_link_options(-fexperimental-library)
add_subdirectory(../.. beam)
target_link_libraries(beam-android PRIVATE OpenSSL::SSL OpenSSL::Crypto log android)
