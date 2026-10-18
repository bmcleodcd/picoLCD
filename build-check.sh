#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
common="-g -O2 -Wall -Wextra -Wno-unused-variable -Iinclude -Isrc"
dbus_flags=$(pkg-config --cflags --libs dbus-1)
media_flags="$(pkg-config --cflags --libs json-c) -pthread"
# Build a private library first; nothing here touches the installed service.
gcc $common -fPIC -shared -Wl,-soname,libpicolcd.so.0 -o build/libpicolcd.so.0 \
    lib/hid.c lib/util.c lib/picolcd.c lib/picolcd-common.c lib/splash.c \
    lib/picolcd-20x2.c lib/picolcd-20x4.c lib/picolcd-256x64.c lib/widgets.c lib/rc5.c -lusb -lrt
gcc $common -o build/picolcd src/main.c src/picolcd-util.c src/panel.c src/input.c src/display-monitor.c lib/ortek.c src/media.c \
    -Lbuild -Wl,-rpath,'$ORIGIN' -l:libpicolcd.so.0 -ldl $dbus_flags $media_flags
display_helper=""
if pkg-config --exists xrandr; then
    gcc $common -o build/picolcd-display-probe src/display-probe.c $(pkg-config --cflags --libs xrandr) -lX11
    display_helper=build/picolcd-display-probe
fi

test_flags="-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -ffunction-sections -fdata-sections -Wl,--gc-sections"
gcc $common $test_flags -o build/test-rc6-mce test-rc6-mce.c
ASAN_OPTIONS=detect_leaks=1 ./build/test-rc6-mce test-data/hp-rc6-capture.log
gcc $common $test_flags -o build/test-input test-input.c src/input.c
gcc $common -o build/test-input-live test-input-live.c src/input.c src/display-monitor.c
ASAN_OPTIONS=detect_leaks=1 ./build/test-input

gcc $common $test_flags -o build/test-panel test-panel.c src/display-monitor.c -ldl $dbus_flags
gcc $common $test_flags -o build/test-ortek-panel test-ortek-panel.c lib/ortek.c -ldl $dbus_flags
ASAN_OPTIONS=detect_leaks=1 ./build/test-ortek-panel

if [ -n "$display_helper" ]; then
    gcc $common $test_flags -o build/test-display test-display.c -lX11 -lXrandr $dbus_flags
fi
gcc $common $test_flags -o build/test-usb test-usb.c
gcc $common $test_flags -o build/test-ortek-mouse test-ortek-mouse.c lib/ortek.c
ASAN_OPTIONS=detect_leaks=1 ./build/test-ortek-mouse

gcc $common $test_flags -o build/test-ortek test-ortek.c lib/ortek.c
ASAN_OPTIONS=detect_leaks=1 ./build/test-ortek test-data/ortek-capture.log

gcc $common $test_flags -o build/test-rc5 test-rc5.c -lrt
gcc $common $test_flags -o build/test-launch test-launch.c $dbus_flags
gcc $common $test_flags -o build/test-loop test-loop.c src/input.c lib/ortek.c src/media.c -ldl $dbus_flags $media_flags
gcc $common $test_flags -o build/test-media test-media.c $media_flags
gcc $common $test_flags -o build/test-splash test-splash.c
ASAN_OPTIONS=detect_leaks=1 ./build/test-panel
if [ -n "$display_helper" ]; then ASAN_OPTIONS=detect_leaks=1 ./build/test-display; fi
ASAN_OPTIONS=detect_leaks=1 ./build/test-usb
ASAN_OPTIONS=detect_leaks=1 ./build/test-rc5 > build/test-rc5.log
ASAN_OPTIONS=detect_leaks=1 dbus-run-session -- ./build/test-launch
ASAN_OPTIONS=detect_leaks=1 ./build/test-media
ASAN_OPTIONS=detect_leaks=1 ./build/test-splash
# Driver allocations are outside our leak-check scope; exercise real sensor APIs separately.
case "${PANEL_TEST_GPU:-required}" in
    required) sensor_option=--sensors ;;
    unavailable) sensor_option=--sensors-no-gpu ;;
    *) echo 'PANEL_TEST_GPU must be required or unavailable' >&2; exit 1 ;;
esac
ASAN_OPTIONS=detect_leaks=0 ./build/test-panel "$sensor_option"
ASAN_OPTIONS=detect_leaks=0 ./build/test-loop
./build/picolcd > build/usage.txt
sha256sum build/picolcd build/libpicolcd.so.0 $display_helper > build/release.sha256
if ldd build/picolcd | grep -Eq 'lib(X11|Xrandr|xdo)'; then
    echo 'Unexpected X11 dependency in panel binary' >&2; exit 1
fi
echo 'Build and checks passed (panel has no X11 linkage)'
