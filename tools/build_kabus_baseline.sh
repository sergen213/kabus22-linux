#!/bin/sh
# Build, provision exact distro sources, then publish offline using an existing
# tools/Dockerfile.ubuntu24 toolchain image (built with Dockerfile stdin only).
# Only the enumerated source slices enter containers; never mount the repo root.
set -eu

usage() {
    printf '%s\n' 'Usage: build_kabus_baseline.sh WORK_DIRECTORY [TOOLCHAIN_IMAGE]' \
        'Default image: kabus22-ubuntu24-toolchain' \
        'Publishes WORK_DIRECTORY/Kabus22-Linux-Installer-x86_64.AppImage and its report/source bundle.' \
        'Requires the pinned runtime in build-appimage/cache; existing outputs are never overwritten.'
}
case ${1-} in -h|--help) usage; exit 0 ;; esac
if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then usage >&2; exit 2; fi
root=$(CDPATH= cd -P -- "$(dirname -- "$0")/.." && pwd)
mkdir -p -- "$1"
work=$(CDPATH= cd -P -- "$1" && pwd)
case "$root/" in
    "$work/"*) printf '%s\n' 'Work directory must not expose the repository root or its ancestors.' >&2; exit 2 ;;
esac
image=${2-kabus22-ubuntu24-toolchain}
# Resolve the tag once: building, source acquisition and publication use one image.
image=$(docker image inspect --format '{{.Id}}' "$image")
owner=$(id -u):$(id -g)
runtime=$root/build-appimage/cache/uruntime-appimage-squashfs-lite-x86_64
output=$work/Kabus22-Linux-Installer-x86_64.AppImage
for path in "$root" "$work" "$runtime"; do
    case $path in *,*) printf 'Docker bind paths cannot contain commas: %s\n' "$path" >&2; exit 2 ;; esac
done
if [ ! -f "$runtime" ] || [ ! -r "$runtime" ]; then
    printf 'Missing readable pinned AppImage runtime: %s\n' "$runtime" >&2; exit 1
fi
for suffix in '' .json .sources.tar.gz; do
    if [ -e "$output$suffix" ] || [ -L "$output$suffix" ]; then
        printf 'Refusing to overwrite: %s\n' "$output$suffix" >&2; exit 1
    fi
done
mkdir -p -- "$work/cache" "$work/sources"

container() {
    network=$1
    shift
    # POSIX positional parameters preserve paths and command arguments without eval.
    docker run --rm --pull=never --platform=linux/amd64 \
        --read-only --cap-drop=ALL --security-opt=no-new-privileges \
        --user "$owner" --network "$network" \
        --tmpfs /tmp:rw,nosuid,nodev,mode=1777 --env HOME=/tmp \
        --env CC=/usr/bin/gcc-13 --env CXX=/usr/bin/g++-13 \
        --env 'CFLAGS=-march=x86-64 -mtune=generic' \
        --env 'CXXFLAGS=-march=x86-64 -mtune=generic' \
        --workdir /work \
        --mount "type=bind,src=$work,dst=/work" \
        --mount "type=bind,src=$root/CMakeLists.txt,dst=/src/CMakeLists.txt,readonly" \
        --mount "type=bind,src=$root/native,dst=/src/native,readonly" \
        --mount "type=bind,src=$root/installer,dst=/src/installer,readonly" \
        --mount "type=bind,src=$root/cmake,dst=/src/cmake,readonly" \
        --mount "type=bind,src=$root/third_party,dst=/src/third_party,readonly" \
        --mount "type=bind,src=$root/tools,dst=/src/tools,readonly" \
        --mount "type=bind,src=$root/packaging,dst=/src/packaging,readonly" \
        --mount "type=bind,src=$runtime,dst=/runtime,readonly" \
        "$image" "$@"
}

container none /bin/sh -eu -c '
    for target in engine installer; do
        source=/src
        [ "$target" != installer ] || source=/src/installer
        cmake -S "$source" -B "/work/$target" -G Ninja \
            -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
            -DCMAKE_C_COMPILER=/usr/bin/gcc-13 \
            -DCMAKE_CXX_COMPILER=/usr/bin/g++-13 \
            "-DCMAKE_C_FLAGS=-march=x86-64 -mtune=generic" \
            "-DCMAKE_CXX_FLAGS=-march=x86-64 -mtune=generic" \
            -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON
        cmake --build "/work/$target" --parallel 2
    done'

package() {
    network=$1
    shift
    container "$network" python3 /src/tools/package_kabus_appimage.py \
        --engine /work/engine/kabus22 --installer /work/installer/kabus22-setup \
        --runtime /runtime --cache /work/cache --source-cache /work/sources \
        --output /work/Kabus22-Linux-Installer-x86_64.AppImage "$@"
}

# SourceAcquisition alone has network access; Publish is always offline.
package bridge --acquire-sources
package none
