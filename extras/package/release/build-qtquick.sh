#!/usr/bin/env bash
# Rebuild the matching Qt Quick library so Linux releases include our Qt fixes.
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
qt_prefix=${QT_ROOT_DIR:?QT_ROOT_DIR must point to the installed Qt SDK}
version=$("$qt_prefix/bin/qmake" -query QT_VERSION)
archive="qtdeclarative-everywhere-src-$version.tar.xz"
patch_dir="$root/contrib/src/qtdeclarative"
checksum=$(awk -v archive="$archive" '$2 == archive { print $1 }' "$patch_dir/SHA512SUMS")
[[ -n "$checksum" ]] || { echo "No verified Qt Declarative source for Qt $version" >&2; exit 1; }
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
curl --fail --location --retry 3 \
    "https://download.qt.io/archive/qt/${version%.*}/$version/submodules/$archive" \
    -o "$work/$archive"
printf '%s  %s\n' "$checksum" "$work/$archive" | sha512sum -c -
tar -xJf "$work/$archive" -C "$work"
source_dir="$work/qtdeclarative-everywhere-src-$version"
patch -d "$source_dir" -p1 < "$patch_dir/0001-QtQuick-parent-pixmap-cache-timer-to-application.patch"
cmake -S "$source_dir" -B "$work/build" -G Ninja \
    -DCMAKE_PREFIX_PATH="$qt_prefix" -DCMAKE_BUILD_TYPE=Release \
    -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF \
    -DQT_BUILD_EXAMPLES_BY_DEFAULT=OFF -DQT_BUILD_TOOLS_BY_DEFAULT=OFF
# Qt Quick uses private QML interfaces. Keep its feature configuration equal to
# the binary SDK rather than installing a differently configured replacement.
for module in qml quick; do
    name="Qt${module^}"
    cmp "$work/build/src/$module/qt$module-config.h" "$qt_prefix/include/$name/qt$module-config.h"
    cmp "$work/build/src/$module/qt$module-config_p.h" \
        "$qt_prefix/include/$name/$version/$name/private/qt$module-config_p.h"
done
cmake --build "$work/build" --target Quick --parallel "${JOBS:-4}"

# Exercise the rebuilt library against the rest of the original SDK before
# replacing it. Other Qt Declarative libraries built as dependencies stay local.
mkdir "$work/runtime" "$work/probe"
cp "$work/build/lib/libQt6Quick.so.$version" "$work/runtime/"
# Keep the installed library relocatable and remove build-tree search paths.
patchelf --set-rpath '$ORIGIN' "$work/runtime/libQt6Quick.so.$version"
ln -s "libQt6Quick.so.$version" "$work/runtime/libQt6Quick.so.6"
cat > "$work/probe/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.16)
project(QtQuickCacheLifetime LANGUAGES CXX)
find_package(Qt6 REQUIRED COMPONENTS Widgets Quick)
add_executable(cache-lifetime "${LIFETIME_TEST_SOURCE}")
target_compile_features(cache-lifetime PRIVATE cxx_std_17)
target_link_libraries(cache-lifetime PRIVATE Qt6::Widgets Qt6::Quick)
CMAKE
cmake -S "$work/probe" -B "$work/probe-build" -G Ninja \
    -DCMAKE_PREFIX_PATH="$qt_prefix" \
    -DLIFETIME_TEST_SOURCE="$root/extras/package/release/tests/qtquick-pixmap-cache-lifetime.cpp"
cmake --build "$work/probe-build" --parallel "${JOBS:-4}"
for mode in window no-window app-owned; do
    for scenario in 'same-thread 1' 'same-thread 2' 'different-threads 2'; do
        LD_LIBRARY_PATH="$work/runtime:$qt_prefix/lib:${LD_LIBRARY_PATH:-}" \
        QT_PLUGIN_PATH="$qt_prefix/plugins" QML_IMPORT_PATH="$qt_prefix/qml" \
        QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
            "$work/probe-build/cache-lifetime" $scenario "$mode"
    done
done
install -m 755 "$work/runtime/libQt6Quick.so.$version" "$qt_prefix/lib/libQt6Quick.so.$version"
