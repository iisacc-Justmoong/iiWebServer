#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 0 ]]; then
    echo "Usage: INSTALL_PREFIX=... QT_PREFIX_PATH=... CMAKE_PREFIX_PATH=... ./install.sh" >&2
    exit 2
fi

source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="$source_dir/build"
install_prefix="${INSTALL_PREFIX:-$HOME/.local/SDK/iiWebServer}"
compatibility="${IIWEBSERVER_BUILD_COMPATIBILITY:-ON}"
qt_prefix="${QT_PREFIX_PATH:-}"
if [[ -z "$qt_prefix" && -d "/Volumes/Storage/Qt/6.8.3/macos" ]]; then
    qt_prefix="/Volumes/Storage/Qt/6.8.3/macos"
fi
prefix_path="${CMAKE_PREFIX_PATH:-}"
network_prefix="${IINETWORK_PREFIX:-$HOME/.local/SDK/iiNetwork}"
if [[ -d "$network_prefix/lib/cmake/iiNetwork" ]]; then
    prefix_path="$network_prefix${prefix_path:+;$prefix_path}"
fi
for dependency in iiSDKCore iiJson iiFileProvider; do
    dependency_prefix="$HOME/.local/SDK/$dependency"
    if [[ -d "$dependency_prefix/lib/cmake" ]]; then
        prefix_path="$dependency_prefix${prefix_path:+;$prefix_path}"
    fi
done
if [[ -n "$qt_prefix" ]]; then
    prefix_path="$qt_prefix${prefix_path:+;$prefix_path}"
fi

cmake -S "$source_dir" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=ON \
    -DIIWEBSERVER_BUILD_COMPATIBILITY="$compatibility" \
    -DCMAKE_INSTALL_PREFIX="$install_prefix" \
    -DCMAKE_PREFIX_PATH="$prefix_path"
cmake --build "$build_dir" --config Release --parallel "${BUILD_JOBS:-4}"
if [[ "${IIWEBSERVER_RUN_INTEGRATION_TESTS:-0}" == 1 ]]; then
    ctest --test-dir "$build_dir" -C Release --output-on-failure
else
    ctest --test-dir "$build_dir" -C Release --output-on-failure \
        -R '^iiWebServer\.(source_layout|resource_runner|resource_paths|resource_links|resource_fixture|install_policy|fixture_cleanup|hello_world|optional_backends)$'
fi
cmake --install "$build_dir" --config Release
