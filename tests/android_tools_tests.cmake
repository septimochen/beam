cmake_minimum_required(VERSION 3.25)
file(REMOVE_RECURSE "${TEST_DIR}")
set(sdk "${TEST_DIR}/SDK with spaces")
function(package folder marker revision)
    get_filename_component(parent "${sdk}/${folder}/${marker}" DIRECTORY)
    file(MAKE_DIRECTORY "${parent}")
    file(WRITE "${sdk}/${folder}/${marker}" "fixture")
    file(WRITE "${sdk}/${folder}/source.properties" "Pkg.Revision = ${revision}\n")
endfunction()
package(platforms/android-37.0 android.jar 2)
package(platforms/android-38.1 android.jar 1)
package(platforms/android-39 android.jar 1)
file(APPEND "${sdk}/platforms/android-39/source.properties" "AndroidVersion.CodeName=Preview\n")
package(build-tools/36.9.0 aapt2 36.9.0)
package(build-tools/36.10.0 aapt2 36.10.0)
package(build-tools/37.0.0-rc1 aapt2 37.0.0-rc1)
package(ndk/28.2.100 build/cmake/android.toolchain.cmake 28.2.100)
package(ndk/29.0.200 build/cmake/android.toolchain.cmake 29.0.200)
package(ndk/30.0.300 build/cmake/android.toolchain.cmake 30.0.300-beta1)
package(cmake/3.31.6 bin/cmake 3.31.6)
package(cmake/4.1.2 bin/cmake 4.1.2)
# An incomplete newer install must not displace a usable package.
file(MAKE_DIRECTORY "${sdk}/cmake/5.0.0")
set(command "${CMAKE_COMMAND}" -E env --unset=ANDROID_HOME --unset=ANDROID_SDK_ROOT
    --unset=ANDROID_ABI --unset=ANDROID_NDK --unset=ANDROID_PLATFORM_VERSION
    --unset=ANDROID_BUILD_TOOLS_VERSION --unset=ANDROID_NDK_VERSION
    --unset=ANDROID_CMAKE_VERSION --unset=SDKMANAGER --unset=ANDROID_SETUP_DRY_RUN
    "${CMAKE_COMMAND}" "-DANDROID_HOME=${sdk}")
function(check_tools expected)
    execute_process(COMMAND ${command} -DBEAM_ANDROID_ACTION=properties ${ARGN}
        -P "${BEAM_SOURCE_DIR}/cmake/AndroidTools.cmake"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0 OR NOT output MATCHES "${expected}")
        message(FATAL_ERROR "Unexpected tools: ${output}${error}; expected ${expected}")
    endif()
endfunction()
check_tools("platform=38.1\nbuildTools=36.10.0\nndk=${sdk}/ndk/29.0.200\nndkVersion=29.0.200\ncmake=4.1.2")
check_tools("platform=37.0\nbuildTools=36.9.0\nndk=${sdk}/ndk/28.2.100\nndkVersion=28.2.100\ncmake=3.31.6"
    -DANDROID_PLATFORM_VERSION=37.0 -DANDROID_BUILD_TOOLS_VERSION=36.9.0
    -DANDROID_NDK_VERSION=28.2.100 -DANDROID_CMAKE_VERSION=3.31.6)
check_tools("ndkVersion=28.2.100" "-DANDROID_NDK=${sdk}/ndk/28.2.100")
function(reject expected)
    execute_process(COMMAND ${command} ${ARGN} -P "${BEAM_SOURCE_DIR}/cmake/AndroidTools.cmake"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(result EQUAL 0 OR NOT "${output}${error}" MATCHES "${expected}")
        message(FATAL_ERROR "Invalid selection did not fail as expected: ${output}${error}")
    endif()
endfunction()
reject("No usable stable cmake" -DANDROID_CMAKE_VERSION=5.0.0)
reject("No usable stable ndk" -DANDROID_NDK_VERSION=30.0.300)
reject("select different versions" "-DANDROID_NDK=${sdk}/ndk/28.2.100" -DANDROID_NDK_VERSION=29.0.200)
reject("supports only arm64-v8a" -DANDROID_ABI=x86_64)

# Android Studio's SDK setting must resolve identically without shell exports.
set(project "${TEST_DIR}/Studio project")
file(MAKE_DIRECTORY "${project}/cmake" "${project}/android")
file(COPY "${BEAM_SOURCE_DIR}/cmake/AndroidTools.cmake" DESTINATION "${project}/cmake")
string(REPLACE " " "\\ " escaped_sdk "${sdk}")
file(WRITE "${project}/android/local.properties" "sdk.dir=${escaped_sdk}\n")
set(studio_command ${command})
list(POP_BACK studio_command)
execute_process(COMMAND ${studio_command} -DBEAM_ANDROID_ACTION=properties
    -P "${project}/cmake/AndroidTools.cmake"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR NOT output MATCHES "sdk=${sdk}\nplatform=38.1")
    message(FATAL_ERROR "Android Studio SDK selection failed: ${output}${error}")
endif()

# Fake the catalog, then run the real installation path without network access.
set(manager "${TEST_DIR}/fake sdkmanager")
file(WRITE "${manager}" [=[#!/bin/sh
case "$*" in
  *--list*) cat <<'CATALOG'
Installed packages:
  platforms;android-37.0 | 2 | SDK Platform
  ndk;28.2.100 | 28.2.100 | NDK
Available Packages:
  platforms;android-38.1 | 1 | SDK Platform
  platforms;android-Preview | 1 | Preview
  build-tools;36.9.0 | 36.9.0 | Build Tools
  build-tools;36.10.0 | 36.10.0 | Build Tools
  build-tools;37.0.0-rc1 | 37.0.0 rc1 | Preview
  ndk;29.0.200 | 29.0.200 | NDK
  ndk;30.0.300 | 30.0.300 beta1 | Preview
  cmake;4.1.2 | 4.1.2 | CMake
CATALOG
  ;;
  *) for argument do
    case "$argument" in --package_file=*) cp "${argument#--package_file=}" "$ANDROID_TOOLS_TEST_LOG";; esac
  done ;;
esac
]=])
file(CHMOD "${manager}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
set(ENV{ANDROID_TOOLS_TEST_LOG} "${TEST_DIR}/installed.txt")
execute_process(COMMAND ${command} -DBEAM_ANDROID_ACTION=setup "-DSDKMANAGER=${manager}"
    -P "${BEAM_SOURCE_DIR}/cmake/AndroidTools.cmake" COMMAND_ERROR_IS_FATAL ANY)
file(READ "${TEST_DIR}/installed.txt" installed)
if(NOT installed STREQUAL "platforms;android-38.1\nbuild-tools;36.10.0\nndk;29.0.200\ncmake;4.1.2\nplatform-tools\n")
    message(FATAL_ERROR "Installer selected incorrect packages: ${installed}")
endif()
file(REMOVE "${TEST_DIR}/installed.txt")
execute_process(COMMAND ${command} -DBEAM_ANDROID_ACTION=setup "-DSDKMANAGER=${manager}"
    -DANDROID_SETUP_DRY_RUN=ON -DANDROID_PLATFORM_VERSION=37.0
    -P "${BEAM_SOURCE_DIR}/cmake/AndroidTools.cmake" COMMAND_ERROR_IS_FATAL ANY)
if(EXISTS "${TEST_DIR}/installed.txt")
    message(FATAL_ERROR "Dry run installed packages")
endif()
reject("not an available stable package" -DBEAM_ANDROID_ACTION=setup
    "-DSDKMANAGER=${manager}" -DANDROID_NDK_VERSION=30.0.300)
