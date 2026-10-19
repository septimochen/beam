if(NOT BEAM_ENABLE_QUIC OR NOT UNIX)
    message(FATAL_ERROR "The desktop app requires the macOS/Linux QUIC core")
endif()

add_library(beam-desktop-support ALIAS beam-app-support)

if(APPLE)
    # An explicit cache value also works for custom and multi-config builds.
    set(beam_default_bundle_id org.beam.desktop.dev)
    if(NOT CMAKE_CONFIGURATION_TYPES AND CMAKE_BUILD_TYPE STREQUAL "Release" AND
       NOT "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELEASE} ${CMAKE_SWIFT_FLAGS} ${CMAKE_EXE_LINKER_FLAGS}" MATCHES "sanitize")
        set(beam_default_bundle_id org.beam.desktop)
    endif()
    set(BEAM_MACOS_BUNDLE_ID "" CACHE STRING "macOS bundle identifier (empty: Release production, otherwise development)")
    if(BEAM_MACOS_BUNDLE_ID STREQUAL "")
        set(BEAM_MACOS_BUNDLE_ID "${beam_default_bundle_id}")
    endif()
    if(NOT BEAM_MACOS_BUNDLE_ID MATCHES "^org\\.beam\\.desktop(\\.[A-Za-z0-9-]+)*$")
        message(FATAL_ERROR "BEAM_MACOS_BUNDLE_ID must be org.beam.desktop or a sub-identifier")
    endif()
    set(BEAM_MACOS_BUNDLE_ID "${BEAM_MACOS_BUNDLE_ID}" PARENT_SCOPE)
    set(BEAM_MACOS_DISPLAY_NAME Beam)
    if(BEAM_MACOS_BUNDLE_ID STREQUAL "org.beam.desktop.dev")
        set(BEAM_MACOS_DISPLAY_NAME "Beam Dev")
    endif()
    if(NOT CMAKE_GENERATOR MATCHES "Ninja|Xcode")
        message(FATAL_ERROR "Native macOS Swift builds require Ninja or Xcode; use make desktop or configure a fresh directory with -G Ninja")
    endif()
    enable_language(OBJCXX Swift)
    # Every bridge consumer also links the C++ core. CMake otherwise appends
    # the same implicit runtime once for CXX and once for OBJCXX to swiftc.
    list(REMOVE_ITEM CMAKE_OBJCXX_IMPLICIT_LINK_LIBRARIES ${CMAKE_CXX_IMPLICIT_LINK_LIBRARIES})
    set(CMAKE_OBJCXX_STANDARD 23)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    add_library(beam-desktop-bridge STATIC macos/BeamBridge.mm)
    target_include_directories(beam-desktop-bridge PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/macos")
    target_link_libraries(beam-desktop-bridge PRIVATE beam-desktop-support PUBLIC "-framework Foundation")
    target_compile_options(beam-desktop-bridge PRIVATE -fobjc-arc)
    beam_enable_warnings(beam-desktop-bridge)

    # A shared source list keeps the headless model/bridge tests on the same code
    # as the app, without distributing an extra Swift framework or Xcode project.
    set(native_ui_sources macos/DesktopModel.swift macos/BeamView.swift macos/DevicesView.swift macos/PairingView.swift macos/ReviewTextEditor.swift macos/LaunchOptions.swift macos/AppDelegate.swift)
    set(native_arch "${CMAKE_SYSTEM_PROCESSOR}")
    if(CMAKE_OSX_ARCHITECTURES)
        list(LENGTH CMAKE_OSX_ARCHITECTURES native_arch_count)
        if(NOT native_arch_count EQUAL 1)
            message(FATAL_ERROR "Configure native Swift desktop builds separately for each macOS architecture")
        endif()
        set(native_arch "${CMAKE_OSX_ARCHITECTURES}")
    endif()
    function(beam_native_target target)
        # Persist bridging PCHs so debug information never names a deleted
        # swift-driver TemporaryDirectory when the linker reads the objects.
        set(bridging_pch_dir "${CMAKE_CURRENT_BINARY_DIR}/${target}-pch")
        file(MAKE_DIRECTORY "${bridging_pch_dir}")
        target_link_libraries(${target} PRIVATE beam-desktop-bridge "-framework AppKit" "-framework SwiftUI" "-framework UserNotifications")
        set_target_properties(${target} PROPERTIES Swift_LANGUAGE_VERSION 6)
        target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:Swift>:-parse-as-library>"
            "$<$<COMPILE_LANGUAGE:Swift>:-target>"
            "$<$<COMPILE_LANGUAGE:Swift>:${native_arch}-apple-macosx${CMAKE_OSX_DEPLOYMENT_TARGET}>"
            "$<$<COMPILE_LANGUAGE:Swift>:-warnings-as-errors>"
            "$<$<COMPILE_LANGUAGE:Swift>:-pch-output-dir>"
            "$<$<COMPILE_LANGUAGE:Swift>:${bridging_pch_dir}>"
            "$<$<COMPILE_LANGUAGE:Swift>:-import-objc-header>"
            "$<$<COMPILE_LANGUAGE:Swift>:${CMAKE_CURRENT_SOURCE_DIR}/macos/BeamBridge.h>")
    endfunction()
    add_executable(beam-desktop MACOSX_BUNDLE macos/Main.swift ${native_ui_sources})
    beam_native_target(beam-desktop)
    if(BEAM_ENABLE_QUIC)
        file(GLOB pairing_notices "${PROJECT_SOURCE_DIR}/third_party/licenses/*.md")
        target_sources(beam-desktop PRIVATE ${pairing_notices})
        set_source_files_properties(${pairing_notices} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/Licenses)
    endif()
    set(icon_root "${PROJECT_SOURCE_DIR}/assets/icons")
    target_sources(beam-desktop PRIVATE "${icon_root}/Beam.icns" "${icon_root}/BeamTemplate.pdf")
    set_source_files_properties("${icon_root}/Beam.icns" "${icon_root}/BeamTemplate.pdf"
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set(icon_resources
        "$<TARGET_BUNDLE_CONTENT_DIR:beam-desktop>/Resources/Beam.icns"
        "$<TARGET_BUNDLE_CONTENT_DIR:beam-desktop>/Resources/BeamTemplate.pdf")
    if(NOT BEAM_ACTOOL)
        execute_process(COMMAND xcrun --find actool OUTPUT_VARIABLE detected_actool
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        set(BEAM_ACTOOL "${detected_actool}" CACHE FILEPATH "Apple asset catalog compiler" FORCE)
    endif()
    if(NOT EXISTS "${BEAM_ACTOOL}")
        message(FATAL_ERROR "macOS desktop builds require Xcode's actool for native app icons. "
            "Install full Xcode and select it with "
            "'sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer'.")
    endif()
    set(native_icon_dir "${CMAKE_CURRENT_BINARY_DIR}/icon")
    set(native_icon "${native_icon_dir}/Assets.car")
    add_custom_command(OUTPUT "${native_icon}"
        COMMAND "${CMAKE_COMMAND}" "-DBEAM_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DBEAM_ICON_OUTPUT=${native_icon_dir}" "-DBEAM_ACTOOL=${BEAM_ACTOOL}"
            "-DBEAM_MACOS_VERSION=${CMAKE_OSX_DEPLOYMENT_TARGET}"
            -P "${PROJECT_SOURCE_DIR}/cmake/CompileMacIcon.cmake"
        DEPENDS "${PROJECT_SOURCE_DIR}/cmake/CompileMacIcon.cmake"
            "${icon_root}/Beam.icon/icon.json"
            "${icon_root}/Beam.icon/Assets/ray-1.svg"
            "${icon_root}/Beam.icon/Assets/ray-2.svg"
            "${icon_root}/Beam.icon/Assets/ray-3.svg"
        VERBATIM)
    # Own the generated catalog independently of the Swift bundle's copy
    # target; otherwise Ninja can make the compiler depend on its copies.
    add_custom_target(beam-native-icon DEPENDS "${native_icon}")
    target_sources(beam-desktop PRIVATE "${native_icon}")
    set_source_files_properties("${native_icon}" PROPERTIES GENERATED TRUE MACOSX_PACKAGE_LOCATION Resources)
    list(APPEND icon_resources "$<TARGET_BUNDLE_CONTENT_DIR:beam-desktop>/Resources/Assets.car")
    # Ninja's combined Swift compile/link rule does not follow the generated
    # object-order resource dependency. Make the bundle copies explicit.
    if(CMAKE_GENERATOR MATCHES "Ninja")
        add_custom_target(beam-desktop-resources DEPENDS ${icon_resources})
        add_dependencies(beam-desktop beam-desktop-resources)
    endif()
    set_target_properties(beam-desktop PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "${BEAM_MACOS_BUNDLE_ID}"
        MACOSX_BUNDLE_BUNDLE_NAME "Beam"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist.in")
else()
    find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
    add_library(beam-desktop-ui STATIC qt/window.cpp qt/devices_dialog.cpp)
    if(BEAM_ENABLE_PAIRING)
        target_sources(beam-desktop-ui PRIVATE qt/pairing_dialog.cpp)
    endif()
    qt_add_resources(beam-desktop-ui beam-icons PREFIX /beam/icons
        BASE "${PROJECT_SOURCE_DIR}/assets/icons" FILES "${PROJECT_SOURCE_DIR}/assets/icons/beam.png")
    target_link_libraries(beam-desktop-ui PUBLIC beam-desktop-support Qt6::Widgets)
    beam_enable_warnings(beam-desktop-ui)
    add_executable(beam-desktop qt/main.cpp)
    target_link_libraries(beam-desktop PRIVATE beam-desktop-ui)
    beam_enable_warnings(beam-desktop)
endif()

set_target_properties(beam-desktop PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
if(APPLE)
    # Spotlight must not treat a compiler output as another installed app.
    # Keep this on the target so custom build directories and Xcode also work.
    set_target_properties(beam-desktop PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/apps.noindex")
endif()

if(BUILD_TESTING)
    add_executable(beam-desktop-tests "${PROJECT_SOURCE_DIR}/tests/desktop_tests.cpp")
    target_link_libraries(beam-desktop-tests PRIVATE beam-desktop-support Threads::Threads)
    beam_enable_warnings(beam-desktop-tests)
    add_test(NAME desktop COMMAND beam-desktop-tests)
    set_tests_properties(desktop PROPERTIES TIMEOUT 45)
    if(BEAM_ENABLE_DISCOVERY)
        add_custom_target(desktop-live-test COMMAND "$<TARGET_FILE:beam-desktop-tests>" --discovery
            DEPENDS beam-desktop-tests USES_TERMINAL)
    endif()
    if(APPLE)
        add_executable(beam-desktop-ui-tests "${PROJECT_SOURCE_DIR}/tests/desktop/macos/ui_tests.swift" ${native_ui_sources})
        beam_native_target(beam-desktop-ui-tests)
    else()
        add_executable(beam-desktop-ui-tests "${PROJECT_SOURCE_DIR}/tests/desktop/qt/ui_tests.cpp")
        target_link_libraries(beam-desktop-ui-tests PRIVATE beam-desktop-ui)
        beam_enable_warnings(beam-desktop-ui-tests)
    endif()
    set_target_properties(beam-desktop-ui-tests beam-desktop-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
    if(APPLE)
        add_test(NAME desktop-ui COMMAND beam-desktop-ui-tests "$<TARGET_FILE:beam>")
    else()
        add_test(NAME desktop-ui COMMAND beam-desktop-ui-tests)
    endif()
    set_tests_properties(desktop-ui PROPERTIES TIMEOUT 30)
    add_test(NAME desktop-help COMMAND beam-desktop --help)
    add_test(NAME desktop-version COMMAND beam-desktop --version)
    if(NOT APPLE)
        set_tests_properties(desktop-ui desktop-help desktop-version PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()
