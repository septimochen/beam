cmake_minimum_required(VERSION 3.25)
file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}")
set(adb "${TEST_DIR}/fake adb")
set(apk "${TEST_DIR}/Beam test.apk")
file(WRITE "${apk}" "fixture")
file(WRITE "${adb}" [=[#!/bin/sh
if [ "$1" = devices ]; then
    printf 'List of devices attached\n%b\n' "$ADB_TEST_DEVICES"
    exit 0
fi
printf '%s\n' "$@" > "$ADB_TEST_LOG"
exit "${ADB_TEST_EXIT:-0}"
]=])
file(CHMOD "${adb}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
function(check_case devices serial expected_success expected_serial)
    file(REMOVE "${TEST_DIR}/install.log")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "ADB_TEST_DEVICES=${devices}" "ADB_TEST_LOG=${TEST_DIR}/install.log"
        "${CMAKE_COMMAND}" "-DADB=${adb}" "-DANDROID_APK=${apk}" "-DANDROID_SERIAL=${serial}"
        -P "${BEAM_SOURCE_DIR}/cmake/InstallAndroid.cmake"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(expected_success)
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "Install unexpectedly failed: ${output}${error}")
        endif()
        file(READ "${TEST_DIR}/install.log" arguments)
        if(NOT arguments STREQUAL "-s\n${expected_serial}\ninstall\n-r\n${apk}\n")
            message(FATAL_ERROR "Incorrect adb arguments: ${arguments}")
        endif()
    elseif(result EQUAL 0 OR EXISTS "${TEST_DIR}/install.log")
        message(FATAL_ERROR "Unsafe device selection installed an APK: ${devices}")
    endif()
endfunction()
check_case("" "" FALSE "")
check_case("phone\tunauthorized" "" FALSE "")
check_case("phone\toffline" "phone" FALSE "")
check_case("phone\tdevice" "" TRUE phone)
check_case("phone\tdevice\ntablet\tdevice" "" FALSE "")
check_case("phone\tdevice\ntablet\tdevice" "tablet" TRUE tablet)
check_case("phone\tdevice" "absent" FALSE "")
set(ENV{ADB_TEST_EXIT} 1)
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "ADB_TEST_DEVICES=phone\tdevice"
    "ADB_TEST_LOG=${TEST_DIR}/install.log"
    "${CMAKE_COMMAND}" "-DADB=${adb}" "-DANDROID_APK=${apk}" -DANDROID_SERIAL=phone
    -P "${BEAM_SOURCE_DIR}/cmake/InstallAndroid.cmake" RESULT_VARIABLE result
    OUTPUT_QUIET ERROR_QUIET)
if(result EQUAL 0)
    message(FATAL_ERROR "adb install failure was ignored")
endif()
file(REMOVE_RECURSE "${TEST_DIR}")
