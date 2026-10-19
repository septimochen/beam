cmake_minimum_required(VERSION 3.25)
foreach(required BEAM_SOURCE_DIR TEST_DIR)
    if(NOT IS_ABSOLUTE "${${required}}")
        message(FATAL_ERROR "Pass an absolute ${required} for clean fixtures")
    endif()
endforeach()
find_program(make NAMES gmake make REQUIRED)
file(REMOVE_RECURSE "${TEST_DIR}")
set(project "${TEST_DIR}/source with spaces")
file(MAKE_DIRECTORY "${project}/cmake" "${project}/include" "${project}/src"
    "${project}/tests" "${project}/tools" "${project}/android/app/src")
file(COPY "${BEAM_SOURCE_DIR}/Makefile" DESTINATION "${project}")
file(COPY "${BEAM_SOURCE_DIR}/cmake/Clean.cmake" DESTINATION "${project}/cmake")
set(protected "${project}/src/main.cpp" "${project}/android/local.properties"
    "${project}/.git/keep" "${project}/received/file.txt" "${TEST_DIR}/outside/keep")
foreach(path IN LISTS protected)
    file(WRITE "${path}" "keep")
endforeach()

function(run_clean success)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=MAKEFLAGS
        "${make}" -C "${project}" clean "CMAKE=${CMAKE_COMMAND}" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if((success AND NOT status EQUAL 0) OR (NOT success AND status EQUAL 0))
        message(FATAL_ERROR "Unexpected clean result (${status}):\n${output}${error}")
    endif()
    foreach(path IN LISTS protected)
        file(READ "${path}" contents)
        if(NOT contents STREQUAL "keep")
            message(FATAL_ERROR "Clean changed source/user data: ${path}")
        endif()
    endforeach()
endfunction()

# No CMake cache, including toolchain/dependency folders under the build root.
set(outputs "${project}/build/deps/include/library.h" "${project}/build/android-tools/sdk/tool"
    "${project}/build/android-deps/library.a" "${project}/build-debug/object.o"
    "${project}/build-release/CMakeFiles/partial" "${project}/android/build/output"
    "${project}/android/app/build/output.apk" "${project}/android/app/.cxx/object.o")
foreach(path IN LISTS outputs)
    file(WRITE "${path}" "output")
endforeach()
file(CREATE_LINK "${TEST_DIR}/outside" "${project}/build-debug/linked" SYMBOLIC)
run_clean(TRUE)
foreach(path build build-debug build-release android/build android/app/build android/app/.cxx)
    if(EXISTS "${project}/${path}")
        message(FATAL_ERROR "Clean retained build output: ${path}")
    endif()
endforeach()
run_clean(TRUE)

# Validate all paths before removing any output, including normalized traversal.
file(WRITE "${project}/build/sentinel" "keep until a valid cleanup")
foreach(path "." ".." "src" "build/../src" "${project}/.git" "${TEST_DIR}/outside")
    run_clean(FALSE "BUILD_DIR=${path}")
    if(NOT EXISTS "${project}/build/sentinel")
        message(FATAL_ERROR "Invalid cleanup deleted output before validating every root")
    endif()
endforeach()
run_clean(FALSE "BUILD_DIR=")
# Pass the semicolon as one Make argument instead of expanding it as a CMake list.
execute_process(COMMAND "${CMAKE_COMMAND}" -E env --unset=MAKEFLAGS
    "${make}" -C "${project}" clean "CMAKE=${CMAKE_COMMAND}"
    "BUILD_DIR=build;other" RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
if(status EQUAL 0 OR NOT EXISTS "${project}/build/sentinel")
    message(FATAL_ERROR "Malformed cleanup path was accepted or removed build output")
endif()
file(WRITE "${TEST_DIR}/outside/CMakeCache.txt"
    "CMAKE_HOME_DIRECTORY:INTERNAL=/another/project\nCMAKE_PROJECT_NAME:STATIC=Beam\n")
run_clean(FALSE "BUILD_DIR=${TEST_DIR}/outside")

# Explicit custom builds are supported, including outside the checkout.
set(custom "${TEST_DIR}/custom build")
file(WRITE "${custom}/CMakeCache.txt"
    "CMAKE_HOME_DIRECTORY:INTERNAL=${project}\nCMAKE_PROJECT_NAME:STATIC=Beam\n")
run_clean(TRUE "BUILD_DIR=${custom}")
if(EXISTS "${custom}" OR EXISTS "${project}/build")
    message(FATAL_ERROR "Custom build was not removed")
endif()

# Remove directory links themselves, never the linked directory or its contents.
file(CREATE_LINK "${TEST_DIR}/outside" "${project}/build" SYMBOLIC)
file(CREATE_LINK "${TEST_DIR}/outside" "${project}/build-linked" SYMBOLIC)
run_clean(TRUE)
if(IS_SYMLINK "${project}/build" OR IS_SYMLINK "${project}/build-linked")
    message(FATAL_ERROR "Clean retained build symlinks")
endif()
file(CREATE_LINK "${TEST_DIR}/outside" "${project}/linked-parent" SYMBOLIC)
run_clean(FALSE "BUILD_DIR=${project}/linked-parent/nested")
file(REMOVE_RECURSE "${TEST_DIR}")
