# visionOS port (Apple Vision Pro and its simulator): the Mac / iOS game
# (mac/source: Metal D3D9 device, renderer, audio, game entry; ios/source:
# paths and pad input) in a SwiftUI window that shows it in stereoscopic 3D
# (visionos/source; visionos/README.md). Xcode generator only (Swift).
enable_language(OBJCXX)
enable_language(Swift)
if(NOT CMAKE_GENERATOR STREQUAL "Xcode")
    message(FATAL_ERROR "The visionOS port builds with the Xcode generator (cmake --preset visionos-simulator or visionos-device)")
endif()
set(OR2_MAC_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../mac/source")
set(OR2_IOS_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../ios/source")
set(OR2_VISIONOS_BUNDLE_ID "org.outrun.visionos" CACHE STRING "visionOS bundle identifier (must be unique to your Apple developer team)")
set(OR2_VISIONOS_TEAM "" CACHE STRING "Apple developer team ID for device signing")
# CFBundleSupportedPlatforms in Info.plist.in.
if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
    set(OR2_VISIONOS_PLATFORM_NAME XRSimulator)
    set(OR2_VISIONOS_SIGN "-")   # ad hoc, as Xcode signs simulator applications
else()
    set(OR2_VISIONOS_PLATFORM_NAME XROS)
    set(OR2_VISIONOS_SIGN "Apple Development")
endif()

# The shared pad / lifecycle adapters (DualSense layout, runtime platform).
add_library(outrun_ps5_platform STATIC ../src/input/dual_sense.cpp ../src/input/runtime_adapter.cpp)
target_link_libraries(outrun_ps5_platform PUBLIC outrun_native_runtime)
target_compile_features(outrun_ps5_platform PUBLIC cxx_std_17)
target_compile_options(outrun_ps5_platform PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off -fno-fast-math)

# Codecs (Bink movies, Vorbis music): the visionOS builds of the Mac's FFmpeg and Vorbis.
find_package(PkgConfig REQUIRED)
pkg_check_modules(OR2_VISIONOS_CODECS REQUIRED IMPORTED_TARGET libavformat libavcodec libswscale libswresample libavutil vorbisfile vorbis ogg)
target_compile_definitions(outrun_native_runtime PRIVATE OR2_HAVE_VORBIS=1 OUTRUN_WITH_FFMPEG=1)
target_link_libraries(outrun_native_runtime PUBLIC PkgConfig::OR2_VISIONOS_CODECS)

# SDL2 without its video layer (ios/tools/build-dependencies.sh xros xrsimulator): audio, events, game controllers.
find_package(SDL2 CONFIG REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(OR2_METAL_SHADERS "${CMAKE_CURRENT_BINARY_DIR}/generated/pc_metal_shaders.hpp")
add_custom_command(OUTPUT "${OR2_METAL_SHADERS}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../mac/tools/generate_metal_shaders.py" "${OR2_METAL_SHADERS}"
    DEPENDS ../mac/tools/generate_metal_shaders.py ../mac/tools/generate_metal_vertices.py ../src/platform/pc_shader_programs_generated.cpp ../src/platform/pc_shader_programs.hpp ../switch/shaders/pc_ffp_fsh.glsl ../switch/shaders/pc_ps11_uber_fsh.glsl ../switch/shaders/pc_ps14_uber_fsh.glsl)

add_library(outrun_visionos_metal STATIC "${OR2_MAC_SOURCE}/metal_d3d9.mm" "${OR2_MAC_SOURCE}/metal_vertices.cpp" "${OR2_METAL_SHADERS}")
target_compile_features(outrun_visionos_metal PUBLIC cxx_std_17)
target_include_directories(outrun_visionos_metal PUBLIC "${OR2_MAC_SOURCE}")
target_include_directories(outrun_visionos_metal PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_options(outrun_visionos_metal PRIVATE -fobjc-arc -Wall -Wextra -Wpedantic)
target_link_libraries(outrun_visionos_metal PUBLIC outrun_native_runtime "-framework Metal" "-framework Foundation")

add_executable(OutRunVision MACOSX_BUNDLE
    source/OutRunApp.swift source/vision_main.mm source/vision_display.mm source/vision_screens.cpp source/vision_gamepad.mm
    "${OR2_MAC_SOURCE}/main.cpp" "${PROJECT_SOURCE_DIR}/src/runtime/game_host.cpp"
    "${OR2_MAC_SOURCE}/renderer.cpp" "${OR2_MAC_SOURCE}/audio.cpp"
    "${OR2_IOS_SOURCE}/platform_api.cpp" "${OR2_IOS_SOURCE}/ios_paths.mm"
    source/StereoMaterial.usda Assets.xcassets)
target_compile_features(OutRunVision PRIVATE cxx_std_17)
# OR2_IOS: the shared iOS paths, pad input and Mac-file branches; OR2_VISIONOS: the window and stereo.
target_compile_definitions(OutRunVision PRIVATE $<$<NOT:$<COMPILE_LANGUAGE:Swift>>:OR2_MAC_HOST=1 OR2_IOS=1 OR2_VISIONOS=1 SDL_MAIN_HANDLED>)
target_include_directories(OutRunVision PRIVATE source "${OR2_IOS_SOURCE}" "${OR2_MAC_SOURCE}")
target_compile_options(OutRunVision PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
# The game's main() runs on its own thread (vision_main.mm); SwiftUI owns the process's main.
set_source_files_properties("${OR2_MAC_SOURCE}/main.cpp" PROPERTIES COMPILE_DEFINITIONS "main=or2_game_main")
set_source_files_properties(source/StereoMaterial.usda PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
# Application icon: a three-layer visionOS image stack (tools/make_icon.swift, from icon.png).
set_source_files_properties(Assets.xcassets PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
target_link_libraries(OutRunVision PRIVATE outrun_visionos_metal outrun_ps5_platform SDL2::SDL2
    "-framework GameController" "-framework CoreHaptics" "-framework AVFoundation" "-framework AudioToolbox"
    "-framework UIKit" "-framework SwiftUI" "-framework RealityKit")
set_target_properties(OutRunVision PROPERTIES
    OUTPUT_NAME OutRun
    RESOURCE "source/StereoMaterial.usda;Assets.xcassets"
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${OR2_VISIONOS_BUNDLE_ID}"
    MACOSX_BUNDLE_BUNDLE_NAME "OutRun"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${OR2_VISIONOS_BUNDLE_ID}"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "7"
    XCODE_ATTRIBUTE_XROS_DEPLOYMENT_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}"
    XCODE_ATTRIBUTE_SWIFT_VERSION "5.0"
    XCODE_ATTRIBUTE_SWIFT_OBJC_BRIDGING_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/source/vision_bridge.h"
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${OR2_VISIONOS_TEAM}"
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "${OR2_VISIONOS_SIGN}"
    XCODE_GENERATE_SCHEME ON
    XCODE_SCHEME_ENABLE_GPU_API_VALIDATION OFF
    XCODE_SCHEME_ENABLE_GPU_SHADER_VALIDATION OFF
    XCODE_SCHEME_ENABLE_GPU_FRAME_CAPTURE_MODE Disabled)
