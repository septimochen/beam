cmake_minimum_required(VERSION 3.25)
if(NOT BEAM_SOURCE_DIR OR NOT BEAM_ICON_OUTPUT)
    message(FATAL_ERROR "Pass BEAM_SOURCE_DIR and BEAM_ICON_OUTPUT")
endif()
if(NOT BEAM_ACTOOL)
    execute_process(COMMAND xcrun --find actool OUTPUT_VARIABLE BEAM_ACTOOL
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
if(NOT EXISTS "${BEAM_ACTOOL}")
    message(FATAL_ERROR "Native layered icon compilation requires Xcode's actool; select Xcode with DEVELOPER_DIR or pass BEAM_ACTOOL")
endif()
if(NOT BEAM_MACOS_VERSION)
    execute_process(COMMAND xcrun --sdk macosx --show-sdk-version
        OUTPUT_VARIABLE BEAM_MACOS_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
endif()
file(MAKE_DIRECTORY "${BEAM_ICON_OUTPUT}")
execute_process(COMMAND "${BEAM_ACTOOL}" "${BEAM_SOURCE_DIR}/assets/icons/Beam.icon"
    --compile "${BEAM_ICON_OUTPUT}" --app-icon Beam --platform macosx
    --target-device mac --minimum-deployment-target "${BEAM_MACOS_VERSION}"
    --enable-on-demand-resources NO --output-format human-readable-text
    --output-partial-info-plist "${BEAM_ICON_OUTPUT}/IconInfo.plist"
    --errors --warnings COMMAND_ERROR_IS_FATAL ANY)
if(NOT EXISTS "${BEAM_ICON_OUTPUT}/Assets.car")
    message(FATAL_ERROR "actool did not produce the native icon catalog")
endif()
