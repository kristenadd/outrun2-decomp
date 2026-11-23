#!/bin/sh
# Local, pinned ARM64 iOS dependencies (SDL2, Ogg, Vorbis, FFmpeg) for the
# device (iphoneos) and the simulator (iphonesimulator), plus a host pkgconf.
# Same sources and SHA-256 as the Mac build (mac/tools/dependencies.lock).
# visionOS (xros, xrsimulator; visionos/README.md) uses the same script: SDL2
# is built without its video layer there (SDL2's UIKit video predates
# visionOS); the game uses it for audio and game controllers only.
# Usage: build-dependencies.sh [iphoneos|iphonesimulator|xros|xrsimulator ...]  (default: the two iOS SDKs)
set -eu
task_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
task_cmake=${OR2_CMAKE:-cmake}
task_ios_min=16.0
task_xr_min=2.0
task_sources="$task_root/build/dependency-sources"
task_archives="$task_root/build/downloads"
task_host="$task_root/build/ios-dependencies/host"
mkdir -p "$task_archives" "$task_sources" "$task_root/build/dependency-build"
while IFS='|' read -r task_name task_url task_sha; do
    if [ ! -f "$task_archives/$task_name" ]; then
        curl --fail --location "$task_url" -o "$task_archives/$task_name"
    fi
    printf '%s  %s\n' "$task_sha" "$task_archives/$task_name" | shasum -a 256 -c -
    tar -xf "$task_archives/$task_name" -C "$task_sources"
done < "$task_root/mac/tools/dependencies.lock"

# pkgconf runs on the Mac: one host build serves both SDKs.
if [ ! -x "$task_host/bin/pkgconf" ]; then
    rm -rf "$task_root/build/dependency-build/pkgconf-host"
    cp -R "$task_sources/pkgconf-2.3.0" "$task_root/build/dependency-build/pkgconf-host"
    (
        cd "$task_root/build/dependency-build/pkgconf-host"
        ./configure --prefix="$task_host" --disable-shared --enable-static
        make -j 6
        make install
    )
fi

[ $# -gt 0 ] || set -- iphoneos iphonesimulator
for task_sdk in "$@"; do
    case "$task_sdk" in
        iphoneos) task_min=$task_ios_min; task_target="arm64-apple-ios$task_min" ;;
        iphonesimulator) task_min=$task_ios_min; task_target="arm64-apple-ios$task_min-simulator" ;;
        xros) task_min=$task_xr_min; task_target="arm64-apple-xros$task_min" ;;
        xrsimulator) task_min=$task_xr_min; task_target="arm64-apple-xros$task_min-simulator" ;;
        *) echo "unknown SDK: $task_sdk" >&2; exit 2 ;;
    esac
    case "$task_sdk" in
        xr*) task_system=visionOS
             # SDL_PLATFORM_VISIONOS: SDL_sysurl.m's visionOS branch (SDL2 never defines it).
             # No HIDAPI (SDL2 has no visionOS backend for it): controllers come through GameController (MFi).
             task_sdl_options="-DSDL_VIDEO=OFF -DSDL_HIDAPI=OFF -DSDL_RENDER=OFF -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF -DSDL_METAL=OFF -DCMAKE_C_FLAGS=-DSDL_PLATFORM_VISIONOS=1 -DCMAKE_OBJC_FLAGS=-DSDL_PLATFORM_VISIONOS=1" ;;
        *) task_system=iOS; task_sdl_options="" ;;
    esac
    task_prefix="$task_root/build/ios-dependencies/$task_sdk"
    task_build="$task_root/build/dependency-build/ios-$task_sdk"
    task_sysroot=$(xcrun --sdk "$task_sdk" --show-sdk-path)
    for task_pair in SDL2-2.32.10:sdl2 libogg-1.3.5:ogg libvorbis-1.3.7:vorbis; do
        task_source=${task_pair%:*}
        task_name=${task_pair#*:}
        "$task_cmake" -S "$task_sources/$task_source" -B "$task_build/$task_name" \
            -DCMAKE_SYSTEM_NAME=$task_system -DCMAKE_OSX_SYSROOT="$task_sdk" \
            -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=$task_min \
            -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$task_prefix" \
            -DCMAKE_PREFIX_PATH="$task_prefix" -DCMAKE_FIND_ROOT_PATH="$task_prefix" \
            -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DBUILD_SHARED_LIBS=OFF \
            -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF $task_sdl_options
        "$task_cmake" --build "$task_build/$task_name" -j 6
        "$task_cmake" --install "$task_build/$task_name"
    done
    # FFmpeg configures in its own tree (the Mac build may have used the shared
    # sources): a fresh copy per SDK.
    rm -rf "$task_build/ffmpeg"
    mkdir -p "$task_build"
    cp -R "$task_sources/ffmpeg-8.0" "$task_build/ffmpeg"
    (
        cd "$task_build/ffmpeg"
        make distclean >/dev/null 2>&1 || true
        ./configure --prefix="$task_prefix" \
            --enable-cross-compile --target-os=darwin --arch=aarch64 \
            --cc="$(xcrun --sdk "$task_sdk" --find clang)" --sysroot="$task_sysroot" \
            --extra-cflags="-target $task_target" --extra-ldflags="-target $task_target" \
            --disable-shared --enable-static \
            --disable-programs --disable-doc --disable-everything --disable-autodetect \
            --enable-decoder=bink,binkaudio_dct,binkaudio_rdft,pcm_s16le,vorbis \
            --enable-demuxer=bink,ogg --enable-protocol=file --enable-swscale --enable-swresample
        make -j 6
        make install
    )
done
printf 'iOS / visionOS dependencies installed in %s\n' "$task_root/build/ios-dependencies"
