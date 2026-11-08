cmake_minimum_required(VERSION 3.25)
include("${BEAM_SOURCE_DIR}/cmake/GradleLatest.cmake")
file(REMOVE_RECURSE "${TEST_DIR}")
file(MAKE_DIRECTORY "${TEST_DIR}")
set(output "${TEST_DIR}/wrapper with spaces/gradle-wrapper.properties")
set(checksum "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")
set(metadata "{\"version\":\"99.2.3\",\"checksum\":\"${checksum}\",\"current\":true,\"snapshot\":false,\"nightly\":false,\"broken\":false}")
beam_gradle_properties("${metadata}" "${output}")
file(READ "${output}" properties)
foreach(expected IN ITEMS
        "distributionUrl=https\\://downloads.gradle.org/distributions/gradle-99.2.3-bin.zip"
        "distributionSha256Sum=${checksum}" "networkTimeout=30000" "retries=3"
        "validateDistributionUrl=true")
    string(FIND "${properties}" "${expected}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Missing wrapper setting: ${expected}")
    endif()
endforeach()

# A newer stable release changes both version and checksum without source edits.
string(JSON newer SET "${metadata}" version "\"100.0.0\"")
string(REPEAT a 64 newer_checksum)
string(JSON newer SET "${newer}" checksum "\"${newer_checksum}\"")
beam_gradle_properties("${newer}" "${output}")
file(READ "${output}" properties)
if(NOT properties MATCHES "gradle-100\\.0\\.0-bin\\.zip"
        OR NOT properties MATCHES "distributionSha256Sum=${newer_checksum}")
    message(FATAL_ERROR "Wrapper did not follow latest release")
endif()

function(reject invalid expected)
    file(WRITE "${TEST_DIR}/invalid.json" "${invalid}")
    file(WRITE "${TEST_DIR}/reject.cmake"
        "include(\"${BEAM_SOURCE_DIR}/cmake/GradleLatest.cmake\")\n"
        "file(READ \"${TEST_DIR}/invalid.json\" metadata)\n"
        "beam_gradle_properties(\"\${metadata}\" \"${output}\")\n")
    execute_process(COMMAND "${CMAKE_COMMAND}" -P "${TEST_DIR}/reject.cmake"
        RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(result EQUAL 0 OR NOT "${stdout}${stderr}" MATCHES "${expected}")
        message(FATAL_ERROR "Invalid release accepted or wrong error: ${stdout}${stderr}")
    endif()
    file(READ "${output}" after)
    if(NOT after STREQUAL properties OR EXISTS "${output}.tmp")
        message(FATAL_ERROR "Invalid metadata replaced verified wrapper properties")
    endif()
endfunction()
reject("not JSON" "Invalid Gradle release metadata")
foreach(field IN ITEMS version checksum current snapshot nightly broken)
    string(JSON invalid REMOVE "${metadata}" "${field}")
    reject("${invalid}" "Invalid Gradle release metadata")
endforeach()
foreach(field IN ITEMS current snapshot nightly broken)
    if(field STREQUAL "current")
        set(value false)
    else()
        set(value true)
    endif()
    string(JSON invalid SET "${metadata}" "${field}" "${value}")
    reject("${invalid}" "current stable release")
    string(JSON invalid SET "${metadata}" "${field}" "\"false\"")
    reject("${invalid}" "Invalid Gradle release metadata type")
endforeach()
foreach(version IN ITEMS "99.2.3-rc-1" "99.2.3-SNAPSHOT" "../../escape" "99.2.3\ninjected=true")
    string(JSON invalid SET "${metadata}" version "\"${version}\"")
    reject("${invalid}" "current stable release")
endforeach()
foreach(hash IN ITEMS "" "abc" "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdeg")
    string(JSON invalid SET "${metadata}" checksum "\"${hash}\"")
    reject("${invalid}" "Invalid Gradle distribution SHA-256")
endforeach()

# Direct wrapper calls must resolve first and never launch Java after a failed lookup,
# even when old properties exist. Exercise the launcher without downloading Gradle.
set(project "${TEST_DIR}/project with spaces")
set(bin "${TEST_DIR}/fake tools")
file(MAKE_DIRECTORY "${project}/android/gradle/wrapper" "${bin}/bin")
file(COPY "${BEAM_SOURCE_DIR}/android/gradlew" DESTINATION "${project}/android")
file(WRITE "${project}/android/gradle/wrapper/gradle-wrapper.properties"
    "distributionUrl=https\\://downloads.gradle.org/distributions/gradle-1.0.0-bin.zip\n")
file(WRITE "${bin}/cmake" [=[#!/bin/sh
printf '%s\n' "$@" > "$BEAM_GRADLE_TEST_LOG.cmake"
exit "$BEAM_GRADLE_TEST_STATUS"
]=])
file(WRITE "${bin}/bin/java" [=[#!/bin/sh
printf '%s\n' "$@" > "$BEAM_GRADLE_TEST_LOG.java"
]=])
file(CHMOD "${bin}/cmake" "${bin}/bin/java"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
foreach(status IN ITEMS 0 23)
    file(REMOVE "${TEST_DIR}/launcher.java")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=${bin}:$ENV{PATH}" "JAVA_HOME=${bin}" "JAVA_OPTS=" "GRADLE_OPTS="
        "BEAM_GRADLE_TEST_LOG=${TEST_DIR}/launcher" "BEAM_GRADLE_TEST_STATUS=${status}"
        sh "${project}/android/gradlew" -p "${project}/android" --version
        RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    file(READ "${TEST_DIR}/launcher.cmake" arguments)
    if(NOT arguments STREQUAL "-P\n${project}/android/../cmake/GradleLatest.cmake\n")
        message(FATAL_ERROR "Wrapper did not resolve latest Gradle: ${arguments}")
    endif()
    if(status EQUAL 0)
        if(NOT result EQUAL 0 OR NOT EXISTS "${TEST_DIR}/launcher.java")
            message(FATAL_ERROR "Wrapper failed to launch: ${stdout}${stderr}")
        endif()
        file(READ "${TEST_DIR}/launcher.java" arguments)
        string(FIND "${arguments}" "-p\n${project}/android\n--version\n" found)
        if(found EQUAL -1)
            message(FATAL_ERROR "Wrapper lost arguments: ${arguments}")
        endif()
    elseif(result EQUAL 0 OR EXISTS "${TEST_DIR}/launcher.java")
        message(FATAL_ERROR "Wrapper launched after release lookup failed")
    endif()
endforeach()
