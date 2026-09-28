#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
common="-g -O2 -Wall -Wextra -Wno-unused-variable -Iinclude -Isrc"
dbus_flags=$(pkg-config --cflags --libs dbus-1)
# Build a private library first; nothing here touches the installed service.
gcc $common -fPIC -shared -Wl,-soname,libpicolcd.so.0 -o build/libpicolcd.so.0 \
    lib/hid.c lib/util.c lib/picolcd.c lib/picolcd-common.c \
    lib/picolcd-20x2.c lib/picolcd-20x4.c lib/picolcd-256x64.c lib/widgets.c lib/rc5.c -lusb -lrt
gcc $common -o build/picolcd src/main.c src/picolcd-util.c src/panel.c \
    -Lbuild -Wl,-rpath,'$ORIGIN' -l:libpicolcd.so.0 -lxdo -lX11 -ldl $dbus_flags
test_flags="-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -ffunction-sections -fdata-sections -Wl,--gc-sections"
gcc $common $test_flags -o build/test-panel test-panel.c -ldl $dbus_flags
gcc $common $test_flags -o build/test-usb test-usb.c
gcc $common $test_flags -o build/test-rc5 test-rc5.c -lrt
gcc $common $test_flags -o build/test-launch test-launch.c $dbus_flags
gcc $common $test_flags -o build/test-loop test-loop.c lib/rc5.c -lxdo -lX11 -ldl $dbus_flags
ASAN_OPTIONS=detect_leaks=1 ./build/test-panel
ASAN_OPTIONS=detect_leaks=1 ./build/test-usb
ASAN_OPTIONS=detect_leaks=1 ./build/test-rc5 > build/test-rc5.log
ASAN_OPTIONS=detect_leaks=1 dbus-run-session -- ./build/test-launch
# Driver allocations are outside our leak-check scope; exercise real sensor APIs separately.
ASAN_OPTIONS=detect_leaks=0 ./build/test-panel --sensors
ASAN_OPTIONS=detect_leaks=0 ./build/test-loop
./build/picolcd > build/usage.txt
sha256sum build/picolcd build/libpicolcd.so.0 > build/release.sha256
echo 'Build and checks passed'
