include(GNUInstallDirs)

if(APPLE)
    # Keep fixup_bundle's recursive scan inside Beam's private directory, never
    # the user's bin directory, which can contain unrelated executables.
    install(TARGETS beam RUNTIME DESTINATION "${CMAKE_INSTALL_LIBDIR}/beam" COMPONENT cli)
    configure_file("${PROJECT_SOURCE_DIR}/cmake/InstallMac.cmake.in"
        "${PROJECT_BINARY_DIR}/InstallMac.cmake" @ONLY)
    install(CODE "set(beam_install_component cli)" COMPONENT cli)
    install(SCRIPT "${PROJECT_BINARY_DIR}/InstallMac.cmake" COMPONENT cli)
    if(BEAM_ENABLE_DESKTOP)
        install(DIRECTORY "$<TARGET_BUNDLE_DIR:beam-desktop>/"
            DESTINATION Beam.app USE_SOURCE_PERMISSIONS COMPONENT desktop PATTERN "*.d" EXCLUDE)
        install(CODE "set(beam_install_component desktop)" COMPONENT desktop)
        install(SCRIPT "${PROJECT_BINARY_DIR}/InstallMac.cmake" COMPONENT desktop)
    endif()
else()
    file(RELATIVE_PATH beam_library_path "${CMAKE_INSTALL_FULL_BINDIR}"
        "${CMAKE_INSTALL_FULL_LIBDIR}/beam")
    set_target_properties(beam PROPERTIES INSTALL_RPATH "$ORIGIN/${beam_library_path}")
    install(TARGETS beam RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT cli)
    if(UNIX AND BEAM_ENABLE_QUIC)
        get_filename_component(msquic_directory "${MSQUIC_LIBRARY}" DIRECTORY)
        # Preserve the ELF SONAME symlink as well as the versioned library.
        file(GLOB msquic_runtime "${msquic_directory}/libmsquic.so*")
        install(FILES ${msquic_runtime} DESTINATION "${CMAKE_INSTALL_LIBDIR}/beam" COMPONENT cli)
    endif()
    if(BEAM_ENABLE_DESKTOP)
        set_target_properties(beam-desktop PROPERTIES INSTALL_RPATH "$ORIGIN/${beam_library_path}")
        install(TARGETS beam-desktop RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT desktop)
        install(FILES "${PROJECT_SOURCE_DIR}/assets/icons/beam.svg"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
            RENAME org.beam.desktop.svg COMPONENT desktop)
        install(FILES "${PROJECT_SOURCE_DIR}/assets/icons/beam.png"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps"
            RENAME org.beam.desktop.png COMPONENT desktop)
        install(FILES ${msquic_runtime} DESTINATION "${CMAKE_INSTALL_LIBDIR}/beam" COMPONENT desktop)
        set(BEAM_DESKTOP_ENTRY_DIR "${CMAKE_INSTALL_DATADIR}/applications" CACHE STRING
            "Desktop entry directory (absolute or relative to the install prefix)")
        configure_file("${PROJECT_SOURCE_DIR}/cmake/InstallDesktopEntry.cmake.in"
            "${PROJECT_BINARY_DIR}/InstallDesktopEntry.cmake" @ONLY)
        install(SCRIPT "${PROJECT_BINARY_DIR}/InstallDesktopEntry.cmake" COMPONENT desktop)
    endif()
endif()

if(BEAM_ENABLE_PAIRING)
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/third_party/licenses/"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/beam" COMPONENT cli)
    if(BEAM_ENABLE_DESKTOP AND NOT APPLE)
        install(DIRECTORY "${PROJECT_SOURCE_DIR}/third_party/licenses/"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/beam" COMPONENT desktop)
    endif()
endif()
