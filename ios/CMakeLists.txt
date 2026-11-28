# iOS / iPadOS port (Apple silicon devices and the arm64 simulator): the Mac
# game (mac/source: Metal D3D9 device, SDL display, audio, game entry) with
# iOS paths, screen rate, and a required game controller (ios/source).
enable_language(OBJCXX)
set(OR2_MAC_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../mac/source")
set(OR2_IOS_BUNDLE_ID "org.outrun.ios" CACHE STRING "iOS bundle identifier (must be unique to your Apple developer team)")
set(OR2_IOS_TEAM "" CACHE STRING "Apple developer team ID for device signing (Xcode generator)")
# CFBundleSupportedPlatforms in Info.plist.in.
if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
    set(OR2_IOS_PLATFORM_NAME iPhoneSimulator)
else()
    set(OR2_IOS_PLATFORM_NAME iPhoneOS)
endif()

# The shared pad / lifecycle adapters (DualSense layout, runtime platform).
add_library(outrun_ps5_platform STATIC ../src/input/dual_sense.cpp ../src/input/runtime_adapter.cpp)
target_link_libraries(outrun_ps5_platform PUBLIC outrun_native_runtime)
target_compile_features(outrun_ps5_platform PUBLIC cxx_std_17)
target_compile_options(outrun_ps5_platform PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off -fno-fast-math)

# Codecs (Bink movies, Vorbis music): the iOS builds of the Mac's FFmpeg and Vorbis.
find_package(PkgConfig REQUIRED)
pkg_check_modules(OR2_IOS_CODECS REQUIRED IMPORTED_TARGET libavformat libavcodec libswscale libswresample libavutil vorbisfile vorbis ogg)
target_compile_definitions(outrun_native_runtime PRIVATE OR2_HAVE_VORBIS=1 OUTRUN_WITH_FFMPEG=1)
target_link_libraries(outrun_native_runtime PUBLIC PkgConfig::OR2_IOS_CODECS)

find_package(SDL2 CONFIG REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(OR2_METAL_SHADERS "${CMAKE_CURRENT_BINARY_DIR}/generated/pc_metal_shaders.hpp")
add_custom_command(OUTPUT "${OR2_METAL_SHADERS}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../mac/tools/generate_metal_shaders.py" "${OR2_METAL_SHADERS}"
    DEPENDS ../mac/tools/generate_metal_shaders.py ../mac/tools/generate_metal_vertices.py ../src/platform/pc_shader_programs_generated.cpp ../src/platform/pc_shader_programs.hpp ../switch/shaders/pc_ffp_fsh.glsl ../switch/shaders/pc_ps11_uber_fsh.glsl ../switch/shaders/pc_ps14_uber_fsh.glsl)

add_library(outrun_ios_metal STATIC "${OR2_MAC_SOURCE}/metal_d3d9.mm" "${OR2_MAC_SOURCE}/metal_vertices.cpp" source/display_rate.mm "${OR2_METAL_SHADERS}")
target_compile_features(outrun_ios_metal PUBLIC cxx_std_17)
target_include_directories(outrun_ios_metal PUBLIC source "${OR2_MAC_SOURCE}")
target_include_directories(outrun_ios_metal PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_options(outrun_ios_metal PRIVATE -fobjc-arc -Wall -Wextra -Wpedantic)
target_link_libraries(outrun_ios_metal PUBLIC outrun_native_runtime "-framework Metal" "-framework QuartzCore" "-framework UIKit" "-framework Foundation")

# Application icon. Xcode builds: an asset catalog with one 1024x1024 icon (App Store Connect
# requires the 1024 px icon in an asset catalog; actool makes the device sizes and CFBundleIcons).
# Makefile builds (the simulator, no actool): icon.png at each size iOS asks for (CFBundleIcons in
# Info.plist.in).
set(OR2_IOS_ICON_DIR "${CMAKE_CURRENT_BINARY_DIR}/icons")
set(OR2_IOS_ICONS)
if(CMAKE_GENERATOR STREQUAL "Xcode")
    set(_catalog "${CMAKE_CURRENT_BINARY_DIR}/Assets.xcassets")
    file(WRITE "${_catalog}/Contents.json" "{\"info\":{\"author\":\"xcode\",\"version\":1}}\n")
    file(WRITE "${_catalog}/AppIcon.appiconset/Contents.json"
        "{\"images\":[{\"filename\":\"icon-1024.png\",\"idiom\":\"universal\",\"platform\":\"ios\",\"size\":\"1024x1024\"}],\"info\":{\"author\":\"xcode\",\"version\":1}}\n")
    # At configure time: the catalog must be complete when Xcode reads the project.
    execute_process(COMMAND sips -z 1024 1024 "${PROJECT_SOURCE_DIR}/icon.png" --out "${_catalog}/AppIcon.appiconset/icon-1024.png"
        OUTPUT_QUIET RESULT_VARIABLE _sips)
    if(NOT _sips EQUAL 0)
        message(FATAL_ERROR "sips couldn't make the 1024 px app icon from icon.png")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/icon.png")
    set_source_files_properties("${_catalog}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    list(APPEND OR2_IOS_ICONS "${_catalog}")
else()
foreach(_icon "AppIcon60x60@2x.png:120" "AppIcon60x60@3x.png:180" "AppIcon76x76@2x~ipad.png:152" "AppIcon83.5x83.5@2x~ipad.png:167")
    string(REPLACE ":" ";" _pair "${_icon}")
    list(GET _pair 0 _name)
    list(GET _pair 1 _size)
    add_custom_command(OUTPUT "${OR2_IOS_ICON_DIR}/${_name}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${OR2_IOS_ICON_DIR}"
        COMMAND sips -z ${_size} ${_size} "${PROJECT_SOURCE_DIR}/icon.png" --out "${OR2_IOS_ICON_DIR}/${_name}" > /dev/null
        DEPENDS "${PROJECT_SOURCE_DIR}/icon.png")
    set_source_files_properties("${OR2_IOS_ICON_DIR}/${_name}" PROPERTIES GENERATED TRUE MACOSX_PACKAGE_LOCATION .)
    list(APPEND OR2_IOS_ICONS "${OR2_IOS_ICON_DIR}/${_name}")
endforeach()
endif()

add_executable(OutRunIOS MACOSX_BUNDLE
    "${OR2_MAC_SOURCE}/main.cpp" "${OR2_MAC_SOURCE}/setup_display.cpp" "${PROJECT_SOURCE_DIR}/src/runtime/game_host.cpp"
    "${OR2_MAC_SOURCE}/renderer.cpp" "${OR2_MAC_SOURCE}/sdl_backend.cpp" "${OR2_MAC_SOURCE}/audio.cpp"
    source/platform_api.cpp source/ios_paths.mm source/ios_screens.cpp source/ios_scene.mm
    ${OR2_IOS_ICONS})
target_compile_features(OutRunIOS PRIVATE cxx_std_17)
target_compile_definitions(OutRunIOS PRIVATE OR2_MAC_HOST=1 OR2_IOS=1)
target_include_directories(OutRunIOS PRIVATE source "${OR2_MAC_SOURCE}")
target_compile_options(OutRunIOS PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
# SDL runs the game's main() from its UIApplication (SDL_main, SDL2main).
set_source_files_properties("${OR2_MAC_SOURCE}/main.cpp" PROPERTIES COMPILE_OPTIONS "-include;SDL_main.h")
target_link_libraries(OutRunIOS PRIVATE outrun_ios_metal outrun_ps5_platform SDL2::SDL2main SDL2::SDL2
    "-framework GameController" "-framework CoreHaptics" "-framework AVFoundation" "-framework AudioToolbox" "-framework CoreMotion")
set_target_properties(OutRunIOS PROPERTIES
    OUTPUT_NAME OutRun
    MACOSX_BUNDLE_GUI_IDENTIFIER "${OR2_IOS_BUNDLE_ID}"
    MACOSX_BUNDLE_BUNDLE_NAME "OutRun"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${OR2_IOS_BUNDLE_ID}"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${OR2_IOS_TEAM}"
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development"
    XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"
    # Archive (xcodebuild archive / -exportArchive, the CI's ad-hoc .ipa): only the app is
    # installed; the libraries skip it (CMAKE_XCODE_ATTRIBUTE_SKIP_INSTALL in the preset).
    XCODE_ATTRIBUTE_SKIP_INSTALL "NO"
    XCODE_ATTRIBUTE_INSTALL_PATH "$(LOCAL_APPS_DIR)"
    # Xcode: the one scheme to run (the project's test and tool targets get none).
    XCODE_GENERATE_SCHEME ON
    # Run from Xcode at full speed: Metal API / shader validation and GPU frame
    # capture (Xcode's defaults) check every one of the game's many draws.
    XCODE_SCHEME_ENABLE_GPU_API_VALIDATION OFF
    XCODE_SCHEME_ENABLE_GPU_SHADER_VALIDATION OFF
    XCODE_SCHEME_ENABLE_GPU_FRAME_CAPTURE_MODE Disabled)
if(CMAKE_GENERATOR STREQUAL "Xcode")
    set_target_properties(OutRunIOS PROPERTIES RESOURCE "${CMAKE_CURRENT_BINARY_DIR}/Assets.xcassets")
endif()
if(NOT CMAKE_GENERATOR STREQUAL "Xcode")
    # Makefile builds (the simulator): ad-hoc signature, as Xcode gives simulator apps.
    add_custom_command(TARGET OutRunIOS POST_BUILD
        COMMAND codesign --force --sign - --timestamp=none "$<TARGET_BUNDLE_DIR:OutRunIOS>"
        VERBATIM)
endif()
