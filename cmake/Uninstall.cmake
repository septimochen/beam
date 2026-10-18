cmake_minimum_required(VERSION 3.25)

# Mirror make install's fixed layout without depending on its build directory.
# Never remove the shared prefix/bin/lib/share directories or user data.
set(roots INSTALL_PREFIX)
if(APPLE)
    list(APPEND roots APPLICATIONS_DIR)
else()
    if(NOT DEFINED DESKTOP_ENTRY_DIR OR DESKTOP_ENTRY_DIR STREQUAL "")
        message(FATAL_ERROR "Pass -DDESKTOP_ENTRY_DIR=<launcher directory>")
    endif()
    if(NOT IS_ABSOLUTE "${DESKTOP_ENTRY_DIR}")
        set(DESKTOP_ENTRY_DIR "${INSTALL_PREFIX}/${DESKTOP_ENTRY_DIR}")
    endif()
    list(APPEND roots DESKTOP_ENTRY_DIR)
endif()
foreach(root IN LISTS roots)
    if(NOT IS_ABSOLUTE "${${root}}" OR "${${root}}" MATCHES "[;\r\n]")
        message(FATAL_ERROR "${root} must be a nonempty absolute path without semicolons or newlines")
    endif()
endforeach()
set(staging "$ENV{DESTDIR}")
if(NOT staging STREQUAL "" AND (NOT IS_ABSOLUTE "${staging}" OR staging MATCHES "[;\r\n]"))
    message(FATAL_ERROR "DESTDIR must be an absolute staging path")
endif()

set(files "${staging}${INSTALL_PREFIX}/bin/beam")
set(directories "${staging}${INSTALL_PREFIX}/lib/beam" "${staging}${INSTALL_PREFIX}/share/licenses/beam")
if(APPLE)
    if(NOT DEFINED BEAM_MACOS_BUNDLE_ID OR BEAM_MACOS_BUNDLE_ID STREQUAL "")
        set(BEAM_MACOS_BUNDLE_ID org.beam.desktop)
    endif()
else()
    list(APPEND files "${staging}${INSTALL_PREFIX}/bin/beam-desktop"
        "${staging}${DESKTOP_ENTRY_DIR}/beam.desktop"
        "${staging}${INSTALL_PREFIX}/share/icons/hicolor/scalable/apps/org.beam.desktop.svg"
        "${staging}${INSTALL_PREFIX}/share/icons/hicolor/512x512/apps/org.beam.desktop.png")
endif()

# Validate every target before removing any files. A binary/launcher replaced by
# a directory may contain unrelated data; only the private runtime/app are trees.
foreach(path IN LISTS files)
    if(IS_DIRECTORY "${path}" AND NOT IS_SYMLINK "${path}")
        message(FATAL_ERROR "Refusing to remove unexpected directory: ${path}")
    endif()
endforeach()
if(APPLE)
    set(staging_args)
    if(NOT staging STREQUAL "")
        list(APPEND staging_args --staging)
    endif()
    execute_process(COMMAND "${CMAKE_CURRENT_LIST_DIR}/../scripts/uninstall-macos.sh"
        --applications-dir "${staging}${APPLICATIONS_DIR}"
        --bundle-id "${BEAM_MACOS_BUNDLE_ID}" ${staging_args}
        COMMAND_ERROR_IS_FATAL ANY)
endif()
foreach(path IN LISTS files directories)
    if(IS_SYMLINK "${path}" OR path IN_LIST files)
        file(REMOVE "${path}")
    else()
        file(REMOVE_RECURSE "${path}")
    endif()
    if(EXISTS "${path}" OR IS_SYMLINK "${path}")
        message(FATAL_ERROR "Could not remove ${path}; check installation permissions")
    endif()
endforeach()
message(STATUS "Uninstalled Beam. Settings, identities, paired devices and received files are retained.")
